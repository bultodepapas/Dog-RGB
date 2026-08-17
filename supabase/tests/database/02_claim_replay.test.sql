begin;
create extension if not exists pgtap with schema extensions;

select plan(10);

select ok(
  private.secure_digest_equal(decode(repeat('aa', 32), 'hex'), decode(repeat('aa', 32), 'hex')),
  'fixed-length digest comparison accepts equal values'
);
select ok(
  not private.secure_digest_equal(decode(repeat('aa', 32), 'hex'), decode(repeat('ab', 32), 'hex')),
  'fixed-length digest comparison rejects unequal values'
);
select ok(
  not has_function_privilege(
    'anon',
    'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  ),
  'anonymous role cannot consume a device claim'
);
select ok(
  not has_function_privilege(
    'service_role',
    'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  ),
  'service role cannot bypass claim-attempt accounting through the internal RPC'
);
select ok(
  has_function_privilege(
    'service_role',
    'api.consume_device_claim_gateway_v1(bytea,bytea,bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  ),
  'service role can execute the narrow claim gateway RPC'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('11', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);

create temporary table claim_results (attempt integer primary key, response_json jsonb);

insert into claim_results values (
  1,
  api.consume_device_claim_v1(
    decode(repeat('11', 32), 'hex'),
    'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2',
    decode(repeat('22', 32), 'hex'),
    '11111111-1111-4111-8111-111111111111',
    '44444444-4444-4444-8444-444444444444',
    decode(repeat('33', 32), 'hex'),
    jsonb_build_object(
      'device_id', '11111111-1111-4111-8111-111111111111',
      'hardware_revision', 'test-hw',
      'firmware_version', 'test-fw',
      'protocol_version', 1,
      'telemetry_schema', 3,
      'config_schema', 7,
      'capability_hash', 'axujEtmjJJG2KBuvFLVzLiEyoy-Nv4PBSrA2Y5EG1WI'
    ),
    '{}'::jsonb
  )
);

insert into claim_results values (
  2,
  api.consume_device_claim_v1(
    decode(repeat('11', 32), 'hex'),
    'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2',
    decode(repeat('22', 32), 'hex'),
    '11111111-1111-4111-8111-111111111111',
    '44444444-4444-4444-8444-444444444444',
    decode(repeat('33', 32), 'hex'),
    jsonb_build_object(
      'device_id', '11111111-1111-4111-8111-111111111111',
      'hardware_revision', 'test-hw',
      'firmware_version', 'test-fw',
      'protocol_version', 1,
      'telemetry_schema', 3,
      'config_schema', 7,
      'capability_hash', 'axujEtmjJJG2KBuvFLVzLiEyoy-Nv4PBSrA2Y5EG1WI'
    ),
    '{}'::jsonb
  )
);

select is(
  (select response_json from claim_results where attempt = 2),
  (select response_json from claim_results where attempt = 1),
  'an exact claim replay returns the persisted logical response'
);
select is(
  (select count(*) from api.collars where device_public_id = '11111111-1111-4111-8111-111111111111'),
  1::bigint,
  'claim replay creates one collar'
);
select is(
  (select count(*) from private.device_credentials where credential_id = '44444444-4444-4444-8444-444444444444'),
  1::bigint,
  'claim replay creates one credential'
);

select throws_ok(
  $$
    select api.consume_device_claim_v1(
      decode(repeat('11', 32), 'hex'),
      'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2',
      decode(repeat('44', 32), 'hex'),
      '11111111-1111-4111-8111-111111111111',
      '44444444-4444-4444-8444-444444444444',
      decode(repeat('33', 32), 'hex'),
      '{}'::jsonb,
      '{}'::jsonb
    )
  $$,
  '23505',
  'request_id_conflict',
  'same claim request ID with different bytes is rejected'
);

select throws_ok(
  $$
    select api.consume_device_claim_v1(
      decode(repeat('11', 32), 'hex'),
      'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb2',
      decode(repeat('55', 32), 'hex'),
      '11111111-1111-4111-8111-111111111111',
      '44444444-4444-4444-8444-444444444444',
      decode(repeat('33', 32), 'hex'),
      '{}'::jsonb,
      '{}'::jsonb
    )
  $$,
  '28000',
  'claim_not_available',
  'a consumed claim cannot authorize a different request'
);

select * from finish();
rollback;
