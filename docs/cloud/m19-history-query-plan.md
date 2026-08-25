# M1.9 History query and index evidence

**Captured:** 2026-08-25 (America/Bogota)

**Implementation:** `c43313747d154f91b49f888f772b9f924f6e6e2c`

**Scope:** local comparative evidence for the exact authenticated PostgREST History query; not a hosted latency SLO

## Acceptance gate

The exact first, deep known-time, and deep null-time Data API queries must each:

- run as the seeded owner through `authenticated` RLS;
- use the exact embedded collar projection/filter and global order used by the portal DAL;
- return no more than the `20 + 1` lookahead rows;
- complete within 100 ms after fixture load and `ANALYZE`;
- use no disk-backed temporary sort.

The candidate index is accepted only if it is narrow, remains below 64 bytes per fixture recording, adds no more than 10 microseconds per recording in the rollback-only 5,000-row write sample, and is restored by transaction rollback. These are local comparison gates for this DIY workload, not promises about a future hosted region.

## Environment and fixture

- Supabase CLI: `2.113.0`
- PostgreSQL: `17.6` (`public.ecr.aws/supabase/postgres:17.6.1.158`)
- `shared_buffers`: 128 MiB
- `effective_cache_size`: 128 MiB
- `work_mem`: 4 MiB
- `random_page_cost`: 4
- 10,000 authorized recordings across one active and one revoked collar
- 10,000 unauthorized recordings for another dog
- 8,000 known and 2,000 null `started_at` values per dog
- explicit owner session, collar embed/filter, RLS, `started_at DESC NULLS LAST, id DESC`, and limit 21

The fixture is synthetic and transactional by default. [`m19_history_benchmark.sql`](../../tools/cloud_capacity/m19_history_benchmark.sql) creates it, compares index write/size cost, and proves rollback. [`m19_history_postgrest_plan.mjs`](../../tools/cloud_capacity/m19_history_postgrest_plan.mjs) uses Supabase's plan media type so the measured SQL is PostgREST's actual `!inner` embed rather than an inaccurate hand-written join.

## Read-plan result

| Exact Data API page | Existing indexes | Candidate index | Candidate access path | Result |
| --- | ---: | ---: | --- | --- |
| First | 7,275.000 ms | 14.222 ms | ordered `recordings_history_started_id_idx` scan; 21 accepted and 21 RLS-filtered rows | PASS |
| Deep known-time | 3,114.923 ms | 15.137 ms | ordered candidate scan; no explicit Sort; 21 rows | PASS |
| Deep null-time | 319.983 ms | 5.845 ms | candidate `Index Cond` on null time plus lower UUID; 21 rows | PASS |

All candidate executions were below 100 ms and had no temporary-disk spill. The baseline plans used a sequential or bitmap scan plus top-N sort. The candidate plans stopped through the exact global-order index; the known-time case still advanced past earlier index entries to the cursor, but remained bounded by the ordered access path and gate.

## Size, write, and rollback result

- Candidate size at 20,000 fixture rows: 827,392 bytes (808 KiB), 41.37 bytes per row.
- The conservative observed insert delta was 13.578 ms for 5,000 rows, or 2.72 microseconds per row; the candidate sample still completed in 59.392 ms. Repeated local runs varied because FK-trigger and cache time dominate this tiny bulk operation, so the absolute per-row ceiling is the governing gate rather than a misleading percentage.
- The benchmark creates and drops the candidate only inside a transaction by default. `to_regclass` proved it existed before rollback and that the migration-owned index existed again afterward.
- No covering columns were added. The pre-existing collar-leading index remains available for collar-scoped reads.

## Reproduction

Start from the clean local stack. The commands below keep secrets in process variables and do not print them. A committed fixture is required only while the Data API plan is captured and is deleted immediately afterward.

```powershell
$status = npx --yes supabase@2.113.0 status -o json 2>$null | ConvertFrom-Json
$env:SUPABASE_URL = $status.API_URL
$env:SUPABASE_ANON_KEY = $status.ANON_KEY

Get-Content -Raw tools/cloud_capacity/m19_history_benchmark.sql |
  docker exec -i supabase_db_Dog-RGB-1 psql -X -v ON_ERROR_STOP=1 -v keep_fixture=1 -U postgres -d postgres

docker exec supabase_db_Dog-RGB-1 psql -X -v ON_ERROR_STOP=1 -U postgres -d postgres -c `
  "alter role authenticator set pgrst.db_plan_enabled = true; notify pgrst, 'reload config'; drop index api.recordings_history_started_id_idx; analyze api.recordings;"

node tools/cloud_capacity/m19_history_postgrest_plan.mjs

docker exec supabase_db_Dog-RGB-1 psql -X -v ON_ERROR_STOP=1 -U postgres -d postgres -c `
  "create index recordings_history_started_id_idx on api.recordings (started_at desc nulls last, id desc); analyze api.recordings;"

$env:M19_HISTORY_MAX_EXECUTION_MS = '100'
node tools/cloud_capacity/m19_history_postgrest_plan.mjs

docker exec supabase_db_Dog-RGB-1 psql -X -v ON_ERROR_STOP=1 -U postgres -d postgres -c `
  "create index if not exists recordings_history_started_id_idx on api.recordings (started_at desc nulls last, id desc); delete from api.dogs where id in ('31900000-0000-4000-8000-000000000001','31900000-0000-4000-8000-000000000002'); analyze api.recordings; alter role authenticator reset pgrst.db_plan_enabled; notify pgrst, 'reload config'; select to_regclass('api.recordings_history_started_id_idx') is not null as migration_index_restored;"
```

If any command fails after committing the fixture, including the baseline probe after it drops the index, run the final idempotent cleanup command before continuing. Do not consider cleanup complete unless `migration_index_restored` returns `t`; no benchmark row, temporary plan setting, or raw route response belongs in a committed production environment.
