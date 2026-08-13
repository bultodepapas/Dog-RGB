# Dog-RGB Roadmap

**Status:** Current priorities as of 2026-08-12. Future phases are optional; they do not redefine the local-first DIY baseline.

## Baseline delivered

- Modular ESP32-S3 firmware for GNSS, metrics, sessions, routes, LEDs, Wi-Fi, portal, storage, and optional BLE.
- Four LED modes, 12 effects, status pixels, welcome animation, optional Day Mode, and one global estimated-current limiter across both LED buses.
- Local AP/STA portal with captive helpers, network scan, route preview/export, runtime configuration, diagnostics, and optional write PIN.
- CRC-protected transactional persistence and dedicated two-hour route storage.
- Pinned production/Wokwi builds, host contracts, portal smoke, Playwright/a11y coverage, and visual baselines.

## Milestone 1 — Physical MVP evidence (highest priority)

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
- Replace duplicated effect knowledge with one metadata registry consumed by firmware diagnostics and the portal.
- Explore a small semantic layout (`status`, `body A`, `body B`, `alert`) instead of a general-purpose segment editor.
- Add RGBW-aware palettes and crossfades when they improve the collar's existing modes; avoid effect-count inflation.
- Consider compiling/compressing portal source assets if the embedded C++ strings become a maintenance or flash-size constraint.

These remaining ideas are explored in the [WLED lessons and implementation plan](analisis-wled-y-plan-implementacion.md). Phase 1 current limiting is delivered in software; later phases remain optional design directions.

## Milestone 4 — Optional sensing

- Evaluate an IMU only after power/noise/mechanical budget is known.
- If adopted, add calibrated motion classification and fuse it with GNSS activity evidence.
- Evaluate heart-rate sensing only as a separate experimental module with placement and signal-quality evidence.

## Milestone 5 — Optional companion/cloud work

- Reassess BLE only with an explicit SoftAP/STA coexistence strategy and phone matrix.
- Build the read-only companion app only after BLE is a supported runtime mode.
- Treat cloud sync, accounts, maps, remote ingestion, and retention/privacy controls as a separate product architecture. The dated cloud plan is research, not a commitment.

## Milestone 6 — Optional product hardening

- Unique provisioning, secure boot, flash/NVS encryption, signed update/recovery, manufacturing keys, and debug-port policy.
- Battery gauge/current/temperature hardware and calibrated telemetry.
- Formal environmental, EMC/RF, electrical, and pet-wearability validation.

Do not implement later milestones at the expense of local recovery or the physical MVP evidence. The immediate work queue is in [tasks.md](tasks.md).
