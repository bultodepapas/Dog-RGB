begin;
create extension if not exists pgtap with schema extensions;
select plan(31);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  ('00000000-0000-0000-0000-000000000000', '6a560000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'm56a-owner@example.test', extensions.crypt('local-export-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', '6a560000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'm56a-editor@example.test', extensions.crypt('local-export-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', '6a560000-0000-4000-8000-000000000003',
    'authenticated', 'authenticated', 'm56a-viewer@example.test', extensions.crypt('local-export-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', '6a560000-0000-4000-8000-000000000004',
    'authenticated', 'authenticated', 'm56a-other-owner@example.test', extensions.crypt('local-export-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', '6a560000-0000-4000-8000-000000000005',
    'authenticated', 'authenticated', 'm56a-deleted-owner@example.test', extensions.crypt('local-export-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', '');

insert into api.profiles (user_id) values
  ('6a560000-0000-4000-8000-000000000001'), ('6a560000-0000-4000-8000-000000000002'),
  ('6a560000-0000-4000-8000-000000000003'), ('6a560000-0000-4000-8000-000000000004'),
  ('6a560000-0000-4000-8000-000000000005')
on conflict (user_id) do nothing;

insert into api.dogs (id, name, timezone, created_by) values
  ('6a561000-0000-4000-8000-000000000001', 'Mora export', 'America/Bogota', '6a560000-0000-4000-8000-000000000001'),
  ('6a561000-0000-4000-8000-000000000002', 'Other export', 'America/New_York', '6a560000-0000-4000-8000-000000000004');
insert into api.dog_memberships (dog_id, user_id, role) values
  ('6a561000-0000-4000-8000-000000000001', '6a560000-0000-4000-8000-000000000001', 'owner'),
  ('6a561000-0000-4000-8000-000000000001', '6a560000-0000-4000-8000-000000000002', 'editor'),
  ('6a561000-0000-4000-8000-000000000001', '6a560000-0000-4000-8000-000000000003', 'viewer'),
  ('6a561000-0000-4000-8000-000000000001', '6a560000-0000-4000-8000-000000000005', 'owner'),
  ('6a561000-0000-4000-8000-000000000002', '6a560000-0000-4000-8000-000000000004', 'owner');
update auth.users set deleted_at = statement_timestamp()
where id = '6a560000-0000-4000-8000-000000000005';

insert into api.collars (id, device_public_id, dog_id, display_name, state, hardware_revision,
  firmware_version, protocol_version, telemetry_schema, config_schema, linked_at)
values
  ('6a562000-0000-4000-8000-000000000001', '6a562100-0000-4000-8000-000000000001',
    '6a561000-0000-4000-8000-000000000001', 'Collar Mora', 'active', 'xiao-s3-r1',
    'm56a-fixture', 1, 3, 7, statement_timestamp()),
  ('6a562000-0000-4000-8000-000000000002', '6a562100-0000-4000-8000-000000000002',
    '6a561000-0000-4000-8000-000000000002', 'Other collar', 'active', 'xiao-s3-r1',
    'm56a-fixture', 1, 3, 7, statement_timestamp());

insert into private.device_credentials (credential_id, collar_id, secret_digest)
values ('6a563000-0000-4000-8000-000000000001', '6a562000-0000-4000-8000-000000000001',
  extensions.digest(convert_to('m56a-private-credential-secret', 'UTF8'), 'sha256'));

insert into api.recordings (id, collar_id, boot_sequence, started_at, ended_at,
  timezone_at_start, state, first_point_sequence, last_point_sequence, point_count,
  clock_quality, telemetry_schema, firmware_version)
values
  ('6a564000-0000-4000-8000-000000000001', '6a562000-0000-4000-8000-000000000001',
    7, '2026-08-25T10:00:00Z', '2026-08-25T10:00:10Z', 'America/Bogota', 'closed',
    1, 3, 3, 'gnss_trusted', 3, 'm56a-fixture'),
  ('6a564000-0000-4000-8000-000000000002', '6a562000-0000-4000-8000-000000000002',
    8, '2026-08-25T10:00:00Z', '2026-08-25T10:00:10Z', 'America/New_York', 'closed',
    null, null, 0, 'unknown', 3, 'm56a-fixture');

insert into api.telemetry_points (collar_id, boot_sequence, point_sequence, recorded_at,
  received_at, lat_e7, lon_e7, reported_speed_cmps, satellites, flags, time_quality,
  telemetry_schema, firmware_version, chunk_sequence)
values
  ('6a562000-0000-4000-8000-000000000001', 7, 1, '2026-08-25T10:00:00Z',
    '2026-08-25T10:00:01Z', 471100000, -740721000, 120, 9, 5, 'gnss_trusted', 3, 'm56a-fixture', 4),
  ('6a562000-0000-4000-8000-000000000001', 7, 2, null,
    '2026-08-25T10:00:05Z', null, null, null, null, 32, 'unknown', 3, 'm56a-fixture', 4),
  ('6a562000-0000-4000-8000-000000000001', 7, 3, '2026-08-25T10:00:10Z',
    '2026-08-25T10:00:11Z', 471100100, -740721100, 0, 8, 9, 'gnss_trusted', 3, 'm56a-fixture', 4);

insert into private.telemetry_loss_markers (id, collar_id, request_id, boot_sequence,
  first_missing_point_sequence, last_missing_point_sequence, dropped_points, reason,
  recorded_at, recorded_utc_ms)
values ('6a565000-0000-4000-8000-000000000001', '6a562000-0000-4000-8000-000000000001',
  '6a565100-0000-4000-8000-000000000001', 7, 4, 5, 2, 'buffer_overflow',
  '2026-08-25T10:00:12Z', null);

insert into api.config_revisions (id, collar_id, resource_key, mutation_id, resource_schema,
  origin, actor_user_id, actor_device_id, submitted_hlc_physical_ms, submitted_hlc_logical,
  submitted_actor_id, submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
  accepted_actor_id, ordering_mode, server_version, body, body_sha256, disposition)
values ('6a566000-0000-4000-8000-000000000001', '6a562000-0000-4000-8000-000000000001',
  'brightness', '6a566100-0000-4000-8000-000000000001', 7, 'web',
  '6a560000-0000-4000-8000-000000000001', '6a566200-0000-4000-8000-000000000001',
  1, 0, '6a566300-0000-4000-8000-000000000001', 'server_anchored',
  1, 0, '6a566400-0000-4000-8000-000000000001', 'authored', 1,
  '{"brightness":50}'::jsonb, extensions.digest(convert_to('{"brightness":50}', 'UTF8'), 'sha256'), 'winning');

select ok(has_function_privilege('authenticated', 'api.export_dog_data_v1(uuid,uuid)', 'execute'),
  'authenticated clients can call the owner-only export RPC');
select ok(not has_function_privilege('anon', 'api.export_dog_data_v1(uuid,uuid)', 'execute'),
  'anonymous clients cannot call the export RPC');

select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000001', true);
set local role authenticated;
create temporary table m56a_dog_export on commit drop as
  select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001') as body;
select is((select body ->> 'schema_version' from m56a_dog_export), '1',
  'dog export declares schema version 1');
select is((select body ->> 'complete' from m56a_dog_export), 'true',
  'bounded export is marked complete only after it is materialized');
select is((select body ->> 'export_type' from m56a_dog_export), 'dog_data',
  'default RPC mode returns the dog bundle');
select ok((select body ->> 'snapshot_at' is not null from m56a_dog_export),
  'export identifies its single-statement snapshot time');
select is((select body #>> '{dog,id}' from m56a_dog_export), '6a561000-0000-4000-8000-000000000001',
  'owner export contains the requested dog profile');
select is((select body ->> 'timezone' from m56a_dog_export), 'America/Bogota',
  'export keeps the dog timezone');
select is((select body #>> '{units,distance}' from m56a_dog_export), 'm',
  'export declares metric units');
select is((select jsonb_array_length(body -> 'collars') from m56a_dog_export), 1,
  'export includes the dog collar metadata');
select is((select jsonb_array_length(body -> 'recordings') from m56a_dog_export), 1,
  'export includes all dog recordings');
select is((select jsonb_array_length(body -> 'telemetry_points') from m56a_dog_export), 3,
  'export includes all retained points for the collar');
select is((select body #>> '{telemetry_points,0,point_sequence}' from m56a_dog_export), '1',
  'telemetry is ordered by boot and point sequence');
select is((select body #>> '{telemetry_points,2,latitude}' from m56a_dog_export), '47.11001',
  'export contains decimal coordinates as well as integer e7 values');
select is((select jsonb_array_length(body -> 'loss_markers') from m56a_dog_export), 1,
  'export preserves explicit telemetry loss ranges');
select is((select jsonb_array_length(body #> '{configuration,revisions}') from m56a_dog_export), 1,
  'export preserves the configuration revision body');
select ok(not ((select body #> '{configuration,revisions,0}' from m56a_dog_export) ?| array[
  'actor_user_id', 'actor_device_id', 'submitted_actor_id', 'accepted_actor_id'
]), 'export omits internal actor identifiers');
select ok(not ((select body::text from m56a_dog_export) ~* '(secret_digest|credential_secret|request_id|device_public_id|capability_hash)'),
  'export omits credentials, transport receipts, and device identifiers');

create temporary table m56a_recording_export on commit drop as
  select api.export_dog_data_v1(
    '6a561000-0000-4000-8000-000000000001', '6a564000-0000-4000-8000-000000000001'
  ) as body;
select is((select body ->> 'export_type' from m56a_recording_export), 'recording_geojson_source',
  'recording mode returns a GeoJSON source package');
select is((select jsonb_array_length(body -> 'recordings') from m56a_recording_export), 1,
  'recording mode includes exactly the selected recording');
select is((select jsonb_array_length(body -> 'telemetry_points') from m56a_recording_export), 3,
  'recording mode includes its full retained point set');
select is((select jsonb_array_length(body -> 'daily_summaries') from m56a_recording_export), 0,
  'recording mode omits unrelated dog-wide summaries');

select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000002', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized', 'editor cannot export dog data');
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000003', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized', 'viewer cannot export dog data');
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000004', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized', 'another dog owner cannot export this dog');
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000001', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001', '6a564000-0000-4000-8000-000000000002')$$,
  '42501', 'not_authorized', 'recording mode cannot cross dog ownership');
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000005', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '28000', 'authentication_required', 'soft-deleted Auth identity cannot export despite stale membership');
reset role;
update auth.users set deleted_at = null where id = '6a560000-0000-4000-8000-000000000005';

insert into api.recording_summaries (
  recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
  valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
  phase_durations, algorithm_version
) values (
  '6a564000-0000-4000-8000-000000000001', 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  jsonb_build_object('payload', repeat('x', 65537)), 1
);
set local role authenticated;
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000001', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '54000', 'export_limit_exceeded', 'oversize recording summary JSON aborts the dog export');
select throws_ok($$select api.export_dog_data_v1(
    '6a561000-0000-4000-8000-000000000001', '6a564000-0000-4000-8000-000000000001'
  )$$,
  '54000', 'export_limit_exceeded', 'oversize selected recording summary JSON aborts the recording export');
reset role;

insert into api.config_revisions (id, collar_id, resource_key, mutation_id, resource_schema,
  origin, submitted_hlc_physical_ms, submitted_hlc_logical, submitted_actor_id,
  submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
  accepted_actor_id, ordering_mode, server_version, body, body_sha256, disposition)
values ('6a566000-0000-4000-8000-000000000002', '6a562000-0000-4000-8000-000000000001',
  'brightness', '6a566100-0000-4000-8000-000000000002', 7, 'web',
  2, 0, '6a566300-0000-4000-8000-000000000002', 'server_anchored',
  2, 0, '6a566400-0000-4000-8000-000000000002', 'authored', 2,
  jsonb_build_object('payload', repeat('x', 65537)),
  extensions.digest(convert_to('bounded-fixture', 'UTF8'), 'sha256'), 'winning');
set local role authenticated;
select set_config('request.jwt.claim.sub', '6a560000-0000-4000-8000-000000000001', true);
select throws_ok($$select api.export_dog_data_v1('6a561000-0000-4000-8000-000000000001')$$,
  '54000', 'export_limit_exceeded', 'oversize configuration aborts the entire export without a partial document');
reset role;

select ok((select provolatile = 's'
  from pg_catalog.pg_proc where oid = 'api.export_dog_data_v1(uuid,uuid)'::regprocedure),
  'export RPC is STABLE so all source reads share its statement snapshot');
select * from finish();
rollback;
