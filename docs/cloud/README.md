# Optional cloud documentation

**Status:** Phase 0 evidence and explicitly authorized local Phase 1 implementation. Dog-RGB still has no deployed production website or firmware cloud sync.

| Document | Purpose |
| --- | --- |
| [Phase 0 execution report](phase0-execution-report.md) | delivered 0A/0B/0C work, final validation snapshot, explicit open/closed gate register, and Phase 1 non-implementation handoff |
| [Field matrix](phase0-field-matrix.md) | every current runtime-config field and telemetry/status group: units, range, privacy, source, and accepted sync/exclusion policy |
| [Storage feasibility](phase0-storage-feasibility.md) | fixed Track v3 codec, provisional 664-slot raw geometry, remediated 49/49 host matrix awaiting independent acceptance, LittleFS comparison, and open physical gate |
| [PostgreSQL capacity](phase0-capacity-benchmark.md) | one-million-point local sizing/query evidence and initial index/partition decision |
| [Phase 1 migrated capacity](phase1-capacity-benchmark.md) | one-million-point evidence on the migrated/RLS-protected schema and retention consequences |
| [Phase 1 deletion drill](phase1-deletion-drill.md) | complete dog/account cascade inventory, cross-dog isolation, FK-index audit, and explicit backup-lag boundary |
| [Phase 1 restore drill](phase1-restore-drill.md) | isolated logical restore, coordinate-free manifests, Auth linkage, function/RLS equivalence, and hosted boundary |
| [Threat model](threat-model.md) | actors, boundaries, assets, threats, mandatory controls and verification owners |
| [Privacy/data flow](privacy-data-flow.md) | opt-in promise, processors, data purposes, minimization, export/delete lifecycle |
| [Retention policy](retention-policy.md) | exact initial lifetimes, purge jobs, backup/restore deletion behavior |
| [Credential checklist](credential-checklist.md) | environment-specific credentials, permitted storage, provisioning/rotation/incident gates |

Accepted decisions are indexed in [`docs/adr`](../adr/README.md). Implementation order and unresolved Phase 0 exit gates are in the [roadmap](../roadmap.md#optional-cloud-workstream--phase-0-in-progress).
