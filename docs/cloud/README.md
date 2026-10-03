# Optional cloud documentation

**Status:** Phase 0 evidence and explicitly authorized local Phase 1 implementation. Dog-RGB still has no deployed production website or firmware cloud sync.

| Document | Purpose |
| --- | --- |
| [Phase 0 execution report](phase0-execution-report.md) | delivered 0A/0B/0C work, validation snapshot, explicit open/closed gate register, and owner-authorized local Phase 1 boundary |
| [Field matrix](phase0-field-matrix.md) | every current runtime-config field and telemetry/status group: units, range, privacy, source, and accepted sync/exclusion policy |
| [Storage feasibility](phase0-storage-feasibility.md) | fixed Track v3 codec, provisional 664-slot raw geometry, independently accepted 67/67 host matrix, LittleFS comparison, and open physical gate |
| [Outbox independent-review packet](phase0-outbox-review-packet.md) | clean-room P0-R1 verifier, thirteen mandatory regressions, 12 manual invariants, severity rules, and final accepted/rejected ledger contract; not itself an acceptance |
| [Outbox independent review — 2026-10-02](phase0-outbox-independent-review.md) | rejected AI review of the frozen host candidate; identity reuse/stale ACK, corrupt-loss fallback, evidence gaps, historical failing regressions and raw clean-worktree readiness JSON; no firmware acceptance |
| [Outbox remediation — 2026-10-02](phase0-outbox-remediation-2026-10-02.md) | durable identity, conservative corrupt-record recovery, loss cut matrix, preflight counters, and canonical evidence; host scope only |
| [Outbox remediation review — 2026-10-02](phase0-outbox-remediation-review-2026-10-02.md) | independent AI acceptance of all 12 host invariants on `fb6dbef`; clean 67/67 readiness proof and explicit physical/fault-model limits |
| [M2.4a native Track v3 codec](m24a-track-v3-codec-evidence.md) | bounded C++ serialization, Python interoperability, malformed-input rejection, and the remaining identity/observation integration boundary |
| [M2.3a device identity and sequences](m23a-device-identity-evidence.md) | UUID/boot A/B storage, fail-closed recovery, volatile per-boot sequence ranges and explicit NVS adapter; credential/observation integration remains open |
| [M2.4b native chunk assembly](m24b-chunk-assembly-evidence.md) | bounded identity/codec bridge with stable reservation/encoding retries and explicit local handoff; runtime capture and durable outbox remain open |
| [PostgreSQL capacity](phase0-capacity-benchmark.md) | one-million-point local sizing/query evidence and initial index/partition decision |
| [Phase 1 migrated capacity](phase1-capacity-benchmark.md) | one-million-point evidence on the migrated/RLS-protected schema and retention consequences |
| [M1.9 History query/index](m19-history-query-plan.md) | authenticated PostgREST pagination plans, measured narrow index, write/size cost, and rollback proof |
| [M1.10 recording detail](m110-recording-detail-evidence.md) | bounded point reads, RLS matrix, continuity decisions, one-million-point plans, and browser/accessibility proof |
| [M1.11 brightness configuration](m111-brightness-configuration-evidence.md) | desired/reported truth, serialized RPC concurrency, role matrix, simulator convergence, and browser/accessibility proof |
| [M1.12 collar diagnostics/revoke](m112-collar-diagnostics-revoke-evidence.md) | accepted capability and pre-ACK queue truth, owner-only revocation, sync/revoke races, and browser/accessibility proof |
| [M1.13 Playwright owner journey](m113-playwright-owner-journey-evidence.md) | two independent clean local owner journeys, exact database/protocol checkpoints, Mailpit confirmation, simulator convergence, revoke, logout, and artifact controls |
| [M1.14 identity/object authorization](m114-identity-object-authorization-evidence.md) | exact protected/Data API surface, two-owner/editor/viewer matrix, crafted objects, deleted-Auth denial, zero-effect digests, and twice-clean Playwright evidence |
| [M1.15 deterministic transport/faults](m115-deterministic-fault-evidence.md) | committed-response loss, exact restart replay, conflict/telemetry/config/revocation faults, forced lock ordering, and twice-clean state/ACK evidence |
| [Phase 1 deletion drill](phase1-deletion-drill.md) | owner-authorized dog job, bounded worker/retry, durable tombstone/receipt, cascade inventory, and backup-lag boundary |
| [Phase 1 restore drill](phase1-restore-drill.md) | dual isolated logical restore, coordinate-free manifests, tamper-resistant deletion-tombstone replay, Auth/function/RLS equivalence, and hosted boundary |
| [Signed tombstone artifact](phase1-tombstone-artifact.md) | canonical Ed25519 batch/chain format, trust boundary, local verification, and still-open KMS/off-site custody gate |
| [Threat model](threat-model.md) | actors, boundaries, assets, threats, mandatory controls and verification owners |
| [Privacy/data flow](privacy-data-flow.md) | opt-in promise, processors, data purposes, minimization, export/delete lifecycle |
| [Retention policy](retention-policy.md) | exact initial lifetimes, purge jobs, backup/restore deletion behavior |
| [Credential checklist](credential-checklist.md) | environment-specific credentials, permitted storage, provisioning/rotation/incident gates |

Accepted decisions are indexed in [`docs/adr`](../adr/README.md). Implementation order and unresolved Phase 0 exit gates are in the [roadmap](../roadmap.md#optional-cloud-workstream--local-product-slice-implemented-firmware-foundation-open).
