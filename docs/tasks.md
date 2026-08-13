# Dog-RGB Work Queue

**Status:** Open work derived from the current repository on 2026-08-12. Completed implementation history lives in audits/plans and Git; this page lists actionable remaining work.

## P0 — Safe physical prototype

- [ ] Record exact manufacturer/part numbers and ratings for the 21700 cell, holder, charger/BMS, boost, 3.3 V regulator, level shifter, connectors, wire, and strips.
- [ ] Draw and review the final schematic, including charging/power-path behavior, fusing/protection, decoupling, ground distribution, and test points.
- [ ] Measure 5 V/3.3 V rails and cell current at boot, GNSS acquisition, AP traffic, representative effects, and maximum intended brightness.
- [ ] Compare those measurements with `/api/dev` requested/estimated current, tune the base/RGB/W coefficients conservatively, and freeze a safe budget for the exact hardware revision.
- [ ] Confirm converter/connector/wire temperature and voltage-drop margin during a sustained worst-intended-load test.
- [ ] Define a numeric skin-contact/surface-temperature limit and stop criteria.
- [ ] Validate strain relief, flex, diffuser edges, cell restraint, serviceability, and charging isolation.
- [ ] Run controlled ingress checks before any weather-resistance claim.

## P0 — Field behavior

- [ ] Compare distance and route against a reference track for stationary, 200–500 m walk, longer walk, and short run.
- [ ] Record time-to-first-fix, satellites, HDOP, rejected segments, and recovery in representative environments.
- [ ] Verify the final enclosure/boost/LED wiring does not degrade GNSS quality.
- [ ] Test AP discovery, captive portal, station setup/scan, mDNS fallback, idle shutdown, and retry recovery on at least two phone platforms.
- [ ] Measure runtime with a documented cell, brightness, LED mode, GNSS, AP/STA, and Day Mode profile.

## P1 — Firmware and diagnostics

- [ ] Add native PlatformIO/Unity tests for extracted pure C++ logic; keep Python contracts as complementary regression tests.
- [ ] Define physical-device loop-latency, UART-overflow, heap, and radio-retry acceptance thresholds.
- [ ] Exercise slow/aborted route exports against a real phone while recording GNSS overflow and loop diagnostics.
- [ ] Decide whether the three completed session slots should be presented chronologically or by storage slot in the public API contract.
- [ ] Document and test the supported upgrade path for partition-table changes and future schema migrations.

## P1 — Documentation and release hygiene

- [ ] Add automated internal-link and canonical-document checks to CI.
- [ ] Add a measured-results template for hardware revision, instruments, ambient conditions, firmware commit, and pass/fail values.
- [ ] Add a release checklist only after a repeatable physical flash/bench/field workflow exists.
- [ ] Keep Spanish user/build translations aligned when user-visible behavior changes.

## P2 — Optional experiments

- [ ] Evaluate voltage/temperature compensation or a physical current sensor only if bench evidence shows that the delivered schema-6 estimator cannot remain conservative enough.
- [ ] Design one effect metadata registry before adding more effects or portal controls.
- [ ] Validate the implemented RGBW palettes, A-forward/B-reverse layout, mirror, alert visibility and maximum LED tick time on the mounted collar; change only the orientation flags if physical direction differs.
- [ ] Measure whether generated/compressed portal assets provide enough flash/maintenance benefit to justify a build step.
- [ ] Evaluate BLE in STA-only and AP-transition matrices before enabling `BLE_ENABLED` in normal builds.
- [ ] Prototype IMU current/noise/mechanical impact before choosing a sensor.
- [ ] Revisit portal presets if real users need repeatable profiles.
- [ ] Revisit companion/cloud work only with explicit privacy, retention, authentication, cost, and offline-recovery requirements.

## Completed baseline (reference)

The repository already contains modular firmware, trusted GNSS metrics/date rollover, route/session persistence, Wi-Fi event ownership/retries/scanning, local portal security/UX remediation, four LED modes, Day Mode, a global estimated-current limiter with advanced calibration/diagnostics, semantic LED layout/mirror, eight RGBW palettes, status-preserving crossfades/alerts, optional portal PIN, host/Wokwi/Playwright tests, and visual regression baselines. Do not duplicate those items as open tasks without a concrete regression or enhancement.

See [Roadmap](roadmap.md) for milestone ordering and [Requirements](requirements.md) for acceptance context.
