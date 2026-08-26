begin;
create extension if not exists pgtap with schema extensions;

select plan(38);

create function pg_temp.m112_sync_request(
  p_request_id uuid,
  p_capability_hash bytea,
  p_capabilities jsonb,
  p_diagnostics jsonb
)
returns jsonb
language sql
stable
as $$
  select jsonb_build_object(
    'protocol_version', 1,
    'request_id', p_request_id,
    'device', jsonb_build_object(
      'device_id', '8d000000-0000-4000-8000-000000000001',
      'boot_sequence', 1,
      'firmware_version', '2.0.0-cloud.1',
      'hardware_revision', 'xiao-s3-r1',
      'telemetry_schema', 3,
      'config_schema', 7,
      'capability_hash', private.base64url_encode(p_capability_hash)
    ),
    'clock', jsonb_build_object(
      'utc_ms', null,
      'quality', 'unknown',
      'uncertainty_ms', null
    ),
    'capabilities', p_capabilities,
    'diagnostics', p_diagnostics,
    'upload', jsonb_build_object(
      'chunks', '[]'::jsonb,
      'summaries', '[]'::jsonb,
      'loss_markers', '[]'::jsonb
    ),
    'configuration', jsonb_build_object(
      'mutations', '[]'::jsonb,
      'reported', '[]'::jsonb
    )
  )
$$;

select is(
  (
    select count(*)
    from information_schema.columns
    where table_schema = 'api'
      and table_name = 'collars'
      and column_name in (
        'diagnostics_observed_at', 'outbox_chunks', 'outbox_points',
        'outbox_used_bytes', 'outbox_capacity_bytes',
        'oldest_unacknowledged_at', 'dropped_points_total',
        'sync_error_present'
      )
  ),
  8::bigint,
  'the collar row has exactly the bounded latest-snapshot fields'
);
select hasnt_column(
  'api', 'collars', 'last_error_code',
  'the device machine error code is not persisted'
);
select ok(
  (select c.relrowsecurity
   from pg_catalog.pg_class c
   join pg_catalog.pg_namespace n on n.oid = c.relnamespace
   where n.nspname = 'api' and c.relname = 'collars'),
  'collar diagnostics remain behind the existing collar RLS boundary'
);
select ok(
  has_function_privilege(
    'service_role',
    'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)',
    'execute'
  ),
  'only the Edge service path can execute the sync gateway'
);
select ok(
  not has_function_privilege(
    'service_role',
    'api.device_sync_v1(uuid,bytea,uuid,bytea,jsonb)',
    'execute'
  ),
  'the Edge service path cannot bypass the gateway transaction'
);
select ok(
  has_function_privilege('authenticated', 'api.revoke_collar_v1(uuid)', 'execute'),
  'authenticated website sessions can reach the owner-authorized revoke RPC'
);
select ok(
  not has_function_privilege('anon', 'api.revoke_collar_v1(uuid)', 'execute')
  and not has_function_privilege('service_role', 'api.revoke_collar_v1(uuid)', 'execute'),
  'anonymous and service callers cannot use the website revoke RPC'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    '8a000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'm112-editor@example.test',
    extensions.crypt('local-m112-editor-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '8a000000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'm112-viewer@example.test',
    extensions.crypt('local-m112-viewer-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  )
on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values
  ('30000000-0000-4000-8000-000000000003', '8a000000-0000-4000-8000-000000000001', 'editor'),
  ('30000000-0000-4000-8000-000000000003', '8a000000-0000-4000-8000-000000000002', 'viewer')
on conflict (dog_id, user_id) do update set role = excluded.role;

insert into api.collars (
  id, device_public_id, dog_id, display_name, state,
  hardware_revision, firmware_version, telemetry_schema, config_schema,
  capability_manifest, capability_hash, linked_at
) values (
  '8c000000-0000-4000-8000-000000000001',
  '8d000000-0000-4000-8000-000000000001',
  '30000000-0000-4000-8000-000000000003',
  'M1.12 transaction fixture', 'active',
  'xiao-s3-r1', '2.0.0-cloud.0', 3, 7,
  '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7]}'::jsonb,
  decode(repeat('11', 32), 'hex'), statement_timestamp()
);
insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state
) values (
  '8e000000-0000-4000-8000-000000000001',
  '8c000000-0000-4000-8000-000000000001',
  decode(repeat('22', 32), 'hex'), 'active'
);

create temporary table m112_results (
  attempt integer primary key,
  response_json jsonb not null
);
insert into m112_results values (
  1,
  api.device_sync_gateway_v1(
    '8e000000-0000-4000-8000-000000000001',
    decode(repeat('22', 32), 'hex'),
    '8f000000-0000-4000-8000-000000000001',
    decode(repeat('31', 32), 'hex'),
    pg_temp.m112_sync_request(
      '8f000000-0000-4000-8000-000000000001',
      decode(repeat('11', 32), 'hex'),
      null,
      '{"outbox_chunks":2,"outbox_points":7,"outbox_used_bytes":224,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":1786641000000,"dropped_points_total":3,"last_error_code":"storage_pressure"}'::jsonb
    )
  )
);

select is(
  (select response_json ->> 'request_id' from m112_results where attempt = 1),
  '8f000000-0000-4000-8000-000000000001',
  'a valid sync commits through the composed gateway'
);
select is(
  (
    select row(
      protocol_version, outbox_chunks, outbox_points,
      outbox_used_bytes, outbox_capacity_bytes, dropped_points_total,
      sync_error_present
    )::text
    from api.collars where id = '8c000000-0000-4000-8000-000000000001'
  ),
  '(1,2,7,224,4096,3,t)',
  'root protocol and the safe diagnostic values persist together'
);
select is(
  (select diagnostics_observed_at from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  (select last_sync_at from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  'the snapshot is explicitly the pre-ACK report from the committed sync instant'
);
select is(
  (
    select extract(epoch from oldest_unacknowledged_at)::bigint
    from api.collars where id = '8c000000-0000-4000-8000-000000000001'
  ),
  1786641000::bigint,
  'the oldest pending observation is persisted without inventing recency'
);

create temporary table m112_changed_capability as
select '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7],"revision_marker":"m112"}'::jsonb as manifest;
insert into m112_results values (
  2,
  api.device_sync_gateway_v1(
    '8e000000-0000-4000-8000-000000000001',
    decode(repeat('22', 32), 'hex'),
    '8f000000-0000-4000-8000-000000000002',
    decode(repeat('32', 32), 'hex'),
    pg_temp.m112_sync_request(
      '8f000000-0000-4000-8000-000000000002',
      decode(repeat('33', 32), 'hex'),
      (select manifest from m112_changed_capability),
      '{"outbox_chunks":0,"outbox_points":0,"outbox_used_bytes":0,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
    )
  )
);

select is(
  (select capability_manifest from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  (select manifest from m112_changed_capability),
  'a complete changed capability manifest replaces accepted capability truth'
);
select is(
  (select capability_hash from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  decode(repeat('33', 32), 'hex'),
  'the accepted capability hash advances with its complete manifest'
);
select is(
  (
    select row(outbox_chunks, outbox_points, outbox_used_bytes, sync_error_present)::text
    from api.collars where id = '8c000000-0000-4000-8000-000000000001'
  ),
  '(0,0,0,f)',
  'an exact zero report is stored as an empty pre-ACK queue snapshot'
);

create temporary table m112_before_replay as
select diagnostics_observed_at, updated_at
from api.collars where id = '8c000000-0000-4000-8000-000000000001';
insert into m112_results values (
  3,
  api.device_sync_gateway_v1(
    '8e000000-0000-4000-8000-000000000001',
    decode(repeat('22', 32), 'hex'),
    '8f000000-0000-4000-8000-000000000002',
    decode(repeat('32', 32), 'hex'),
    pg_temp.m112_sync_request(
      '8f000000-0000-4000-8000-000000000002',
      decode(repeat('33', 32), 'hex'),
      (select manifest from m112_changed_capability),
      '{"outbox_chunks":0,"outbox_points":0,"outbox_used_bytes":0,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
    )
  )
);
select is(
  (select response_json from m112_results where attempt = 3),
  (select response_json from m112_results where attempt = 2),
  'an exact sync replay returns the committed response'
);
select is(
  (
    select row(diagnostics_observed_at, updated_at)::text
    from api.collars where id = '8c000000-0000-4000-8000-000000000001'
  ),
  (select row(diagnostics_observed_at, updated_at)::text from m112_before_replay),
  'an exact replay cannot advance diagnostic or collar timestamps'
);

select throws_ok(
  $$
    select api.device_sync_gateway_v1(
      '8e000000-0000-4000-8000-000000000001', decode(repeat('22', 32), 'hex'),
      '8f000000-0000-4000-8000-000000000003', decode(repeat('34', 32), 'hex'),
      pg_temp.m112_sync_request(
        '8f000000-0000-4000-8000-000000000003', decode(repeat('33', 32), 'hex'), null,
        '{"outbox_chunks":1,"outbox_points":1,"outbox_used_bytes":4097,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
      )
    )
  $$,
  '22023', 'invalid_diagnostics',
  'impossible diagnostic capacity fails before the inner transaction'
);
select is(
  (select count(*) from private.sync_requests where collar_id = '8c000000-0000-4000-8000-000000000001'),
  2::bigint,
  'invalid diagnostics leave no request receipt'
);
select throws_ok(
  $$
    select api.device_sync_gateway_v1(
      '8e000000-0000-4000-8000-000000000001', decode(repeat('22', 32), 'hex'),
      '8f000000-0000-4000-8000-000000000004', decode(repeat('35', 32), 'hex'),
      pg_temp.m112_sync_request(
        '8f000000-0000-4000-8000-000000000004', decode(repeat('44', 32), 'hex'), null,
        '{"outbox_chunks":0,"outbox_points":0,"outbox_used_bytes":0,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
      )
    )
  $$,
  '22023', 'capability_hash_mismatch',
  'an omitted manifest must reference the exact accepted capability hash'
);
select is(
  (select count(*) from private.sync_requests where collar_id = '8c000000-0000-4000-8000-000000000001'),
  2::bigint,
  'a capability mismatch leaves no request receipt'
);

select throws_ok(
  $$
    select api.device_sync_gateway_v1(
      '8e000000-0000-4000-8000-000000000001', decode(repeat('22', 32), 'hex'),
      '8f000000-0000-4000-8000-000000000005', decode(repeat('36', 32), 'hex'),
      jsonb_set(
        pg_temp.m112_sync_request(
          '8f000000-0000-4000-8000-000000000005', decode(repeat('55', 32), 'hex'),
          (select manifest || '{"revision_marker":"must-rollback"}'::jsonb from m112_changed_capability),
          '{"outbox_chunks":0,"outbox_points":0,"outbox_used_bytes":0,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
        ),
        '{device,device_id}', '"8d000000-0000-4000-8000-000000000099"'::jsonb
      )
    )
  $$,
  '28000', 'device_identity_mismatch',
  'an inner transaction failure rejects the whole composed sync'
);
select is(
  (select capability_hash from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  decode(repeat('33', 32), 'hex'),
  'an inner failure rolls back the provisional capability update'
);
select is(
  (select count(*) from private.sync_requests where collar_id = '8c000000-0000-4000-8000-000000000001'),
  2::bigint,
  'an inner failure leaves neither a processing nor committed receipt'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  (select count(*) from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  1::bigint,
  'the owner can read the active collar diagnostic snapshot through RLS'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '8a000000-0000-4000-8000-000000000001', true);
select is(
  (select count(*) from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  1::bigint,
  'an editor reads the same collar truth through RLS'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '8a000000-0000-4000-8000-000000000002', true);
select is(
  (select count(*) from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  1::bigint,
  'a viewer reads the same collar truth through RLS'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  (select count(*) from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  0::bigint,
  'a non-member cannot read the collar or its diagnostics through RLS'
);
reset role;

set local role authenticated;
select set_config('request.jwt.claim.sub', '8a000000-0000-4000-8000-000000000001', true);
select throws_ok(
  $$select api.revoke_collar_v1('8c000000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized',
  'an editor cannot revoke collar cloud authority'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '8a000000-0000-4000-8000-000000000002', true);
select throws_ok(
  $$select api.revoke_collar_v1('8c000000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized',
  'a viewer cannot revoke collar cloud authority'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select throws_ok(
  $$select api.revoke_collar_v1('8c000000-0000-4000-8000-000000000001')$$,
  '42501', 'not_authorized',
  'a non-member receives the same bounded revoke denial'
);
reset role;

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select ok(
  api.revoke_collar_v1('8c000000-0000-4000-8000-000000000001'),
  'the owner revokes the exact selected collar'
);
reset role;
select is(
  (select state from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  'revoked',
  'owner revocation persists the collar terminal state'
);
select is(
  (select state from private.device_credentials where collar_id = '8c000000-0000-4000-8000-000000000001'),
  'revoked',
  'owner revocation atomically revokes the device credential'
);
create temporary table m112_first_revoke as
select revoked_at from api.collars where id = '8c000000-0000-4000-8000-000000000001';
set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select ok(
  api.revoke_collar_v1('8c000000-0000-4000-8000-000000000001'),
  'an exact owner retry confirms the already-revoked target'
);
reset role;
select is(
  (select revoked_at from api.collars where id = '8c000000-0000-4000-8000-000000000001'),
  (select revoked_at from m112_first_revoke),
  'an exact revoke retry preserves the first terminal timestamp'
);
select throws_ok(
  $$
    select api.device_sync_gateway_v1(
      '8e000000-0000-4000-8000-000000000001', decode(repeat('22', 32), 'hex'),
      '8f000000-0000-4000-8000-000000000006', decode(repeat('37', 32), 'hex'),
      pg_temp.m112_sync_request(
        '8f000000-0000-4000-8000-000000000006', decode(repeat('33', 32), 'hex'), null,
        '{"outbox_chunks":0,"outbox_points":0,"outbox_used_bytes":0,"outbox_capacity_bytes":4096,"oldest_unacknowledged_utc_ms":null,"dropped_points_total":3,"last_error_code":null}'::jsonb
      )
    )
  $$,
  '42501', 'device_revoked',
  'the revoked credential cannot synchronize again'
);

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at
) values (
  '8c000000-0000-4000-8000-000000000002',
  '8d000000-0000-4000-8000-000000000002',
  '30000000-0000-4000-8000-000000000003',
  'M1.12 retired fixture', 'retired', statement_timestamp()
);
set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.revoke_collar_v1('8c000000-0000-4000-8000-000000000002'),
  false,
  'a retired collar cannot be rewritten as revoked'
);
reset role;

select ok(
  position(
    'from private.device_credentials' in lower(pg_get_functiondef(
      'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)'::regprocedure
    ))
  ) < position(
    'from api.collars' in lower(pg_get_functiondef(
      'api.device_sync_gateway_v1(uuid,bytea,uuid,bytea,jsonb)'::regprocedure
    ))
  )
  and position(
    'order by credential.credential_id' in lower(pg_get_functiondef(
      'api.revoke_collar_v1(uuid)'::regprocedure
    ))
  ) < position(
    'select c.state into v_state' in lower(pg_get_functiondef(
      'api.revoke_collar_v1(uuid)'::regprocedure
    ))
  ),
  'sync and revoke use the same credential-before-collar lock order'
);

select * from finish();
rollback;
