# Optional cloud documentation

**Status:** Phase 0 evidence and explicitly authorized local Phase 1 implementation. Dog-RGB still has no deployed production website or firmware cloud sync.

| Document | Purpose |
| --- | --- |
| [Phase 0 execution report](phase0-execution-report.md) | delivered 0A/0B/0C work, validation snapshot, explicit open/closed gate register, and owner-authorized local Phase 1 boundary |
| [Field matrix](phase0-field-matrix.md) | every current runtime-config field and telemetry/status group: units, range, privacy, source, and accepted sync/exclusion policy |
| [Storage feasibility](phase0-storage-feasibility.md) | fixed Track v3 codec, provisional 664-slot raw geometry, remediated 51/51 host matrix awaiting independent acceptance, LittleFS comparison, and open physical gate |
| [Outbox independent-review packet](phase0-outbox-review-packet.md) | clean-room P0-R1 verifier, seven historical regressions, 12 manual invariants, severity rules, and final accepted/rejected ledger contract; not itself an acceptance |
| [PostgreSQL capacity](phase0-capacity-benchmark.md) | one-million-point local sizing/query evidence and initial index/partition decision |
| [Phase 1 migrated capacity](phase1-capacity-benchmark.md) | one-million-point evidence on the migrated/RLS-protected schema and retention consequences |
| [M1.9 History query/index](m19-history-query-plan.md) | authenticated PostgREST pagination plans, measured narrow index, write/size cost, and rollback proof |
| [M1.10 recording detail](m110-recording-detail-evidence.md) | bounded point reads, RLS matrix, continuity decisions, one-million-point plans, and browser/accessibility proof |
| [Phase 1 deletion drill](phase1-deletion-drill.md) | owner-authorized dog job, bounded worker/retry, durable tombstone/receipt, cascade inventory, and backup-lag boundary |
| [Phase 1 restore drill](phase1-restore-drill.md) | dual isolated logical restore, coordinate-free manifests, tamper-resistant deletion-tombstone replay, Auth/function/RLS equivalence, and hosted boundary |
| [Signed tombstone artifact](phase1-tombstone-artifact.md) | canonical Ed25519 batch/chain format, trust boundary, local verification, and still-open KMS/off-site custody gate |
| [Threat model](threat-model.md) | actors, boundaries, assets, threats, mandatory controls and verification owners |
| [Privacy/data flow](privacy-data-flow.md) | opt-in promise, processors, data purposes, minimization, export/delete lifecycle |
| [Retention policy](retention-policy.md) | exact initial lifetimes, purge jobs, backup/restore deletion behavior |
| [Credential checklist](credential-checklist.md) | environment-specific credentials, permitted storage, provisioning/rotation/incident gates |

Accepted decisions are indexed in [`docs/adr`](../adr/README.md). Implementation order and unresolved Phase 0 exit gates are in the [roadmap](../roadmap.md#optional-cloud-workstream--phase-0-in-progress).
