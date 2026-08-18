# Cloud retention and deletion policy

**Status:** Phase 0 accepted default, 2026-08-13. The local Phase 1 cascade
primitive is tested; enforcement jobs, export/user controls, and hosted restore
remain unimplemented.

This operational policy implements [ADR-0010](../adr/0010-retention-and-truthful-activity-vocabulary.md). It is a launch input, not a claim that Supabase/Vercel currently contain or delete Dog-RGB data.

## Principles

- Cloud collection is opt-in and off by default.
- Precise location receives the shortest useful default window.
- Retention is based on the data's purpose, not available storage.
- Expiry and user deletion are enforced server-side and observable; hiding a row in the UI is not deletion.
- Operational logs never carry coordinates or secrets.
- Backups have disclosed expiry and a restore procedure that reapplies deletions before serving data.
- No indefinite/legal-hold exception exists in the initial DIY service. If law or an incident creates one, document authority, scope, access, expiry, and user notice before enabling it.

## Schedule

| Data set | Active retention start | Default deadline | Deletion action / evidence |
| --- | --- | --- | --- |
| telemetry points | trustworthy observation instant; if unknown, recording/ingest rule must be explicit | 12 months | daily bounded purge deletes expired points; record counts/time range only, no coordinates |
| chunk identity/header/hash receipt metadata | observation-end instant | 12 months, and never longer than its points | delete with point history; successful raw request/chunk bodies are never retained as a second blob copy |
| recording metadata | recording start/end | dog/account lifetime | owner deletion cascades point/derived rows; metadata not kept as a location tombstone |
| daily/versioned summaries | local day | dog/account lifetime | cascade on dog deletion; superseded versions may be reduced to active + audit within revision window |
| current desired/reported config | latest resource mutation/report | collar link/dog lifetime | delete on unlink if no audit need; always delete on dog/account deletion |
| config revisions/apply reports | server acceptance/report instant | 12 months | rolling purge preserves current state independently |
| claim-code digest/attempt rows | issue instant | valid no more than 900 seconds; consumed/expired purge within 24 hours | atomic state then purge; aggregated coordinate-free abuse counters only |
| request/idempotency response receipt | first authenticated receipt | 30 days | purge response/details; database telemetry identity constraints continue to prevent duplicate logical points |
| device credential digest | issue instant | until revocation/unlink | revoke immediately; keep a non-usable, revoke-only digest/tombstone at most 90 days so exact replay returns its stored disposition and a later/different revoke can authenticate an `already_revoked` receipt, then purge; a collar still pending beyond that window requires the warned force-clear/revoke-status recovery path |
| security/audit event | event instant | 12 months | rolling purge; event is redacted and coordinate-free |
| Edge/Vercel application logs | event instant | 14-day target or shortest supported setting | provider expiry; sample/scan proves no request body, auth, coordinate, dog name |
| account/profile/membership | creation/mutation | account/dog lifetime | account/dog deletion workflow; retain coordinate-free deletion receipt only |
| deletion job/receipt | request/completion | 12 months | contains scope IDs/hash/status/times only; no location, profile payload, or secret |
| export artifact | completed export | download once or maximum 24 hours | encrypted/private storage, signed short-lived URL, delete job/artifact after deadline |

Use UTC database instants for retention comparison. Local calendar days are presentation/analytics semantics, not a reason to extend raw-point retention. Rows with invalid/unknown observation time are quarantined and use the earliest defensible ingest/recording time for deletion so malformed clocks cannot create immortal data.

## User-selectable behavior

Initial UI may offer “delete now” at recording/dog/account scope. A later setting may shorten raw location retention (for example 30 or 90 days). It must not offer a longer/default-infinite period without a new privacy/cost decision and explicit consent. Shortening applies to existing eligible data in the next purge, with confirmation explaining loss of future recomputation.

Deleting a collar link and deleting history are separate choices. Revocation always stops future device access; it does not silently remove history. Deleting a dog/account cascades all active location, summary, configuration, membership, and association data in scope.

## Job design

- A scheduled database/Edge worker claims small batches using an indexed cutoff and commits each batch; it is retry-safe and does not hold a table-wide transaction.
- A job state records scope, cutoff, rows attempted/deleted, last error, next retry, and completion without location payload.
- Foreign keys/cascades are explicitly tested; storage objects, derived tables, materialized/cache layers, and search indexes appear in the deletion inventory.
- RLS is not the worker's authorization. User-requested delete first authorizes owner/scope in a narrow transaction, then a service worker executes only that recorded job.
- Expiry jobs never call an Internet endpoint per point. Monitor backlog age and oldest overdue item, not sensitive sample content.
- If deletion partially fails, UI remains `deleting`/`failed`, access is blocked where possible, and automatic retry continues; do not report complete early.

## Backups and restoration

Before production, record the chosen Supabase plan and its current backup/PITR window. Supabase documents plan-dependent daily backup retention and optional PITR; verify the deployed project rather than copying a stale number into UI.

Deletion cannot instantly edit immutable backups. Promise instead:

1. active systems purge within 24 hours after a successful authorized request;
2. encrypted backup remnants expire under the documented provider window;
3. restore is isolated, then all deletion tombstones/jobs whose request predates the restored point are replayed and verified before traffic resumes;
4. backup access is restricted/audited and restore drills use synthetic data.

Free-plan projects without suitable managed backup/restore controls require regular encrypted exports and their own tested expiration; they are not acceptable for persistent external-user production merely because the application is small.

## External/provider records

Dog-RGB controls its database and configured logs, not all provider security/billing records. The production privacy notice names Supabase, Vercel, email provider, and map provider and links current policies. The map architecture avoids sending the route, but tile requests still disclose IP/viewport. DNS, certificate transparency, billing, and provider abuse records may have independent retention.

## Verification

The local [Phase 1 deletion drill](phase1-deletion-drill.md) now covers the full
current dog cascade topology, foreign-key indexability, account ordering,
cross-dog survival, profile/membership cleanup, and retained-audit anonymization.
It deliberately does not activate the policy: export, bounded purge jobs,
receipts/tombstones, and isolated hosted restore remain gates.

At implementation, automated tests and a staging drill must cover:

- exact `deadline - 1`, `deadline`, and `deadline + 1` boundaries;
- leap dates, invalid/unknown/future observation time, batch retry, worker crash, duplicate job, and foreign-key failure;
- record, dog, collar unlink, and account deletion across every table/object/cache;
- export contents before and absence after deletion;
- revoked credential and request receipt expiration without enabling replay duplicates;
- restore of a backup predating deletion followed by mandatory tombstone replay;
- provider log settings and canary scan for credentials/coordinates;
- load/query plan so purge cannot starve ingestion.

The production dashboard alerts on overdue purge jobs, oldest expired row, failed deletion, export artifact past TTL, and backup/restore drill age. A quarterly review reconfirms provider settings, costs, tables/objects, and privacy copy.

## References

- [Supabase database backups](https://supabase.com/docs/guides/platform/backups)
- [Supabase Cron](https://supabase.com/docs/guides/cron)
- [PostgreSQL routine vacuuming](https://www.postgresql.org/docs/current/routine-vacuuming.html)
- [NIST Privacy Framework](https://www.nist.gov/privacy-framework)
