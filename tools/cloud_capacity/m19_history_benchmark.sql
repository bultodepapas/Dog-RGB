\set ON_ERROR_STOP on
\pset pager off
\timing on

-- Reproduces the M1.9 fixture plus index size/write-cost/rollback evidence.
-- The actual read plans must be captured through the Data API plan media type,
-- because PostgREST's embedded !inner relation has a different SQL shape from
-- a hand-written join. See docs/cloud/m19-history-query-plan.md.

select version() as postgres_version;
select name, setting, unit
from pg_settings
where name in ('shared_buffers', 'effective_cache_size', 'work_mem', 'random_page_cost')
order by name;

begin;
set local statement_timeout = '60s';
set local work_mem = '4MB';

insert into api.dogs (id, name, timezone, created_by)
values
  ('31900000-0000-4000-8000-000000000001', 'History target', 'America/Bogota', '10000000-0000-4000-8000-000000000001'),
  ('31900000-0000-4000-8000-000000000002', 'History unrelated', 'America/Bogota', '20000000-0000-4000-8000-000000000002');

insert into api.dog_memberships (dog_id, user_id, role)
values
  ('31900000-0000-4000-8000-000000000001', '10000000-0000-4000-8000-000000000001', 'owner'),
  ('31900000-0000-4000-8000-000000000002', '20000000-0000-4000-8000-000000000002', 'owner');

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, hardware_revision,
  firmware_version, protocol_version, telemetry_schema, config_schema
)
values
  ('41900000-0000-4000-8000-000000000001', '51900000-0000-4000-8000-000000000001', '31900000-0000-4000-8000-000000000001', 'History active', 'active', 'benchmark', 'benchmark', 1, 3, 7),
  ('41900000-0000-4000-8000-000000000002', '51900000-0000-4000-8000-000000000002', '31900000-0000-4000-8000-000000000001', 'History revoked', 'revoked', 'benchmark', 'benchmark', 1, 3, 7),
  ('41900000-0000-4000-8000-000000000003', '51900000-0000-4000-8000-000000000003', '31900000-0000-4000-8000-000000000002', 'History unrelated', 'active', 'benchmark', 'benchmark', 1, 3, 7);

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
)
select
  ('71000000-0000-4000-8000-' || lpad(sample::text, 12, '0'))::uuid,
  case when sample % 2 = 0
    then '41900000-0000-4000-8000-000000000001'::uuid
    else '41900000-0000-4000-8000-000000000002'::uuid
  end,
  sample,
  case when sample <= 8000
    then '2026-01-01 00:00:00+00'::timestamptz + make_interval(secs => sample)
    else null
  end,
  'America/Bogota',
  case when sample % 2 = 0 then 'closed' else 'incomplete' end,
  0,
  case when sample <= 8000 then 'gnss_trusted' else 'unknown' end,
  3,
  'benchmark'
from generate_series(1, 10000) as fixture(sample);

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
)
select
  ('72000000-0000-4000-8000-' || lpad(sample::text, 12, '0'))::uuid,
  '41900000-0000-4000-8000-000000000003'::uuid,
  sample,
  case when sample <= 8000
    then '2026-01-01 00:00:00+00'::timestamptz + make_interval(secs => sample)
    else null
  end,
  'America/Bogota',
  'closed',
  0,
  case when sample <= 8000 then 'gnss_trusted' else 'unknown' end,
  3,
  'benchmark'
from generate_series(1, 10000) as fixture(sample);

analyze api.recordings;

\echo 'Baseline 5,000-row write sample without the M1.9 index'
drop index api.recordings_history_started_id_idx;
savepoint baseline_write;
explain (analyze, buffers, settings)
insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
)
select
  ('73000000-0000-4000-8000-' || lpad(sample::text, 12, '0'))::uuid,
  '41900000-0000-4000-8000-000000000003'::uuid,
  20000 + sample,
  '2026-02-01 00:00:00+00'::timestamptz + make_interval(secs => sample),
  'America/Bogota', 'closed', 0, 'gnss_trusted', 3, 'benchmark'
from generate_series(1, 5000) as fixture(sample);
rollback to savepoint baseline_write;

\echo 'Candidate size and 5,000-row write sample'
create index recordings_history_started_id_idx
  on api.recordings (started_at desc nulls last, id desc);
analyze api.recordings;

select
  pg_size_pretty(pg_relation_size('api.recordings_history_started_id_idx')) as size,
  pg_relation_size('api.recordings_history_started_id_idx') as size_bytes,
  count(*) as fixture_recordings,
  round(pg_relation_size('api.recordings_history_started_id_idx')::numeric / count(*), 2)
    as index_bytes_per_recording
from api.recordings;

savepoint candidate_write;
explain (analyze, buffers, settings)
insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
)
select
  ('73000000-0000-4000-8000-' || lpad(sample::text, 12, '0'))::uuid,
  '41900000-0000-4000-8000-000000000003'::uuid,
  20000 + sample,
  '2026-02-01 00:00:00+00'::timestamptz + make_interval(secs => sample),
  'America/Bogota', 'closed', 0, 'gnss_trusted', 3, 'benchmark'
from generate_series(1, 5000) as fixture(sample);
rollback to savepoint candidate_write;

select to_regclass('api.recordings_history_started_id_idx') is not null
  as candidate_exists_before_rollback;

\if :{?keep_fixture}
  commit;
  \echo 'Fixture committed for the exact PostgREST plan probe; run the documented cleanup immediately afterward.'
\else
  rollback;
  select to_regclass('api.recordings_history_started_id_idx') is not null
    as migration_index_restored_after_rollback;
\endif
