# Optional cloud Phase 0 execution report

**Status:** In progress as of 2026-08-18. Phase 0 has **not exited**. Under explicit owner direction, the local-only Phase 1 cloud foundation is proceeding in parallel; this exception does not waive any Phase 0 evidence gate and does not authorize Phase 2 firmware/cloud integration. No firmware cloud client is implemented.

This report is the audit/handoff view of work executed against Phase 0A–0C in the [accepted implementation plan](../PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md). Accepted ADRs describe design direction; they do not claim field-ready firmware or a deployed cloud service.

## Outcome

Phase 0 now has a current project contract, six accepted cloud ADRs, a complete current-field inventory, a frozen device-v1 protocol, a fixed Track v3 codec, local database-capacity evidence, and security/privacy/retention/credential plans. The complete protocol suite passes **48/48**.

Phase 0 remains open for three evidence items:

1. the corrected byte-addressed host candidate has fixed the five reproduced adversarial fallback/loss/corruption cases, but must still pass independent acceptance review;
2. the chosen outbox must pass physical ESP32-S3 power-cut/timing/wear/energy acceptance;
3. the identical provider bake-off and unapproved-origin tests require temporary restricted MapTiler and Stadia credentials/domain properties, followed by two independent human scores.

The latter two are external hardware/credential blockers. The first is an
in-repository correctness gate; current candidate metrics are provisional.

## Delivered work

### Phase 0A — project contract and decisions

- Updated [requirements](../requirements.md), [architecture](../architecture.md), [roadmap](../roadmap.md), [API reference](../api-reference.md), [testing](../testing.md), and the active implementation plan.
- Made cloud explicitly optional, disabled by default, and per-collar opt-in. LEDs, GNSS, local metrics/history, AP configuration/recovery, and exports remain mandatory without an account, Internet, Vercel, Supabase, DNS, or a map provider.
- Accepted the direct Supabase Edge device gateway and stable owned field hostname in [ADR-0005](../adr/0005-device-cloud-gateway-and-stable-hostname.md).
- Accepted normalized PostgreSQL/RLS/private-schema boundaries in [ADR-0006](../adr/0006-cloud-data-model-and-access-boundaries.md).
- Accepted raw-ring outbox design direction, with implementation evidence still provisional, in [ADR-0007](../adr/0007-durable-telemetry-outbox-and-storage.md).
- Accepted coherent-resource HLC LWW and desired/reported state in [ADR-0008](../adr/0008-resource-level-hlc-lww-configuration-sync.md).
- Accepted MapLibre and a provider-neutral route model; retained Stadia Dark only as the provisional provider front-runner in [ADR-0009](../adr/0009-map-renderer-provider-and-colombia-bakeoff.md).
- Accepted finite retention and truthful observed/moving/stationary/unknown vocabulary in [ADR-0010](../adr/0010-retention-and-truthful-activity-vocabulary.md).
- Inventoried every current `RuntimeConfig`, summary/session/route, GNSS, LED, Wi-Fi, storage, scene, and serial-diagnostic field in the [Phase 0 field matrix](phase0-field-matrix.md), including units/ranges, privacy, source, and sync/exclusion policy.
- Explicitly kept Home, LED power calibration, AP/station credentials, mDNS, local PIN, and scenes collar-local in the first cloud release. The unique device secret is authentication material, not synchronized configuration/history.

### Phase 0B — data/storage feasibility

- Added a fixed Track v3 point/chunk codec, legacy-v2 converter, deterministic fixtures, capacity arithmetic, candidate raw-ring/LittleFS models, and focused tests under [`tools/cloud_phase0`](../../tools/cloud_phase0/).
- Preserved the exact six-value time-quality mapping, point flags/invariants, stable device/boot/chunk/point identities, exact post-commit ACK identity, explicit holes/loss markers, and truthful legacy limitations in the [storage feasibility report](phase0-storage-feasibility.md).
- The corrected candidate's provisional geometry is 664 sealed chunks/63,744 points and approximately 15.624 days for the proposed adaptive profile; the modeled LittleFS candidate is approximately 1.2% larger.
- The raw-ring design direction remains accepted because the data format is fixed and its recovery semantics can be made explicit. It is not approved for firmware/field use.

#### Remediated host storage evidence awaiting independent review

The superseded RAM-only suite completed 20/20, but review proved it unsafe: a
direct reproduction sealed chunks `0..2`, accepted
`acknowledge_through(999)`, and reclaimed all three. That historical result is
invalid.

A corrected candidate now reconstructs from a byte-addressed NOR image, uses
global outbox ordinals, exact manifest-bound ACKs, per-slot durable ACK markers,
A/B journals and two independently erasable loss sectors. The remediated
`storage_model.py` artifact is 89,525 bytes with SHA-256
`30466323bc7caae841d9dcfc6345438a20db35c9b22b8e29c1deb6aca13588f8`. Its
49/49 suite now includes the five reproduced regressions: reclaim intent is
bound to the exact authorized slot ordinals; retained loss tombstones prevent
outbox-sequence reuse after journal fallback; maximum loss intervals recover
without range-sized allocation/iteration; a second loss is durably coalesced
during the prior loss ACK transition; and an ACKed corrupt payload is not
misclassified as unsynchronized coverage loss. The regenerated
664-slot/10,000-cycle metrics remain provisional until independent acceptance.

The historical adversarial reproduction against the superseded model was run from `tools/cloud_phase0` with three calls to `seal(sequence, digest)`, followed by `acknowledge_through(999)` and `reclaim_acknowledged()`. It returned `ack=True`, `durable_ack=999`, `reclaimed=3`, and an empty remaining-unacknowledged set. This directly contradicts the frozen protocol rule that holes or unverified identities cannot be skipped/reclaimed; these method/state names are not the corrected candidate's API.

Until the corrected candidate passes independent acceptance review:

- treat superseded-model figures as invalid and corrected-candidate metrics as provisional;
- do not use either a green test count or generated metrics to declare Phase 0B complete;
- keep all five adversarial cases as permanent byte-image regressions;
- keep the physical ESP32-S3 gate mandatory regardless of the corrected host result.

### Phase 0C — protocol, capacity, map, security, and privacy evidence

- Frozen `contracts/device-v1` with versioned schemas, positive/negative fixtures, compatibility matrix, HLC rules/vectors, problem catalog, structural limits, Track v3 compatibility, exact telemetry ACK semantics, and four narrow operations: issue claim, device claim, sync, and device revoke.
- Defined device-initiated unlink as durable `REVOKE_PENDING`. `device-v1-revoke` returns schema-valid `200` with `newly_revoked` or `already_revoked`; exact replay returns its stored original result, a matching revoke-only tombstone supports an already-revoked request, and generic errors never permit local credential erasure.
- Recorded a local one-million-point PostgreSQL/PostGIS sizing/query run in the [capacity report](phase0-capacity-benchmark.md). It supports starting unpartitioned and without GiST; hosted Supabase/RLS/cost evidence remains future implementation work.
- Captured durable schema-v2 [map evidence](../../tools/map_bakeoff/evidence/2026-08-13/manifest.json) for Stadia dark/light/outdoor across six deterministic urban/park/trail/rural/sparse/dense fixtures at desktop and exact 428 px mobile, DPR `1`/`2`, plus label, CVD, cold-cache, and throttled-network diagnostics. The manifest hashes sources/assets/screenshots and records browser, requests, errors, accessibility, attribution, overflow, route-coordinate leak checks, and credential blockers.
- Wrote the [threat model](threat-model.md), [privacy/data flow](privacy-data-flow.md), [retention policy](retention-policy.md), and [credential checklist](credential-checklist.md).

## Validation snapshot

Run from the repository root on 2026-08-13:

| Command/check | Result | Interpretation |
| --- | --- | --- |
| `node --test contracts/device-v1/test-contracts.mjs` | **48/48 passed** | Complete frozen device-v1 contract, including revoke fixtures/semantics, is green. |
| `python -m unittest discover -s test -p "test_*.py" -v` from `Platformio/Dog-RGB` | **131/131 passed** | Existing cloud-disabled firmware host regression remains green; no firmware cloud code was added. |
| `python -m unittest discover -s tools/cloud_phase0 -p "test_*.py" -v` | corrected candidate **49/49 passed**, review/open | All five reproduced destructive probes are permanent regressions; independent acceptance is still required. |
| superseded `RawRingModel` invalid-ACK reproduction | historical `ack=True`; `durable_ack=999`; `reclaimed=3` after sealing only `0..2` | Explains why the original 20/20 evidence was invalidated; this is not the current API. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File tools/cloud_capacity/run.ps1` | one-million-row report generated | Local PostgreSQL 17/PostGIS sizing and query-plan input; not hosted Supabase/RLS evidence. |
| `node --test tools/map_bakeoff/test-harness.mjs` | **7/7 passed** | Deterministic fixture sizes/durations, provider variants, coordinate invariants, and committed-secret scan are green. |
| `node tools/map_bakeoff/capture-evidence.mjs` | **17/17 passed**; 12 Stadia matrix + 5 diagnostic cells | Full no-credential matrix is durable; MapTiler rendering, provider origin rejection, human review, and comparative scoring remain open. |
| `git diff --check` | passed | No whitespace/error-marker defects in the shared Phase 0 diff. |
| relative Markdown-link check over ADR/cloud/core/active-plan docs | **27 files passed** | Every checked local documentation target exists. |
| stale contract/evidence wording scan | passed | No obsolete four-value time enum, 42/46 protocol count, revoke blocker, or disposable-map-evidence wording remains. |

The checked map manifest is durable technical evidence rather than a performance SLO or aesthetic acceptance. Its Stadia loopback run does not satisfy the missing MapTiler comparison, origin-rejection proof, or two-reviewer scoring. The PostgreSQL measurement is local sizing evidence, not a hosted Supabase SLA/cost proof.

## Gate register

| Gate | State | Evidence required to close |
| --- | --- | --- |
| Current project contract, opt-in/offline invariant, and field ownership | Closed for Phase 0 documentation | Reopen on any new firmware field, remote Home/power proposal, or cloud boundary change. |
| Device-v1 protocol, HLC/config, ACK/hole, and revoke contract | Closed; regression gate remains | Keep protocol 48/48 and codec compatibility green on every change. |
| PostgreSQL capacity direction | Closed as local sizing input only | Repeat representative authenticated/RLS queries and current plan/cost checks during implementation; this is not a Phase 0 external blocker. |
| Host outbox recovery/reclaim evidence | **Review / open** | The five reproduced failures are fixed and covered; obtain independent acceptance of the complete byte-image matrix. |
| Physical ESP32-S3 outbox acceptance | **Open external hardware gate** | Production codec/API, at least 10,000 seal/ACK/reclaim cycles, randomized reset/power removal at every boundary, full/corrupt/failure cases, legacy preservation, and measured timing/wear/heap/watchdog/energy. |
| Map renderer | Closed | MapLibre/provider-neutral adapter accepted; keep accessibility/non-map fallback requirements. |
| Basemap provider | **Open external credential gate** | Supply a temporary origin-restricted MapTiler key and Stadia test property/domain auth; run the full identical matrix and unapproved-origin tests; retain manifests/screenshots; complete two-reviewer scoring from captured request counts and amend ADR-0009. |
| Security/privacy/retention/credentials design | Closed for documentation | Implementation/adversarial/deletion/restore/operations tests remain future phase gates; no production credentials are authorized now. |
| Phase 0 overall | **Not passed** | Independently accept the host candidate, close both open physical/map gates, then perform an explicit exit review. |

## Handoff and prohibited inference

The remaining Phase 0 critical path, execution order, failure behavior, artifact
requirements, and exit checklist are now maintained in
[Phase 0 of the accepted plan](../PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md#phase-0--contract-evidence-and-decision-lock-exit-still-open).
Passing host schemas or models does not authorize production deployment, device
credential provisioning, or a firmware cloud client. The implemented Phase 1
local database, Edge Functions, portal scaffold, shared packages, and simulator
are parallel foundation work, not substitute Phase 0 evidence. The repository's
shipped collar behavior remains local-only, and Phase 2 remains unauthorized
until the explicit Phase 0 exit review passes.
