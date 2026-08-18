# Phase 1 deletion-cascade drill

**Status:** Local database primitive verified on 2026-08-17. No user-facing delete
workflow, retention purge, hosted backup restore, or production-data deletion is
enabled by this evidence.

## Result

[`09_deletion_cascade.test.sql`](../../supabase/tests/database/09_deletion_cascade.test.sql)
adds 13 transactional pgTAP assertions around two isolated synthetic accounts and
dogs. One dog is the deletion target and the other has the same complete data
topology as a survivor/control.

The drill verifies that:

- the recursive `ON DELETE CASCADE` graph from `api.dogs` exactly matches the 17
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

The test runs inside a transaction and rolls back. It exercises the database
integrity primitive as `postgres`; it does not pretend that authorization,
confirmation UX, deletion job/receipt, export, or tombstone replay already exist.

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

## Still required before retention activation

- narrow owner-authorized dog/account deletion jobs with durable, coordinate-free
  status and receipts;
- complete pre-deletion export with counts, hashes, schema/units, and short-lived
  artifact cleanup;
- bounded retention purges with cutoff-boundary, retry, crash, and ingestion-load
  tests;
- deletion-tombstone export and replay into an isolated hosted restore;
- provider-specific backup expiry copy and verification of any future Storage
  object lifecycle.

Automatic raw telemetry or sync-receipt deletion therefore remains disabled.
