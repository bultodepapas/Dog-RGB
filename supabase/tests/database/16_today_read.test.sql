begin;
create extension if not exists pgtap with schema extensions;

select plan(16);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname = 'api'
      and c.relname in ('collars', 'daily_summaries', 'recordings', 'recording_summaries')
      and c.relrowsecurity
  ),
  4::bigint,
  'every Today product table has RLS enabled'
);
select is(
  (
    select count(*)
    from unnest(array[
      'api.collars', 'api.daily_summaries', 'api.recordings', 'api.recording_summaries'
    ]) as table_name
    where has_table_privilege('authenticated', table_name, 'select')
  ),
  4::bigint,
  'authenticated sessions can select every Today product table'
);
select is(
  (
    select count(*)
    from unnest(array[
      'api.collars', 'api.daily_summaries', 'api.recordings', 'api.recording_summaries'
    ]) as table_name
    where has_table_privilege('anon', table_name, 'select')
  ),
  0::bigint,
  'anonymous sessions have no Today table read grant'
);
select is(
  (
    select count(*)
    from unnest(array[
      'api.collars', 'api.daily_summaries', 'api.recordings', 'api.recording_summaries'
    ]) as table_name
    where has_table_privilege(
      'authenticated', table_name,
      'insert,update,delete,truncate,references,trigger'
    )
  ),
  0::bigint,
  'Today adds no authenticated table-wide write grant'
);

insert into api.dog_memberships (dog_id, user_id, role) values (
  '30000000-0000-4000-8000-000000000003',
  '20000000-0000-4000-8000-000000000002',
  'viewer'
);

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at, last_sync_at
) values
  (
    '61000000-0000-4000-8000-000000000001',
    '62000000-0000-4000-8000-000000000001',
    '30000000-0000-4000-8000-000000000003',
    'Anterior', 'active', '2026-08-20 12:00:00+00', '2026-08-24 12:00:00+00'
  ),
  (
    '61000000-0000-4000-8000-000000000002',
    '62000000-0000-4000-8000-000000000002',
    '30000000-0000-4000-8000-000000000003',
    'Seleccionado', 'active', '2026-08-21 12:00:00+00', '2026-08-24 12:00:00+00'
  ),
  (
    '61000000-0000-4000-8000-000000000003',
    '62000000-0000-4000-8000-000000000003',
    '30000000-0000-4000-8000-000000000003',
    null, 'active', null, null
  ),
  (
    '61000000-0000-4000-8000-000000000004',
    '62000000-0000-4000-8000-000000000004',
    '30000000-0000-4000-8000-000000000003',
    'Revocado', 'revoked', '2026-08-23 12:00:00+00', '2026-08-25 12:00:00+00'
  );

insert into api.daily_summaries (
  dog_id, local_date, timezone, observed_s, moving_s, inactive_s, unknown_s,
  distance_m, valid_points, warning_points, gap_count, dropped_points,
  coverage_ratio, algorithm_version, source_revision, computed_at
) values
  (
    '30000000-0000-4000-8000-000000000003', '2026-08-24', 'America/Bogota',
    3600, 900, 1200, 82800, 1000, 40, 1, 2, 0, 0.041667, 1, 1,
    '2026-08-25 03:00:00+00'
  ),
  (
    '30000000-0000-4000-8000-000000000003', '2026-08-24', 'America/Bogota',
    7200, 1800, 2400, 79200, 2000, 80, 2, 3, 0, 0.083333, 2, 2,
    '2026-08-25 04:00:00+00'
  );

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, min_lat_e7, max_lat_e7, min_lon_e7, max_lon_e7,
  clock_quality, telemetry_schema, firmware_version, created_at
) values
  (
    '63000000-0000-4000-8000-000000000001',
    '61000000-0000-4000-8000-000000000002', 1,
    '2026-08-24 20:00:00+00', 'America/Bogota', 'closed', 30,
    47110000, 47120000, -740721000, -740720000,
    'gnss_trusted', 3, 'today-fixture', '2026-08-24 20:05:00+00'
  ),
  (
    '63000000-0000-4000-8000-000000000002',
    '61000000-0000-4000-8000-000000000002', 2,
    '2026-08-24 20:00:00+00', 'America/Bogota', 'closed', 40,
    47110000, 47120000, -740721000, -740720000,
    'gnss_trusted', 3, 'today-fixture', '2026-08-24 20:05:00+00'
  ),
  (
    '63000000-0000-4000-8000-000000000003',
    '61000000-0000-4000-8000-000000000002', 3,
    null, 'America/Bogota', 'open', 0,
    null, null, null, null,
    'unknown', 3, 'today-fixture', '2026-08-25 04:00:00+00'
  );

insert into api.recording_summaries (
  recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
  valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
  algorithm_version, computed_at
) values
  (
    '63000000-0000-4000-8000-000000000002',
    1200, 400, 500, 300, 500, 40, 1, 1, 0, 0.800000, 1,
    '2026-08-25 03:00:00+00'
  ),
  (
    '63000000-0000-4000-8000-000000000002',
    1200, 400, 500, 300, 500, 40, 1, 1, 0, 0.850000, 2,
    '2026-08-25 04:00:00+00'
  );

select is(
  (
    select id from api.collars
    where dog_id = '30000000-0000-4000-8000-000000000003' and state = 'active'
    order by last_sync_at desc nulls last, linked_at desc nulls last, id asc
    limit 1
  ),
  '61000000-0000-4000-8000-000000000002'::uuid,
  'active collar selection has exact null and tie ordering'
);
select is(
  (
    select algorithm_version from api.daily_summaries
    where dog_id = '30000000-0000-4000-8000-000000000003'
      and local_date = '2026-08-24'
    order by algorithm_version desc
    limit 1
  ),
  2,
  'Today selects the highest daily-summary algorithm version'
);
select is(
  (
    select id from api.recordings
    where collar_id = '61000000-0000-4000-8000-000000000002'
    order by started_at desc nulls last, created_at desc, id desc
    limit 1
  ),
  '63000000-0000-4000-8000-000000000002'::uuid,
  'latest recording selection has exact null and total tie ordering'
);
select is(
  (
    select algorithm_version from api.recording_summaries
    where recording_id = '63000000-0000-4000-8000-000000000002'
    order by algorithm_version desc
    limit 1
  ),
  2,
  'Today selects the highest recording-summary algorithm version'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  (select count(*) from api.collars where dog_id = '30000000-0000-4000-8000-000000000003'),
  4::bigint,
  'owner sees authorized collars'
);
select is(
  (select count(*) from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000003'),
  2::bigint,
  'owner sees authorized daily summaries'
);
select is(
  (
    select count(*) from api.recordings
    where collar_id = '61000000-0000-4000-8000-000000000002'
  ),
  3::bigint,
  'owner sees authorized recordings'
);
select is(
  (
    select count(*) from api.recording_summaries
    where recording_id = '63000000-0000-4000-8000-000000000002'
  ),
  2::bigint,
  'owner sees authorized recording summaries'
);

select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  (
    select
      (select count(*) from api.collars where dog_id = '30000000-0000-4000-8000-000000000003')
      + (select count(*) from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000003')
      + (select count(*) from api.recordings where collar_id = '61000000-0000-4000-8000-000000000002')
      + (select count(*) from api.recording_summaries where recording_id = '63000000-0000-4000-8000-000000000002')
  ),
  11::bigint,
  'viewer has the same read-only Today visibility'
);

select set_config('request.jwt.claim.sub', '90000000-0000-4000-8000-000000000009', true);
select is(
  (
    select
      (select count(*) from api.collars where dog_id = '30000000-0000-4000-8000-000000000003')
      + (select count(*) from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000003')
      + (select count(*) from api.recordings where collar_id = '61000000-0000-4000-8000-000000000002')
      + (select count(*) from api.recording_summaries where recording_id = '63000000-0000-4000-8000-000000000002')
  ),
  0::bigint,
  'non-member sees no cross-dog Today row'
);
select is(
  (
    select count(*) from api.recordings
    where id = '63000000-0000-4000-8000-000000000002'
      and collar_id = '61000000-0000-4000-8000-000000000002'
  ),
  0::bigint,
  'forged collar and recording filters do not bypass RLS'
);

reset role;
set local role anon;
select throws_ok(
  $$ select count(*) from api.collars $$,
  '42501',
  null,
  'anonymous Today read fails before row filtering'
);

select * from finish();
rollback;
