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
  \quit 3
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

\echo 'Owner RLS: recording/keyset route page'
explain (analyze, buffers, settings)
select point_sequence, recorded_at, lat_e7, lon_e7, reported_speed_cmps, satellites, flags
from api.telemetry_points
where collar_id = '41000000-0000-4000-8000-000000000001'::uuid
  and boot_sequence = 3
  and point_sequence > 40000
order by point_sequence
limit 2000;

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
  \quit 4
\endif

reset role;
