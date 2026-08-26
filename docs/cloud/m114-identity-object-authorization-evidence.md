# M1.14 identity and object-authorization evidence

**Captured:** 2026-08-26 (America/Bogota)

**Implementation:** local M1.14 commit created with this evidence; its hash is reported in Git history

**Scope:** adversarial local identity and object authorization for the existing portal/Data API boundary; not transport fault injection, exhaustive privacy/cache inspection, the complete accessibility matrix, performance budgets, hosted deployment, firmware networking, or physical-collar proof

## Frozen external surface

The gate fails if this reviewed surface widens without an explicit M1.14 update:

- six protected route shapes: onboarding, Today, History, Configuration, Collars, and recording detail;
- four product/object Server Actions: dog creation, claim issuance, brightness mutation, and collar revocation;
- eleven authenticated `SELECT` projections in the exposed `api` schema: `profiles`, `dogs`, `dog_memberships`, `collars`, `recordings`, `telemetry_points`, `daily_summaries`, `recording_summaries`, `config_revisions`, `config_resource_heads`, and `config_reported`;
- five authenticated `api` RPCs: `create_dog_v1`, `mutate_config_resource_v1`, `revoke_collar_v1`, `request_dog_deletion_v1`, and `get_deletion_job_v1`;
- one authenticated user Edge endpoint: `user-v1-issue-claim`.

The catalog pgTAP assertion enumerates all authenticated `api` function execution, rather than checking only known function names. All eleven readable tables must retain RLS. Anonymous callers retain no `api` RPC execution.

## Fixture and denial contract

Each authorization cycle creates four confirmed accounts through the local Auth schema and acquires real password sessions:

| Identity | Allowed scope |
| --- | --- |
| owner A | owner of dog A and its collar/recording/configuration |
| owner B | owner of dog B only |
| editor | read/write member of dog A; no revoke |
| viewer | read-only member of dog A; later hard-deleted for the stale-JWT stage |

Both dogs are created through the public user RPC. Both collars are issued through the real user Edge endpoint, paired through the existing pair-only simulator, and populated through real simulator uploads. Dog A also has desired/reported brightness and one bounded row in both public summary tables. No service-role or database-administrator credential enters the portal or Playwright process.

The accepted denial taxonomy is surface-specific:

- anonymous canonical portal route: redirect to the allowlisted login route;
- malformed, canonical-missing, inaccessible, or cross-dog portal object: the same generic 404 body;
- RLS-filtered missing or inaccessible row: HTTP 200 with `[]`;
- anonymous REST/RPC: exact PostgREST `401 / 42501 / permission denied for schema api`; authenticated authorization failures: exact `403 / 42501 / not_authorized`; malformed transport/input remains a separate bounded validation class;
- missing versus inaccessible form target: identical action-specific bounded guidance;
- deleted Auth subject: protected portal redirect or bounded action failure, zero raw projections, Edge denial, and denial from every user RPC.

This gate does not promise immediate invalidation of an otherwise valid access JWT merely because its session was signed out. Supabase access tokens remain independently valid until expiry unless the application adds a session-row check. The deterministic stale case is a hard-deleted Auth user whose still-unexpired JWT has both `sub` and `session_id`: fresh portal/Edge identity checks reject it, membership/profile cascades remove raw access, and user RPCs must fail closed.

## Security findings closed

The audit found and fixed five concrete boundary defects:

1. `create_dog_v1` previously let a deleted Auth subject reach the `created_by` foreign key and leak a raw `23503`. It now checks the current, non-deleted Auth row first and returns `28000 authentication_required` with no row.
2. deletion-request replay and `get_deletion_job_v1` previously trusted only a SHA-256 fingerprint of `auth.uid()`, so a deleted user's unexpired JWT could replay/read its retained receipt. Both now require a current, non-deleted Auth row before replay lookup or result access.
3. another identity could distinguish an existing deletion request ID through `deletion_request_conflict`. Cross-identity existing and unknown request IDs now both return `42501 not_authorized`; the original requester's changed payload remains the distinct `request_id_reused` contract.
4. password update used claim-only identity. It now performs the same fresh Auth-server identity lookup required by other sensitive portal mutations.
5. claim issuance accepted the first value when duplicate `dogId` fields were submitted. It now requires exactly one string value before authorization or Edge invocation.

The replacement deletion-request function also preserves its earlier `TimeZone=UTC` setting. A full ordered pgTAP run caught the missing function setting during implementation; preserving it keeps tombstone timestamps and hashes caller-timezone independent.

## Adversarial proof

The authorization Playwright test runs as one dependent test with one worker and zero retries. It proves:

- anonymous denial for every protected route shape;
- the same rendered 404 for malformed, missing, inaccessible, cross-dog, and cross-recording identifiers;
- editor read/write controls and viewer read-only controls;
- crafted missing/cross-object form values for claim, brightness, and revoke produce matching bounded results and no mutation;
- exact row counts and primary/foreign-key scope for every returned row in all eleven raw projections across owner A, owner B, editor, and viewer, plus exact anonymous denial and reciprocal owner isolation;
- editor success plus viewer/other-owner/anonymous denial at the claim Edge endpoint;
- allowed create/configure/delete/status behavior plus role, missing/inaccessible, anonymous, and cross-owner behavior across all five user RPCs; direct editor revocation remains denied without weakening M1.12 owner retry, collar-state, or lock-order semantics;
- a privileged fixture-scoped SHA-256 snapshot of public rows plus claims, credentials, sync receipts, chunks, and deletion tombstones/jobs/receipts is unchanged by every denied action/RPC cluster;
- after viewer deletion, the already-open private page redirects to login, the already-rendered onboarding action creates no dog, all eleven raw projections are empty, all five user RPCs and the claim Edge endpoint deny the stale token, and the post-cascade graph digest remains unchanged.

Trace, screenshots, video, and HTML reporting remain disabled. Tokens, passwords, claim codes, credential material, and raw response bodies stay in memory. Automatic Playwright failure context is removed after each run; pass and failure artifacts are checked against an exact allowlisted schema containing only schema version, phase, cycle number, surface counts, per-table counts, and checkpoint names.

## Toolchain and result

The reviewed local result used:

- checksum-verified Node `24.18.0`;
- Supabase CLI `2.113.0` and local PostgreSQL 17;
- Next.js `16.3.1` production build/start;
- Playwright `1.62.1` with Chromium revision `1234`;
- 16 imperative migrations and 21 database pgTAP files.

Final recorded checks:

- `supabase test db`: 492/492 assertions passed;
- portal unit/static contracts: 122/122 passed;
- `npm run phase1:check`: passed, including lint, workspace types/tests, generated-asset/contracts checks, and secret scan;
- `node tools/portal-e2e/run.mjs --clean`: the M1.13 owner journey and M1.14 authorization matrix each passed from two independent clean resets with one worker and zero retries;
- database lint and advisors: no blocking findings.

The runner performs a final database reset, clears Mailpit, stops only its owned portal process, removes the ignored function env, and retains no browser failure context. No hosted endpoint, push, deployment, or remote CI run was performed or inspected.

## Reproduction

From the repository root with the checksum-verified Node 24.18.0 selected:

```powershell
supabase test db
npm run phase1:check
node tools/portal-e2e/run.mjs --clean
```

The last command is intentionally destructive only to this repository's disposable local Supabase project and refuses to run without `--clean`.
