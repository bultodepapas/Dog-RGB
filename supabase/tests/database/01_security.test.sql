begin;
create extension if not exists pgtap with schema extensions;

select plan(17);

select is(
  (select count(*) from private.extension_inventory where extension_name in ('pgcrypto', 'postgis')),
  2::bigint,
  'installed extension versions are recorded'
);
select ok((select relrowsecurity from pg_class where oid = 'api.dogs'::regclass), 'dogs has RLS enabled');
select ok((select relrowsecurity from pg_class where oid = 'api.telemetry_points'::regclass), 'telemetry has RLS enabled');
select ok((select relrowsecurity from pg_class where oid = 'api.config_resource_heads'::regclass), 'config heads have RLS enabled');

select ok(not has_table_privilege('anon', 'api.dogs', 'select'), 'anonymous role cannot select dogs');
select ok(not has_table_privilege('anon', 'api.telemetry_points', 'select'), 'anonymous role cannot read routes');
select ok(not has_table_privilege('authenticated', 'api.telemetry_points', 'insert'), 'browser users cannot insert telemetry');
select ok(not has_table_privilege('authenticated', 'api.config_resource_heads', 'update'), 'browser users cannot update desired heads directly');

select ok(
  not has_function_privilege('anon', 'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)', 'execute'),
  'anonymous role cannot execute device sync'
);
select ok(
  not has_function_privilege('authenticated', 'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)', 'execute'),
  'user role cannot execute device sync'
);
select ok(
  has_function_privilege('service_role', 'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)', 'execute'),
  'service role can execute the narrow device sync gateway RPC'
);
select ok(
  not has_function_privilege('service_role', 'api.device_sync_v1(uuid,bytea,uuid,bytea,jsonb)', 'execute'),
  'service role cannot bypass credential-state locking through the internal sync RPC'
);

insert into api.collars (id, device_public_id, dog_id, state)
values (
  '41000000-0000-4000-8000-000000000099',
  '51000000-0000-4000-8000-000000000099',
  '30000000-0000-4000-8000-000000000003',
  'active'
);
insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
)
values (
  '41000000-0000-4000-8000-000000000099', 1, 1,
  '2026-08-17 12:00:00+00', 47110000, -740721000,
  0, 8, 13, 'gnss_trusted', 3, 'security-fixture', 1
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is((select count(*) from api.dogs), 1::bigint, 'owner sees their dog');
select is((select count(*) from api.telemetry_points), 1::bigint, 'owner sees telemetry through the visible-collar policy');

select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is((select count(*) from api.dogs), 0::bigint, 'cross-user dog read is blocked');
select is((select count(*) from api.dog_memberships), 0::bigint, 'cross-user membership read is blocked');
select is((select count(*) from api.telemetry_points), 0::bigint, 'cross-user telemetry read remains blocked');

select * from finish();
rollback;
