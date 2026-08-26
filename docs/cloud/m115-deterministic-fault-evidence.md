# M1.15 deterministic transport and fault evidence

**Captured:** 2026-08-26 (America/Bogota)

**Implementation:** local M1.15 commit created with this evidence; its hash is reported in Git history

**Scope:** deterministic local simulator-to-Edge-to-RPC transport faults, durable receipts, telemetry/configuration effects, service restart, and sync/revoke ordering; not exhaustive privacy/cache inspection, the complete accessibility matrix, performance budgets, hosted deployment, firmware networking, arbitrary packet impairment, or physical-collar proof

## Frozen fault contract

M1.15 adds no transport proxy, automatic product retry, browser test, database migration, or production fault hook. The focused harness sends raw device `POST` bytes through the existing local Edge endpoint and reads the authoritative private/public state only from the repository-owned local PostgreSQL container. It keeps one simulated request pending until the exact success envelope and chunk acknowledgement are validated.

The gate freezes these outcomes:

| Fault | Required response | Required persisted/device result |
| --- | --- | --- |
| response lost after commit | first HTTP 200 body deliberately canceled | one committed receipt, one chunk/recording, three points, one winning configuration revision/head; simulated request remains pending |
| exact resend after restart | HTTP 200 response deep-equals the stored response | request ID, raw SHA-256, and chunk identity remain exact; no timestamp or logical row advances; pending request is reclaimed only after ACK validation |
| same ID, different valid body | HTTP 409 `request_id_reused`, no `Retry-After` | original receipt/response and the full collar data/configuration graph remain unchanged |
| out-of-order separate requests | each HTTP 200 with its exact chunk ACK | later point range may arrive first; the final boot namespace contains point sequences 0, 1, 2 and chunk sequences 1, 2 exactly once |
| persisted overlap | HTTP 422 `invalid_telemetry`, no ACK | no receipt survives and the full graph remains byte-for-byte equivalent to the pre-attempt snapshot |
| unknown clock | HTTP 200 with exact chunk ACK plus exact config outcome | unknown-time gap point has null recorded time/location/speed and its recording remains `unknown`; `gps_quality` is rebased through `fallback_received` onto the server HLC, and exact replay advances neither data nor clock state |
| stale desired report | HTTP 200 returning current desired v2 | reported v1 persists as device truth, head remains v2, hashes differ, so current desired state remains pending rather than falsely applied |
| revoked credential | HTTP 403 `device_revoked`, including a replay of an older committed request | no new or changed receipt/data/config/diagnostic state; a denied pending request is not reclaimed |
| sync-first/revoke-second | sync transaction held open, revoke proven blocked by that exact holder, then sync committed | one exact committed sync receipt and expected sync metadata, terminal collar/credential revocation, zero processing rows and zero telemetry/configuration effects |
| revoke-first/sync-second | revoke transaction held open, sync proven blocked, then revoke committed | sync fails with `device_revoked`, zero sync receipt/effect, coherent terminal revocation |

The two ordering cases use named PostgreSQL sessions and `pg_blocking_pids()` as the ordering proof. A holder returns from the first RPC while retaining its transaction locks; the second named session must be observed blocked before the holder is allowed to commit. No sleep determines the winner, and deadlock, lock timeout, or statement timeout output fails the gate.

## Preserved restart boundary

The runner's initial `supabase stop --no-backup` remains the explicit destructive clean-room replacement. Inside each M1.15 cycle, the restart callback uses only `supabase stop` followed by `supabase start`. It verifies Auth, Data API, Edge, and Mailpit readiness again and requires the API URL and publishable key to remain exact before resending the pending bytes.

Before that restart, the harness captures the committed receipt, stored response, request hash, and full collar snapshot while the simulated request is still pending. After restart it proves the full snapshot survived, resends the same raw body, receives the stored response, and proves the snapshot did not advance. A database reset, migration replay, recreated fixture, new request ID, or new body is not accepted as restart evidence.

## State and artifact boundary

Internal checkpoints compare ordered full rows for the collar, redacted credential metadata, request receipts and stored responses, telemetry chunks/points/recordings, configuration revisions/heads/reports, and diagnostic timestamps. Permanent 403/409/422 problems are explicitly not ACKs.

The retained artifact schema is limited to:

- schema version, phase, cycle number;
- the exact ten-fault allowlist and nine checkpoint names;
- frozen pass counts for receipts, chunks, points, recordings, revisions, heads, reports, and forced race schedules; failed runs use the same schema, the exact completed-checkpoint prefix, and zero counts.

It cannot contain IDs, claim codes, credentials, bearer/user tokens, raw request/response bodies, hashes, coordinates, SQL, or error details. The runner validates exact keys/enums/count types and additionally scans each successful artifact for the cycle's in-memory private fixture values.

Both passing cycles retained the same frozen counts for the primary collar: six committed receipts, four chunks, seven points, three recordings, three configuration revisions, two heads, one stale report, and two forced race schedules. These counts are supporting evidence; the exact before/after state comparisons and ACK assertions are the acceptance authority.

## Toolchain and result

The reviewed local run used:

- checksum-verified Node `24.18.0`;
- Supabase CLI `2.113.0` with local PostgreSQL 17 and the real local Edge runtime;
- Next.js `16.3.1` production build/start;
- Playwright `1.62.1`, one worker, and zero retries for the unchanged M1.13/M1.14 browser gates;
- 16 migrations replayed before every independent phase cycle.

Final recorded command:

- `node tools/portal-e2e/run.mjs --clean`: M1.13 owner journey 2/2, M1.14 authorization matrix 2/2, and M1.15 deterministic fault matrix 2/2 after six independent migration/seed resets; each fault cycle also completed one preserved full-stack stop/start and both forced database lock schedules; the final reusable reset completed successfully.

Pre-acceptance runs that stopped during readiness, assertion tightening, or teardown hardening were not counted. Caught matrix failures finalized and validated a sanitized `phase=failed` artifact before rethrow; the final complete clean command passed both cycles and the reusable reset. No remote CI, hosted endpoint, push, deployment, exhaustive privacy scan, accessibility matrix, or performance run was executed or inspected.

## Reproduction

From the repository root with the checksum-verified Node 24.18.0 selected:

```powershell
node tools/portal-e2e/run.mjs --clean
```

The command is intentionally destructive only to this repository's disposable local Supabase project and refuses to run without `--clean`. The two mid-fault restart operations preserve local volumes; only the command's initial clean-room replacement deletes them.
