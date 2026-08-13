# Dog-RGB Requirements

**Status:** Current MVP requirements and verification state, reviewed 2026-08-12.

The terms **must**, **should**, and **may** indicate required, recommended, and optional behavior. “Implemented” means present in source; it does not imply that the physical collar has passed field certification.

## Functional requirements

| ID | Requirement | State / evidence |
| --- | --- | --- |
| FR-01 | The collar must produce visible RGBW patterns on one or two configured SK6812 strips. | Implemented; production default is two × 24 pixels. Physical visibility still requires field testing. |
| FR-02 | The collar must parse RMC/GGA GNSS data and expose trustworthy fix/quality state. | Implemented with checksum/parser counters and configurable GGA quality gates. |
| FR-03 | The collar must calculate daily distance, active time, average active speed, and maximum valid speed. | Implemented with Haversine segments, gap rules, and speed filtering. |
| FR-04 | Daily rollover must not erase metrics because of a single bad/stale date observation. | Implemented with trusted contiguous rollover or repeated forward-date confirmation plus a completed-day journal. |
| FR-05 | Runtime settings and critical multi-field state must survive ordinary reboot and interrupted writes without mixing generations. | Implemented with validated CRC-protected A/B records and recovery counters. |
| FR-06 | The user must be able to view metrics and configure the collar without cloud service or native app. | Implemented through the local AP/STA portal. |
| FR-07 | The portal must support Speed, Geofence, Show, and Simple LED modes. | Implemented. |
| FR-08 | The collar must retain a bounded recent route and make it exportable without unbounded response memory. | Implemented: about two hours/1,440 points; chunked JSON, CSV, GeoJSON streams. |
| FR-09 | The Wi-Fi access point must remain recoverable when GNSS/station connectivity is unavailable, while allowing power-saving shutdown when appropriate. | Implemented through GPS/stationary/client/activity policy and bounded retry. |
| FR-10 | Nearby Wi-Fi discovery must run only on explicit user request. | Implemented asynchronous scan with a 20-network response cap. |
| FR-11 | State-changing portal actions must reject cross-origin form/no-CORS writes. | Implemented through required `X-Dog-Portal` header. |
| FR-12 | A user may enable a simple local PIN for write actions without making first boot dependent on setup. | Implemented, optional/off by default. Reads remain unguarded. |
| FR-13 | Day Mode may turn off effect pixels during a configured daylight window without stopping tracking or status alerts. | Implemented, optional/off by default; fixed 06:00–16:00 America/Bogota window. |
| FR-14 | A compact daily summary may be read over BLE when explicitly compiled in. | Implemented wire format; disabled by default due SoftAP/BLE coexistence. |

## Quality and resource requirements

| ID | Requirement | State / target |
| --- | --- | --- |
| QR-01 | Main-loop work should remain bounded and observable by subsystem. | Implemented diagnostics; Wokwi regression thresholds exist. Confirm on physical hardware under slow-client conditions. |
| QR-02 | Synchronous route export must continue servicing GNSS and stop work after client disconnect. | Implemented and host-tested. |
| QR-03 | Wi-Fi callbacks must not mutate owner-loop state directly. | Implemented through a fixed event queue with overflow recovery. |
| QR-04 | Runtime configuration must be semantically validated before persistence/application. | Implemented with field/range/order validation. |
| QR-05 | The embedded portal should remain usable at a 320 CSS-pixel viewport, support keyboard focus, reduced motion, semantic labels, and adequate contrast. | Covered by Playwright/a11y review and mobile baselines; continue testing after UI changes. |
| QR-06 | Portal pages must fit explicit embedded page-size/reserve budgets. | Enforced by static smoke checks. |
| QR-07 | Production builds must use pinned framework/library versions. | Implemented in `platformio.ini`; dependency changes require build + regression evidence. |
| QR-08 | The collar should provide at least five hours of typical operation. | **Not yet verified.** Requires a defined brightness/effect/radio profile and measured cell-side energy. |
| QR-09 | The enclosure and surfaces must remain within a safe, comfortable thermal range. | **Not yet specified or verified.** Establish numeric limits and test points before field use. |
| QR-10 | The finished assembly should resist expected splashes/weather. | **Not yet verified.** No IP rating may be claimed without enclosure validation. |

## Hardware safety requirements

- Use a protected, known-good 1S Li-ion cell and charger/BMS appropriate for the cell.
- The 5 V converter, connectors, wiring, and power distribution must tolerate measured peak load with margin.
- LED data should use a 5 V-compatible HCT/AHCT level shifter, series resistor, common ground, and local bulk capacitance.
- The GNSS supply and antenna should be kept away from noisy converter/LED wiring and checked under active animations.
- Charging must not occur while worn.
- The enclosure must prevent shorts, cell crushing/puncture, sharp edges, hot spots, and accessible conductors.
- Every hardware revision must be bench-tested at low brightness before full intended load.

## Privacy and security requirements

- Normal operation must not upload location data.
- Passwords must not be returned by read APIs.
- Built-in portal content must escape stored values before HTML/DOM insertion.
- Every write endpoint must use the common write guard.
- Advanced measures such as TLS, secure boot, flash/NVS encryption, unique provisioning, signed OTA, and account authorization remain optional product-hardening work; they are not silently claimed by this prototype.
- Documentation must state that local read APIs expose location/diagnostics to clients with network access.

## Explicitly out of current scope

- Cloud sync, accounts, multi-collar fleet management, and remote Internet access.
- Native Android/iOS app.
- IMU-based activity classification or heart-rate sensing.
- Battery percentage, current, or temperature telemetry without added hardware.
- OTA release channel and production key management.
- Portal configuration presets.
- Certified waterproofing, EMC/RF compliance, or pet-safety certification.

## Release evidence for a physical MVP

The working prototype milestone is complete in software only when repository checks pass. A field-ready hardware milestone additionally requires recorded power/thermal/runtime measurements, GNSS route comparison, AP/STA recovery tests, mechanical/fit inspection, and controlled ingress tests as described in [Testing and simulation](testing.md#physical-validation-checklist).
