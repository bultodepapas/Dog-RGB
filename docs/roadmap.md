# Dog-RGB Roadmap

**Status:** Cloud implementation reconciled 2026-10-02; general priorities as of 2026-08-24; Display subset reconciled on 2026-09-12 through I6a confirmation and I6c experimental preparation and I6d State. Future phases are optional; they do not redefine the local-first DIY baseline.

## Baseline delivered

- Modular ESP32-S3 firmware for GNSS, metrics, sessions, routes, LEDs, Wi-Fi, portal, storage, and optional BLE.
- Four LED modes, a versioned 12-effect registry, eight RGBW palettes, semantic A/B layout with mirror, status-preserving crossfades/alerts, explicit LED state/policy, four built-in plus four user scene slots, Show-by-scenes, welcome animation, optional Day Mode, and one global estimated-current limiter across both LED buses.
- Local AP/STA portal with captive helpers, network scan, route preview/export, runtime configuration, a capabilities-driven scene/palette editor, diagnostics, and optional write PIN; source-owned pages are deterministically minified and embedded as gzip assets.
- CRC-protected transactional persistence, an independent scene A/B bank, and dedicated two-hour route storage.
- Pinned production/Wokwi builds, host contracts, portal smoke, Playwright/a11y coverage, and visual baselines.

## Optional cloud workstream — local product slice implemented, firmware foundation open

The [web-platform master plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) is the active execution contract. Cloud stays optional and off by default: the collar, AP portal, local history, and exports remain usable without an account or Internet connection.

The local backend and M1.1–M1.16 owner portal have recorded acceptance; the site is not finished. The 2026-10-02 source review found missing returning-owner navigation, a summary worker that only deletes its queue, absent export/delete UI, local-only Auth redirects and incomplete release coverage. The master plan owns the detailed evidence and backlog.

Next: **M1.19 returning-owner navigation**, then account/product recovery and accessibility/performance. Complete useful summaries and owner data lifecycle locally before hosted preview. Firmware integration proceeds independently; it is required for a collar release, not for finishing web features. Tiled maps are optional, with their own provider gate.

M2A has independent AI host acceptance; isolated codec/identity/assembler increments exist. Runtime capture/outbox/credentials/config integration and physical proof remain open. No hosted project, physical cloud client or production operation is claimed. See the [master dependency order](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md#7-dependency-order) and [cloud evidence](cloud/README.md).

## Milestone 1 — Physical MVP evidence (highest priority)

### Parallel board variant — owner-agreed direction, 2026-09-12

Classic XIAO remains active with one shared firmware core and separate board
profiles. The [Display incremental plan](PLANS/2026-09-12_display-incremental-delivery.md)
is the execution authority. **Display resumed after owner confirmation of I6a appearance, BOOT and wake**; this does not pause or redefine the independent cloud plan.

Delivered: seven targets, I0–I3 diagnostics, I4 software preparation, LVGL 8.4.0
shared renderer and BOOT release/wake. I6d adds State to Activity/Wi-Fi, fourteen
PNGs and five CTest contracts; firmware host suite: 147/147. Historical I6a USB
navigation maximum: 44.768 ms
per service call after reduced page redraw and staged margin restoration.
[Baseline I6a](baselines/display-i6-2026-09-12.md) records the exact image and
limits; full optical/radio and GNSS/LED/HTTP joint acceptance remain open. I6c adds an opt-in bench timeout, disabled on boot; see the [guide](../Platformio/Dog-RGB/docs/display-i6c.md).

[VIS-3 integration](baselines/display-vis3-2026-09-12.md) connects the saved identity to conditional three/four-page navigation. [USB setup, persistence, physical QR and BOOT](baselines/display-identity-usb-2026-09-12.md) have basic bench acceptance. [VIS-4](baselines/display-vis4-2026-09-12.md) adds shared styling and pet-name headers, built and loaded with 30 USB navigation changes verified; new header optical review is pending. Next is VIS-5 temporal captures and one measured optional indicator transition. AP, expanded optical conditions and V2/V3 remain open.

[28 GitHub/MCP investigations](display-github-research-2026-09-12.md) refine that
step into VIS-1a (bounded QR, explicit margin, independent decoder) and VIS-1b
(font/layout prototypes). Optional editors, sprites and alternate drivers remain
separate experiments; the host encoder probe is not physical QR acceptance.

| Resume order | Scope |
| --- | --- |
| V1 | Confirm final pages, black/border, physical BOOT and real radio/portal client cases |
| V2 | Complete physical I0 identification/power, I1 LEDs, I2 GPS+LEDs and I3 LCD/live data |
| V3 | I4 thirty-minute joint load, persistence/export and final I6a comparison |
| I6b | Optional transition after accepted navigation and joint baseline |
| I6c | Separate inactivity timeout, initially backlight only; wake does not navigate |
| I6d | State implemented: GPS and effective LED policy; [current evidence](baselines/display-i6d-2026-09-12.md), five CTest, fourteen PNGs, 147 host tests |
| I6e | Estimated pause only with a validated observation contract |
| I7 | Choose one extension: battery, walk lifecycle, sensors, typography or alerts |

Software preparation can proceed without absent peripherals once work resumes;
it cannot close physical gates. Bench acceptance and portable-collar validation
remain distinct. No new graphics dependency or copied GPS/LED/portal core is
required. Wokwi Classic runtime recovery remains a separate regression task.

### Shared physical evidence

- Freeze the actual schematic/BOM with exact charger, protection, boost, regulator, connectors, cell, and strip part numbers.
- Measure cell/rail current, converter efficiency, voltage drop, brownout margin, heat, and runtime under defined profiles.
- Calibrate the schema-6 LED base/channel model conservatively against those measurements and freeze the safe whole-device budget for the selected hardware.
- Validate GNSS acquisition/route accuracy with the final enclosure, wiring, converter, and LED activity.
- Validate AP/STA visibility/recovery and portal behavior on representative phones.
- Build and test strain relief, diffuser, fit, charging access, serviceability, and controlled weather resistance.
- Publish a dated hardware revision and measured results; replace planning estimates with evidence.

## Milestone 2 — Firmware testability and maintainability

- Add native C++ tests for pure parser, time, geometry, and record-codec logic; reduce dependence on source-string contracts.
- Split the large GNSS implementation along parser, metrics/session, and route-storage seams when a functional change justifies it.
- Add a documentation/link consistency check to CI after its false-positive baseline is clean.
- Define a repeatable physical HIL smoke procedure with pass/fail thresholds.

## Milestone 3 — Optional LED and portal evolution

- Revisit the delivered estimator only if physical calibration shows that the simple base/channel model needs voltage, temperature, or hardware-sensor inputs.
- Evolve the delivered effect/palette registries only when a new entry has an honest control/safety contract and characterization vectors.
- Physically validate the delivered A-forward/B-reverse layout, mirror, alert legibility and crossfade timing on the mounted strips.
- Exercise the delivered scene API/store against Wokwi runtime or hardware: seven HTTP routes, reboot recovery, heap after repeated writes, apply latency and maximum LED gap during NVS writes.
- Evolve the delivered scene editor only through the capabilities/catalog/import contract; do not duplicate registries or firmware validation in JavaScript.
- Keep the delivered generated/compressed portal pipeline reproducible across operating systems and within its per-route and total flash budgets.

These remaining ideas are explored in the [WLED lessons and implementation plan](analisis-wled-y-plan-implementacion.md). Phases 1–5 are delivered in software; physical/HIL acceptance remains separate and later phases remain optional design directions.

## Milestone 4 — Optional sensing

- Evaluate an IMU only after power/noise/mechanical budget is known.
- If adopted, add calibrated motion classification and fuse it with GNSS activity evidence.
- Evaluate heart-rate sensing only as a separate experimental module with placement and signal-quality evidence.

## Milestone 5 — Optional companion/cloud work

- Reassess BLE only with an explicit SoftAP/STA coexistence strategy and phone matrix.
- Build the read-only companion app only after BLE is a supported runtime mode.
- Continue the optional web platform under its [master dependency order](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md#7-dependency-order); complete local user workflows, analytics and data lifecycle without waiting for hardware. Physical integration and production activation retain separate gates.
- Keep the first vertical slice deliberately small: real claim/upload, Today/recording history, a plain route, and brightness desired/applied state. Additional remote configuration and physical activity claims wait for physical retry/power-cut and cross-user proof; basic versioned analytics may be completed with simulator fixtures first.
- Treat live/cellular tracking, advanced analytics, sharing, Google Maps, OTA, and new sensors as later independent decisions, not foundation work.

## Milestone 6 — Optional product hardening

- Unique provisioning, secure boot, flash/NVS encryption, signed update/recovery, manufacturing keys, and debug-port policy.
- Battery gauge/current/temperature hardware and calibrated telemetry.
- Formal environmental, EMC/RF, electrical, and pet-wearability validation.

Do not implement later milestones at the expense of local recovery or the physical MVP evidence. The immediate work queue is in [tasks.md](tasks.md).
