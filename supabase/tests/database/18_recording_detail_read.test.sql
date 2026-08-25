begin;
create extension if not exists pgtap with schema extensions;

select plan(25);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname = 'api'
      and c.relname in ('collars', 'recordings', 'telemetry_points')
      and c.relrowsecurity
  ),
  3::bigint,
  'all recording-detail product tables have RLS enabled'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings', 'api.telemetry_points']) as table_name
    where has_table_privilege('authenticated', table_name, 'select')
  ),
  3::bigint,
  'authenticated sessions can select all recording-detail tables'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings', 'api.telemetry_points']) as table_name
    where has_table_privilege('anon', table_name, 'select')
  ),
  0::bigint,
  'anonymous sessions have no recording-detail table read grant'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings', 'api.telemetry_points']) as table_name
    where has_table_privilege(
      'authenticated', table_name,
      'insert,update,delete,truncate,references,trigger'
    )
  ),
  0::bigint,
  'recording detail adds no authenticated table-wide write grant'
);
select is(
  pg_get_indexdef('api.telemetry_points_pkey'::regclass),
  'CREATE UNIQUE INDEX telemetry_points_pkey ON api.telemetry_points USING btree (collar_id, boot_sequence, point_sequence)',
  'point pagination uses the frozen three-column primary key'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    '10000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'recording-owner@example.test',
    extensions.crypt('local-recording-owner-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '20000000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'recording-viewer@example.test',
    extensions.crypt('local-recording-viewer-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '18000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'recording-editor@example.test',
    extensions.crypt('local-recording-editor-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  )
on conflict (id) do nothing;

insert into api.dogs (id, name, timezone, created_by) values (
  '30000000-0000-4000-8000-000000000003',
  'Recording detail fixture',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
) on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values
  ('30000000-0000-4000-8000-000000000003', '10000000-0000-4000-8000-000000000001', 'owner'),
  ('30000000-0000-4000-8000-000000000003', '18000000-0000-4000-8000-000000000001', 'editor'),
  ('30000000-0000-4000-8000-000000000003', '20000000-0000-4000-8000-000000000002', 'viewer')
on conflict (dog_id, user_id) do update set role = excluded.role;

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at
) values
  ('81000000-0000-4000-8000-000000000001', '82000000-0000-4000-8000-000000000001', '30000000-0000-4000-8000-000000000003', 'Detalle activo', 'active', '2026-08-20 12:00:00+00'),
  ('81000000-0000-4000-8000-000000000002', '82000000-0000-4000-8000-000000000002', '30000000-0000-4000-8000-000000000003', 'Detalle pendiente', 'pending', '2026-08-20 12:00:00+00'),
  ('81000000-0000-4000-8000-000000000003', '82000000-0000-4000-8000-000000000003', '30000000-0000-4000-8000-000000000003', 'Detalle retirado', 'retired', '2026-08-20 12:00:00+00'),
  ('81000000-0000-4000-8000-000000000004', '82000000-0000-4000-8000-000000000004', '30000000-0000-4000-8000-000000000003', null, 'revoked', '2026-08-20 12:00:00+00');

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, ended_at, timezone_at_start,
  state, first_point_sequence, last_point_sequence, point_count,
  clock_quality, telemetry_schema, firmware_version
) values
  ('83000000-0000-4000-8000-000000000001', '81000000-0000-4000-8000-000000000001', 1, '2026-08-25 10:00:00+00', '2026-08-25 10:20:00+00', 'America/Bogota', 'closed', 1, 205, 205, 'gnss_trusted', 3, 'detail-fixture'),
  ('83000000-0000-4000-8000-000000000002', '81000000-0000-4000-8000-000000000002', 2, null, null, 'America/Bogota', 'open', 1, 1, 1, 'unknown', 3, 'detail-fixture'),
  ('83000000-0000-4000-8000-000000000003', '81000000-0000-4000-8000-000000000003', 3, null, null, 'America/Bogota', 'legacy', 1, 1, 1, 'legacy_minute', 3, 'detail-fixture'),
  ('83000000-0000-4000-8000-000000000004', '81000000-0000-4000-8000-000000000004', 4, null, null, 'America/Bogota', 'incomplete', 1, 1, 1, 'unknown', 3, 'detail-fixture'),
  ('83000000-0000-4000-8000-000000000005', '81000000-0000-4000-8000-000000000004', 5, null, null, 'America/Bogota', 'incomplete', null, null, 9, 'unknown', 3, 'retained-fixture');

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality,
  telemetry_schema, firmware_version, chunk_sequence
)
select
  '81000000-0000-4000-8000-000000000001'::uuid,
  1,
  sequence,
  '2026-08-25 10:00:00+00'::timestamptz + (sequence * interval '5 seconds'),
  47110000 + sequence,
  -740721000 + sequence,
  123,
  9,
  7,
  'gnss_trusted',
  3,
  'detail-fixture',
  ((sequence - 1) / 96) + 1
from generate_series(1, 205) as fixture(sequence);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  (
    select count(distinct c.state)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  4::bigint,
  'owner recording detail includes active, pending, retired, and revoked collars'
);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  5::bigint,
  'owner sees the exact recording fixture including retained null bounds'
);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where r.id = '83000000-0000-4000-8000-000000000001'
      and c.dog_id = '30000000-0000-4000-8000-000000000003'
      and r.collar_id = '81000000-0000-4000-8000-000000000001'
      and r.boot_sequence = 1
  ),
  1::bigint,
  'owner exact recording/collar/dog identity resolves once'
);
select is(
  (
    select count(*) from (
      select point_sequence
      from api.telemetry_points
      where collar_id = '81000000-0000-4000-8000-000000000001'
        and boot_sequence = 1
        and point_sequence >= 1 and point_sequence <= 205
      order by point_sequence asc
      limit 101
    ) page
  ),
  101::bigint,
  'first point page is bounded to 100 rows plus one lookahead'
);
select is(
  (
    select min(point_sequence)::text || ':' || max(point_sequence)::text from (
      select point_sequence
      from api.telemetry_points
      where collar_id = '81000000-0000-4000-8000-000000000001'
        and boot_sequence = 1
        and point_sequence >= 1 and point_sequence <= 205
      order by point_sequence asc
      limit 101
    ) page
  ),
  '1:101',
  'first point page has exact stable ascending boundaries'
);
select is(
  (
    select count(*) from (
      select point_sequence
      from api.telemetry_points
      where collar_id = '81000000-0000-4000-8000-000000000001'
        and boot_sequence = 1
        and point_sequence >= 1 and point_sequence <= 205
        and point_sequence > 100
      order by point_sequence asc
      limit 101
    ) page
  ),
  101::bigint,
  'deep point page is bounded to 100 rows plus one lookahead'
);
select is(
  (
    select min(point_sequence)::text || ':' || max(point_sequence)::text from (
      select point_sequence
      from api.telemetry_points
      where collar_id = '81000000-0000-4000-8000-000000000001'
        and boot_sequence = 1
        and point_sequence >= 1 and point_sequence <= 205
        and point_sequence > 100
      order by point_sequence asc
      limit 101
    ) page
  ),
  '101:201',
  'deep point page has exact stable ascending boundaries'
);
select is(
  (
    select count(*)
    from api.telemetry_points
    where collar_id = '81000000-0000-4000-8000-000000000001'
      and boot_sequence = 1 and point_sequence > 200 and point_sequence <= 205
  ),
  5::bigint,
  'tail page exposes only remaining bounded points'
);
select is(
  (select count(*) from api.telemetry_points where collar_id = '81000000-0000-4000-8000-000000000001' and boot_sequence = 99),
  0::bigint,
  'a forged boot sequence returns no point'
);
select is(
  (select count(*) from api.telemetry_points where collar_id = '81000000-0000-4000-8000-000000000002' and boot_sequence = 1),
  0::bigint,
  'a forged collar and boot pairing returns no point'
);

select set_config('request.jwt.claim.sub', '18000000-0000-4000-8000-000000000001', true);
select is((select count(*) from api.recordings where id::text like '83000000-0000-4000-8000-%'), 5::bigint, 'editor has the same read-only recording visibility');
select is((select count(*) from api.telemetry_points where collar_id = '81000000-0000-4000-8000-000000000001'), 205::bigint, 'editor has the same read-only point visibility');

select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is((select count(*) from api.recordings where id::text like '83000000-0000-4000-8000-%'), 5::bigint, 'viewer has the same read-only recording visibility');
select is((select count(*) from api.telemetry_points where collar_id = '81000000-0000-4000-8000-000000000001'), 205::bigint, 'viewer has the same read-only point visibility');

select set_config('request.jwt.claim.sub', '90000000-0000-4000-8000-000000000009', true);
select is((select count(*) from api.recordings where id::text like '83000000-0000-4000-8000-%'), 0::bigint, 'non-member sees no recording detail');
select is((select count(*) from api.telemetry_points where collar_id = '81000000-0000-4000-8000-000000000001'), 0::bigint, 'non-member sees no recording points');
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    join api.telemetry_points p on p.collar_id = r.collar_id and p.boot_sequence = r.boot_sequence
    where r.id = '83000000-0000-4000-8000-000000000001'
      and c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  0::bigint,
  'forged dog, recording, collar, and boot filters cannot bypass RLS'
);

reset role;
set local role anon;
select throws_ok($$ select count(*) from api.collars $$, '42501', null, 'anonymous collar read fails before row filtering');
select throws_ok($$ select count(*) from api.recordings $$, '42501', null, 'anonymous recording read fails before row filtering');
select throws_ok($$ select count(*) from api.telemetry_points $$, '42501', null, 'anonymous point read fails before row filtering');

select * from finish();
rollback;
