# Architecture decision record index

ADRs record accepted design direction. **Accepted does not mean implemented or physically verified.** Each record states its evidence maturity and review gate; the current product remains the local firmware described in [architecture](../architecture.md).

| ADR | Status | Decision / maturity |
| --- | --- | --- |
| [0001](0001-wled-clean-room-y-licencia-del-proyecto.md) | Accepted | WLED clean-room/reference and licensing boundary |
| [0002](0002-project-license-mit.md) | Accepted | MIT project licence |
| [0003](0003-scene-model-and-store.md) | Accepted | bounded LED scene model/store |
| [0004](0004-generated-flash-web-portal.md) | Accepted | generated deterministic flash portal |
| [0005](0005-device-cloud-gateway-and-stable-hostname.md) | Accepted target | direct Supabase Edge device gateway, unique device credential, and owned stable field API hostname; not implemented |
| [0006](0006-cloud-data-model-and-access-boundaries.md) | Accepted target | normalized/RLS cloud data model, private security schema, exact telemetry plus versioned derivations; not implemented |
| [0007](0007-durable-telemetry-outbox-and-storage.md) | Accepted direction; host independent review open; physical open | raw-partition durable outbox remains the design direction; corrected 664-slot byte-image candidate passes its 51/51 remediation matrix, while independent acceptance and physical power-cut/timing/wear/energy evidence remain mandatory |
| [0008](0008-resource-level-hlc-lww-configuration-sync.md) | Accepted target | coherent resource HLC LWW and desired/reported state; Home/power/secrets local-only; not implemented |
| [0009](0009-map-renderer-provider-and-colombia-bakeoff.md) | Renderer accepted, provider gate open | MapLibre accepted; Stadia Dark provisional; full credentialed comparison, origin-control tests, and two-reviewer score not yet run |
| [0010](0010-retention-and-truthful-activity-vocabulary.md) | Accepted target | finite location retention and honest observed/moving/stationary/unknown vocabulary; jobs/analytics not implemented |

The complete device-v1 protocol, including dedicated revoke, and the fixed v3 codec are reconciled; the protocol suite passes 48/48. The superseded RAM-only storage model's 20/20 result is invalid historical evidence. The corrected 664-slot byte-addressed candidate now passes 51/51, including all seven reproduced adversarial regressions, but the host recovery/reclaim gate still awaits independent acceptance. The optional cloud Phase 0 exit remains open—and Phase 2 remains unauthorized—until that host gate is independently accepted, raw outbox behavior is evidenced on the target ESP32-S3, and the full credentialed provider comparison/origin-control evidence is run and retained. Local-only Phase 1 proceeds under the explicit owner exception in the parent plan. See the [execution report](../cloud/phase0-execution-report.md).
