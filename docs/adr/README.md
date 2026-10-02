# Architecture decision record index

ADRs record accepted design direction. **Accepted does not mean implemented or physically verified.** Each record states its evidence maturity and review gate; the current product remains the local firmware described in [architecture](../architecture.md).

| ADR | Status | Decision / maturity |
| --- | --- | --- |
| [0001](0001-wled-clean-room-y-licencia-del-proyecto.md) | Accepted | WLED clean-room/reference and licensing boundary |
| [0002](0002-project-license-mit.md) | Accepted | MIT project licence |
| [0003](0003-scene-model-and-store.md) | Accepted | bounded LED scene model/store |
| [0004](0004-generated-flash-web-portal.md) | Accepted | generated deterministic flash portal |
| [0005](0005-device-cloud-gateway-and-stable-hostname.md) | Accepted; local gateway implemented | four local Edge gateways and unique-credential flows exist; firmware HTTPS, hosted parity, and stable field hostname remain pending |
| [0006](0006-cloud-data-model-and-access-boundaries.md) | Accepted; local data foundation implemented | migrations, explicit grants/RLS, private security schema, exact telemetry, and tests exist locally; portal reads and hosted parity remain pending |
| [0007](0007-durable-telemetry-outbox-and-storage.md) | Host accepted by independent AI review; physical open | journal v3/emergency v2 preserve logical identity, fail read-only on corrupt committed metadata; 67/67 host tests; initial rejection retained |
| [0008](0008-resource-level-hlc-lww-configuration-sync.md) | Accepted; database/simulator implemented | resource HLC LWW and desired/reported database paths pass local tests; firmware common mutation path and brightness UI remain pending |
| [0009](0009-map-renderer-provider-and-colombia-bakeoff.md) | Renderer accepted, provider gate open | MapLibre accepted; Stadia Dark provisional; full credentialed comparison, origin-control tests, and two-reviewer score not yet run |
| [0010](0010-retention-and-truthful-activity-vocabulary.md) | Accepted; local operations scaffolded | local retention/deletion/tombstone paths exist without active scheduling; truthful summary computation, product UX, and production policy remain pending |

The complete device-v1 protocol, including dedicated revoke, and the fixed v3 codec are reconciled; the protocol suite passes 48/48. The corrected 664-slot byte-addressed storage candidate passes 67/67 and has independent AI host acceptance; target ESP32-S3 proof in M2 remains open. The credentialed provider comparison belongs to M4 and does not block the M0/M1 local web slice or M2 firmware foundation. Current execution order and evidence gates are controlled by the [active master plan](../PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md).
