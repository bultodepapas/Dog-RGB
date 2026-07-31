# Dog-RGB Firmware and Electronics Audit

**Audit date:** 2026-07-31  
**Scope:** repository at `C:\DEVELOPMENT\Dog-RGB`, with the active firmware under `Platformio/Dog-RGB`  
**Role:** electronics/embedded-systems engineering review  
**Evidence rule:** the repository code is the only source of truth for the product's implemented behavior. External material is used only to challenge assumptions and design improvements.

## Executive assessment

Dog-RGB is a credible, buildable ESP32-S3 DIY project with a sensibly modular firmware structure, strict NMEA checksum parsing, bounded LED buffers, streamed track output, animated LED effects, and useful compile-time headroom. The current firmware builds successfully for the Seeed XIAO ESP32-S3 at **15.0% RAM** and **28.4% flash**.

The local portal's simple default access is intentional for this DIY project: the owner can connect immediately and can change the AP credentials afterward. This audit therefore treats it as a usability choice, not a release-blocking security defect. Commercial IoT provisioning and enterprise-style access control are explicitly outside the current objective.

The largest functional defect found was the track ring: it was dimensioned for 1,440 samples but normally overwrote history after approximately **7.5 minutes**. This audit iteration fixes it with partial-chunk rewriting, one staging chunk, and a dedicated 192 KiB track NVS partition. A host simulation verifies the latest 1,440 points through multiple ring wraps. Other high-priority code issues remain: cross-task Wi-Fi state races, corruption-unprotected records, and synchronous HTTP work that can delay UART draining.

**Recommendation:** keep the simple DIY interaction model and prioritize the demonstrated software defects: track retention/integrity, Wi-Fi event ownership, persistence recovery, UART servicing, and executable tests.

## Scope and architecture recovered from code

The active firmware is an Arduino application built with PlatformIO for `seeed_xiao_esp32s3` using Espressif32 6.7.0. It integrates:

- a XIAO ESP32-S3;
- an EBYTE E108-GN02 GNSS receiver over UART;
- two 24-pixel SK6812 RGBW strips (48 pixels total);
- a permanently available Wi-Fi access point plus optional station mode;
- a synchronous `WebServer` configuration/telemetry portal;
- NVS/`Preferences` persistence for configuration, home, sessions, metrics, and track chunks;
- optional BLE code, disabled by the current configuration;
- speed, simple, show, and day-mode LED behavior.

The firmware is split into GPS, Wi-Fi, web, LED, BLE, configuration, system-state, and orchestrator modules. The principal risk concentration is in `gps.cpp`, `portal_http.cpp`, `led_ui.cpp`, and `wifi_mgr.cpp`, rather than in `main.cpp`.

## Verification performed

| Check | Result | What it proves / limitation |
|---|---|---|
| `pio run -e seeed_xiao_esp32s3` | **Pass** | Firmware compiles and links. RAM: 49,260/327,680 bytes (15.0%); flash: 949,033/3,342,336 bytes (28.4%). It does not prove runtime behavior. |
| Compiler warnings | **Needs work** | ArduinoJson 7 reports deprecated `StaticJsonDocument` and `containsKey()` usage in `src/web/portal_http.cpp`. |
| `python -m unittest discover -s test -p "test_*.py" -v` | **7/7 pass** | Includes four day-mode source-contract tests, a continuous 2,400-point track-ring simulation, firmware contract guards, and a dedicated-partition guard. The model is host-side, not HIL. |
| `pio check -e seeed_xiao_esp32s3 --skip-packages` | **Pass with findings** | No medium/high project-source defect was reported. Cppcheck produced low-severity project findings and one third-party ArduinoJson preprocessor false positive. The current command does not fail the build on defects and translation-unit analysis causes unused-code noise. |
| `npm ci` | **Pass** | Three packages installed; npm reported zero known vulnerabilities in this small host-tool dependency set. |
| Portal page extraction with `python` | **Pass** | The generated portal HTML can be extracted on this machine when invoked directly. |
| Playwright portal suite | **Blocked before tests** | The preview server cannot start on Windows because `server.mjs` uses URL pathname handling and a hard-coded `python3` command; visual npm scripts also use POSIX environment assignment. No UI assertion ran. |
| Hardware-in-loop, power, RF, GNSS replay, and power-cut testing | **Not available** | Real receiver behavior, timing, power draw, and recovery were not measured; no failure claim is inferred solely from that absence. |

## Risk register

No critical code-backed defect was identified. Severity meanings: **High** defeats a primary function or can cause silent corruption/instability; **Medium** reduces correctness, diagnosability, or portability; **Low** is maintainability/dead-code debt; **Informational** records an intentional design choice or optional measurement. “Confirmed” means directly demonstrated by code/build behavior; missing hardware measurements are not classified as defects.

### AUD-001 — Informational — Intentionally simple local portal access

**Evidence:** `include/config.h:119-120` defines the initial `DogRGB` / `Dog12345` AP credentials. `src/web/portal_http.cpp:716-740` lets the user change them or intentionally select an open AP. `src/web/portal_http.cpp:886-910` exposes the local setup and telemetry routes without an additional login layer.

**Assessment:** this matches the project's DIY objective: fast initial connection, no account system, and user-controlled credentials. Adding mandatory per-device secrets, QR provisioning, sessions, or CSRF infrastructure would increase setup and recovery complexity without serving the declared priority. The only residual trade-off is that a user who leaves the default password or chooses an open AP accepts local access by nearby clients.

**Action:** no mandatory firmware change. Keep credential changes easy, mention the default/open-AP trade-off in the DIY setup documentation, and provide a one-action reset path so the owner cannot permanently lock themselves out.

### AUD-002 — Informational — User-adjustable animated LED output

**Evidence:** `include/config.h:83` defaults brightness to 77 (~30%). `src/web/portal_http.cpp:566-571` deliberately lets the user select 1 through 255, while `src/led/led_ui.cpp` renders time-varying effects and applies the selected global brightness. The LEDs are an effect output that turns pixels on and off; the code does not assert continuous full-white operation.

**Assessment:** the adjustable range and dynamic effects are intentional DIY functionality. The repository does not include enough hardware evidence or measured duty-cycle data to conclude that brightness 255 is unsafe, and the absence of a current estimator is not itself a firmware defect.

**Action:** no mandatory code change. If the builder wants quantitative battery-runtime or converter-sizing data, measure representative effects at several brightness settings and document the results. Add a limiter only if the actual hardware demonstrates a need.

### AUD-003 — Resolved High — The two-hour track ring retained about 7.5 minutes

**Evidence:** `src/gps/gps.cpp:110-116` sets five-second samples, 1,440 maximum points, 48 points per chunk, a 15-second flush, and only 30 chunk slots. `flush_track_chunk()` flushes on time or fullness at `:929-930`, so steady valid movement produces roughly three points per stored chunk. After 30 time flushes—about 450 seconds—the ring overwrites its oldest chunk at `:937-942`. The `persisted_points` counter nevertheless accumulates toward 1,440 at `:961-963`.

**Impact:** the implementation contradicts the represented two-hour history and can silently lose roughly 94% of the promised moving history. Metadata can temporarily overstate what remains in the ring.

**Implemented:** the active partial chunk is now rewritten in place every 15 seconds instead of consuming a new ring slot. A 31st staging chunk prevents the partial write from evicting any part of the 1,440-point window; iteration trims only points older than the two-hour limit. Track storage moved from the 20 KiB general NVS area to a dedicated 192 KiB `tracknvs` partition, while configuration remains in the original NVS. A one-time migration clears obsolete track keys from general NVS without clearing configuration. Chunk writes must report their full byte count before RAM metadata advances. `test_track_retention.py` simulates 2,400 continuous samples and verifies the exact latest-point window after every insertion. Remaining work under AUD-006 covers CRC/power-cut recovery rather than retention geometry.

### AUD-004 — High — Wi-Fi callback and loop share mutable state across tasks

**Evidence:** `src/wifi/wifi_mgr.cpp:165-228` changes connection flags, retry/diagnostic data, AP client state, and timestamps from `on_wifi_event()`. The callback is registered at `:412`. Those same ordinary globals are read and written by the main loop/state machine without a queue, mutex, critical section, or atomic abstraction.

**Impact:** Arduino-ESP32 Wi-Fi events execute in a separate FreeRTOS task. The current design therefore has C++ data races and can observe inconsistent transition state or lose updates under connect/disconnect bursts.

**Action:** have the callback copy only event ID and required payload into a bounded FreeRTOS queue. Make `wifi_mgr::update()` the sole owner of connection state. Define overflow behavior and add a reconnect-storm HIL test. If any scalar must remain shared, protect it with the appropriate primitive rather than `volatile` alone.

### AUD-005 — Informational — GNSS is configured for 9,600 baud and one-second processing

**Evidence:** `include/config.h:139-140` configures 9,600 baud and a one-second processing interval. `src/gps/gps.cpp:1462` opens both RX and TX pins, so the ESP32-S3 can actively drive the receiver. The repository contains no receiver command/ACK sequence that selects message set, baud, update rate, or dynamic model.

**Assessment:** code truth supports a one-second application interval; the receiver's advertised maximum 10 Hz capability is not a project requirement. The repository also does not prove the physical ESP-to-GNSS TX connection or its electrical conditioning, so no electrical defect is inferred.

**Action:** no mandatory rate change. Document the intended one-second behavior. If the builder later wants a higher update rate or uses the TX wire for receiver configuration, then verify baud capacity and the actual circuit levels.

### AUD-006 — High — Track corruption field is unused and writes are unchecked

**Evidence:** `TrackChunkHeader` has a CRC field, but `src/gps/gps.cpp:950` always writes zero. The reader at `:1095-1111` does not validate CRC or require an exact expected blob length. `Preferences::putBytes()` results at `:723`, `:758`, `:801`, `:842`, and `:959` are ignored. Track metadata uses a weak XOR-style check rather than integrity over the records.

**Impact:** a short write, interrupted update, corrupt count, or stale slot can be accepted and exported as legitimate location data. Recovery has no authoritative way to distinguish the newest complete generation.

**Action:** use CRC32 over a versioned header and payload; validate exact size, point count, finite/ranged coordinates, date/time, and monotonic sequence; check every write result; and recover by scanning valid generations rather than trusting one metadata record. Fault-inject short writes, erased bytes, bit flips, stale metadata, and power cuts at every write boundary.

### AUD-007 — High — Synchronous HTTP work can starve the GNSS/LED loop

**Evidence:** the main loop ultimately calls `server.handleClient()` at `src/web/portal_http.cpp:917`. Track export performs repeated synchronous content sends, while several portal handlers construct large HTML/JSON `String` objects and JSON documents up to 6,656 bytes. The same application loop drains the GNSS UART and advances LED/state logic.

**Impact:** a slow or lossy web client can hold execution inside HTTP writes. During that time the UART receive ring can overflow, fixes can be dropped, LED timing can jitter, and the active-time/track metrics can develop gaps. Heap churn from large temporary strings also needs an uptime test.

**Action:** call `GPS.setRxBufferSize()` before `begin()` based on a measured worst-case service gap; bound response work per iteration; use larger efficient chunks and explicit yields; or move exports to a carefully isolated task while preserving single ownership of shared data. Prefer flash-resident/static page assets or fixed reusable buffers. Acceptance: export the maximum track to a throttled client while receiving a worst-case GNSS stream with zero UART overflow and bounded LED jitter/minimum heap.

### AUD-008 — High — Multi-key persistence is not transactionally coherent

**Evidence:** `src/config/runtime_config.cpp:160-179` stores a configuration version and then writes configuration fields as separate NVS keys without checking results. Home, metrics, session, track data, and their flags/metadata are likewise separate writes. A power interruption can leave an internally mixed generation; for example, a boolean “set” marker can survive without the corresponding coordinates.

**Impact:** NVS protects individual entries against many power-loss cases, but it does not make this application-level group atomic. Brownout during an update can produce plausible but inconsistent settings, geofence state, or statistics.

**Action:** serialize each logical object into a versioned, validated blob with CRC. Maintain A/B slots with generation counters: write and read-back-verify the inactive slot, then commit a small active marker last. Keep Wi-Fi configuration separate from metrics so either can recover independently. Validate every loaded field and fall back to the newest complete generation. Run automated power-cut tests during every persistence operation.

### AUD-009 — Informational — Simple mode intentionally uses the full strip

**Evidence:** normal speed/show paths call `paint_status_leds()` at `src/led/led_ui.cpp:657`, `:840`, and `:1013`. `update_simple_mode()` at `:871-896` applies its effect from index zero across `LED_STRIP_COUNT` and never repaints status LEDs.

**Assessment:** simple mode produces a homogeneous effect over the full strip, so the status pixels are not independently displayed. This can be an intentional visual-mode choice; the code behavior is clear and is not classified as a defect.

**Action:** no mandatory change. Document that simple mode uses every pixel. Only reserve the status segment in this mode if the desired visual behavior changes.

### AUD-010 — Medium — Active time undercounts loop stalls

**Evidence:** when an accepted processing interval is due, `src/gps/gps.cpp:1398-1399` adds exactly `GPS_SAMPLE_MS` rather than the elapsed interval. If the loop is blocked for several seconds, only one second is recorded.

**Impact:** active-time and derived statistics become systematically low during HTTP stalls, flash writes, RF activity, or other scheduling delays.

**Action:** calculate the unsigned elapsed duration, define a defensible gap policy, and accumulate a bounded delta only when the motion/fix evidence supports it. Keep observation time separate from GNSS fix time. Test jitter, 3–10 second stalls, missing fixes, and `millis()` wrap.

### AUD-011 — Medium — Some stale/deadline checks are not rollover-safe

**Evidence:** several GPS checks first require `now_ms >= previous_ms` before subtracting (`src/gps/gps.cpp:430`, `:432`, `:1292`, `:1636`). `millis()` wraps after roughly 49.7 days. Other modules already use a signed-deadline/subtraction idiom correctly.

**Impact:** a receiver that stops near the wrap boundary can leave stale state valid longer than intended; behavior depends on a new sentence arriving after wrap.

**Action:** centralize wrap-safe elapsed and deadline helpers using fixed-width unsigned arithmetic, migrate every timer, and unit-test boundaries around `UINT32_MAX`.

### AUD-012 — Medium — One GNSS date change can roll daily metrics

**Evidence:** `src/gps/gps.cpp:1349-1355` resets daily distance/time/max values when a nonzero parsed date differs from the stored date. There is no consecutive-fix confirmation, plausible calendar range check beyond parsing, monotonic policy, or preserved previous daily record before reset.

**Impact:** one checksum-valid but erroneous/backward date can erase the live daily counters.

**Action:** validate the full calendar date, require consecutive confirmation for non-midnight discontinuities, handle midnight as a defined transition, and journal the completed day before activating a new one. Test midnight, loss/reacquisition, old almanac time, and malformed-but-checksummed input.

### AUD-013 — Medium — Home/geofence state needs structural validation

**Evidence:** the home flag and floating-point coordinates are persisted separately and loaded without a blob-level integrity check. The code does not establish that persisted latitude/longitude are finite, ranged, and from the same committed generation before treating home as set.

**Impact:** corruption or a partial update can produce a false geofence reference, including NaN or default coordinates.

**Action:** use the same A/B validated-object scheme as configuration; require finite latitude in [-90, 90], longitude in [-180, 180], a valid source/fix age, and generation-consistent “set” state. Reject and visibly report invalid storage.

### AUD-014 — Medium — Portal test tooling is not cross-platform

**Evidence:** `tools/ap_portal_preview/server.mjs:6` converts a file URL using `.pathname`, and `:12` hard-codes `python3`. `package.json:7` also hard-codes `python3`; `:10-11` use POSIX `AP_PORTAL_VISUAL=1 command` syntax. On this Windows environment, extraction succeeds with `python`, but Playwright exits because its configured web server cannot start.

**Impact:** a nominal automated suite provides no regression signal on the current supported development machine.

**Action:** resolve paths with Node's `fileURLToPath()`, invoke a portable extractor or implement it in Node, and set environment variables through a Node wrapper or `cross-env`. Make “server started” and page load separate diagnostics. Run the suite on Windows and one Linux CI worker.

### AUD-015 — Medium — Dependency ranges and ArduinoJson API drift reduce reproducibility

**Evidence:** `platformio.ini` declares caret ranges (`ArduinoJson@^7.2.1`, `Adafruit NeoPixel@^1.12.3`); the build resolved newer 7.4.3 and 1.15.4 versions. ArduinoJson 7 warns on all `StaticJsonDocument` and `containsKey()` calls in `portal_http.cpp`.

**Impact:** identical source can compile against materially different library revisions over time, and deprecated APIs can become future build failures. ArduinoJson 7 also allocates `JsonDocument` storage on the heap, so migration affects memory behavior.

**Action:** select tested exact versions or commit a reproducible dependency lock policy. Migrate to `JsonDocument` and `is<T>()`/null-aware access with explicit deserialization limits, then measure peak/minimum heap under repeated requests. Treat warnings as CI failures after cleanup.

### AUD-016 — Informational — Portal handlers use temporary heap allocations

**Evidence:** page builders and JSON handlers construct large temporary `String` values and heap-backed ArduinoJson 7 documents per request. The linked image has ample static RAM, but no soak test records minimum free heap, largest block, or allocation failures.

**Assessment:** repeated portal use creates heap activity, but the successful build has substantial RAM headroom and no runtime failure or fragmentation trend has been demonstrated.

**Action:** no mandatory rewrite. Add heap-watermark telemetry or run a soak test if real use shows instability; optimize page storage only in response to measured pressure.

### AUD-017 — Low — Static analysis and tests are not yet release gates

**Evidence:** the current Python tests inspect source text rather than execute behavior. `pio check` passes despite low findings such as redundant initializations and an always-false unsigned comparison, while third-party/preprocessor analysis is noisy. There are no native tests for NMEA parsing, metrics, storage recovery, ring wrap, or LED bounds.

**Action:** add PlatformIO native Unity tests for pure modules, embedded/HIL tests for hardware boundaries, focused Cppcheck source filters, and `--fail-on-defect` at an agreed severity. Refactor pure parsing/accounting/storage-format logic away from Arduino globals to make it executable on the host.

### AUD-018 — Low — Disabled BLE remains compile-time maintenance surface

**Evidence:** current configuration disables BLE, which is a good RF/power decision, but BLE implementation remains part of the normal source build rather than an environment/source-filtered feature.

**Impact:** unused code increases compilation/noise and can accidentally re-enter future images without a complete coexistence/security test.

**Action:** gate it with an explicit PlatformIO build feature/source filter. A BLE-enabled build should have separate power and Wi-Fi coexistence acceptance tests.

## Positive design observations

- NMEA sentences are checksum-checked and parsed with explicit field/range gates before use.
- GPS distance/speed processing includes quality, segment, and spike filters rather than blindly integrating every fix.
- LED arrays and track working buffers are bounded; there is no obvious unbounded allocation in the animation hot path.
- The modules have clear ownership intentions and a single top-level loop, which makes the queue/state-owner corrections practical.
- Track HTTP output is already incremental rather than assembled as one enormous JSON response.
- BLE is disabled by default, reducing radio coexistence and power uncertainty.
- The firmware has sufficient flash/RAM headroom to add integrity, instrumentation, and tests without an immediate hardware migration.

## Internet investigation matrix

Twenty targeted investigations were performed. These sources do **not** override the repository; each asks a specific engineering question and informs a proposed test or change. Primary manufacturer/project/standards sources were preferred.

| # | Investigation question | Primary source and finding | Effect on this plan |
|---:|---|---|---|
| 1 | Can the XIAO ESP32-S3 directly report battery voltage/state? | [Seeed XIAO ESP32-S3 getting started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) documents battery charging/power behavior but not a built-in GPIO battery-voltage reading. | Do not invent a firmware-only battery percentage. Add an external gauge/divider/current/temperature design if power telemetry is required. |
| 2 | What signal-conditioning practices apply to addressable pixels? | [Adafruit NeoPixel best practices](https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices) recommends local bulk capacitance, a data-line resistor, common ground, and level shifting where logic levels differ. | Inspect the real PCB/harness for capacitor, series resistor, grounding, and 3.3-to-5 V data margin; verify with a scope. |
| 3 | How should pixel power be distributed and converted? | [Adafruit NeoPixel powering guidance](https://learn.adafruit.com/adafruit-neopixel-uberguide/powering-neopixels) emphasizes separate supply sizing, distributed injection, and logic-level margin. | Add connector/wire/boost drop and injection measurements to the 48-pixel worst-case test; do not rely on USB or board traces without evidence. |
| 4 | What reference power data exists for a comparable RGBW strip? | [SK6812 RGBW strip data](https://cdn-shop.adafruit.com/product-files/2824/SJ-10030-SC-6812RGBW.pdf) lists 5 V and 9.6 W/m maximum for one 30-pixel/m product. | This is background only, not evidence about the project's actual animated load or hardware. Measure the installed build only if runtime/converter characterization is desired. |
| 5 | What does the named GNSS vendor specify for rate and UART levels? | [EBYTE E108-GN02 product page](https://www.cdebyte.com/products/E108-GN02) lists NMEA 0183, up to 10 Hz, default 9,600 baud, 2.8–4.3 V supply, and nominal 2.8 V serial logic. | The code's one-second behavior is valid unless a higher rate is actually desired. Check TX circuit levels only if that physical connection is used. |
| 6 | Is concurrent SoftAP/BLE behavior automatically reliable on ESP32-S3? | [Espressif RF coexistence guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html) documents coexistence limitations and status-dependent combinations. | Keeping BLE off is justified. Any BLE release needs a deliberate coexistence matrix, not only a compile test. |
| 7 | Does NVS make a multi-key application update atomic and confidential? | [Espressif NVS documentation](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32s3/api-reference/storage/nvs_flash.html) describes per-entry power-loss behavior, blobs, and NVS encryption; it also notes security limits without encryption. | Use versioned A/B blobs and commit markers for application consistency; separately plan NVS/flash encryption for product threat models. |
| 8 | On which execution context do Arduino-ESP32 Wi-Fi callbacks run? | [Arduino-ESP32 Wi-Fi API documentation](https://github.com/espressif/arduino-esp32/blob/master/docs/en/api/wifi.rst) states callbacks execute on a separate FreeRTOS task and shared globals require synchronization. | Elevates `on_wifi_event()` shared state from style concern to a concrete concurrency defect; queue events to the owner loop. |
| 9 | Would adding a portal login require a new web framework? | [Arduino-ESP32 WebServer header](https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/src/WebServer.h) exposes `authenticate()` and `requestAuthentication()`. | No change is recommended for this DIY objective. This remains an available option only if a future user specifically wants another local access layer. |
| 10 | What failure does the ESP task watchdog detect? | [Espressif watchdog documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-reference/system/wdts.html) explains that the Task Watchdog detects tasks that run without yielding for too long. | Instrument and bound HTTP/export work; a watchdog is a last-resort detector, not a substitute for non-blocking service and UART-overflow tests. |
| 11 | Why does the current ArduinoJson code warn after dependency resolution? | [ArduinoJson 7 upgrade guide](https://arduinojson.org/v7/how-to/upgrade-from-v6/) explains that `StaticJsonDocument`/`DynamicJsonDocument` merged into heap-backed `JsonDocument` and documents API migration. | Migrate deliberately and measure heap behavior; alternatively pin a deliberately selected compatible release while migration is scheduled. |
| 12 | Can parser/accounting code be executed on the host through PlatformIO? | [PlatformIO unit-testing documentation](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html) supports native and embedded Unity tests. | Build a host test pyramid for pure logic plus target/HIL tests, replacing source-string assertions as the main correctness signal. |
| 13 | Can static analysis become a failing CI gate? | [PlatformIO `check` documentation](https://docs.platformio.org/en/latest/core/userguide/cmd_check.html) provides environment selection and fail-on-defect options. | Filter framework noise, define defect thresholds, and make new project-source findings fail CI after the baseline is cleaned. |
| 14 | Which commercial-IoT controls would be disproportionate here? | [NIST IoT cybersecurity technical catalog](https://pages.nist.gov/IoT-Device-Cybersecurity-Requirement-Catalogs/technical/) includes authorization, data protection, interface control, and secure-update capabilities. | These controls are useful context but are not adopted as DIY release gates. Simple setup and recoverability take priority for the stated project. |
| 15 | What trade-off accompanies a shared initial password? | [OWASP Internet of Things project](https://owasp.org/www-project-internet-of-things/) lists weak/hard-coded passwords among common product risks. | Record the local-access trade-off in setup documentation; do not burden this DIY collar with commercial provisioning unless its objective changes. |
| 16 | What platform hardening exists against offline firmware/data modification? | [Espressif Secure Boot v2 documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html) recommends Secure Boot with flash encryption for the ESP32-S3. | Add manufacturing keys, flash/NVS encryption, secure boot, debug-port policy, and a tested recovery/update path in product hardening—not casually during prototype debugging. |
| 17 | Can the Arduino UART receive buffer be sized before start? | [Arduino-ESP32 HardwareSerial API](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/HardwareSerial.h) exposes `setRxBufferSize()` and buffer/overflow APIs. | Size from measured worst-case HTTP/flash latency, enable overflow diagnostics, and require zero loss during throttled exports. |
| 18 | Can receiver motion models improve dynamic positioning? | [u-blox ZOE-M8B receiver/protocol specification](https://content.u-blox.com/sites/default/files/ZOE-M8B_ReceiverDescrProtSpec_%28UBX-18031559%29.pdf) documents platform/dynamic models as a GNSS receiver concept. It is not the EBYTE protocol. | Use only as inspiration: ask EBYTE for the exact chipset/protocol and supported motion model. Never send u-blox commands to this receiver without compatibility evidence. |
| 19 | What are Cppcheck's limits in the PlatformIO integration? | [PlatformIO Cppcheck documentation](https://docs.platformio.org/en/stable/advanced/static-code-analysis/tools/cppcheck.html) describes configuration, severities, and the role of tests alongside analysis. | Baseline real source findings, suppress only demonstrated framework false positives, and keep behavioral tests as the stronger oracle. |
| 20 | What lifecycle framing exists beyond a single security checklist? | [NISTIR 8259 series overview](https://www.nist.gov/itl/applied-cybersecurity/nist-cybersecurity-iot-program/nistir-8259-series) frames device capabilities and manufacturer lifecycle activities. | Include provisioning, vulnerability handling, update/recovery, end-of-support, and data-disposal decisions before calling the collar a product. |

## Prioritized improvement plan

### Phase 0 — Restore the core behavior (0–3 engineering days)

1. Freeze a reproducible build: pin tested library versions and record the PlatformIO/package versions.
2. **Completed for retention:** correct the track geometry, partial-chunk behavior, storage capacity, and chunk write-result handling. CRC32 remains under AUD-006.
3. Route Wi-Fi events through a bounded queue to one state owner.
4. Preserve the easy default AP workflow and document how the user changes credentials or resets them.

**Exit criteria:** the deterministic 2+ hour retention simulation now passes. Remaining Phase 0 criteria are corrupt-chunk rejection, single-owner Wi-Fi state, and straightforward AP credential change/reset.

### Phase 1 — Runtime and persistence robustness (1–2 weeks)

1. Replace multi-key logical objects with validated A/B blobs and generation commits.
2. Make track export non-starving; size and monitor the GNSS RX buffer.
3. Correct active-time accumulation, rollover comparisons, and daily-date transition validation.

**Exit criteria:** no UART overflow occurs during slow maximum-track export; power cuts at every NVS write boundary recover the previous or next complete generation, never a mixture; elapsed-time, `millis()` rollover, and date-transition tests pass.

### Phase 2 — Executable correctness and continuous verification (2–4 weeks)

1. Refactor NMEA parsing, time math, track-ring accounting, metrics, storage formats, geofence validation, and effect boundaries into host-testable units.
2. Add native Unity tests, target integration tests, recorded NMEA replay, storage fault injection, and reconnect storms.
3. Repair Windows portal tooling and run Playwright on Windows and Linux.
4. Clean ArduinoJson deprecations, configure focused static-analysis gates, and make compiler warnings fail CI.
5. Add heap-watermark, UART-overflow, reset-reason, and storage-error diagnostic counters.

**Exit criteria:** boundary tests cover malformed/checksummed NMEA, midnight, backward date, `millis()` wrap, NaN/range rejection, two-hour ring wrap, corrupt storage, and every LED mode; the build, unit tests, static checks, and Playwright tests are green on a clean checkout; a 24-hour mixed-load soak has no reset, overflow, unbounded heap decline, or corrupt record.

### Phase 3 — Optional only if the DIY project later becomes a distributed product

1. Define the privacy, retention, owner-transfer, reset, and lost-device expectations for that new scope.
2. Consider manufacturing provisioning, flash protection, and signed update/recovery only if distribution and support requirements justify them.
3. Add battery state/current sensing only if future features need accurate runtime information.
4. Consider RF, environmental, and mechanical validation only if the project scope expands beyond a personal DIY build.
5. Define update policy, vulnerability response, data deletion, diagnostics/redaction, and end-of-support behavior.

**Exit criteria for that future product scope:** provisioning and recovery are repeatable; field data can be erased by the owner; all claimed environmental/electrical limits have recorded test evidence; and any enabled firmware protection has a tested recovery plan.

## Optional electronics and HIL characterization

These tests can improve understanding and debugging, but the audit does not treat their absence as a defect:

1. **Power characterization:** record average and peak draw for representative effects and brightness settings to estimate battery runtime and confirm converter margin.
2. **Rail integrity:** scope the 5 V and 3.3 V rails during all-pixel steps, Wi-Fi association/transmit, GNSS startup, and flash writes. Define permitted droop, ripple, reset, and recovery behavior.
3. **Pixel interface:** measure data high/low levels and ringing at the first and farthest pixels, cold/hot, with the real harness. Confirm bulk capacitance, series resistance, common-ground path, and power injection.
4. **Temperature observation:** if useful, record converter, battery, connector, and pixel temperatures during a long effect run; change the design only if measurements expose a problem.
5. **GNSS UART:** measure both directions; verify ESP TX never violates receiver input limits. Capture boot configuration, baud, complete sentence rate, checksum-error rate, and RX overflow under concurrent web load.
6. **GNSS behavior:** replay recorded normal, weak, multipath, stationary-jitter, sprint, tunnel/loss, bad-date, and malformed streams; then repeat open-sky routes with reference logging.
7. **Power interruption:** automate cuts during each config/home/session/track write and during factory reset. Verify the previous or new complete generation is selected and no invalid home/track is exposed.
8. **Networking:** run AP client churn, STA reconnect storms, slow HTTP clients, malformed/oversized requests, and maximum track exports while monitoring loop latency and state ownership.
9. **Mechanical/environmental:** inspect wire gauge and strain relief; test flex, snag, vibration, drops, connector heating, sweat/water exposure, and failure containment appropriate to the intended collar use.

## Recommended completion gate

For the stated DIY objective, the informational observations require no code change. Track-retention geometry is now corrected and host-tested. The next valuable completion gates are rejecting corrupt track records, removing cross-task Wi-Fi state mutation, and verifying UART servicing while exporting a full track.

## Audit limitations

- No schematic, PCB layout, bill of materials, exact strip lot/datasheet, battery/protection/boost specifications, enclosure thermal model, or populated hardware measurements were available as executable evidence in this audit.
- No device was flashed or exercised; no RF, GNSS, current, voltage, thermal, charging, ESD, or ingress measurement was performed.
- The external links describe platform/vendor/standards behavior and design inspiration. Where they conflict with observed hardware or active source, the active source and measurements must drive the defect record.
- This review intentionally did not modify production code. It establishes the evidence-backed change order and acceptance tests first.
