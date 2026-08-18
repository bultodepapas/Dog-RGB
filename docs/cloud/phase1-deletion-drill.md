# Phase 1 deletion workflow drill

**Status:** Local cascade, owner-authorized dog jobs, and isolated restore replay
verified and extended on 2026-08-18. No user-facing delete UI, data export,
account-deletion workflow, scheduled retention purge, hosted backup restore, or
production-data
deletion is enabled by this evidence.

## Result

[`09_deletion_cascade.test.sql`](../../supabase/tests/database/09_deletion_cascade.test.sql)
adds 13 transactional pgTAP assertions around two isolated synthetic accounts and
dogs. One dog is the deletion target and the other has the same complete data
topology as a survivor/control.

The drill verifies that:

- the recursive `ON DELETE CASCADE` graph from `api.dogs` exactly matches the 20
  current dependent tables; adding another dependent table makes the inventory
  assertion fail until its fixture and verification are reviewed;
- every foreign key in the project-owned `api` and `private` schemas has a usable
  leading-column index, including the safe `actor_user_id is not null` partial
  index used by `ON DELETE SET NULL`;
- an account that still owns a dog fails closed through
  `api.dogs.created_by ON DELETE RESTRICT`;
- deleting the dog removes collar, credential (including a revoked credential),
  claim, sync receipt, recording, raw point, chunk, loss marker, daily/recording
  summary, dirty-work item, desired/reported configuration, revision, and HLC
  state rows;
- the equally populated control dog is unchanged;
- after owned dogs are deleted, account deletion removes its profile and
  cross-dog memberships while setting retained configuration audit authorship to
  `NULL` rather than deleting the other owner's data.

The test runs inside a transaction and rolls back. It exercises the complete
database integrity primitive as `postgres`.

## Owner-authorized job primitive

Migration
[`20260818014827_phase1_deletion_jobs.sql`](../../supabase/migrations/20260818014827_phase1_deletion_jobs.sql)
adds three private, RLS-enabled tables for append-only tombstones, mutable jobs,
and immutable completion receipts. Neither browser nor `service_role` receives
direct table access.

`api.request_dog_deletion_v1` is the only authenticated request path. It:

- serializes the durable request ID and returns one result for exact concurrent
  replay;
- verifies a current owner inside the transaction;
- waits for credential-locked sync work, then revokes credentials and collars;
- snapshots counts for all 21 active dog-scoped data classes without copying
  row contents;
- inserts the tombstone and job before setting `deleted_at` and removing every
  membership, closing both RLS and membership-backed RPC authorization paths.

`private.process_dog_deletion_batch_v1` is executable only by `service_role`.
Each invocation claims one ready job with `FOR UPDATE SKIP LOCKED`, deletes a
configurable 1–10,000 telemetry points, and returns. Once no points remain it
verifies the cumulative count, cascades the remaining graph, proves the active
scope is empty, and writes a SHA-256 receipt. A failed batch rolls back its data
changes, records only SQLSTATE plus the next retry time, and never reports early
completion. No Cron schedule exists yet.

[`10_deletion_jobs.test.sql`](../../supabase/tests/database/10_deletion_jobs.test.sql)
adds 34 transactional assertions. It also deliberately corrupts the expected
point count before finalization, proving that the last point and progress remain
intact through failure and that the same job later retries successfully.

The clean local gate then exercises PostgREST rather than calling the owner RPC
as `postgres`: two simultaneous requests converge to one job, the owner sees no
dog or route after commit, another account receives `403`, and six simulator
points are removed in three two-row worker transactions. The ignored/CI artifact
contains counts, statuses and hash-presence flags only.

## Tombstone export and restore replay

Migration
[`20260818182500_phase1_deletion_tombstone_replay.sql`](../../supabase/migrations/20260818182500_phase1_deletion_tombstone_replay.sql)
adds a bounded, cursor-based `service_role` export and an exact restore-replay
boundary. Twenty-four additional pgTAP assertions cover grants, fixed UTC hash
semantics, exact coordinate-free fields, pagination, malformed/tampered input,
idempotency, restored ingress/access closure, bounded completion, and receipt
creation. The complete database suite now passes 250 assertions.

The [local restore drill](phase1-restore-drill.md) restores one snapshot twice,
creates and exports a later deletion in one isolated database, then rejects a
modified item and replays the exact tombstone into the other before traffic.
The export item and logical backup remain memory-only; CI keeps hashes, counts,
statuses, and timings. Off-site custody/authentication and a managed hosted
restore remain operational gates.

## Backup lag and restore boundary

Active database deletion does not rewrite immutable backups. Supabase currently
documents daily backups for Pro, Team, and Enterprise projects with plan-dependent
windows, while PITR has its own configurable recovery window. Free projects should
make their own regular off-site logical exports. Storage objects are not contained
in database backups. These are provider capabilities, not a Dog-RGB deletion SLA:
[Supabase database backups](https://supabase.com/docs/guides/platform/backups).

Before any persistent field deployment, the exact selected project's backup/PITR
window must be recorded in the user-facing policy. If an isolated restore point
predates a deletion request, deletion tombstones/jobs must be replayed and the same
data-class manifest, RLS, Auth linkage, counts, and coordinate-free hashes must pass
before traffic is allowed. Until that hosted restore drill exists, Dog-RGB must not
claim immediate deletion from backups or durable recoverability.

## Still required before production/user activation

- complete pre-deletion export with counts, hashes, schema/units, and short-lived
  artifact cleanup;
- strong confirmation plus recent-session/reauthentication UX, and the analogous
  account-deletion orchestration around Supabase Auth;
- hosted concurrency/ingestion-load validation and an explicitly reviewed Cron
  schedule for the locally tested raw-telemetry worker, plus bounded workers for
  the remaining retention classes;
- authenticated/monitored off-site tombstone custody and replay into an isolated
  managed hosted restore; the local two-database drill cannot substitute the
  provider operation;
- provider-specific backup expiry copy and verification of any future Storage
  object lifecycle.

Automatic raw telemetry or sync-receipt retention deletion therefore remains
disabled. The dog worker is reachable only after an explicit owner request; it
is not a retention scheduler. The separate
[raw-telemetry retention drill](phase1-retention-drill.md) is local execution
evidence only and installs no Cron job.
