# Phase 0 PostgreSQL capacity benchmark

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

