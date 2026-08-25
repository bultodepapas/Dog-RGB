# Dog-RGB Roadmap

**Status:** Current priorities as of 2026-08-24. Future phases are optional; they do not redefine the local-first DIY baseline.

## Baseline delivered

- Modular ESP32-S3 firmware for GNSS, metrics, sessions, routes, LEDs, Wi-Fi, portal, storage, and optional BLE.
- Four LED modes, a versioned 12-effect registry, eight RGBW palettes, semantic A/B layout with mirror, status-preserving crossfades/alerts, explicit LED state/policy, four built-in plus four user scene slots, Show-by-scenes, welcome animation, optional Day Mode, and one global estimated-current limiter across both LED buses.
- Local AP/STA portal with captive helpers, network scan, route preview/export, runtime configuration, a capabilities-driven scene/palette editor, diagnostics, and optional write PIN; source-owned pages are deterministically minified and embedded as gzip assets.
- CRC-protected transactional persistence, an independent scene A/B bank, and dedicated two-hour route storage.
- Pinned production/Wokwi builds, host contracts, portal smoke, Playwright/a11y coverage, and visual baselines.

## Optional cloud workstream — local foundation implemented, product slice pending

The [web-platform master plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) is the active execution contract. Cloud stays optional and off by default: the collar, AP portal, local history, and exports remain usable without an account or Internet connection.

Implemented local foundation:

- device-v1 schemas/fixtures and HLC vectors pass 48/48;
- the corrected host storage candidate passes its 51/51 author suite, with independent and physical acceptance still open;
- 11 additive Supabase migrations, 12 pgTAP suites, explicit grants/RLS, and four Edge gateways recreate locally;
- the deterministic simulator covers claim, replay-safe upload, revoke, and desired/reported configuration;
- local capacity, deletion, retention, restore, and tombstone drills are retained as engineering evidence;
- the Next.js workspace exists, but it is still a shell rather than the owner product.

Current implementation order:

| Order | Milestone | Current boundary |
| --- | --- | --- |
| M0 | Reproduce and close the local baseline | Next.js production-build CI, generated `api` database-type drift check, and status-document reconciliation; all must pass on one reviewed commit |
| M1 | Simulator-driven local web slice | Auth, one dog, one simulated collar, claim/upload/history, brightness desired/reported, revoke, denial and browser tests; no map provider or firmware cloud code |
| M2 | Offline firmware data foundation | Track v3, time/config foundations, selected raw outbox, independent host review, and physical power-cut evidence with cloud disabled |
| M3 | Hosted development plus one collar | hosted parity, verified TLS, `/cloud`, real replay/config fault proof; no production launch |
| M4 | Truthful analytics and route product | replace the current queue-delete summary placeholder before any Cron schedule, then complete provider decision and map UI |
| M5 | Production opt-in and operations | explicit owner decision, privacy/export/delete/retention/restore/domain/SMTP/cost gates |
| M6 | Later capabilities | only with measured need and a new ADR |

Map credentials do not block M0–M3. The host outbox review and physical proof block the M2 exit, not unrelated portal work. Existing deletion/retention/restore prototypes are preserved but must not expand into production custody work before the end-to-end product slice exists.

Detailed evidence remains in the [cloud reports](cloud/README.md). No website, hosted project, firmware cloud client, physical outbox proof, final map provider, scheduled summary worker, or production operation is currently claimed.

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
- Continue the separately gated optional web platform only in the order defined by its dated plan: Phase 0 contract/evidence; local Supabase/simulator; offline firmware data foundation; one-collar hosted vertical slice; reliable configuration; truthful analytics; product UI/maps; then operations/privacy completion.
- Keep the first vertical slice deliberately small: real claim/upload, Today/recording history, a plain route, and brightness desired/applied state. Do not expand configuration or analytics until physical retry/power-cut and cross-user tests pass.
- Treat live/cellular tracking, advanced analytics, sharing, Google Maps, OTA, and new sensors as later independent decisions, not foundation work.

## Milestone 6 — Optional product hardening

- Unique provisioning, secure boot, flash/NVS encryption, signed update/recovery, manufacturing keys, and debug-port policy.
- Battery gauge/current/temperature hardware and calibrated telemetry.
- Formal environmental, EMC/RF, electrical, and pet-wearability validation.

Do not implement later milestones at the expense of local recovery or the physical MVP evidence. The immediate work queue is in [tasks.md](tasks.md).
