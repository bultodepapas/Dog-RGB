# Phase 1 raw telemetry retention drill

**Status:** Local primitive verified on 2026-08-18. Automatic deletion is not
scheduled or enabled. Hosted load/monitoring, user disclosure/consent, export,
backup replay, and the remaining policy data classes are still gates.

## Result

Migration
[`20260818174922_phase1_raw_telemetry_retention.sql`](../../supabase/migrations/20260818174922_phase1_raw_telemetry_retention.sql)
implements the first narrow retention class: raw telemetry points and their
orphaned chunk identity metadata. The worker is private and executable only by
`service_role`; browser roles and `service_role` have no direct table access.
No `pg_cron` extension or schedule is created by the migration.

[`11_raw_telemetry_retention.test.sql`](../../supabase/tests/database/11_raw_telemetry_retention.test.sql)
adds 33 transactional assertions. The complete database suite passes **226/226**.

## Semantics

One enqueue call creates at most one job per collar and UTC day. Its cutoff is
the UTC day boundary minus one calendar year, preserving the 12-month policy
across leap years. The deletion boundary is inclusive:

- `cutoff - 1 microsecond`: expired;
- `cutoff`: expired;
- `cutoff + 1 microsecond`: retained.

A plausible `recorded_at` is authoritative. Unknown time uses authenticated
`received_at`. A timestamp more than ten minutes ahead of receipt also falls
back to `received_at`, preventing a malformed device clock from creating
immortal location.

## Bounded and replay-safe execution

Each processor call claims one ready job with `FOR UPDATE SKIP LOCKED`, accepts
between 1 and 10,000 rows, and commits one bounded stage:

1. purge eligible points;
2. purge chunk identities no longer backed by any retained point;
3. advance the fully-purged watermark and write a hashed receipt.

The worker and sync ingestion use the same per-collar advisory lock. Before the
first physical batch, the worker advances `reject_at_or_before`; the point
trigger then rejects an expired replay with `telemetry_expired_by_retention`.
`purged_at_or_before` advances only after both point and chunk stages finish.
This distinction prevents deleted telemetry from being resurrected without
claiming physical completion early.

Failures roll back the data batch, retain cumulative progress, store only the
SQLSTATE, and become retryable after five minutes. The drill injects a forced
delete failure, proves the rows remain, and resumes the same job. Retention also
serializes against the collar-revocation transition used by dog deletion, so
the dog job cannot snapshot counts in the middle of a retention commit.

## Capacity decision

The first design added a fourth full telemetry index. The one-million-point gate
measured 406,208,512 bytes, or **406.21 bytes/point**, and rejected it above the
Phase 0 +20% ceiling.

The accepted design reuses `telemetry_points_collar_time_idx` for normal trusted
timestamps and adds a partial fallback index only for unknown or implausibly
future clocks. With one million normal points:

| Measurement | Result |
| --- | ---: |
| Heap | 174,301,184 bytes |
| All indexes | 149,405,696 bytes |
| Partial retention fallback index | 8,192 bytes |
| Total | 323,788,800 bytes |
| Total per point | **323.79 bytes** |

The owner day, owner month, 2,000-point keyset route, bbox diagnostic, and
non-member exact lookup completed in 8.20 ms, 277.68 ms, 1.06 ms, 272.01 ms,
and 0.37 ms respectively. The capacity runner was also corrected to raise a
real SQL exception on failure; PostgreSQL 17 ignored the earlier `\quit 3/4`
arguments and could otherwise have returned a false green process status.

## Security and topology

- All three retention tables have RLS enabled and no policies.
- Only two narrow worker functions are granted to `service_role`.
- Receipts contain IDs, cutoff, counts, timestamps, and a SHA-256 hash; no
  coordinates, request bodies, names, email, or secret columns exist.
- The deletion cascade inventory now covers 20 dependent tables, including
  retention watermark/job/receipt state.
- The dog deletion count manifest now covers 21 data classes and includes the
  new retention state before cascade.

## Still required before activation

- pre-deletion export plus reviewed disclosure/consent and strong confirmation;
- disposable hosted concurrency and ingestion-load measurements for the worker;
- reviewed Cron cadence, timeout, backlog/oldest-overdue alerts, and runbook;
- tombstone export/replay and managed backup/PITR restore before traffic;
- bounded retention for sync receipts, claims, revoked credentials, config
  revisions, deletion audit state, logs, and any future export objects;
- a production decision for provider backup/log expiry.

Until those gates close, invoking the local worker is test/engineering evidence,
not authorization to purge user or production data.
