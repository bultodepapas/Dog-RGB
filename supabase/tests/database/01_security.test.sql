begin;
create extension if not exists pgtap with schema extensions;

select plan(15);

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

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is((select count(*) from api.dogs), 1::bigint, 'owner sees their dog');

select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is((select count(*) from api.dogs), 0::bigint, 'cross-user dog read is blocked');
select is((select count(*) from api.dog_memberships), 0::bigint, 'cross-user membership read is blocked');

select * from finish();
rollback;
