# Dog-RGB Requirements

**Status:** Current local MVP plus accepted optional-cloud requirements, reviewed 2026-08-13. Cloud states below are targets, not implemented features.

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
| FR-15 | Both LED buses must share one configurable estimated-current ceiling so their combined logical frame cannot exceed the configured model budget. | Implemented in `LedBus`/`PowerLimiter`, enabled by default with provisional coefficients; physical calibration remains required. |
| FR-16 | Normal visual changes must preserve reserved status, avoid a forced black inter-frame, and allow a new System/Geofence alert to preempt decoration on the next LED tick. | Implemented through semantic layout and buffer crossfade; native-tested. Physical visual/timing acceptance remains required. |
| FR-17 | The user must be able to apply built-in visual recipes and manage a bounded user-scene bank without changing the persisted LED mode accidentally. | Implemented with four immutable built-ins, four A/B-protected user slots, volatile apply/cancel, generation-aware save/delete/import/export, and Show-by-scenes. Live HTTP/reboot acceptance remains pending. |
| FR-18 | Portal controls for effects, palettes, layout, and scenes must derive their supported values from firmware capabilities rather than a duplicate JavaScript catalog. | Implemented in the `/config` workspace and enforced by smoke/Playwright contracts. |
| FR-19 | Every collar function available before cloud work—including GNSS recording, metrics, LEDs, AP recovery/configuration, persistence, and local export—must continue to work with cloud disabled, unpaired, offline, or unavailable. | Mandatory invariant; current firmware satisfies the local baseline. Cloud regression evidence is not yet available because cloud code is not implemented. |
| FR-20 | Cloud collection must be off by default and enabled only by an explicit, informed per-collar pairing action; disabled/unlinked collars must not contact or upload to Dog-RGB cloud services. | Accepted target; not implemented. |
| FR-21 | Optional pairing must use a short-lived one-use claim and a unique revocable device credential. A collar must never store or transmit the user's website password or a Supabase publishable/secret/service-role key as device identity. | Accepted by [ADR-0005](adr/0005-device-cloud-gateway-and-stable-hostname.md); not implemented. |
| FR-22 | On known Wi-Fi, a paired collar must eventually upload sealed telemetry at least once until a durable post-commit ACK, tolerate identical replay/out-of-order/lost responses, and expose last-success/error/backlog truth without blocking the cooperative loop. | Accepted target; raw-ring design has deterministic model evidence, but physical ESP32 power-cut/timing/wear/energy acceptance remains open. |
| FR-23 | Supported configuration must synchronize bidirectionally as coherent resources using deterministic HLC last-write-wins, shared validation/A/B commit, and distinct desired/pending/applied/rejected state. | Accepted by [ADR-0008](adr/0008-resource-level-hlc-lww-configuration-sync.md); not implemented. Brightness is the first proof resource. |
| FR-24 | Home, LED power calibration, AP/station credentials, mDNS, and the local PIN must remain local-only in the first full cloud release. The plaintext device credential may cross only verified TLS as claim/sync/revoke authentication; it must never enter synchronized configuration/history, read APIs, logs, browser state, or persistent server storage. | Accepted privacy/safety boundary; not implemented or uploaded today. |
| FR-25 | Cloud history must preserve source, unit, schema/algorithm version, time/fix quality, gaps, coverage, and unknown time. It must not label a boot recording as a walk, a threshold as running, or missing data as inactivity. | Accepted by [ADR-0010](adr/0010-retention-and-truthful-activity-vocabulary.md); requires v3 observations and field evidence, not current route v2. |
| FR-26 | Route maps must use a provider-neutral observation/GeoJSON adapter, break lines at gaps, keep route data out of tile-provider URLs, and offer a non-map alternative. | MapLibre accepted; Stadia Dark is provisional pending the full credentialed provider comparison and unapproved-origin tests in [ADR-0009](adr/0009-map-renderer-provider-and-colombia-bakeoff.md). |
| FR-27 | Normal AP unlink must stop ordinary exchange, durably enter `REVOKE_PENDING`, and retry a bounded idempotent `device-v1-revoke`; local credential erasure is allowed only after a schema-valid matching `200` with disposition `newly_revoked` or `already_revoked`. Exact replay returns the persisted original logical response; generic errors never clear. Offline force-clear must warn that server-side website revocation is still required. | Frozen in the green device-v1 contract; firmware/server implementation is pending. |

## Quality and resource requirements

| ID | Requirement | State / target |
| --- | --- | --- |
| QR-01 | Main-loop work should remain bounded and observable by subsystem. | Implemented diagnostics; Wokwi regression thresholds exist. Confirm on physical hardware under slow-client conditions. |
| QR-02 | Synchronous route export must continue servicing GNSS and stop work after client disconnect. | Implemented and host-tested. |
| QR-03 | Wi-Fi callbacks must not mutate owner-loop state directly. | Implemented through a fixed event queue with overflow recovery. |
| QR-04 | Runtime configuration must be semantically validated before persistence/application. | Implemented with field/range/order validation. |
| QR-05 | The embedded portal should remain usable at a 320 CSS-pixel viewport, support keyboard focus, reduced motion, semantic labels, and adequate contrast. | Covered by Playwright/a11y review and mobile baselines; continue testing after UI changes. |
| QR-06 | Generated portal pages must fit explicit per-route and total gzip budgets. | Enforced during generation and by manifest/array smoke checks: 12/13/23/10 KiB by route and 55 KiB total. |
| QR-07 | Production builds must use pinned framework/library versions. | Implemented in `platformio.ini`; dependency changes require build + regression evidence. |
| QR-08 | The collar should provide at least five hours of typical operation. | **Not yet verified.** Requires a defined brightness/effect/radio profile and measured cell-side energy. |
| QR-09 | The enclosure and surfaces must remain within a safe, comfortable thermal range. | **Not yet specified or verified.** Establish numeric limits and test points before field use. |
| QR-10 | The finished assembly should resist expected splashes/weather. | **Not yet verified.** No IP rating may be claimed without enclosure validation. |
| QR-11 | LED limiting must reduce immediately, release gradually, preserve one common scale across buses, and expose requested/final estimates and limiter activity. | Implemented and covered by deterministic host vectors plus portal/API tests; visible behavior still requires strip-level inspection. |
| QR-12 | LED orientation, mirror, palettes and transitions must be allocation-free in the hot path and discoverable through the versioned LED API. | Implemented and native/static-tested; A-forward/B-reverse still requires final mounting validation. |
| QR-13 | A clean checkout must build firmware offline from tracked portal assets, while regenerating those assets with the pinned web toolchain must be byte-reproducible across Windows and Unix. | Implemented with a locked Node/minifier build, canonical text/gzip, manifest hashes, PlatformIO pre-build guard, generator unit tests, and clean-checkout-safe smoke. |
| QR-14 | Cloud-disabled firmware must have no cloud DNS/TLS/upload dependency and must stay within measured loop, heap, flash, energy, and storage budgets. | Target. Existing local behavior is the regression baseline; physical measurements remain required. |
| QR-15 | Device/cloud protocols must be versioned, bounded, schema-validated, idempotent, and byte/semantic-compatible with the frozen v3 storage codec. | The complete device-v1 protocol passes 48/48. The superseded RAM-only storage model's 20/20 result is invalid historical evidence. A corrected 664-slot byte-addressed candidate passes its 51/51 provisional suite, but the host recovery/reclaim gate remains review/open until independent acceptance; codec cross-compatibility remains a required regression gate. No protocol is implemented in firmware. Local-only Phase 1 proceeds under the parent plan's explicit exception, and Phase 2 remains unauthorized. |
| QR-16 | User-facing cloud queries must be membership-authorized with explicit database grants and RLS, and cross-user/anonymous/service-function attacks must be automated before UI rollout. | Accepted target; database and tests are not implemented. |
| QR-17 | Raw location must have a finite enforced retention period, export/deletion workflow, redacted logs, and documented provider/backup lag. | Initial 12-month raw-location default accepted; enforcement and controls are not implemented. |

## Hardware safety requirements

- Use a protected, known-good 1S Li-ion cell and charger/BMS appropriate for the cell.
- The 5 V converter, connectors, wiring, and power distribution must tolerate measured peak load with margin.
- The software current estimate must never replace an external bench current limit, correctly rated protection, or measured electrical/thermal evidence.
- LED data should use a 5 V-compatible HCT/AHCT level shifter, series resistor, common ground, and local bulk capacitance.
- The GNSS supply and antenna should be kept away from noisy converter/LED wiring and checked under active animations.
- Charging must not occur while worn.
- The enclosure must prevent shorts, cell crushing/puncture, sharp edges, hot spots, and accessible conductors.
- Every hardware revision must be bench-tested at low brightness before full intended load.

## Privacy and security requirements

- Cloud-disabled operation is the default and must not upload location data or require a cloud lookup.
- Cloud-enabled upload requires explicit per-collar opt-in, authenticated ownership, a visible last-sync state, and a usable unlink/revoke path.
- Passwords must not be returned by read APIs.
- Built-in portal content must escape stored values before HTML/DOM insertion.
- Every write endpoint must use the common write guard.
- Local HTTP security and Internet security are separate: once cloud is enabled, verified TLS, a unique/revocable device credential, request bounds/replay protection, account authorization, explicit grants/RLS, and redacted logs are mandatory. They are not implemented today.
- Secure Boot, flash/NVS encryption, eFuse/debug-port policy, signed OTA, and production manufacturing keys remain optional product hardening and are not silently claimed.
- Precise routes, times, and Home are sensitive. Home, Wi-Fi/AP passwords, the local PIN, and calibration remain collar-local in the first release; the device bearer secret is transmitted only to authenticate claim/sync/revoke over verified TLS and is otherwise excluded from cloud data. Route access must follow dog membership and raw location expires under the documented policy.
- Documentation must state that local read APIs expose location/diagnostics to clients with network access.

See the Phase 0 [threat model](cloud/threat-model.md), [privacy/data flow](cloud/privacy-data-flow.md), [retention policy](cloud/retention-policy.md), and [field matrix](cloud/phase0-field-matrix.md).

## Explicitly out of the first cloud release

- Native Android/iOS app.
- Live/realtime or cellular tracking, anti-theft guarantees, public route links, social feeds, and fleet administration.
- IMU-based activity classification or heart-rate sensing.
- Walk/run/sleep/health/calorie/stress/readiness claims without sensor and validation evidence.
- Battery percentage, current, or temperature telemetry without added hardware.
- OTA release channel and production key management.
- Portal configuration presets.
- Remote Home or LED power-calibration changes.
- Advanced heatmaps, anomaly analytics, goals/coaching, sharing roles beyond the initial membership model, and Google Maps migration before the foundational sync/history gates pass.
- Certified waterproofing, EMC/RF compliance, or pet-safety certification.

## Release evidence for a physical MVP

The working prototype milestone is complete in software only when repository checks pass. A field-ready hardware milestone additionally requires recorded power/thermal/runtime measurements, GNSS route comparison, AP/STA recovery tests, mechanical/fit inspection, and controlled ingress tests as described in [Testing and simulation](testing.md#physical-validation-checklist).
