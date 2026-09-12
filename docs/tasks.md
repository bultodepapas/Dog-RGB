# Dog-RGB Work Queue

**Status:** Shared queue reviewed on 2026-08-13; Display subset reconciled through I6a on 2026-09-12. Completed implementation history lives in audits/plans and Git; this page lists actionable remaining work.

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

### Parallel Display integration — planning update 2026-09-12

**Display resumed by owner request after I6a visual/BOOT/wake confirmation.** Follow the [Display incremental contract](PLANS/2026-09-12_display-incremental-delivery.md) for detailed entry/work/exit criteria. I0–I6a implementation/evidence is retained; physical acceptance remains distinct. Resume with V1 on the available board, V2 when peripherals are available and then V3. I6c is opt-in bench preparation; V3 is still open. Classic continues independently.

- [x] I0a: reproduce Classic/Wokwi builds and host baseline; preserve local work and record evidence in the [I0 baseline](baselines/display-i0-2026-09-12.md).
- [x] I0b–I0c software: compile-time profiles, optional heartbeat, product/bringup targets, native GPIO tests and CI matrix.
- [ ] I0 physical/runtime: complete PCB identification and power/soak checks; five USB resets and 16 MiB flash / 8 MiB PSRAM detection now have [partial bench evidence](baselines/display-i4-2026-09-12.md). Run Wokwi scenarios when CLI/token are available.
- [x] I1 software: command-driven one-pixel/full-strip diagnostic using the existing bus/limiter, bounded brightness/runtime, RGBW/native tests and a separate build/CI target.
- [ ] I1 physical: verify RGBW, both strips and off behavior for 15 minutes after I0; record supply and actual current if measured.
- [x] I2 software: normal GPS/LED core, typed reception state, bounded queued reports, transport brightness/current limits and native tests; see [I2 evidence](baselines/display-i2-2026-09-12.md).
- [ ] I2 physical: valid NMEA/trusted fix, 15 minutes with normal Speed mode, data loss/recovery and no new UART overflows or resets.
- [x] I3 software: pinned ST7789 driver, value-only snapshot, partial text page, bounded diagnostic controls, native tests and display-free Classic/I0–I2 builds; see [I3 evidence](baselines/display-i3-2026-09-12.md).
- [ ] I3 physical: bars/window/colors, real-data agreement, 15 minutes with GPS/LEDs/LCD, backlight/service independence and measured timing.
- [x] I4 software preparation: native configuration save/reload/fault tests for Classic and Display, isolated embedded browser suite, explicitly labeled LCD-only demo and optional serial capture. See [I4 progress](baselines/display-i4-2026-09-12.md).
- [ ] I4 full bench: combined portal/persistence verification, 30-minute window with real GPS/strips, ten mode changes, three hardware save/restart/read cycles and populated route export. Bare-board LCD testing does not close this item.
- [x] I5 software: shared LVGL 8.4.0 Paseo view, three static captures, real LVGL/service host contracts and basic-page comparison controls. See [I5 evidence](baselines/display-i5-2026-09-12.md).
- [x] I5 USB comparison: ten minutes across LVGL/text/backlight/pause; black palette revision uploaded with a separate 90-second smoke. See [I5 evidence](baselines/display-i5-2026-09-12.md).
- [x] Display technical investigation: official demo, V2 schematic, ST7789V2 timing/color registers, forums and related repositories; [findings and staged follow-up](waveshare-lcd169-technical-research.md).
- [x] I6a redraw correction: reduce page invalidation and split diagnostic margin restoration; final USB maximum 44.768 ms per service call. This closes that budget only in the observed bare-board window.
- [ ] V1 / I5 visual acceptance: black/RGBW/border/orientation and final page readability; investigate PWM separately only if needed. V3 retains the real joint-load timing gate.
- [x] Display product analysis: [use and screen contract](PLANS/2026-09-12_display-use-and-screens.md), with Activity/Connection first and explicit limits for rest, sessions and battery telemetry.
- [x] I6a software: Activity distance hierarchy, Connection read-only snapshot/view, BOOT release/wake logic, nine PNGs and four CTest contracts. See [guide](../Platformio/Dog-RGB/docs/display-i6.md).
- [x] V1 subset: owner confirmed improved appearance, BOOT navigation and wake-only first press. See [I6c observations](baselines/display-i6c-2026-09-12.md).
- [ ] V1 remainder: complete RGBW/border/rapid-input and real AP/STA/portal-client checks.
- [ ] V2: close physical I0 → I1 → I2 → I3 items above with identified wiring and connected peripherals.
- [ ] V3: close I4 joint acceptance and compare final I6a with text/backlight-off/UI-paused windows; no claim of joint performance from USB-only results.
- [ ] I6b proposed: one optional measured transition after V1/V3, retaining instant change if it adds no value.
- [x] I6c software: opt-in 30 s backlight timer in stage 3, disabled on boot, deadline/rollover/dark-redraw tests. See [guide](../Platformio/Dog-RGB/docs/display-i6c.md).
- [x] I6c USB/owner subset: three expirations and thirteen expected reported states; owner confirmed automatic darkness and physical wake/navigation. See [evidence](baselines/display-i6c-2026-09-12.md).
- [ ] I6c final acceptance: V3 continuity and measured power if claiming savings; automatic product policy/configuration remains deferred.
- [ ] I6d proposed: State page only with useful, valid domain data; extend snapshots without inventing hardware health.
- [ ] I6e proposed: typed movement/still/unknown observation contract before contextual estimated pause; GPS gaps are not rest.
- [ ] I7 selection: explicit walk lifecycle, calibrated battery, international SSID typography, IMU/RTC or contextual alerts are separate optional increments.
- [x] Pause reconciliation: governing plan, usage/workflow, research notes, queue and indexes aligned with I6a; that documentation-only pause was subsequently ended by the owner.

I0 physical acceptance remains open before LED bench work. Classic fixes continue in parallel; battery telemetry/sensors, advanced tooling and cloud are not dependencies of the basic Display build.

### Existing shared firmware queue

- [ ] Add native PlatformIO/Unity tests for extracted pure C++ logic; keep Python contracts as complementary regression tests.
- [ ] Define physical-device loop-latency, UART-overflow, heap, and radio-retry acceptance thresholds.
- [ ] Exercise slow/aborted route exports against a real phone while recording GNSS overflow and loop diagnostics.
- [ ] Exercise all seven scene routes against Wokwi runtime or a physical ESP32, including auth/media/length errors, apply state after one tick, export/import and reboot recovery.
- [ ] Measure scene-store NVS latency, maximum LED gap during writes and heap before/after 100 save/import cycles; require ≤100 ms write gap or record an explicit revised SLO with evidence.
- [ ] Decide whether the three completed session slots should be presented chronologically or by storage slot in the public API contract.
- [ ] Document and test the supported upgrade path for partition-table changes and future schema migrations.

## P1 — Documentation and release hygiene

- [ ] Add automated internal-link and canonical-document checks to CI.
- [ ] Add a measured-results template for hardware revision, instruments, ambient conditions, firmware commit, and pass/fail values.
- [ ] Add a release checklist only after a repeatable physical flash/bench/field workflow exists.
- [ ] Keep Spanish user/build translations aligned when user-visible behavior changes.

## P2 — Optional experiments

- [ ] Evaluate voltage/temperature compensation or a physical current sensor only if bench evidence shows that the delivered schema-6 estimator cannot remain conservative enough.
- [ ] Validate the implemented RGBW palettes, A-forward/B-reverse layout, mirror, alert visibility and maximum LED tick time on the mounted collar; change only the orientation flags if physical direction differs.
- [ ] Revisit portal compression or budgets only if measured flash pressure or maintenance cost justifies changing the delivered deterministic pipeline.
- [ ] Evaluate BLE in STA-only and AP-transition matrices before enabling `BLE_ENABLED` in normal builds.
- [ ] Prototype IMU current/noise/mechanical impact before choosing a sensor.
- [ ] Revisit companion/cloud work only with explicit privacy, retention, authentication, cost, and offline-recovery requirements.

## Completed baseline (reference)

The repository already contains modular firmware, trusted GNSS metrics/date rollover, route/session persistence, Wi-Fi event ownership/retries/scanning, local portal security/UX remediation, four LED modes, Day Mode, a global estimated-current limiter with advanced calibration/diagnostics, semantic LED layout/mirror, versioned effects and eight RGBW palettes, status-preserving crossfades/alerts, four built-in plus four user scene slots with A/B recovery and strict API, a capabilities-driven graphical scene editor with import/export and approximate preview, deterministic compressed portal assets, optional portal PIN, host/Wokwi/Playwright tests, and visual regression baselines. Do not duplicate those items as open tasks without a concrete regression or enhancement.

See [Roadmap](roadmap.md) for milestone ordering and [Requirements](requirements.md) for acceptance context.
