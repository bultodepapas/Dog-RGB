from pathlib import Path
import json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
tracked = subprocess.check_output(['git', 'ls-files', '*.md'], text=True).splitlines()
original = {p: Path(p).read_text(encoding='utf-8-sig') for p in tracked}
mapping = {}
content = dict(original)
origins = {p: p for p in tracked}

def write(path, text):
    content[path] = text.strip() + '\n'
    origins[path] = path

def move(old, new):
    mapping[old] = new
    content[new] = content.pop(old)
    origins[new] = old

def retire(old, target):
    mapping[old] = target
    content.pop(old, None)

for old, new in {
 'docs/manual_de_construccion.en.md':'docs/hardware/build.md',
 'docs/bom_power_budget.md':'docs/hardware/power.md',
 'docs/sk6812_wiring.md':'docs/hardware/wiring.md',
 'xiao_s3_pin.md':'docs/hardware/pins.md',
 'docs/config_params.md':'docs/firmware/configuration.md',
 'docs/gps_analysis.md':'docs/firmware/gnss.md',
 'docs/led_effects.md':'docs/firmware/effects.md',
 'docs/led_ui_spec.md':'docs/firmware/leds.md',
 'docs/color-reference.md':'docs/firmware/colors.md',
 'docs/geofence_mode_plan.md':'docs/firmware/geofence.md',
 'docs/ble_spec.md':'docs/firmware/ble.md',
 'docs/wifi_portal_spec.md':'docs/firmware/wifi.md',
 'docs/portal_config.md':'docs/portal/configuration.md',
 'docs/api-reference.md':'docs/portal/api.md',
 'docs/web_portal_spec.md':'docs/portal/interface.md',
 'docs/ap_portal_visual_screenshot_workflow_guide.md':'docs/portal/visual-testing.md',
 'Platformio/Dog-RGB/docs/modo-dia.md':'docs/firmware/day-mode.md',
 'docs/cloud/phase0-field-matrix.md':'docs/cloud/field-map.md',
}.items():
    move(old, new)

for p in tracked:
    if p.startswith('docs/PLANS/') or p.startswith('Platformio/Dog-RGB/docs/superpowers/'):
        retire(p, 'docs/roadmap.md')
    elif p.startswith('docs/baselines/'):
        retire(p, 'docs/testing.md')
    elif p.startswith('Platformio/Dog-RGB/docs/display-'):
        retire(p, 'Platformio/Dog-RGB/docs/display.md')

for old, target in {
 'README.en.md':'README.md', 'README.es.md':'README.md', 'CHANGELOG.md':'README.md',
 'AUDIT_ANALYSIS_AND_IMPROVEMENT_PLAN.md':'docs/roadmap.md',
 'WIFI_AP_DEEP_AUDIT.md':'docs/firmware/wifi.md',
 'docs/manual_de_construccion.md':'docs/hardware/build.md',
 'docs/manual_de_construccion.es.md':'docs/hardware/build.md',
 'docs/manual_de_colores.md':'docs/firmware/colors.md',
 'docs/manual_de_uso.md':'docs/user-guide.md',
 'docs/tasks.md':'docs/roadmap.md',
 'docs/phase0_freeze.md':'docs/hardware/build.md',
 'docs/main_refactor.md':'docs/architecture.md',
 'docs/wifi_portal_state_diagram.md':'docs/firmware/wifi.md',
 'docs/analisis-wled-y-plan-implementacion.md':'docs/firmware/leds.md',
 'docs/led_show_mode_plan.md':'docs/firmware/scenes.md',
 'docs/show_mode_hardening_review.md':'docs/firmware/scenes.md',
 'docs/gps_speed_mode_hardening_review.md':'docs/firmware/gnss.md',
 'docs/ap_analysis.md':'docs/firmware/wifi.md',
 'docs/ap_access_point_comprehensive_review_2026-05-05.md':'docs/firmware/wifi.md',
 'docs/ap_portal_ui_deep_review_2026-05-06.md':'docs/portal/interface.md',
 'docs/ap_portal_visual_screenshot_workflow_plan_2026-05-06.md':'docs/portal/visual-testing.md',
 'docs/auditoria_externa.md':'docs/roadmap.md',
 'docs/web_portal_deep_audit_2026-08-11.md':'docs/portal/interface.md',
 'docs/web_portal_ux_review_2026-08-11.md':'docs/portal/interface.md',
 'docs/dependency_update_audit_2026-08-01.md':'docs/testing.md',
 'docs/dependency_update_execution_2026-08-01.md':'docs/testing.md',
 'docs/display-github-research-2026-09-12.md':'Platformio/Dog-RGB/docs/display.md',
 'docs/display-library-research.md':'Platformio/Dog-RGB/docs/display.md',
 'docs/display-visual-identity-research.md':'Platformio/Dog-RGB/docs/display.md',
 'docs/waveshare-lcd169-technical-research.md':'Platformio/Dog-RGB/docs/boards.md',
 'docs/display-identity-api.md':'docs/portal/identity.md',
 'docs/app_mvp_spec.md':'docs/roadmap.md',
 'docs/flow_wireframe.md':'docs/roadmap.md',
 'docs/portal_config_presets.md':'docs/roadmap.md',
 'docs/cloud/local-web-v1-implementation-2026-10-03.md':'docs/cloud/portal.md',
}.items(): retire(old,target)

for p in tracked:
    if not p.startswith('docs/cloud/') or p not in content or p.endswith('README.md'):
        continue
    name=Path(p).name
    if name.startswith('m23') or name.startswith('m24'):
        retire(p,'docs/cloud/firmware.md')
    elif name.startswith('m1'):
        retire(p,'docs/cloud/portal.md')
    elif name.startswith('phase0-'):
        retire(p,'docs/cloud/outbox.md')
    elif name.startswith('phase1-'):
        retire(p,'docs/cloud/operations.md')

write('README.md', r'''
# Dog-RGB

A local-first DIY dog collar with ESP32-S3, RGBW LEDs, GNSS recording and an embedded Wi-Fi portal. Electronics and firmware are experimental; the website stays small and optional.

**Status:** working software prototype. Battery runtime, electrical limits, enclosure, weather resistance and integrated field behavior still require physical validation. No hosted service or physical collar cloud synchronization is delivered.

## Start

1. Follow the [hardware build guide](docs/hardware/build.md) before connecting power.
2. Install PlatformIO Core 6.1.19 and build from the repository root:

```powershell
pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3
```

3. Upload to the identified XIAO and open its monitor:

```powershell
pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3 -t upload
pio device monitor -d Platformio/Dog-RGB -e seeed_xiao_esp32s3
```

Use the upload target when installing the partition layout; uploading only `firmware.bin` does not install the route-storage partition.

4. Join `DogRGB` with `Dog12345`, open `http://192.168.4.1/`, and change the AP password. On a connected home network, use `http://dog-collar.local/` where mDNS is supported.

## Capabilities

| Area | Current boundary |
| --- | --- |
| Hardware | Default XIAO ESP32-S3, E108-GN02 UART GNSS, two 24-pixel SK6812 RGBW strips; experimental Waveshare LCD profile |
| GNSS | Trusted RMC/GGA metrics, daily rollover, current and three completed session summaries, two-hour route ring and JSON/CSV/GeoJSON export |
| LEDs | Speed, Geofence, Show and Simple; 12 effects, eight palettes, four built-in and four user scenes; Day Mode and estimated-current limiting |
| Local portal | Dashboard, Wi-Fi, configuration, scene editor, diagnostics and optional write PIN; Display builds also expose contact identity |
| Display | Activity, Wi-Fi, State and optional Identity page; BOOT navigation, wake-only first press, local contact QR |
| Optional web | Local Next.js/Supabase owner portal, simulator pairing, summaries/history, brightness, private exports and deletion |
| Cloud firmware | Isolated identity, Track v3 codec and chunk assembler; runtime capture, durable outbox and HTTPS sync remain pending |

The collar requires neither Internet nor an account. BLE summary code is disabled by default. Battery telemetry, native mobile apps, OTA and IMU-based activity classification are not implemented.

## Develop

Use Node and npm versions pinned in [`.node-version`](.node-version) and [`package.json`](package.json).

```powershell
npm ci
npm run webui:check
npm run webui:unit
python tools/web_pages_smoke.py
```

Edit embedded pages in [`webui/src`](webui/src/), then run `npm run webui:build`. Tracked generated assets allow firmware builds without Node once PlatformIO dependencies are installed.

- [Documentation index](docs/README.md)
- [User guide](docs/user-guide.md)
- [Architecture](docs/architecture.md)
- [Testing](docs/testing.md)
- [Optional web setup](docs/cloud/README.md)
- [Open work](docs/roadmap.md)
- [Contributing](CONTRIBUTING.md)

## License

Original project material uses [MIT](LICENSE). Bundled fonts and other third-party material retain their own licenses; see [licensing rules](docs/adr/0001-wled-clean-room-y-licencia-del-proyecto.md).
''')

write('docs/README.md', r'''
# Documentation

English is canonical. Each page owns one topic. Source, schemas and executable checks define implemented behavior; [open work](roadmap.md) records missing implementation and acceptance. Git contains change history.

## Build and use

| Document | Purpose |
| --- | --- |
| [Build](hardware/build.md) | Assembly, flashing and bench acceptance |
| [Wiring](hardware/wiring.md) | LED power, data and grounding |
| [Pins](hardware/pins.md) | XIAO assignments and reserved interfaces |
| [Power](hardware/power.md) | BOM, estimates and measurement matrix |
| [User guide](user-guide.md) | Daily operation and troubleshooting |
| [Board profiles](../Platformio/Dog-RGB/docs/boards.md) | Classic/Waveshare targets and diagnostic procedures |
| [Display](../Platformio/Dog-RGB/docs/display.md) | Pages, input, rendering and bench controls |

## Firmware

| Document | Purpose |
| --- | --- |
| [Architecture](architecture.md) | Module ownership and data flow |
| [Configuration](firmware/configuration.md) | Compile-time constants and runtime defaults |
| [GNSS](firmware/gnss.md) | Trust, metrics, time, sessions and route persistence |
| [LED policy](firmware/leds.md) | Status, mode priority, composition and limiting |
| [Effects](firmware/effects.md) | Stable effect IDs and controls |
| [Colors](firmware/colors.md) | Status, speed colors and palettes |
| [Scenes](firmware/scenes.md) | Recipes, playback and persistence |
| [Day Mode](firmware/day-mode.md) | Fixed daylight window and trusted time |
| [Geofence](firmware/geofence.md) | Home, bands and hysteresis |
| [Wi-Fi](firmware/wifi.md) | AP/STA policy, scans and recovery |
| [BLE](firmware/ble.md) | Optional 16-byte summary contract |

## Local portal

| Document | Purpose |
| --- | --- |
| [HTTP API](portal/api.md) | Routes, write guards, errors and payloads |
| [Configuration API](portal/configuration.md) | Update validation and persistence |
| [Identity API](portal/identity.md) | Display contact validation and storage |
| [Interface](portal/interface.md) | Page behavior and accessibility |
| [Web assets](../webui/README.md) | Deterministic generation and preview |
| [Visual testing](portal/visual-testing.md) | Browser captures and baseline updates |

## Optional cloud

| Document | Purpose |
| --- | --- |
| [Setup and scope](cloud/README.md) | Local stack and implementation boundaries |
| [Owner portal](cloud/portal.md) | Routes, authorization, exports and deletion |
| [Firmware foundation](cloud/firmware.md) | Identity, codec and assembler integration contracts |
| [Outbox](cloud/outbox.md) | Host model, recovery rules and physical gates |
| [Field map](cloud/field-map.md) | Local fields, privacy and sync ownership |
| [Operations](cloud/operations.md) | Retention, deletion and isolated restore |
| [Testing](cloud/testing.md) | Local gates and sanitized evidence |
| [Credentials](cloud/credential-checklist.md) | Environment and secret ownership |
| [Privacy](cloud/privacy-data-flow.md) | Collection, processors and exposure |
| [Retention](cloud/retention-policy.md) | Data-class deadlines and enforcement gaps |
| [Threat model](cloud/threat-model.md) | Trust boundaries and required controls |
| [Device protocol](../contracts/device-v1/README.md) | Normative wire schemas, replay and compatibility |

## Maintenance

- [Testing](testing.md): executable verification and physical acceptance.
- [Requirements](requirements.md): product invariants and release criteria.
- [Open work](roadmap.md): one prioritized backlog, with explicit exit conditions.
- [Architecture decisions](adr/README.md): current decisions and rationale.
- [Contribution rules](../CONTRIBUTING.md): scope, checks and documentation standards.
''')

write('docs/architecture.md', r'''
# Architecture

Dog-RGB has three independent surfaces: ESP32 firmware, its embedded AP portal, and an optional Next.js owner portal backed by local Supabase. The collar works offline. Physical cloud synchronization is not integrated.

## Ownership

| Path | Responsibility |
| --- | --- |
| `Platformio/Dog-RGB/src/main.cpp` | Boot order and cooperative scheduling |
| `src/board`, `include/board` | Compile-time hardware profiles and power hold |
| `src/gps` | NMEA validation, trust, metrics, sessions and Track v2 |
| `src/led` | Effect/palette/scene catalogs, policy, composition and RGBW transport |
| `src/wifi`, `src/web` | Radio policy, queued events, DNS and synchronous HTTP |
| `src/config`, `src/storage` | Runtime validation and persistent records |
| `src/geofence`, `src/power` | Home and Day Mode |
| `src/display` | Optional shared LVGL views, snapshots, identity and panel service |
| `src/track` | Isolated Track v3 codec and chunk assembler |
| `webui/src` | Editable embedded HTML/CSS |
| `apps/portal` | Next.js owner workflows and server-side data access |
| `contracts/device-v1`, `packages/contracts` | Wire contracts and application types |
| `packages/analytics` | Pure versioned summary rules |
| `supabase` | Local migrations, grants/RLS and Edge Functions |
| `tools/device-simulator` | Synthetic device protocol client |

Paths beginning `src/` are relative to `Platformio/Dog-RGB`.

## Local execution

Boot initializes the board, storage, configuration and scene player; then GNSS, Home and LEDs. Optional BLE initializes before Wi-Fi. The portal loads contact identity before the Display service starts. Diagnostic stages 0/1 use a minimal entry path.

The main loop drains GNSS first, then processes domain/storage work, queued radio events, LEDs, HTTP and optional display work. Synchronous HTTP/NVS operations remain bounded and observable; callbacks enqueue radio events instead of mutating owner-loop state.

```mermaid
flowchart LR
    UART[NMEA RMC/GGA] --> GPS[Trust and metrics]
    GPS --> ROUTE[Track v2 and summaries]
    GPS --> POLICY[LED policy]
    CONFIG[Config / Home / Day Mode] --> POLICY
    SCENES[Scene player] --> POLICY
    POLICY --> FRAME[Effects and semantic composition]
    FRAME --> LIMIT[Shared estimated-current limiter]
    LIMIT --> BUS[Two RGBW buses]
    GPS --> SNAP[Value snapshots]
    SNAP --> UI[HTTP / optional LCD]
```

Renderers do not own GNSS, Wi-Fi, storage or policy. Scenes provide body recipes; status, alerts, Day Mode and the global current model retain authority. Display adapters copy domain state and do not infer movement, health or Internet connectivity.

## Persistence model

| Namespace / partition | Data |
| --- | --- |
| `dogrgb` / `nvs` | Metrics, completed-day journal, sessions and station credentials |
| `dogrgb_cfg` / `nvs` | Runtime configuration, Home, optional PIN and Display contact identity |
| `dogrgb_scn` / `nvs` | Four user scenes in `scene_a` / `scene_b` |
| `dogrgb_trk` / `tracknvs` | Bounded Track v2 metadata and chunks |

Stores validate their own versions, sizes, semantic fields, CRC and generation rules. Do not generalize one store's recovery policy to another: scenes allow explicit recovery; device identity fails closed on an invalid bank pair; contact writes can have an indeterminate result after readback failure.

Track v2 uses the dedicated 192 KiB `tracknvs` partition, up to 1,440 points, nominal five-second sampling and 15-second partial flushes. Export snapshots bounds and streams data. The partition table reserves dual application slots, but there is no OTA product workflow. Its unused `spiffs` partition is a proposed raw outbox location, not active cloud storage.

## Embedded web assets

`webui/build.mjs` validates/inlines/minifies sources and emits deterministic gzip, hashes, ETags and C++ `PROGMEM` arrays. Tracked outputs are checked by a Python PlatformIO pre-script without npm or network access. Dependency installation is still required before an offline firmware build.

HTTP serves compressed bytes directly from flash. API responses use `no-store`; HTML uses ETag revalidation. Writes require `X-Dog-Portal` plus the optional PIN. Reads expose local telemetry to network clients. The AP portal has no TLS or account authorization.

## Optional cloud

The owner portal uses authenticated server-side data access with explicit grants and membership RLS in the exposed `api` schema. The unexposed `private` schema holds credential digests, claims, receipts and internal workers. Device identity never grants history-read access.

Local Edge Functions implement claim, sync, revoke and account deletion. The simulator exercises transactional ingestion and exact replay. Summary workers, retention and deletion are bounded local operations; hosted schedules remain unconfigured.

Native identity allocation, the v3 codec and chunk assembly have no production GNSS/startup caller. Their pending RAM batch is not durable. Runtime observation scheduling, raw flash storage, time bootstrap, shared configuration commits and HTTPS transport remain [open](roadmap.md).

See [cloud setup](cloud/README.md), [device protocol](../contracts/device-v1/README.md), [firmware integration](cloud/firmware.md) and [decisions](adr/README.md).
''')

write('docs/roadmap.md', r'''
# Open work

Only incomplete implementation or acceptance belongs here. Source defines delivered behavior; [testing](testing.md) defines reproducible checks. The collar, Display and optional cloud have separate gates.

## Physical collar

| Priority | Work | Exit condition |
| --- | --- | --- |
| P0 | Freeze exact cell, charger/protection, converter, regulators, connectors, wiring and enclosure | Versioned schematic/BOM, verified polarity and charge/power topology |
| P0 | Measure power, rail transients and temperatures | Calibrated current model; rated components and numeric thermal limits under the maximum allowed profile |
| P0 | Measure runtime | At least five hours under a declared brightness/radio/GNSS profile; no estimate reported as measurement |
| P0 | Validate GNSS with LEDs/radio active | Reference-route comparison, acquisition/recovery and spike/gap tests without UART overflow |
| P0 | Validate mechanical fit and ingress | Strain relief, cell protection, comfortable surfaces and controlled exposure tests |
| P1 | Exercise real AP/STA/slow-client behavior | Recovery, scans, exports and repeated writes stay within measured loop/heap limits |
| P1 | Accept scene persistence and visual output | All scene routes, reboot/fault recovery, 100 save/import cycles, apply by next LED tick, write gap ≤100 ms; verify mirror/orientation/color on both strips |

## Display

1. Confirm the physical No Touch PCB revision and power-hold circuit against the [profile](../Platformio/Dog-RGB/docs/boards.md).
2. Complete staged power/USB, RGBW, GNSS and LCD bench acceptance; USB-only observation does not close joint peripheral behavior.
3. Validate contact text and QR scanning at real panel size, brightness, viewing angles and expected lighting; verify BOOT, wake and reboot persistence.
4. Measure SPI/service latency, heap, UART gaps and power with the full system. Product inactivity, animated transitions and backlight PWM remain deferred until these measurements justify them.

## Optional web release

Local product flows and automated owner/authorization/fault/privacy checks are implemented. Remaining acceptance:

- Named human keyboard/focus/status, 200% zoom and reduced-motion review at 320/428/768/1280 px; automation is not human sign-off.
- Current remote CI release run and artifact review. Local success does not establish remote execution.
- Isolated hosted preview using synthetic data, trusted site origin, exact Auth redirects and real confirmation/recovery email delivery.
- Hosted grants/RLS, Edge auth, replay, cache, secret separation, latency and measured cost limits.

Keep the current small owner portal. Reuse existing Auth, DAL, workers and tests; no additional administration service is required.

## Offline firmware foundation

Complete before activating physical cloud capture:

1. Integrate explicit public UUID provisioning and durable boot allocation. Add credential lifecycle separately; reject corrupt/ambiguous identity state and sequence exhaustion.
2. Add v3 observation cadence, explicit gaps and time quality around the existing codec/assembler. Nominal target: moving 5 s, trusted stationary 60 s. Preserve v2 read/export.
3. Implement the [raw outbox](cloud/outbox.md) with immutable seals, durable exact ACKs, conservative recovery and reserved loss accounting. Never overwrite unacknowledged data.
4. Add time anchors. Verified TLS requires plausible GNSS/SNTP/persisted time before sending credentials; server-anchored time cannot bootstrap its own TLS connection.
5. Route AP and cloud configuration through one validation/atomic commit service with HLC, mutation ID and desired/reported version/hash.
6. Keep networking disabled by default and preserve GNSS, LEDs, scenes, AP recovery, exports and persistence without cloud.

Physical storage acceptance requires at least 10,000 seal/ACK/reclaim cycles and 1,000 asynchronous reset/power cuts across write boundaries. Record p50/p95/p99 recovery, wear distribution, heap/largest block, watchdog margin, GNSS gaps, LED jitter, energy and full-storage behavior. Retain sanitized traces with hardware revision, harness, firmware hash and conditions.

## Physical cloud integration

- Verified hostname/chain/date TLS; bounded DNS/connect/send/receive/JSON/overall deadlines; one in-flight request and exact persisted retries with jitter.
- Freeze numeric `Retry-After` and matching bounded JSON semantics across schemas, simulator and firmware before implementation.
- Add a bounded AP `/cloud` surface for opt-in, claim, backlog/freshness, retry and guarded unlink. It is not an existing route.
- Prove pairing, lost-response replay without duplicates, web brightness applied to the exact version/hash, offline AP edits and convergence.
- Prove revoke/re-enrollment for the same dog with a fresh credential, retained identity/history and harmless old-credential revoke retries.
- Normal unlink persists `REVOKE_PENDING` until a matching valid revoke result; force-clear must explain the remaining server-side revocation.

## Private hosted operation

Before persistent real-data use:

- Record intended users/collars, operator, service regions/tiers, monthly ceiling and alerts. An owned stable device API domain is required for durable field firmware; a custom website domain is optional.
- Configure production SMTP, exact origins, TLS, release/rollback smoke and support contact.
- Enforce the [retention policy](cloud/retention-policy.md) with monitored bounded retention/deletion/summary schedules. Policy text alone does not enforce expiry.
- Test protected backups and deletion records outside the same failure domain. Isolate restores, disable outbound jobs, verify hashes/RLS and replay post-backup dog **and account/Auth** deletions before traffic. Current replay covers dog tombstones only.
- Establish measured RPO/RTO, backup expiry, stalled-job recovery, credential rotation, quota and DNS/certificate runbooks. Encrypted logical backups are acceptable if tested.

Secure Boot, flash encryption, mTLS, KMS/HSM, SIEM/WAF, paid cloning and multi-region operation remain optional.

## Deferred

Optional tiled maps require the existing credentialed Stadia/MapTiler comparison, denied-origin/key-leak checks, current cost/terms and a recorded provider decision. Preserve the segmented SVG/table fallback and keep route data out of provider URLs.

Native apps, BLE default enablement, cellular/realtime tracking, IMU classifiers, battery telemetry, OTA, sharing, advanced analytics, configuration presets, remote Home/power calibration and general profile settings require a demonstrated need and their own bounded acceptance.
''')

write('docs/requirements.md', r'''
# Product requirements

## Local invariants

- GNSS, metrics, LEDs, storage, configuration, AP recovery and exports work without Internet, an account or cloud activation.
- RMC/GGA checksum, freshness and quality gates precede metric updates. Missing evidence is unknown; a boot recording is not automatically a walk.
- Daily rollover preserves the completed record before resetting accumulated metrics.
- Critical persistent state validates schema, size, semantics, CRC and generation. Ambiguous writes never report verified success.
- Route retention and exports are bounded. Slow/disconnected clients must not cause unbounded memory or prevent GNSS servicing.
- LED scenes control the body only. Status, alerts, Day Mode, global brightness and estimated-current limits retain authority.
- Runtime writes use common validation; HTTP writes require intent and the optional PIN. Reads expose telemetry to local-network clients and never return passwords.
- BLE remains disabled by default. No OTA, battery percentage or activity/health inference is implied by unused hardware/partition capability.

## Hardware acceptance

- Validate exact power/protection topology, current ratings, polarity, insulation, strain relief and cell restraint.
- Measure cell/rail current, transients, GNSS interference and temperatures for the allowed LED/radio profile. The software current model is not a sensor or protection circuit.
- Target five hours of operation under a reproducible profile; runtime, thermal limits and ingress resistance remain unverified.
- Do not charge while worn. No certified safety, waterproofing or tracking guarantee is claimed.

## Portal acceptance

- Four embedded pages: dashboard, Wi-Fi, configuration and diagnostics. Controls derive supported values from firmware capabilities.
- Generate deterministic offline assets. Gzip budgets: `/` 12 KiB, `/wifi` 13 KiB, `/config` 24 KiB, `/dev` 10 KiB; combined 55 KiB.
- Preserve drafts on failures/conflicts; separate identity and runtime-configuration commits.
- Support narrow layouts, keyboard operation, meaningful labels/status, visible focus and reduced motion. Owner portal targets are at least 44 px and tested at 320/428/768/1280 px.
- Compiling, automated accessibility checks and screenshots do not substitute for physical or human acceptance.

## Optional cloud invariants

- Collection requires explicit per-collar opt-in. A collar never contains human account credentials or Supabase project keys.
- Device transport verifies TLS and uses a unique revocable credential. Schema bounds, stable identities and exact replay prevent duplication or silent loss.
- Reclaim only after the exact chunk's durable post-commit ACK. A generic HTTP success never authorizes deletion.
- Membership authorization, explicit grants and RLS apply to every exposed data path. Public IDs are not permission.
- Desired and applied configuration remain distinct; `Applied` requires the exact reported version/hash.
- Home, LED power calibration, scenes, network credentials, mDNS, local PIN and Display contact identity stay local in the first release.
- Preserve time/fix quality, gaps, coverage, units and algorithm provenance. Never turn absent samples into stationary/rest/sleep evidence.
- Owner exports and dog/account deletion are bounded, freshly authorized and private. Retention and post-backup deletion replay must operate before hosted real-data use.
- The website stays useful with synthetic/local data before physical sync exists. Hosting and tiled maps are separate optional workstreams.

See [open acceptance](roadmap.md), [device contracts](../contracts/device-v1/README.md) and [testing](testing.md).
''')

write('docs/user-guide.md', r'''
# User guide

Dog-RGB operates through its LEDs and local Wi-Fi portal. Complete the [build and bench checks](hardware/build.md#bench-acceptance-checklist) before use. Runtime, weather resistance and thermal behavior are not certified. Never charge while worn.

## Connect

1. Power on outdoors with a clear sky view.
2. Join `DogRGB` using `Dog12345`; open `http://192.168.4.1/` if the captive prompt does not appear.
3. Change the AP password. Optionally configure a home network on `/wifi`.
4. On that network, use the station IP or `http://dog-collar.local/` where mDNS works.

The AP is forced on without a trusted GNSS fix and held during setup/client activity. Idle shutdown follows [Wi-Fi policy](firmware/wifi.md); it is not proof the collar is off.

## Portal pages

| Page | Use |
| --- | --- |
| `/` | Metrics, current/completed sessions, route preview and exports |
| `/wifi` | AP/STA credentials and explicit nearby-network scan |
| `/config` | Modes, brightness, GNSS gates, Home, scenes, optional PIN and supported Display identity |
| `/dev` | GNSS/radio/storage diagnostics and estimated LED current |

Configuration reset restores runtime defaults but keeps station credentials, Home, PIN, scenes, identity, metrics and route data. Network changes can disconnect the browser. Reads remain accessible to clients on the collar network even with a write PIN.

## LED modes

| Mode | Behavior |
| --- | --- |
| Speed | Ten color/effect ranges driven by trusted usable speed |
| Geofence | Ten distance bands from saved Home; no containment or tracking guarantee |
| Show | Shuffled eligible scenes, 30 seconds of active playback per scene |
| Simple | One configured effect and color |

Two leading pixels per strip indicate Wi-Fi and GNSS. A trusted GNSS fix uses solid blue; searching uses a blue pulse. See [all colors and alerts](firmware/colors.md).

Manual scene apply is temporary and does not save a new mode. Cancel or explicitly change mode to return to configured policy. Editing an active scene requires reapplying it to replace the copied playback recipe.

Day Mode is off by default. When enabled with trusted time, it blacks the effect body from 06:00 inclusive to 16:00 exclusive at UTC−5 while keeping status, recording and networking active.

## Metrics and export

Distance, active time and speed use accepted GNSS observations. Average speed is distance divided by active time. Poor fix, gaps and speed spikes are excluded. Missing data does not mean the dog was resting.

The collar keeps a current session, three completed summaries and about two hours of recent route points. The local route has minute-level timestamps. Export JSON, CSV or GeoJSON before older points roll out. A retained date may belong to a previous day.

## Display variant

With a valid saved name/phone, navigation is **Identity → Activity → Wi-Fi → State**. Without identity, it starts at Activity and uses three pages. A short BOOT release advances; the first press while dark only wakes the retained page. The product has no automatic screen timeout.

Configure contact details on `/config`. QR options are WhatsApp, call or disabled; the destination is derived from the international phone number. Saving does not navigate or wake the panel. Clearing visible identity selects Activity. QR readability requires physical testing.

Activity shows registered distance/date and usable speed. Wi-Fi reports local interfaces, not Internet access. State reports firmware policy, not measured strip output. `GPS DEMO` identifies diagnostic GPS fixtures. See [Display reference](../Platformio/Dog-RGB/docs/display.md).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| AP missing | Supply/reset state, client device and radio diagnostics; BLE is normally disabled |
| No usable GNSS | Outdoor sky view, UART wiring, receiver supply, RMC/GGA counters and quality gates |
| Effects dark | Day Mode, selected scene/body level, mode and transport state; verify power separately |
| Flicker or resets | Stop the load test; inspect rails, ground return, logic translation and converter margin |
| Write rejected | Current PIN, validation message, generation conflict or storage error; reload before retrying |
| Display contact did not save | Preserve the draft and reload; a storage readback failure can leave the durable result unknown |

The optional [owner website](cloud/README.md) is locally implemented with a simulator. Physical collar upload and hosted operation remain pending.
''')

write('CONTRIBUTING.md', r'''
# Contributing

Read [architecture](docs/architecture.md), [requirements](docs/requirements.md) and [open work](docs/roadmap.md). Preserve the practical local collar; cloud and advanced infrastructure remain optional.

## Changes

1. Inspect the current implementation and applicable `AGENTS.md`. Prefer the codebase graph for discovery; use source when its result is missing or stale.
2. Keep each change scoped to one behavior or contract. Use maintained libraries where appropriate and preserve third-party licensing.
3. Update affected reference pages with the implementation. Keep hardware assumptions separate from measurements.
4. Run the relevant [verification](docs/testing.md) and `git diff --check`. Report unexecuted checks accurately.

Firmware uses `Platformio/Dog-RGB/platformio.ini`. Select the correct board target before upload; do not erase storage as a routine troubleshooting step. Changes to `main.cpp` preserve its hardware/build header and ownership comments.

Edit embedded pages in `webui/src`, then regenerate with `npm run webui:build`; do not hand-edit generated assets. For cloud changes, preserve contracts, explicit grants/RLS, local-only test isolation and sanitized artifacts. Read installed Next.js guidance before changing portal code.

## Documentation

- Write concise English and one topic per page: purpose, contract/procedure, limits and verification.
- Describe current behavior. Put incomplete work only in [the backlog](docs/roadmap.md).
- Keep one canonical source for each fact; link instead of duplicating inventories or procedures.
- Record no session narratives, execution diaries, stale test counts or completed-task lists. Use Git for history.
- Preserve normative wire names, units, bounds, failure semantics, license notices and reproducible commands.
- Use repository-relative links. Add maintained pages to [the index](docs/README.md).
- Validate Markdown links/anchors and documented npm/PlatformIO commands with `python tools/check_docs.py`.

## Review

Describe the concrete behavior change, validation and material limitations. Never equate host tests with target timing, safe electrical load, physical QR readability, hosted operation or human accessibility acceptance. Follow [provenance rules](docs/adr/0001-wled-clean-room-y-licencia-del-proyecto.md) for WLED-inspired work.
''')

write('docs/firmware/scenes.md', r'''
# LED scenes

`SceneV1` is a named body recipe: A/B effects and palettes, base/accent colors, speed, intensity, body level, transition, mirror and Show eligibility. It cannot change status, alerts, GNSS, Home, network settings, global brightness or current calibration.

## Catalog and playback

| IDs | Meaning |
| --- | --- |
| `0` | No scene |
| `1..4` | Immutable `high_visibility`, `calm`, `active`, `party` |
| `128..131` | Four user slots |
| `255` | Invalid; other IDs are reserved |

The player owns one pending command, one copied active recipe and an eight-ID Show bag. Apply/cancel is volatile and consumed at the LED tick; the latest pending command wins. Manual apply does not change the persisted mode. Explicit mode change or cancel clears it. Editing the active slot marks the copied recipe stale until reapplied.

Show shuffles every valid eligible scene once per bag and avoids repeating the previous bag's last ID when alternatives exist. Each scene gets 30 seconds of active time. Welcome and Day Mode pause playback time. Scene transition and branch settings apply; status and alerts remain authoritative.

## Storage

Scene schema, record and registry versions are independently versioned at `1`. A canonical little-endian scene is 44 bytes. Each `SCN1` bank is 196 bytes with four slots and CRC-32/IEEE; two banks hold 392 bytes of payload in `dogrgb_scn`, keys `scene_a` and `scene_b`.

Writes validate the whole recipe/bank, alternate banks and verify readback. No-op saves do not write or advance generation. Unknown future/oversized records are read-only. Corrupt or ambiguous state requires explicit recovery that writes two equivalent valid banks. Scene-store failure does not prevent boot or built-in playback.

## API

Seven routes under `/api/v1/led/scenes` provide list, apply, cancel, save, delete, export and import. Mutations require the common intent/PIN guard and JSON. Save/delete/import use `expected_generation`; stale clients receive a conflict.

Bodies require `Content-Length`, at most 4,096 bytes and nesting depth at most 6. Import validates a scratch bank before atomic replace-all; `dry_run` never mutates. Exports contain user recipes only. The `/config` editor derives controls from firmware capabilities.

See [HTTP schemas and errors](../portal/api.md#led-api-v1), [effects](effects.md) and [LED policy](leds.md). Native tests cover the player, wire format and injected storage faults; physical write latency, heap, orientation and output remain [acceptance gates](../roadmap.md).
''')

write('docs/portal/identity.md', r'''
# Display identity API

Local contact identity is supported only by `DOG_RGB_DISPLAY_LVGL` builds. It is independent of LED/GNSS configuration and cloud identity. Reads are visible to local-network clients.

## Read

`GET /api/identity` returns `200`. Classic returns `{"schema_version":1,"supported":false}`. Display returns:

```json
{
  "schema_version": 1,
  "supported": true,
  "configured": true,
  "name": "FREYA",
  "phone": "+100000000000",
  "qr_kind": "whatsapp",
  "qr_payload": "https://wa.me/100000000000",
  "generation": 1
}
```

The example number is synthetic. `configured` requires valid name and phone. `qr_kind` is `whatsapp`, `call` or `disabled`; the payload is derived from the phone and is never an editable URL. Empty identity has empty text/payload, disabled QR and initially generation 0. Generation can wrap to 0; it is not a configured flag.

## Write

`POST /api/identity` requires `X-Dog-Portal: 1` and the enabled optional PIN. Send exactly four fields, no partial update, within 512 bytes:

```json
{"name":"FREYA","phone":"+100000000000","qr_kind":"whatsapp","expected_generation":1}
```

- Name: at most 48 UTF-8 bytes / 24 code points from the alphabet in `include/display/identity.h`. The editor normalizes NFC; firmware rejects decomposed/invalid/unsupported text and leading, trailing or repeated spaces.
- Phone: explicit `+`, 7–15 digits, first digit nonzero. Spaces, parentheses and hyphens are accepted for normalization. No country inference or account/number-assignment check.
- Embedded NUL is rejected. `expected_generation` is the uint32 from the last read.
- Clear by sending empty name/phone and `qr_kind:"disabled"` with the current generation.

| HTTP | Result |
| --- | --- |
| 200 | Canonical GET shape; no-op does not write or advance generation |
| 400 | `body`, `fields`, `name`, `phone` or `qr_kind` |
| 401 / 403 | Existing PIN / intent guard |
| 404 | `unsupported` on Classic |
| 409 | `conflict`; retain draft and reload explicitly |
| 413 | `body_size`; application bound, not a pre-receive WebServer limit |
| 500 | `storage`; save/readback unconfirmed, retain draft and reload |

Identity save/clear and runtime configuration have separate drafts/commits. A preview shows text, phone and destination; it is not a pixel-accurate LVGL preview.

## Persistence

Keys `id_a` / `id_b` use the existing config namespace. The packed little-endian record is 84 bytes: magic `0x49475244`, uint16 version `1`, uint16 size `84`, uint32 generation, name[49], phone[17], channel byte, zero reserved byte and CRC-32/IEEE over the first 80 bytes. Channel IDs: disabled `0`, WhatsApp `1`, call `2`. Padding and canonical strings are validated.

Load selects the newest valid bank with modular generation comparison. Two invalid banks produce empty RAM without automatic writes. Save alternates banks and publishes RAM after exact readback. If the write completes but verification fails, RAM retains its verified state while a reboot may select the new record; the API must not report success or promise the prior contact survived.

Clear writes an empty valid record. Corruption recovery can select the older contact, so clearing is not secure erasure. Runtime configuration reset leaves identity intact.

The portal loads identity before Display initialization. The display copies RAM snapshots; save neither navigates nor wakes the panel. Clearing the visible Identity page selects Activity. See [display behavior](../../Platformio/Dog-RGB/docs/display.md) and [diagnostic USB transport](../../Platformio/Dog-RGB/docs/identity-usb.md).
''')

write('Platformio/Dog-RGB/docs/display.md', r'''
# Display

The experimental Waveshare No Touch V2 profile shares the normal collar core. Only `waveshare_lcd169` and `waveshare_lcd169_displaycheck` include graphics. Physical board identification, integrated peripherals and optical acceptance remain open.

## Rendering contract

| Setting | Value |
| --- | --- |
| Libraries | Arduino_GFX 1.6.7, LVGL 8.4.0 |
| Panel | ST7789, portrait 240×280, rotation 0, IPS inversion, offsets `0,20,0,0` |
| SPI | 40 MHz; DC/CS/SCK/MOSI/RESET GPIO4/5/6/7/8, no MISO |
| Backlight | GPIO15, active high; no product PWM setting |
| Memory | RGB565 partial buffer 9,600 B; LVGL pool 48 KiB |
| Layout | Black background; safe region 192×244, margins 24/20 |

The service initializes once after domain/portal startup and draws a complete initial frame before lighting the panel. Detected failure disables only the UI, without retrying/resetting the collar. Write-only SPI success does not prove panel presence or optical correctness.

Views and objects are allocated once. Value snapshots are copied from GNSS, Wi-Fi, LED and identity owners; the UI does not recalculate metrics, read passwords or change radio/policy. Only the selected view updates at the normal one-second sampling cadence; equal strings do not invalidate labels. GPS remains first in the cooperative loop; display follows LED/HTTP work.

## Pages

| Page | Data and limits |
| --- | --- |
| Identity | Full validated name/phone; derived WhatsApp/call QR or contact-only layout |
| Activity | Registered distance/date, usable speed and GNSS/LED mode; retained data is not necessarily today or one walk |
| Wi-Fi | AP/STA state, SSID and current addresses; association is not Internet access |
| State | GNSS reception and effective LED policy; no physical strip feedback |

Valid identity changes boot/navigation to **Identity → Activity → Wi-Fi → State**. Without it, boot is Activity and the cycle has three pages. Disabling only QR retains the Identity page. Saving preserves page/backlight state; clearing visible identity selects Activity and preserves darkness.

Owner pages share the validated dog name or `RGB DOG`, page count and explicit `GPS DEMO` label for simulated GNSS. Name headers abbreviate by measured UTF-8 width; stored/full Identity names remain unchanged. SSIDs support printable ASCII; unsupported bytes display an explicit fallback with the available IP. No rest, health, battery or Internet claim is inferred.

## Input and inactivity

BOOT/GPIO0 is active-low with pull-up. Debounce is 30 ms; one event occurs on short release. Holds ≥1.5 s, bounce and an initially held button produce no navigation. BOOT still selects the bootloader when held during reset; SYS_OUT/SYS_EN are separate.

A short release while dark wakes the selected page without advancing. The next release advances. Product inactivity timeout is disabled. Stage 3 can enable a volatile 30-second timer; it turns off only backlight, not sampling, GNSS, LEDs, Wi-Fi or storage. GNSS/radio events do not renew it. No battery-saving claim follows from this diagnostic.

## Stage-3 USB controls

Only `waveshare_lcd169_displaycheck` accepts these controls at 115200 baud, bounded to eight input bytes per service. The product target has no diagnostic command handler.

| Command | Action |
| --- | --- |
| `a`, `c`, `e`, `p` | Select Activity, Wi-Fi, State, Identity (Activity if unconfigured) |
| `n` | Inject BOOT short release, including wake-only behavior |
| `t` | Panel bars/border diagnostic |
| `s`, `l` | Basic text / LVGL renderer |
| `f`, `v` | GPS-only synthetic presentation / live data |
| `b` | Toggle backlight |
| `d` | Pause/resume display service |
| `i`, `o` | Enable/restart 30-second inactivity timer / disable timer |
| `r` | Reset drawing statistics; interaction/QR/timeout counters remain |
| `j` | Arm the separate [identity USB protocol](identity-usb.md) |

Fixtures never enter GNSS metrics/storage. Wi-Fi and LED policy remain real under `GPS DEMO`. Pause retains state and blocks SPI until resume. Direct page/fixture commands do not renew inactivity. Disabling the timer does not automatically light a dark screen.

Stage 3 skips welcome and caps effective LED brightness at 16/255 and the enabled estimated budget at 1,000 mA. Normal configuration and GNSS data still persist. It has no USB-disconnect LED shutdown; stored brightness can exceed the bench cap and takes effect again in a product build.

## Verification

Run the [shared simulator](../../../tools/display-simulator/README.md) and firmware [checks](../../../docs/testing.md). Real views, store/HTTP handlers and service are compiled with fake Arduino/SPI/Preferences; QR captures are independently decoded. Fake timing is not an ESP32 benchmark.

On hardware, verify board/panel orientation, RGB order, small text, complete contact/QR payload, actual BOOT/wake, save/reboot/clear, dark edits and joint GNSS/LED/HTTP behavior. Record LCD/loop/heap/UART statistics with supply and firmware identity. Physical QR readability and portable current/thermal behavior remain separate gates.
''')

write('Platformio/Dog-RGB/docs/identity-usb.md', r'''
# Identity over diagnostic USB

Available only in `waveshare_lcd169_displaycheck`. Physical USB access uses the same identity validation/A/B store as HTTP, without the HTTP PIN. Product Display and Classic exclude this transport.

## Usage

From `Platformio/Dog-RGB`, use Python with pyserial and close other readers of the selected port:

```powershell
python tools/identity_usb.py --port COM6 --status
python tools/identity_usb.py --port COM6 --name FREYA --phone +100000000000 --channel whatsapp
```

Select the actual port/contact; the example number is synthetic. `--channel call` creates a call QR; `disabled` retains name/phone without QR. `--clear` writes empty identity. `--output receipt.json` saves a receipt without name/phone. The helper normalizes NFC and never navigates, flashes, opens links or sends messages.

## Protocol v1

1. Send `j`; wait for `[IDENTITY] version=1 status=ready generation=N configured=0|1`.
2. Send at most 512 bytes of JSON plus LF (CRLF accepted), using the exact four [HTTP fields](../../../docs/portal/identity.md#write).
3. Receive one result plus generation/configured state: `saved`, `unchanged`, `conflict`, `name`, `phone`, `qr_kind`, `fields`, `body_size`, `timeout`, `storage` or `cancelled`.

Empty line cancels without a write, so `--status` does not mutate NVS. The total deadline is 5,000 ms from `j`, with at most eight USB bytes consumed per service. NVS commit is synchronous and measured separately.

Overflow, embedded NUL or timeout drains input through LF; remaining JSON must never become LCD commands. The helper discards old input, sends LF+j and requires a fresh compatible handshake before transmitting contact data. Errors/conflicts do not retry or overwrite implicitly. Serial replies never echo contact/QR payloads.

Saving neither navigates nor wakes. Select `p` separately. `display_bench.py` accepts only LCD commands and rejects `j`.

## Verification

The [display simulator](../../../tools/display-simulator/README.md) covers framing, deadlines, drain, generation conflicts, store faults and dark edits. Firmware host discovery includes client handshake/rejection tests. This transport does not establish AP connectivity, physical GNSS/strip behavior or NVS power-cut safety.
''')

# Keep the useful staged electrical procedure, remove execution diary and stale renderer claims.
boards = original['Platformio/Dog-RGB/docs/boards.md']
boards = '# Board profiles and bring-up\n\nClassic is the default. Waveshare is a No Touch V2 candidate; verify the actual PCB and power-hold circuit before upload. Builds do not identify hardware or establish physical acceptance.\n\n' + boards[boards.index('## Targets and ownership'):]
boards = boards[:boards.index('No physical upload or bench measurement')]
boards = boards.replace('Shared application with candidate pins and I3 text LCD; physical integration unverified','Shared application and LVGL Activity/Wi-Fi/State/Identity pages; physical integration unverified')
boards = boards.replace('Normal core, I2 bench limits and text LCD with USB calibration/pause controls','Normal core, bench limits and shared LVGL display with USB diagnostics')
boards = boards.replace('Waveshare product and stage 3 include the text LCD.', 'Waveshare product and stage 3 include the shared LVGL display.')
boards = re.sub(r'Only the Waveshare product and\nstage 3 add Arduino_GFX and, from \[I5\].*?comparison\.', 'Only the Waveshare product and stage 3 add Arduino_GFX 1.6.7 and LVGL 8.4.0. The basic text renderer remains a stage-3 diagnostic.', boards, flags=re.S)
boards = re.sub(r'5\. Save results.*?Proceed to LED bench work after I0 physical acceptance\.', '5. Record board identity, supply, firmware hash and observations. USB-only testing does not prove battery retention/cutoff. Continue after power/USB acceptance.', boards, flags=re.S)
boards = re.sub(r'The \[I1 baseline\].*?LCD remains I3\.', 'Complete physical LED acceptance before joint GNSS/display testing.', boards, flags=re.S)
boards += '\nBOOT/GPIO0 is reserved for short-release navigation in the graphics targets. SYS_OUT/SYS_EN retain their power roles. See [Display](display.md) for rendering, controls and optical acceptance.\n'
write('Platformio/Dog-RGB/docs/boards.md', boards)

write('tools/display-simulator/README.md', r'''
# Shared display simulator

Builds the firmware's actual LVGL 8.4.0 views, formatters, contact store/HTTP handlers and display service. It uses a headless framebuffer and fake Arduino/SPI/Preferences, not a second UI implementation.

## Run

Requirements: Python 3, CMake ≥3.20, Ninja and C/C++17 compiler. From the repository root:

```powershell
pio pkg install -d Platformio/Dog-RGB -e waveshare_lcd169_displaycheck
python tools/display-simulator/render.py
```

The runner builds and executes CTest, then writes PNGs and a source/image-hash manifest under `tools/display-simulator/output`. Main fixtures cover Activity, Wi-Fi, State and Unicode/demo name headers. `CC`/`CXX` may select the compiler before initial CMake configuration. A separate LVGL checkout can be selected with `-DLVGL_SOURCE_DIR=...`; it must be version 8.4.0.

## Independent QR decoding

Optional pinned Python tools validate exact QR payloads and absence of codes in fallback states:

```powershell
python -m venv tools/display-simulator/build/qr-venv
tools/display-simulator/build/qr-venv/Scripts/python.exe -m pip install -r tools/display-simulator/requirements-qr.txt
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_qr.py
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_identity.py
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_port.py
```

On POSIX, use `build/qr-venv/bin/python` instead of `Scripts/python.exe`. Outputs go to `output/qr`, `output/identity-4bpp` and `output/port`. Public fixtures use synthetic contacts. Optional `--name` and `--phone` previews contain personal data and must remain local. No QR URL is opened.

`render_identity.py --bpp 2` uses the optional [font variant](../display-fonts/README.md) in a separate build; the default is 4 bpp.

## Coverage and limits

- Bounds, overlap, complete identity text, accepted glyphs, long/invalid names and QR quiet zones.
- No-op invalidation/QR caching and stable memory across repeated updates/navigation.
- Three/four-page boot/navigation, debounce/long holds, wake-only input, inactivity rollover and dark edits.
- Real contact A/B codec, conflict/no-op/clear, corrupt/truncated writes and indeterminate readback.
- Display/Classic HTTP capability and write behavior; bounded USB framing, deadlines and drain.

Fake SPI times are not hardware measurements. Passing captures do not establish panel readability, physical QR scanning, NVS power-cut behavior, radio connectivity or full-system performance. Use the [Display reference](../../Platformio/Dog-RGB/docs/display.md) for physical acceptance.
''')

write('docs/cloud/README.md', r'''
# Optional cloud

The Next.js owner portal, local Supabase backend and synthetic device simulator are implemented. Physical collar HTTPS synchronization, hosted deployment and operational schedules are pending. The embedded collar portal remains independent.

## Local setup

Use the Node/npm versions in [`.node-version`](../../.node-version) and [`package.json`](../../package.json), the Supabase CLI version in [`.supabase-version`](../../.supabase-version), and a running Docker-compatible engine.

From the repository root:

```powershell
npm ci
supabase start
```

Copy [`apps/portal/.env.example`](../../apps/portal/.env.example) to `apps/portal/.env.local`. Obtain the local URL/publishable key from `supabase status -o env`; set `PORTAL_SITE_ORIGIN` to the exact local origin. Keep environment files ignored. The example is not a deployable credential configuration.

```powershell
npm run portal:dev
```

For a fresh **disposable** local database, the maintained gate provisions synthetic data and exercises migrations, Edge Functions and the simulator:

```powershell
npm run phase1:local -- --clean
```

This resets this repository's local stack. Preserve wanted development data first. Never point reset/drill tools at a hosted or linked database. To develop Edge Functions manually, copy their [environment example](../../supabase/functions/.env.example), replace both peppers independently, and pass the ignored file to `supabase functions serve --env-file supabase/functions/.env`.

## References

- [Owner portal](portal.md): routes, authorization and bounded workflows.
- [Testing](testing.md): clean gates, browser matrix and evidence handling.
- [Firmware foundation](firmware.md) and [outbox](outbox.md): implementation versus integration gaps.
- [Device protocol](../../contracts/device-v1/README.md): normative schemas and replay.
- [Field map](field-map.md): sync ownership and local-only data.
- [Operations](operations.md), [retention](retention-policy.md), [privacy](privacy-data-flow.md), [threat model](threat-model.md) and [credentials](credential-checklist.md).
- [Release work](../roadmap.md): human, hosted and physical acceptance.
''')

write('docs/cloud/portal.md', r'''
# Owner portal

`apps/portal` implements the optional local Next.js website against Supabase and synthetic devices. Automated local workflows exist; named human accessibility acceptance, remote release CI and hosted operation remain separate gates.

## Routes

| Surface | Routes and behavior |
| --- | --- |
| Account entry | `/signup`, `/login`, `/forgot-password`, `/auth/confirm`; generic recovery/resend responses, trusted origin and authorized return paths |
| Dog selection | `/onboarding`: zero memberships creates a dog; one opens Today; multiple open selection |
| Daily/history | `/app/[dogId]/today`, `/history`, `/recordings/[recordingId]`; explicit freshness, quality, gaps and versioned summaries |
| Collar/config | `/app/[dogId]/collars`, `/configuration`; claim/revoke/re-enrollment, diagnostics and brightness desired/reported state |
| Data lifecycle | `/app/[dogId]/data`, `/account`, `/account/deletion-receipt`, `/privacy` |
| Private downloads | `/app/[dogId]/data/export`, `/recordings/[recordingId]/geojson` |
| Account protocol | `POST /account/finalize`: prepare/finalize/receipt/acknowledge; not a navigation page |

Abbreviated dog routes share `/app/[dogId]`. Owners can correct dog names; IANA timezone is read-only. History accepts at most 366 inclusive local dates and uses keyset pagination, including explicit unknown-time recordings. Detail uses segmented SVG and a point table; external map tiles are optional and not integrated.

## Authorization and freshness

Server Components read through the user-scoped SSR client and server-only DAL, not the portal's own HTTP handlers. Every action/download checks fresh authentication and dog membership. Explicit grants and RLS enforce exposed `api` data; privileged internals stay in `private`. Page visibility and public IDs grant no authority.

Configuration shows desired/pending/applied/rejected/unsupported state. `Applied` requires the exact reported version/hash. Summaries require supported algorithm/provenance; receipt time is not observation freshness. Missing coverage is unknown, never rest or inactivity.

At most one active collar is allowed per dog. A current owner can re-enroll the same revoked UUID for the same dog with a fresh claim/credential. History/identity remain; old credentials cannot sync or revoke the replacement. Cross-dog/owner transfer is deferred. Physical credential replacement is not implemented.

## Exports

Owner-only JSON snapshots and recording GeoJSON include retained data, units, timezone, quality/gaps, configuration and available summaries. They exclude device secrets/internal receipts and use private `no-store` attachments without public URLs or retained export objects.

The entire export fails above 25,000 points, 50,000 rows or 16 MiB; no truncated response is labeled complete. Additional collection caps: 100 collars; 10,000 each recordings/loss markers/recording summaries/config revisions; 5,000 daily summaries; 1,000 each config heads/reports. Large JSON fields have 64 KiB per-field and 8 MiB aggregate guards. The snapshot is one SQL statement, with an 8-second authenticated-role statement timeout and 15-second DAL abort.

`/account` links to owned-dog exports; Auth/profile/membership export and asynchronous large exports are not implemented.

## Deletion

Dog deletion requires owner authorization, explicit phrase and password reauthentication. Access/ingestion close before bounded purge; durable status/retry survives logout/login. Revoke alone preserves history.

Account deletion confirms all owned dogs and other members' impact. Viewer/editor memberships detach. Unresolved creator references without ownership block before mutation; ownership never transfers implicitly. Pending accounts can inspect/retry deletion but cannot perform ordinary dog operations. Auth is removed only after owned-data purges complete.

Finalization requires a live identity and password AMR at most five minutes old. Preparation stores a fresh reauthenticated session and HttpOnly/Strict request cookie. If responses are lost, `/account/deletion-receipt` retries the exact receipt; only valid completion and acknowledgement clear recovery state. An unexpired signed JWT bound to the requester authorizes receipt recovery after Auth deletion; request ID alone does not. This JWKS path requires asymmetric signing keys, not legacy HS256 tokens.

Local browser tests cover lost finalization/receipt/acknowledgement responses, reload, expired JWT, malformed receipt, cross-subject denial and retry. Hosted scheduling and post-backup account/Auth deletion replay remain open; see [operations](operations.md).

## Verification

Use [cloud testing](testing.md). Keep automation artifacts to fixed stages, counts, flags, hashes and bounded safe error categories. No credentials, request bodies, route coordinates, raw downloads or private screenshots belong in retained reports. Human keyboard/status/zoom/reduced-motion acceptance remains in [open work](../roadmap.md).
''')

write('docs/cloud/firmware.md', r'''
# Cloud firmware foundation

These native components are isolated from production startup/GNSS. Current Track v2 capture/read/export remains active. No durable v3 sink, observation scheduler, credential lifecycle or HTTPS client exists.

## Device identity

The portable identity service and NVS adapter provide explicit non-nil UUID provisioning, durable boot allocation and per-boot point/chunk reservations.

- Provision only an empty store; recheck both keys immediately before writing identical genesis records. Random UUID generation/enrollment policy belongs to the caller, never a MAC-derived identity.
- Both banks must validate, share UUID and have adjacent generations; equal generations are allowed only for identical genesis records. Missing/corrupt/future/oversized/I/O-failed pairs block allocation. Never fall back to an older boot or auto-repair.
- Publish a new boot only after write/commit, exact target readback and both-bank revalidation. Zero is reserved for legacy data; uint32 exhaustion never wraps.
- Failed/ambiguous mutation locks the instance. A fresh mount inspects durable bytes; committed but unreturned reservations are consumed. Intact prior storage may retry an unissued number.
- An active session cannot remount/reset counters or allocate another boot. `reserve_chunk` atomically reserves one chunk and 1–96 contiguous points; invalid requests leave state/output unchanged.

NVS keys are logical banks in one partition, not isolated physical sectors. Full erasure is indistinguishable from factory blank; re-enrollment requires an explicit fresh-UUID/outbox/history policy. CRC collisions and valid-image rollback are not prevented.

## Track v3 codec

[`track_v3.h`](../../Platformio/Dog-RGB/include/track/track_v3.h) defines caller-owned, allocation-free little-endian encoding: 16-byte points, 92-byte chunk header, 1–96 points and maximum 1,628-byte frame. UUID byte order, payload CRC/SHA-256 and header CRC are explicit.

The SHA-256 callback is mandatory; the target adapter uses SDK mbedTLS. Encoding/decoding validates the whole frame before publishing output: lengths, flags, reserved fields, time quality, coordinate/sequence bounds, monotonic timestamps, time bounds and hashes.

The native decoder also rejects point-sequence overflow. The frozen Python encoder rejects that input; its decoder lacks this check. Such frames are invalid, not interoperability fixtures. Preserve the frozen oracle/source pins when changing native code.

## Chunk assembler

[`chunk_assembler.h`](../../Platformio/Dog-RGB/include/track/chunk_assembler.h) owns one fixed batch of at most 96 classified observations. Construction performs no I/O; an active durable identity grant is required.

- Validate before mutation. Native no-fix observations require explicit gap, zero coordinates and unavailable speed. Known time quality requires nonzero UTC; unknown requires zero UTC. Legacy observations are rejected; equal timestamps are allowed.
- Capacity, time-quality change or backward UTC requires sealing before retrying the unconsumed incoming point.
- Seal reserves identity once, freezes points/final flag and retains that reservation after hash failure. Missing hash callback fails before reservation. Successful encoding retains exact bytes; retry does not hash/allocate again.
- The synchronous sink may return success only after verified **durable local commit**. False/ambiguous commit retains the exact frame for idempotent retry. This is not a server ACK or reclaim permission.
- Successful final handoff closes the assembler for the boot. Reopening requires a new durably allocated boot; empty final chunks and reset/drop/overwrite operations are unsupported.

Use one serialized, noncopyable assembler per granted identity session. Service/hash context outlive it; callbacks neither retain nor modify borrowed buffers. Mutating reentry is rejected. Allocate the fixed staging/frame buffers in a long-lived owner with an explicit RAM budget.

The pending batch is volatile and lost on reset. A new boot prevents identity reuse, not data loss. Do not activate runtime v3 capture until durable sealing and pressure/loss accounting exist.

## Verification

From the repository root:

```powershell
python -m unittest discover -s Platformio/Dog-RGB/test -p "test_device_identity*.py" -v
python -m unittest discover -s Platformio/Dog-RGB/test -p "test_track_v3_native.py" -v
python -m unittest discover -s Platformio/Dog-RGB/test -p "test_chunk_assembler_native.py" -v
npm run contracts:test
```

Native wrappers honor `CXX`/`CXXFLAGS`; supported host compilers can run ASan/UBSan. Tests cover byte-image faults, sequence exhaustion, exact Python-oracle frames, aliasing, output sentinels, stable retry and terminal-final state. Target builds do not measure active memory, latency, wear or power. Remaining integration and physical gates are in [open work](../roadmap.md).
''')

write('docs/cloud/outbox.md', r'''
# Durable outbox design

The raw-ring host model is implemented and independently reviewed within its modeled faults. The ESP32 raw flash driver, runtime integration and physical acceptance are not implemented. Local Track v2 uses its separate `tracknvs` ring.

## Geometry

Proposed use of the currently unused `0x150000`-byte `spiffs` partition:

| Allocation | Capacity |
| --- | ---: |
| Erase sectors | 336 × 4 KiB |
| Metadata journal | 2 sectors |
| Emergency/loss journal | 2 independently erasable sectors |
| Data | 332 sectors × two 2,048-byte slots |
| Chunk / point slots | 664 / 63,744 at 96 points per chunk |

These are host geometry estimates. Firmware must version the layout, rename/retype the partition before raw use and never mount a filesystem over it simultaneously. No initialization may format `tracknvs`.

## Invariants

1. Recover from bytes on every fresh mount; RAM/send state is not recovery evidence.
2. Seal immutable, independently validated chunks. Persist one device's increasing `(boot, chunk)` high-water, including full-storage loss identities; never reuse reclaimed identities.
3. Accept ACK only for a sent chunk with matching device, boot, chunk, digest, point count and through-sequence. HTTP status alone grants nothing.
4. Persist ACK before reclaim. Out-of-order ACKs never bridge an unacknowledged hole.
5. Bind reclaim intent to exact sector ordinals, verify before erase and durably consume the intent before refill. Erase only sectors without live unacknowledged data.
6. Persist loss/ACK transitions in independent A/B emergency storage. Counter/input overflow fails before mutation.
7. Invalid committed journal/emergency bodies or unreadable marked slot headers make mutation/reclaim read-only. Complete valid bodies can recover despite damaged commit markers. Quarantine corrupt payloads while preserving valid identity/ordinal metadata.
8. Reject unknown metadata/layout versions. Never reinterpret ambiguous media as blank writable storage.
9. At pressure/full capacity, retain unacknowledged data, bound ordinary capture and record explicit missing intervals. Preserve local collar operation.

The host model enforces NOR 1→0 programming and aligned sector erases. Its journal v3 and emergency v2 encode identity high-water; loss-ACK generation is uint32 and exhaustion fails closed. Simultaneous loss of publication markers plus body damage, CRC collisions and arbitrary multi-fault destruction are outside the proof.

Legacy boot-zero conversion must precede native boots or use a separately specified migration. Preserve minute-precision/coordinate-only limitations with `LEGACY_V2`; do not invent speed, coverage or movement evidence.

## Reproduce

```powershell
python -m unittest discover -s tools/cloud_phase0 -p "test_*.py" -v
python tools/cloud_phase0/generate_evidence.py
python tools/cloud_phase0/review_readiness_test.py -v
```

The evidence generator emits canonical UTF-8/LF JSON with fixed seeds and no timestamps. [`verify_review_candidate.py`](../../tools/cloud_phase0/verify_review_candidate.py) checks clean-tree ancestry, nine frozen source pins, required regressions and exact evidence. It reports readiness, never review acceptance; a dirty documentation worktree is expected to be ineligible.

Keep the machine-readable readiness artifacts and frozen source manifests as test provenance. Current narrative documentation does not replace those byte-level fixtures. LittleFS remains an idealized comparison, not a measured implementation fallback.

## Physical gate

Execute the cycle/power-cut and resource measurements in [open work](../roadmap.md#offline-firmware-foundation) before enabling capture. Host capacity/recovery/wear calculations do not establish ESP32 timing, endurance, brownout behavior or energy. See [storage decision](../adr/0007-durable-telemetry-outbox-and-storage.md).
''')

# Remaining topic-specific replacements and finalization are appended below.
