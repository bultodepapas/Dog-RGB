\set ON_ERROR_STOP on
\pset pager off
\timing on

\echo 'Phase 1 capacity gate: loading 1,000,000 points into the migrated api.telemetry_points table'
select version() as postgres_version;
select extensions.postgis_full_version() as postgis_version;
select name, setting, unit
from pg_settings
where name in (
  'shared_buffers',
  'effective_cache_size',
  'work_mem',
  'max_parallel_workers_per_gather'
)
order by name;

insert into api.dogs (id, name, timezone, created_by)
values (
  '31000000-0000-4000-8000-000000000003',
  'Capacity Fixture',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
);

insert into api.dog_memberships (dog_id, user_id, role)
values (
  '31000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  'owner'
);

insert into api.collars (
  id,
  device_public_id,
  dog_id,
  display_name,
  state,
  hardware_revision,
  firmware_version,
  protocol_version,
  telemetry_schema,
  config_schema
)
values
  (
    '41000000-0000-4000-8000-000000000001',
    '51000000-0000-4000-8000-000000000001',
    '31000000-0000-4000-8000-000000000003',
    'Capacity A',
    'active',
    'xiao-s3-r1',
    'phase1-capacity',
    1,
    3,
    7
  ),
  (
    '41000000-0000-4000-8000-000000000002',
    '51000000-0000-4000-8000-000000000002',
    '31000000-0000-4000-8000-000000000003',
    'Capacity B',
    'active',
    'xiao-s3-r1',
    'phase1-capacity',
    1,
    3,
    7
  );

insert into api.telemetry_points (
  collar_id,
  boot_sequence,
  point_sequence,
  recorded_at,
  received_at,
  lat_e7,
  lon_e7,
  reported_speed_cmps,
  satellites,
  flags,
  time_quality,
  telemetry_schema,
  firmware_version,
  chunk_sequence
)
select
  case fixture.collar_number
    when 1 then '41000000-0000-4000-8000-000000000001'::uuid
    else '41000000-0000-4000-8000-000000000002'::uuid
  end,
  ((fixture.collar_point - 1) / 100000) + 1,
  ((fixture.collar_point - 1) % 100000) + 1,
  fixture.observed_at,
  fixture.observed_at + make_interval(secs => 30 + (fixture.collar_point % 90)::integer),
  47110000 + round(16000 * sin(fixture.collar_point / 900.0))::integer,
  -740721000 + round(22000 * cos(fixture.collar_point / 1100.0))::integer,
  case when fixture.collar_point % 29 < 20
    then 0
    else 110 + (fixture.collar_point % 510)::integer
  end,
  (6 + fixture.collar_point % 9)::smallint,
  (
    1 -- FIX_VALID
    | 4 -- TIME_TRUSTED
    | case when fixture.collar_point % 29 < 20
        then 8 -- STATIONARY_HEARTBEAT
        else 2 -- MOVEMENT_EVIDENCE
      end
    | case when fixture.collar_point % 97 = 0 then 16 else 0 end -- LOW_QUALITY
  ),
  'gnss_trusted',
  3,
  'phase1-capacity',
  (((fixture.collar_point - 1) % 100000) / 96) + 1
from generate_series(1, 1000000) as source(sample_number)
cross join lateral (
  select
    ((source.sample_number - 1) % 2) + 1 as collar_number,
    ((source.sample_number - 1) / 2) + 1 as collar_point
) as identity
cross join lateral (
  select
    identity.collar_number,
    identity.collar_point,
    '2026-01-01 05:00:00+00'::timestamptz
      + make_interval(secs => ((identity.collar_point - 1) * 5)::double precision) as observed_at
) as fixture;

vacuum (analyze) api.telemetry_points;

\echo 'Actual Phase 1 relation and index sizes'
select
  pg_size_pretty(pg_relation_size('api.telemetry_points')) as heap,
  pg_size_pretty(pg_indexes_size('api.telemetry_points')) as indexes,
  pg_size_pretty(pg_total_relation_size('api.telemetry_points')) as total,
  pg_relation_size('api.telemetry_points') as heap_bytes,
  pg_indexes_size('api.telemetry_points') as index_bytes,
  pg_total_relation_size('api.telemetry_points') as total_bytes,
  round(pg_total_relation_size('api.telemetry_points')::numeric / count(*), 2)
    as total_bytes_per_point,
  count(*) as points
from api.telemetry_points;

select
  indexrelname,
  pg_size_pretty(pg_relation_size(indexrelid)) as size,
  pg_relation_size(indexrelid) as size_bytes
from pg_stat_user_indexes
where schemaname = 'api'
  and relname = 'telemetry_points'
order by pg_relation_size(indexrelid) desc;

-- Phase 0 measured 273.80 B/point for the accepted no-GiST shape. The Phase 1
-- gate permits the documented 20% growth ceiling, including its additional
-- chunk lookup index and generated PostGIS position column.
select (
  pg_total_relation_size('api.telemetry_points')::numeric / count(*) <= 328.56
) as capacity_within_twenty_percent
from api.telemetry_points
\gset

\if :capacity_within_twenty_percent
  \echo 'PASS: bytes/point remains within the Phase 0 +20% capacity ceiling.'
\else
  \echo 'FAIL: bytes/point exceeded the Phase 0 +20% capacity ceiling.'
  do $$ begin
    raise exception using errcode = 'P0001',
      message = 'phase1_capacity_bytes_per_point_exceeded';
  end $$;
\endif

\echo 'Owner RLS: one local-day aggregate (17,280 five-second points)'
set statement_timeout = '60s';
set role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', false);
select set_config(
  'request.jwt.claims',
  '{"sub":"10000000-0000-4000-8000-000000000001","role":"authenticated"}',
  false
);
explain (analyze, buffers, settings)
select
  count(*),
  min(recorded_at),
  max(recorded_at),
  sum(reported_speed_cmps),
  max(reported_speed_cmps)
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and recorded_at >= '2026-01-10 05:00:00+00'::timestamptz
  and recorded_at < '2026-01-11 05:00:00+00'::timestamptz;

\echo 'Owner RLS: thirty-day aggregate (500,000 available points)'
explain (analyze, buffers, settings)
select count(*), sum(reported_speed_cmps), max(reported_speed_cmps)
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and recorded_at >= '2026-01-01 05:00:00+00'::timestamptz
  and recorded_at < '2026-01-31 05:00:00+00'::timestamptz;

\echo 'Owner RLS: exact M1.10 first and deep 101-row recording-detail pages'
create or replace function pg_temp.m110_recording_detail_plan(p_after bigint)
returns jsonb
language plpgsql
as $$
declare
  captured json;
  after_predicate text := case
    when p_after is null then ''
    else format(' and point_sequence > %s', p_after)
  end;
begin
  execute
    'explain (analyze, buffers, settings, format json) '
    || 'select point_sequence, recorded_at, lat_e7, lon_e7, '
    || 'reported_speed_cmps, satellites, flags, time_quality '
    || 'from api.telemetry_points '
    || 'where collar_id = ''41000000-0000-4000-8000-000000000001''::uuid '
    || 'and boot_sequence = 3 '
    || 'and point_sequence >= 1 and point_sequence <= 100000'
    || after_predicate
    || ' order by point_sequence asc limit 101'
  into captured;
  return captured::jsonb;
end;
$$;

create temporary table m110_recording_detail_plans (
  page text primary key,
  plan jsonb not null
);

insert into m110_recording_detail_plans (page, plan) values
  ('first', pg_temp.m110_recording_detail_plan(null)),
  ('deep', pg_temp.m110_recording_detail_plan(40000));

select page, jsonb_pretty(plan) as plan
from m110_recording_detail_plans
order by page;

select bool_and(
  (plan #>> '{0,Execution Time}')::numeric <= 100
  and jsonb_path_exists(
    plan,
    '$[*].** ? (@."Index Name" == "telemetry_points_pkey" && @."Node Type" == "Index Scan")'
  )
  and not jsonb_path_exists(plan, '$[*].** ? (@."Node Type" == "Sort")')
  and not jsonb_path_exists(
    plan,
    '$[*].** ? (@."Relation Name" == "telemetry_points" && @."Node Type" != "Index Scan")'
  )
  and plan::text !~ '"Temp (Read|Written) Blocks": [1-9]'
) as m110_recording_detail_plans_pass
from m110_recording_detail_plans
\gset

\if :m110_recording_detail_plans_pass
  \echo 'PASS: both exact M1.10 pages use telemetry_points_pkey within 100 ms without Sort, spill, or unrelated telemetry scans.'
\else
  \echo 'FAIL: an exact M1.10 recording-detail page violated its frozen capacity plan.'
  do $$ begin
    raise exception using errcode = 'P0001',
      message = 'phase1_capacity_m110_recording_detail_plan_failed';
  end $$;
\endif

\echo 'Owner RLS: Bogotá bounding box without a GiST index'
explain (analyze, buffers, settings)
select count(*)
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and position && extensions.st_makeenvelope(
    -74.0730,
    4.7102,
    -74.0712,
    4.7118,
    4326
  )::extensions.geography;

\echo 'Cross-user RLS isolation at scale'
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', false);
select set_config(
  'request.jwt.claims',
  '{"sub":"20000000-0000-4000-8000-000000000002","role":"authenticated"}',
  false
);
explain (analyze, buffers, settings)
select point_sequence
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and boot_sequence = 1
  and point_sequence = 1;

-- The adversarial pgTAP matrix already proves broad non-member isolation. At
-- capacity scale, use the exact primary-key access path an attacker would use
-- for a guessed identifier; an aggregate over every denied row measures a
-- deliberately unbounded audit query rather than a product/API operation.
select (count(*) = 0) as cross_user_isolated
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and boot_sequence = 1
  and point_sequence = 1
\gset

\if :cross_user_isolated
  \echo 'PASS: the non-member sees zero capacity-fixture points.'
\else
  \echo 'FAIL: cross-user RLS exposed capacity-fixture points.'
  do $$ begin
    raise exception using errcode = 'P0001',
      message = 'phase1_capacity_cross_user_isolation_failed';
  end $$;
\endif

reset role;
