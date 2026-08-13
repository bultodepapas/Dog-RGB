# Dog-RGB Architecture

**Status:** Current architecture, verified against the active firmware on 2026-08-13.

Dog-RGB is a local-first embedded system. The ESP32-S3 owns GNSS acquisition, metrics, route/session persistence, LED rendering, Wi-Fi policy, the HTTP portal, and an optional BLE summary. No backend is required for normal operation.

## System context

```mermaid
flowchart TB
    CELL[21700 cell] --> SAFE[1S protection / charger]
    SAFE --> BOOST[5 V boost]
    BOOST --> STRIPS[Two SK6812 RGBW strips]
    SAFE --> REG[3.3 V rail]
    REG --> MCU[XIAO ESP32-S3]
    REG --> GNSS[EBYTE E108-GN02]
    GNSS -->|UART RMC + GGA| MCU
    MCU -->|two level-shifted data lines| STRIPS
    PHONE[Phone or laptop] <-->|SoftAP or LAN HTTP| MCU
    BLECLIENT[BLE reader] -. read-only, disabled by default .-> MCU
```

The power drawing is conceptual. The real charger/BMS/boost/regulator topology must be validated for the selected modules; do not infer a safe current path solely from this diagram.

## Repository boundaries

- [`Platformio/Dog-RGB`](../Platformio/Dog-RGB/) is the active embedded project.
- [`tests`](../tests/) and [`tools`](../tools/) exercise/extract the embedded portal on a host browser.
- [`docs`](.) contains current references plus dated design history.
- [`software`](../software/) is only a placeholder for optional future companion/cloud work.
- [`hardware`](../hardware/) is a hardware-area entry point; the authoritative pin/default values are currently in firmware headers.

## Firmware modules

| Module | Primary files | Responsibility |
| --- | --- | --- |
| Orchestrator | `src/main.cpp` | Boot order, bounded cooperative loop, heartbeat, periodic diagnostics, serial log queue |
| GNSS/metrics/sessions/routes | `include/gps/gps.h`, `src/gps/gps.cpp` | UART/NMEA, trust gates, distance/activity/speed, date rollover, current/completed sessions, two-hour route ring, JSON/BLE summaries |
| Geofence/Home | `include/geofence/home.h`, `src/geofence/home.cpp` | Home A/B persistence, auto/manual Home, distance, ten ranges, hysteresis |
| Wi-Fi policy | `include/wifi/wifi_mgr.h`, `src/wifi/wifi_mgr.cpp` | AP/STA transitions, credentials, event queue, retry/backoff, idle/hold policy, scan lifecycle, mDNS |
| HTTP portal | `include/web/portal_http.h`, `src/web/portal_http.cpp` | Route registration, request validation, response headers, bounded exports/scene bodies, DNS/captive helpers |
| Scene JSON contract | `include/web/scene_json.h`, `src/web/scene_json.cpp` | Strict allowlist parser, canonical user-scene import/export and field-level errors |
| Portal asset serving | `include/web/{generated_assets,portal_assets}.h`, `src/web/{generated_assets,portal_assets}.cpp` | Immutable gzip pages in flash, ETag/content negotiation and known-length responses |
| Portal web source/build | `../../webui/src`, `../../webui/build.mjs`, `../../webui/generated/manifest.json` | Editable dashboard/Wi-Fi/config/diagnostic sources and deterministic HTML→gzip→C++ pipeline |
| Portal write lock | `include/web/portal_lock.h`, `src/web/portal_lock.cpp` | Optional 4–8 digit PIN, CRC record, constant-work comparison |
| Runtime configuration | `include/config/runtime_config.h`, `src/config/runtime_config.cpp` | Defaults, schema migration, validation, A/B persistence, hot application |
| LED orchestration | `include/led/led_ui.h`, `src/led/led_ui.cpp` | Snapshot device-domain inputs, adapt schema-6 config, render logical body/status, retain current `LedState` |
| LED state and policy | `include/led/led_state.h`, `include/led/led_policy.h`, `src/led/{led_state,led_policy}.cpp` | Pure priority/intent selection from value-only inputs; no GPS, Wi-Fi, geofence, NVS, or pixel ownership |
| Effect registry/renderer | `include/led/effect_registry.h`, `src/led/effect_registry.cpp` | Stable ID/key metadata plus allocation-free RGB generation from explicit time, seed, runtime, and pixel span |
| LED palettes | `include/led/palette_registry.h`, `src/led/palette_registry.cpp` | Eight curated RGBW palettes, stable metadata and allocation-free sampling |
| LED scene model/catalog | `include/led/{scene,scene_catalog}.h`, `src/led/{scene,scene_catalog}.cpp` | 44-byte codec, validation, four immutable built-ins and four user-slot views |
| LED scene player/runtime | `include/led/{scene_player,scene_runtime}.h`, `src/led/{scene_player,scene_runtime}.cpp` | Volatile apply/cancel, active snapshot, shuffled Show bag, store/catalog ownership and diagnostics |
| LED layout/compositor | `include/led/{led_layout,led_compositor}.h`, `src/led/{led_layout,led_compositor}.cpp` | Semantic regions, A/B orientation, mirror, body crossfade, status and alert overlay |
| LED transport | `include/led/led_bus.h`, `src/led/{led_color,led_bus}.cpp` | Shared RGB↔RGBW conversion, NeoPixel ownership and dual-strip output |
| LED power model | `include/led/power_limiter.h`, `src/led/power_limiter.cpp` | Two-bus current estimate, global scale, slow release, diagnostics |
| Day Mode | `include/power/day_mode.h`, `src/power/day_mode.cpp` | Pure evaluation of enabled/trusted-time/day-window state |
| BLE summary | `include/ble/summary_ble.h`, `src/ble/summary_ble.cpp` | Read-only 16-byte characteristic and AP-aware advertising; compile-time disabled by default |
| Scene store | `include/storage/{scene_store,scene_nvs_backend}.h`, `src/storage/{scene_store,scene_nvs_backend}.cpp` | 196-byte A/B record, CRC/generation selection, recovery/readback and Preferences adapter |
| Storage handles | `include/storage/nvs_store.h`, `src/storage/nvs_store.cpp` | Opens the default NVS namespaces, scene namespace and dedicated route partition |
| Utilities | `include/util/{geo,crc32,time_utils}.h`, `src/util/geo.cpp` | Haversine, CRC-32/IEEE, and wrap-safe time primitives |
| Wokwi control | `include/sim/wokwi_control.h`, `src/sim/wokwi_control.cpp` | Simulation-only command channel and fault/profile controls |
| Hardware/defaults | `include/pins.h`, `include/config.h` | Compile-time pin assignment, limits, timing, hardware layout, and fallback defaults |

Domain state is module-local (`static`/namespace scope) and exposed through narrow functions. There is no shared global `AppState` object.

## Boot sequence

Production boot follows this order:

1. Start the console and external status GPIO.
2. Open NVS handles with `storage::begin()`.
3. Load and validate runtime configuration; select the newest valid A/B generation or migrate/fall back to defaults.
4. Load the independent scene A/B bank into `SceneCatalog` and reset/seed `ScenePlayer`; built-ins remain available even if that namespace fails.
5. If `DEBUG_AP_ONLY_MINIMAL` is enabled, start only Wi-Fi + portal and return from setup.
6. Start GNSS/metrics/routes, then restore Home/geofence state.
7. Start the LED driver and welcome animation when `LED_UI_ENABLED` is true.
8. If `BLE_ENABLED` is true, initialize BLE **before** Wi-Fi and wait the configured coexistence margin.
9. Start Wi-Fi policy and register/start HTTP plus the optional portal lock.
10. Start the simulation control channel (compiled to no-op behavior outside Wokwi as applicable).

BLE precedes Wi-Fi deliberately: initializing the Bluetooth controller after SoftAP can disrupt the shared RF scheduler. Normal production defaults avoid that path by keeping BLE off.

## Cooperative loop

```mermaid
flowchart LR
    A[GNSS tick] --> B[Wokwi control]
    B --> C[Geofence]
    C --> D[Metric save + route tick]
    D --> E[Heartbeat / periodic logs]
    E --> F[Optional BLE + Wi-Fi tick]
    F --> G[LED tick]
    G --> H[HTTP + captive DNS]
    H --> I[Drain bounded serial log queue]
    I --> A
```

Each phase records maximum observed time for `/api/dev`. There is no RTOS application task graph; the design relies on short cooperative ticks plus the framework's radio tasks.

Two blocking risks receive special treatment:

- HTTP route export writes are synchronous, so data is coalesced into 768-byte chunks and GNSS is serviced on both sides of each socket write.
- Serial output can block, so periodic logs format into a fixed queue and the loop reports application work separately from logging/drain time.

## Concurrency ownership

Arduino Wi-Fi callbacks execute outside the main application loop. They never mutate the Wi-Fi state machine directly. Instead, callbacks enqueue compact events into a fixed 16-entry queue; `wifi_mgr::tick()` drains them in order and owns transitions, retries, station counts, cached mode/channel/IP, and reconciliation.

Queue high-water and overflow counters are exposed in diagnostics. On overflow, the next owner tick reconciles from the driver rather than trusting an incomplete event history.

## Data flow

1. UART bytes are framed and checksum-validated as NMEA sentences.
2. RMC provides status, position, speed, UTC time/date; GGA provides fix quality, satellites, and HDOP.
3. A trusted fix requires current/acceptable evidence under runtime GNSS gates.
4. Trusted observations update speed usability, Haversine segments, active time, maximum speed, route points, and date-transition state.
5. At the LED tick, `ScenePlayer` consumes the latest fixed apply/cancel command or advances Show from its shuffled eligible-scene bag; it provides a copied recipe, never a live NVS reference.
6. Metrics, device-domain snapshots and that optional scene feed `LedPolicyEngine`, which selects a value-only `LedState`; renderers consume it without importing GNSS, Wi-Fi, geofence, HTTP or storage modules.
7. The selected effect/palette produce logical RGB; `LedCompositor` scales only the target body, maps semantic regions, orientation, mirror, transition and alert; then `LedBus` limits and converts the physical frame for RGBW transport.
8. Runtime configuration and scene-bank mutations are validated/persisted independently. Scene apply/cancel is volatile and never writes flash.

Average speed is `distance / active_time`; it is not a mean of NMEA speed samples. Activity intervals require active evidence at both endpoints and reject gaps longer than the configured bound.

## Persistence model

| Store | Location | Contents |
| --- | --- | --- |
| `dogrgb` | Default `nvs` partition | Daily metric records/journal, session store, station credentials, migration marker |
| `dogrgb_cfg` | Default `nvs` partition | Runtime config A/B records, Home A/B records, optional portal-lock record |
| `dogrgb_scn` | Default `nvs` partition | User-scene bank A/B records (`scene_a`, `scene_b`) |
| `dogrgb_trk` | Dedicated `tracknvs` partition | Route metadata and CRC-protected chunks |

Critical multi-field state uses version/magic/size validation, semantic checks, CRC-32/IEEE, generation numbers with wrap-safe ordering, and alternating slots. A save becomes active only after a complete verified write. Failed portal persistence restores the previous in-memory configuration.

Scenes keep a separate schema/generation from runtime config. Each `SCN1` record is exactly 196 bytes and contains four canonical 44-byte slots; two banks total at most 392 bytes of payload. A future/oversized record is read-only, while corrupt/ambiguous recovery requires an explicit request and writes two equivalent copies. Store unavailability disables user-scene mutations without preventing boot or the flash-resident built-ins.

Route storage is different because of volume:

- dedicated 192 KiB partition (`0x30000`);
- up to 1,440 points at a nominal 5-second interval (about two hours);
- chunked ring with CRC-protected metadata/payload;
- partial chunk rewritten every 15 seconds to bound reboot loss;
- invalid chunks are skipped independently during export;
- export iteration snapshots bounds so concurrent recording cannot expand the request indefinitely.

The partition table also provides dual OTA-sized app slots, SPIFFS space, and coredump space, but the repository does not currently implement an OTA product workflow or use SPIFFS for the portal.

## Wi-Fi and portal policy

The Wi-Fi manager can operate in OFF, AP, STA, or AP+STA mode. AP availability depends on GNSS, stationary state, initial/recent portal hold windows, client presence, and station status. Retries use wrap-safe bounded backoff.

The HTTP layer adds:

- wildcard DNS while AP is active and common captive-portal probe routes;
- relative redirects for unknown page paths;
- JSON 404/405 behavior for API paths;
- no-store and basic browser hardening headers;
- `X-Dog-Portal` on every write as a same-origin intent/CSRF guard;
- optional `X-Dog-Pin` validation for write routes;
- output escaping in both server interpolation and client rendering.

This is proportionate protection for a local DIY portal, not a claim of Internet-safe administration. There is no TLS, account system, encrypted application payload, or authorization on reads.

## LED composition

The default physical layout is two strips with 24 pixels each and two reserved status pixels per strip. Bus A body is forward and bus B body reverse; those compile-time declarations still require confirmation on the final mounting. Rendering is semantic:

- welcome alone owns complete strips during startup;
- normal effects, including Simple, render `body_left`/`body_right` or mirrored `body_all`;
- Show renders the four built-ins plus each valid/eligible user slot through a fixed shuffled bag; a manual scene is a volatile body override;
- normal status always owns the two reserved pixels;
- a visual state change crossfades body buffers for 500 ms while status stays current;
- Day Mode supplies a black body and normal status;
- System and Geofence alerts interrupt a fade and override only the `alert`/status region.

```mermaid
flowchart LR
    STORE[(SceneStore A/B)] --> CATALOG[SceneCatalog]
    CATALOG --> PLAYER[ScenePlayer]
    DOMAIN[GNSS · Wi-Fi · Home · Day Mode · runtime config] --> ADAPTER[led_ui adapter]
    PLAYER --> ADAPTER
    ADAPTER --> POLICY[LedPolicyEngine]
    POLICY --> STATE[LedState]
    STATE --> RENDER[EffectRegistry + PaletteRegistry]
    RENDER --> LOGICAL[Logical LedFrame RGB]
    LOGICAL --> COMP[LedLayout + LedCompositor]
    COMP --> PHYSICAL[Physical LedFrame RGB]
    PHYSICAL --> BUS[PowerLimiter + LedBus RGBW]
```

The policy priorities are explicit and native-tested: welcome `100`, System/Geofence alert `90`, Day Mode `80`, active range/Show/Simple/manual recipes `30`, and idle/guidance `20`. An alert does not silently replace the decorative intent; `LedAlert` and `critical_alert` let the compositor override only the pixels owned by the alert region.

The schema-6 `RangeEffect` records remain the runtime-config persistence contract. Scenes use schema/record version 1 in `dogrgb_scn`, so Fase 4 does not migrate `RuntimeConfig` or change persisted mode ID 2 for Show. `led_ui` copies range records or an optional scene snapshot into a temporary pure `LedPolicyConfig`.

See [LED UI](led_ui_spec.md) for exact behavior.

## Test architecture

- Host Python contracts and native C++ harnesses target persistence, timing, LED state, scene wire/player/store and strict JSON failure modes.
- Wokwi runs the production firmware image with a custom controllable NMEA chip and logic analyzer.
- The portal extractor converts C++ raw literals into local pages served with fixture APIs.
- Playwright tests mobile behavior, accessibility, degraded states, and visual baselines.
- CI runs host contracts, portal behavior/a11y checks, pinned visual comparisons, and the production PlatformIO build with size/artifact evidence.

See [Testing and simulation](testing.md) for commands and limitations.

## Deliberate constraints

- Local-first; no required cloud/backend.
- BLE remains compile-time optional and off by default.
- Advanced security/production provisioning remains optional for this DIY phase.
- No battery percentage is invented without a gauge/divider and calibrated measurements.
- No runtime, thermal, waterproofing, or pet-safety claim is accepted from simulation alone.
- Cooperative loop and synchronous `WebServer` are retained while bounded work and diagnostics satisfy the prototype needs.

Future modules should preserve these constraints or document an explicit architecture decision before changing them.
