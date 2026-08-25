\set ON_ERROR_STOP on

-- Disposable local-only fixture for the M1.10 raw REST and browser checks.
-- Restore the seeded database with `supabase db reset` after use.
insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values (
  '00000000-0000-0000-0000-000000000000',
  '84000000-0000-4000-8000-000000000001',
  'authenticated', 'authenticated', 'recording-outsider@example.test',
  extensions.crypt('local-recording-outsider-password', extensions.gen_salt('bf')),
  statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
  statement_timestamp(), statement_timestamp(), '', '', '', ''
) on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values (
  '30000000-0000-4000-8000-000000000003',
  '20000000-0000-4000-8000-000000000002',
  'viewer'
) on conflict (dog_id, user_id) do update set role = excluded.role;

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at
) values (
  '84000000-0000-4000-8000-000000000002',
  '84000000-0000-4000-8000-000000000003',
  '30000000-0000-4000-8000-000000000003',
  'Collar M1.10 local',
  'active',
  '2026-08-25 09:00:00+00'
) on conflict (id) do nothing;

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, ended_at, timezone_at_start,
  state, first_point_sequence, last_point_sequence, point_count,
  clock_quality, telemetry_schema, firmware_version
) values (
  '84000000-0000-4000-8000-000000000004',
  '84000000-0000-4000-8000-000000000002',
  10,
  '2026-08-25 10:00:00+00',
  '2026-08-25 10:20:00+00',
  'America/Bogota',
  'closed',
  1,
  106,
  105,
  'gnss_trusted',
  3,
  'm1.10-local-fixture'
) on conflict (id) do nothing;

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality,
  telemetry_schema, firmware_version, chunk_sequence
)
select
  '84000000-0000-4000-8000-000000000002'::uuid,
  10,
  sequence,
  case when sequence = 11 then null else
    '2026-08-25 10:00:00+00'::timestamptz
      + (sequence * interval '5 seconds')
      + (case when sequence >= 50 then interval '70 seconds' else interval '0 seconds' end)
  end,
  case when sequence in (20, 30) then null else 47110000 + sequence end,
  case when sequence in (20, 30) then null else -740721000 + sequence end,
  case when sequence in (20, 30) then null else 123 end,
  9,
  case
    when sequence = 11 then 3
    when sequence = 20 then 36
    when sequence = 30 then 4
    else 7
  end,
  case when sequence = 11 then 'unknown' else 'gnss_trusted' end,
  3,
  'm1.10-local-fixture',
  ((sequence - 1) / 96) + 1
from generate_series(1, 106) as fixture(sequence)
where sequence <> 40
on conflict (collar_id, boot_sequence, point_sequence) do nothing;

select
  (select count(*) from api.recordings where id = '84000000-0000-4000-8000-000000000004') as recordings,
  (select count(*) from api.telemetry_points where collar_id = '84000000-0000-4000-8000-000000000002' and boot_sequence = 10) as points;
