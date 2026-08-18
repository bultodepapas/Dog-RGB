# Phase 1 isolated restore drill

**Status:** Local logical restore and deletion-tombstone replay verified on
2026-08-18. A managed restore into a separate hosted Supabase project remains
required before persistent field use.

## Purpose and boundary

`npm run phase1:local -- --clean` now ends by running
[`phase1_restore.mjs`](../../tools/cloud_restore/phase1_restore.mjs). The runner
takes a consistent custom-format `pg_dump` of the synthetic local database and
keeps it only in process memory with a 128 MiB safety ceiling. It restores the
same snapshot into two randomly named databases in the disposable local
Supabase cluster and removes both in a `finally` path.

The drill uses the local-only `supabase_admin` role because current Supabase
Postgres deliberately keeps the ordinary `postgres` role non-superuser. This
also caught a real compatibility boundary: a complete dump contains managed
objects owned by historical platform roles and functions with superuser-only
settings. Granting more authority to the application role would be the wrong
fix. The platform performs those privileged steps for a hosted physical/PITR
restore.

The local stack and its default credentials are development infrastructure, not
a production backup system.

## Verification manifest

[`manifest.sql`](../../tools/cloud_restore/manifest.sql) emits only counts and
SHA-256 digests. It never writes coordinates, payloads, device digests, user
records, or configuration bodies into CI evidence. Source and restored
manifests must match exactly for:

- every row count and full-content hash across all `api` and `private` tables;
- Auth user/identity counts and zero orphan profiles, dog creators, or
  memberships;
- explicit route and desired-config-head hashes;
- installed extension versions and migration history;
- project functions, their security mode/configuration, and effective grants;
- RLS enablement, policies, and effective table privileges for `anon`,
  `authenticated`, `service_role`, and `postgres`.

The runner also changes to the `authenticated` role and repeats owner and
non-member probes. The owner must see the seeded dog, collars, telemetry, and
configuration heads; the non-member must see none. It refuses to run against an
empty reset-only fixture because that would make the route/config/RLS evidence
meaningless.

## Post-restore deletion replay

Migration
[`20260818182500_phase1_deletion_tombstone_replay.sql`](../../supabase/migrations/20260818182500_phase1_deletion_tombstone_replay.sql)
adds two `service_role`-only boundaries. The bounded exporter returns a
versioned, cursor-paginated envelope containing only request/scope IDs,
timestamps, one-way user/request fingerprints, and integrity hashes. The replay
function accepts an exact versioned item, validates its field set, canonical
base64url/UTC forms, original request/tombstone digests, and export digest, then
recreates the ordinary deletion job. It fails closed if the restored dog and an
identical prior tombstone are both absent. Exact replay returns the existing job.

The runner treats one isolated database as the post-snapshot deletion source:
it requests and completes a dog deletion there, exports the resulting tombstone,
and retains only the export SHA-256. The second database remains at the older
restore point. It rejects a modified item, applies the exact export, proves user
and device access close before purge, completes the standard bounded worker, and
compares the resulting non-audit manifest with the source deletion outcome.
Generated audit-row IDs and completion times may differ; their counts and core
tombstone fields must not.

The tombstone export is not a signature. Production must durably store and
authenticate exports outside the same backup domain; possession of
`service_role` already grants destructive authority. The local drill proves the
database boundary and recovery ordering, not off-site custody.

Coordinate-free evidence is written to the ignored
`test-results/restore/phase1-local.json` and uploaded from clean CI runners for
14 days. Neither the backup nor the tombstone item is persisted or uploaded.

## What this closes

- reproducible logical backup/restore of the complete synthetic local database;
- exact application-data, Auth-linkage, schema/function and RLS equivalence;
- export and idempotent replay of a deletion newer than the restore point;
- rejection of tampered tombstones and closure of restored user/device access;
- cleanup of both isolated databases on success and failure;
- a regression gate for Supabase/Postgres ownership or dump-format drift.

## Still open

- restore a managed daily backup or PITR point into a distinct disposable
  hosted project of the selected Postgres version;
- run Edge/Auth services against that restored project, not only SQL probes;
- implement authenticated, monitored, off-site tombstone export custody and
  replay it during the managed hosted drill before enabling traffic;
- verify any future Storage objects separately because database backups contain
  Storage metadata, not deleted object contents;
- record measured RPO/RTO, provider backup window, project region, operator and
  recovery communications.

Supabase's current local-backup guidance requires matching the backup's Postgres
image version and explicitly states that a locally restored database is not
production-ready: [restoring a downloaded backup locally](https://supabase.com/docs/guides/local-development/restoring-downloaded-backup).
