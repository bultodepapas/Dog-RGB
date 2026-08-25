begin;
create extension if not exists pgtap with schema extensions;

select plan(37);

select ok(
  not has_table_privilege('anon', 'private.device_claims', 'select')
  and not has_table_privilege('authenticated', 'private.device_claims', 'select'),
  'browser roles cannot read private claim state'
);
select ok(
  not has_table_privilege('anon', 'private.device_credentials', 'select')
  and not has_table_privilege('authenticated', 'private.device_credentials', 'select'),
  'browser roles cannot read private credential state'
);
select ok(
  not has_function_privilege(
    'anon',
    'api.consume_device_claim_gateway_v1(bytea,bytea,bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  ),
  'anonymous callers cannot bypass the Edge pairing gateway'
);
select ok(
  not has_function_privilege(
    'authenticated',
    'api.consume_device_claim_gateway_v1(bytea,bytea,bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  ),
  'authenticated callers cannot bypass the Edge pairing gateway'
);
select hasnt_column(
  'private', 'device_claims', 'claim_code',
  'claim rows have no raw-code column'
);
select hasnt_column(
  'private', 'device_credentials', 'credential_secret',
  'credential rows have no raw-secret column'
);

create temporary table pair_fixture (
  device jsonb not null,
  capabilities jsonb not null
);
insert into pair_fixture values (
  jsonb_build_object(
    'device_id', '81000000-0000-4000-8000-000000000001',
    'hardware_revision', 'test-hw',
    'firmware_version', 'test-fw',
    'protocol_version', 1,
    'telemetry_schema', 3,
    'config_schema', 7,
    'capability_hash', 'axujEtmjJJG2KBuvFLVzLiEyoy-Nv4PBSrA2Y5EG1WI'
  ),
  '{}'::jsonb
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('81', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);

create temporary table pair_results (
  attempt integer primary key,
  response_json jsonb not null
);
insert into pair_results values (
  1,
  api.consume_device_claim_gateway_v1(
    decode(repeat('91', 32), 'hex'), decode(repeat('92', 32), 'hex'),
    decode(repeat('81', 32), 'hex'), '82000000-0000-4000-8000-000000000001',
    decode(repeat('83', 32), 'hex'), '81000000-0000-4000-8000-000000000001',
    '84000000-0000-4000-8000-000000000001', decode(repeat('85', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  )
);
insert into pair_results values (
  2,
  api.consume_device_claim_gateway_v1(
    decode(repeat('91', 32), 'hex'), decode(repeat('92', 32), 'hex'),
    decode(repeat('81', 32), 'hex'), '82000000-0000-4000-8000-000000000001',
    decode(repeat('83', 32), 'hex'), '81000000-0000-4000-8000-000000000001',
    '84000000-0000-4000-8000-000000000001', decode(repeat('85', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  )
);

select is(
  (select response_json ->> 'disposition' from pair_results where attempt = 1),
  'claimed',
  'the pair-only first use claims the collar'
);
select is(
  (select response_json from pair_results where attempt = 2),
  (select response_json from pair_results where attempt = 1),
  'an exact retry returns the persisted sanitized response'
);
select is(
  (select state from private.device_claims where code_digest = decode(repeat('81', 32), 'hex')),
  'consumed',
  'successful pairing consumes the one-time claim'
);
select is(
  (select attempt_count from private.device_claims where code_digest = decode(repeat('81', 32), 'hex')),
  1,
  'exact replay does not increment the successful attempt count'
);
select is(
  (select octet_length(request_sha256) from private.device_claims where code_digest = decode(repeat('81', 32), 'hex')),
  32,
  'the consumed claim stores one exact-request digest'
);
select ok(
  not (
    select response_json ?| array['claim_code', 'credential_id', 'credential_secret']
    from private.device_claims
    where code_digest = decode(repeat('81', 32), 'hex')
  )
  and not (
    select response_json::text like '%84000000-0000-4000-8000-000000000001%'
      or response_json::text like '%' || repeat('85', 32) || '%'
    from private.device_claims
    where code_digest = decode(repeat('81', 32), 'hex')
  ),
  'stored replay metadata contains no claim code or credential'
);
select is(
  (select response_json from private.device_claims where code_digest = decode(repeat('81', 32), 'hex')),
  (select response_json from pair_results where attempt = 1),
  'the replay response is exactly the committed response'
);
select is(
  (select count(*) from api.collars where device_public_id = '81000000-0000-4000-8000-000000000001'),
  1::bigint,
  'pairing creates exactly one collar'
);
select is(
  (select state from api.collars where device_public_id = '81000000-0000-4000-8000-000000000001'),
  'active',
  'the paired collar is active'
);
select is(
  (select count(*) from private.device_credentials where credential_id = '84000000-0000-4000-8000-000000000001'),
  1::bigint,
  'pairing creates exactly one private credential'
);
select is(
  (select octet_length(secret_digest) from private.device_credentials where credential_id = '84000000-0000-4000-8000-000000000001'),
  32,
  'the private credential stores only a 32-byte digest'
);
select is(
  (select state from private.device_credentials where credential_id = '84000000-0000-4000-8000-000000000001'),
  'active',
  'the paired credential is active'
);
select is(
  (
    select sum(row_count)::bigint from (
      select count(*) as row_count from private.sync_requests where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.recordings where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from private.telemetry_chunks where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.telemetry_points where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from private.telemetry_loss_markers where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from private.device_daily_summaries where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.daily_summaries where dog_id = (
        select (response_json ->> 'dog_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from private.dirty_summary_days where dog_id = (
        select (response_json ->> 'dog_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.config_revisions where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.config_resource_heads where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from api.config_reported where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
      union all select count(*) from private.config_hlc_state where collar_id = (
        select (response_json ->> 'collar_id')::uuid from pair_results where attempt = 1
      )
    ) as side_effects
  ),
  0::bigint,
  'pairing creates no synchronization, telemetry, recording, or configuration side effects'
);

select throws_ok(
  $$
    select api.consume_device_claim_gateway_v1(
      decode(repeat('91', 32), 'hex'), decode(repeat('92', 32), 'hex'),
      decode(repeat('81', 32), 'hex'), '82000000-0000-4000-8000-000000000001',
      decode(repeat('86', 32), 'hex'), '81000000-0000-4000-8000-000000000001',
      '84000000-0000-4000-8000-000000000001', decode(repeat('85', 32), 'hex'),
      (select device from pair_fixture), (select capabilities from pair_fixture)
    )
  $$,
  '23505',
  'request_id_conflict',
  'the same request identity with changed bytes is rejected'
);
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('93', 32), 'hex'), decode(repeat('94', 32), 'hex'),
    decode(repeat('81', 32), 'hex'), '82000000-0000-4000-8000-000000000002',
    decode(repeat('87', 32), 'hex'), '81000000-0000-4000-8000-000000000002',
    '84000000-0000-4000-8000-000000000002', decode(repeat('88', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  ) ->> '_problem',
  'claim_unavailable',
  'a consumed claim rejects a different request and device identity'
);
select is(
  (select count(*) from api.collars where dog_id = '30000000-0000-4000-8000-000000000003'),
  1::bigint,
  'consumed non-replay creates no second collar'
);
select is(
  (select count(*) from private.device_credentials),
  1::bigint,
  'consumed non-replay creates no second credential'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('a1', 32), 'hex'),
  statement_timestamp() - interval '1 second'
);
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a2', 32), 'hex'), decode(repeat('a3', 32), 'hex'),
    decode(repeat('a1', 32), 'hex'), 'a4000000-0000-4000-8000-000000000001',
    decode(repeat('a5', 32), 'hex'), 'a6000000-0000-4000-8000-000000000001',
    'a7000000-0000-4000-8000-000000000001', decode(repeat('a8', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  ) ->> '_problem',
  'claim_unavailable',
  'an expired claim returns the non-enumerating problem'
);
select is(
  (select state from private.device_claims where code_digest = decode(repeat('a1', 32), 'hex')),
  'expired',
  'an expired claim commits its terminal state'
);
select is(
  (select count(*) from api.collars where device_public_id = 'a6000000-0000-4000-8000-000000000001'),
  0::bigint,
  'an expired claim creates no collar'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at, attempt_count, max_attempts
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('b1', 32), 'hex'),
  statement_timestamp() + interval '10 minutes', 5, 5
);
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('b2', 32), 'hex'), decode(repeat('b3', 32), 'hex'),
    decode(repeat('b1', 32), 'hex'), 'b4000000-0000-4000-8000-000000000001',
    decode(repeat('b5', 32), 'hex'), 'b6000000-0000-4000-8000-000000000001',
    'b7000000-0000-4000-8000-000000000001', decode(repeat('b8', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  ) ->> '_problem',
  'claim_unavailable',
  'an exhausted claim returns the non-enumerating problem'
);
select is(
  (select state from private.device_claims where code_digest = decode(repeat('b1', 32), 'hex')),
  'issued',
  'an exhausted claim remains issued only until its short TTL expires'
);
select is(
  (select attempt_count from private.device_claims where code_digest = decode(repeat('b1', 32), 'hex')),
  5,
  'an exhausted claim never exceeds its attempt ceiling'
);
select is(
  (select count(*) from api.collars where device_public_id = 'b6000000-0000-4000-8000-000000000001'),
  0::bigint,
  'an exhausted claim creates no collar'
);
update private.device_claims
set state = 'cancelled'
where code_digest = decode(repeat('b1', 32), 'hex');

select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('c1', 32), 'hex'), decode(repeat('c2', 32), 'hex'),
    decode(repeat('c3', 32), 'hex'), 'c4000000-0000-4000-8000-000000000001',
    decode(repeat('c5', 32), 'hex'), 'c6000000-0000-4000-8000-000000000001',
    'c7000000-0000-4000-8000-000000000001', decode(repeat('c8', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  ) ->> '_problem',
  'claim_unavailable',
  'an unknown claim returns the same non-enumerating problem'
);

insert into private.claim_attempt_windows (
  key_kind, attempt_key, failure_count, blocked_until
) values
  ('source', decode(repeat('d1', 32), 'hex'), 5, statement_timestamp() + interval '15 minutes'),
  ('device', decode(repeat('d2', 32), 'hex'), 5, statement_timestamp() + interval '15 minutes');
create temporary table limited_result as
select api.consume_device_claim_gateway_v1(
  decode(repeat('d1', 32), 'hex'), decode(repeat('d2', 32), 'hex'),
  decode(repeat('d3', 32), 'hex'), 'd4000000-0000-4000-8000-000000000001',
  decode(repeat('d5', 32), 'hex'), 'd6000000-0000-4000-8000-000000000001',
  'd7000000-0000-4000-8000-000000000001', decode(repeat('d8', 32), 'hex'),
  (select device from pair_fixture), (select capabilities from pair_fixture)
) as response_json;
select is(
  (select response_json ->> '_problem' from limited_result),
  'rate_limited',
  'a blocked pairing attempt returns the stable rate-limit problem'
);
select ok(
  (select (response_json ->> 'retry_after_seconds')::integer between 899 and 900 from limited_result),
  'a blocked pairing attempt returns a bounded Retry-After value'
);
select is(
  (select count(*) from api.collars where device_public_id = 'd6000000-0000-4000-8000-000000000001'),
  0::bigint,
  'a rate-limited pairing attempt creates no collar'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('e1', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('e2', 32), 'hex'), decode(repeat('e3', 32), 'hex'),
    decode(repeat('e1', 32), 'hex'), 'e4000000-0000-4000-8000-000000000001',
    decode(repeat('e5', 32), 'hex'), '81000000-0000-4000-8000-000000000001',
    'e7000000-0000-4000-8000-000000000001', decode(repeat('e8', 32), 'hex'),
    (select device from pair_fixture), (select capabilities from pair_fixture)
  ) ->> '_problem',
  'device_identity_conflict',
  'an existing device identity returns the stable conflict problem'
);
select is(
  (select count(*) from private.device_credentials where credential_id = 'e7000000-0000-4000-8000-000000000001'),
  0::bigint,
  'an identity conflict leaves no partial credential'
);
select is(
  (select count(*) from api.collars where device_public_id = '81000000-0000-4000-8000-000000000001'),
  1::bigint,
  'an identity conflict leaves the original single collar'
);

select * from finish();
rollback;
