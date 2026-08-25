# PostgreSQL capacity benchmarks

## Phase 1 migrated-schema gate

`phase1_benchmark.sql` loads one million points across two synthetic collars
into the actual migrated `api.telemetry_points` table. It measures the heap and
every real index, fails above the Phase 0 no-GiST bytes/point baseline plus 20%,
and executes day, month, exact M1.10 first/deep 101-row keyset, bounding-box,
and cross-user RLS checks. Both recording-detail plans fail closed above 100 ms
or if they do not use `telemetry_points_pkey`, add a Sort or temporary spill, or
scan unrelated telemetry rows.

Run it only against this repository's disposable local Supabase stack:

```powershell
npm run phase1:capacity -- --clean
```

The explicit flag is mandatory because the runner resets the local database
before the benchmark and again afterward. Evidence is written to the ignored
`test-results/capacity/phase1-local.txt` path and uploaded by CI. The runner
does not contact a linked or hosted project.

For the M1.10 raw REST and browser fixture after a clean reset, pipe
`m110_recording_detail_fixture.sql` into the local database container, then run
`m110_recording_detail_rest_probe.mjs` with the local `SUPABASE_URL` and
`SUPABASE_ANON_KEY`. Both files reject or avoid hosted-project use. Reset the
local database afterward.

## Phase 0 logical-schema benchmark

This benchmark loads one million representative Dog RGB telemetry points into a
throwaway local PostgreSQL/PostGIS database. It records storage, index, query,
and serialization evidence before the Phase 1 schema is frozen.

It does **not** contact the configured Supabase project. The runner creates an
isolated Docker container bound to loopback, prints evidence to standard output,
and removes only the container it created.

Run from the repository root:

```powershell
pwsh -File tools/cloud_capacity/run.ps1
```

Requirements:

- Docker Desktop with Linux containers;
- the cached/default Supabase PostgreSQL image, or network access to pull it;
- approximately 1 GiB of temporary disk space;
- no process listening on the selected loopback port.

The SQL intentionally reflects the Phase 0 logical telemetry shape rather than
claiming to be a Phase 1 migration. Re-run it after material schema/index changes.
