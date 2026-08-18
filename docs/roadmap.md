# Dog-RGB Roadmap

**Status:** Current priorities as of 2026-08-13. Future phases are optional; they do not redefine the local-first DIY baseline.

## Baseline delivered

- Modular ESP32-S3 firmware for GNSS, metrics, sessions, routes, LEDs, Wi-Fi, portal, storage, and optional BLE.
- Four LED modes, a versioned 12-effect registry, eight RGBW palettes, semantic A/B layout with mirror, status-preserving crossfades/alerts, explicit LED state/policy, four built-in plus four user scene slots, Show-by-scenes, welcome animation, optional Day Mode, and one global estimated-current limiter across both LED buses.
- Local AP/STA portal with captive helpers, network scan, route preview/export, runtime configuration, a capabilities-driven scene/palette editor, diagnostics, and optional write PIN; source-owned pages are deterministically minified and embedded as gzip assets.
- CRC-protected transactional persistence, an independent scene A/B bank, and dedicated two-hour route storage.
- Pinned production/Wokwi builds, host contracts, portal smoke, Playwright/a11y coverage, and visual baselines.

## Optional cloud workstream — Phase 0 and local Phase 1 in progress

The [dated web-platform plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) is now an accepted optional direction. It does not supersede the physical/local milestones below: cloud stays off by default, and all current collar/AP/export behavior remains mandatory without an account or Internet connection.

Phase 0 records decisions and evidence; it does **not** itself implement a website, Supabase project, account system, firmware networking, or synchronization. Under explicit owner direction, the local-only Phase 1 cloud foundation is proceeding in parallel without waiving Phase 0 or authorizing firmware Phase 2.

| Phase 0 item | State on 2026-08-13 | Evidence / remaining gate |
| --- | --- | --- |
| 0A project contract | Documentation complete in this workstream | Requirements/architecture/roadmap/API/testing, six accepted ADRs, and the [field matrix](cloud/phase0-field-matrix.md) now define opt-in/offline behavior and local-only Home/power/secrets. |
| 0B v3/storage feasibility | **Host independent review open; physical acceptance open** | The [storage report](cloud/phase0-storage-feasibility.md) supports the raw-ring capacity/design direction only. A corrected byte-addressed candidate reconstructs from flash bytes, uses exact ACK evidence and reserves two metadata plus two emergency sectors, leaving a provisional 664 chunks/63,744 points. All seven reproduced adversarial fallback/loss/corruption cases are permanent regressions and the suite passes 51/51; independent acceptance remains required. Journal v2 consumes reclaim intent before refill, quarantines corrupt payloads with readable identity, fails read-only on unreadable committed headers, and closes acknowledged sparse loss without a duplicate server ACK. The superseded RAM-only 20/20 run is invalid historical evidence. Random physical ESP32 power removal, latency, wear distribution, and energy remain mandatory. |
| 0C protocol/LWW evidence | Reconciled contract evidence complete | Versioned schemas/fixtures/HLC vectors, including dedicated revoke, agree with the frozen v3 codec, exact chunk ACK identity and out-of-order-hole semantics. Protocol result: 48/48. This closes protocol reconciliation only; the separate 0B host-storage independent review remains open. |
| 0C database capacity | Deterministic fixture evidence available; hosted costs/plans must be rechecked | See [capacity benchmark](cloud/phase0-capacity-benchmark.md); validate plans/query shape again on the selected hosted environment before production. |
| 0C security/privacy/retention/credentials | Documentation complete; implementation tests pending | [Threat model](cloud/threat-model.md), [privacy flow](cloud/privacy-data-flow.md), [retention policy](cloud/retention-policy.md), and [credentials checklist](cloud/credential-checklist.md). |
| 0C map bake-off | Durable keyless matrix complete; credentialed runner ready; external credential/human gate open | MapLibre accepted; the retained 7/7 harness and 17/17 Stadia matrix/diagnostic evidence covers six synthetic fixtures, dark/light/outdoor, desktop/428 px mobile at DPR 1/2, label/CVD/cache/network stress. The hardened readiness suite passes 12/12 and prepares a symmetric credentialed matrix with origin rejection, but no MapTiler visual result, unapproved-origin proof, or two-reviewer score exists; run it with restricted provider setups before selection. |

**Phase 0 exit is not reached, and Phase 2 is not authorized.** The corrected host outbox candidate awaits independent acceptance, physical ESP32 storage evidence is missing, and the credentialed comparative map/origin-control gate remains unresolved. Explicitly authorized Phase 1 work remains local/cloud-only and cannot change the collar's local product baseline.

The Phase 1 database, four Edge gateways, shared contracts, and deterministic simulator now pass the clean local gate. The migrated one-million-point capacity gate also passes after replacing a per-row telemetry RLS membership plan with one hashed visible-collar subplan; the same storage and query-shape evidence passed on a clean Ubuntu CI runner. The local [deletion workflow drill](cloud/phase1-deletion-drill.md) inventories all 17 cascade dependants and now adds an owner-authorized, replay-safe dog job with immediate access closure, bounded worker transactions and coordinate-free tombstone/receipt evidence. The [isolated restore drill](cloud/phase1-restore-drill.md) verifies exact application hashes, Auth linkage, functions, effective privileges/RLS and owner/non-member behavior without persisting the backup. Disposable hosted concurrency/network measurement, export/strong-confirmation/account deletion, scheduled retention purges, tombstone replay, a managed hosted restore, and the remaining operational drills stay open.

The consolidated evidence and gate state are in the [Phase 0 execution report](cloud/phase0-execution-report.md). The corrected candidate now carries passing regressions for every reproduced adversarial failure; independent host acceptance must still complete before the exit review. This is an in-repository review gate rather than an external hardware/credential blocker.

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
