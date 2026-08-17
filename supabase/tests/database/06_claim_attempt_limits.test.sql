begin;
create extension if not exists pgtap with schema extensions;

select plan(23);

select ok(
  not has_table_privilege('service_role', 'private.claim_attempt_windows', 'select'),
  'service role cannot inspect pseudonymous claim-attempt buckets'
);
select ok(
  not has_function_privilege('service_role', 'private.claim_attempt_retry_after_v1(text,bytea)', 'execute'),
  'claim cooldown lookup is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.record_claim_failure_v1(text,bytea,integer,integer)', 'execute'),
  'claim failure recording is not an exposed RPC'
);
select is(
  (select claim_failures_per_key_15m from private.cloud_limits where singleton),
  5,
  'the default cooldown threshold is five failures per key'
);
select is(
  (select claim_cooldown_seconds from private.cloud_limits where singleton),
  900,
  'the default claim cooldown is fifteen minutes'
);

insert into private.claim_attempt_windows (
  key_kind, attempt_key, window_started_at, failure_count, updated_at
) values (
  'device', decode(repeat('09', 32), 'hex'),
  statement_timestamp() - interval '3 days', 1,
  statement_timestamp() - interval '3 days'
);
do $$
begin
  perform private.record_claim_failure_v1(
    'source', decode(repeat('08', 32), 'hex'), 5, 900
  );
end
$$;
select is(
  (select count(*) from private.claim_attempt_windows where attempt_key = decode(repeat('09', 32), 'hex')),
  0::bigint,
  'bounded opportunistic cleanup removes stale attempt buckets'
);
delete from private.claim_attempt_windows where attempt_key = decode(repeat('08', 32), 'hex');

create temporary table failed_claim_results (
  attempt integer primary key,
  response_json jsonb not null
);

insert into failed_claim_results (attempt, response_json)
select attempt, api.consume_device_claim_gateway_v1(
  decode(repeat('a1', 32), 'hex'),
  decode(repeat('b1', 32), 'hex'),
  decode(lpad(to_hex(attempt), 64, '0'), 'hex'),
  ('61000000-0000-4000-8000-' || lpad(attempt::text, 12, '0'))::uuid,
  decode(lpad(to_hex(attempt + 16), 64, '0'), 'hex'),
  '62000000-0000-4000-8000-000000000001',
  '63000000-0000-4000-8000-000000000001',
  decode(repeat('c1', 32), 'hex'),
  '{}'::jsonb,
  '{}'::jsonb
)
from generate_series(1, 5) as attempt;

select is(
  (select count(*) from failed_claim_results where attempt < 5 and response_json ->> '_problem' = 'claim_unavailable'),
  4::bigint,
  'the first four unavailable-code failures retain the non-enumerating response'
);
select is(
  (select response_json ->> '_problem' from failed_claim_results where attempt = 5),
  'rate_limited',
  'the fifth failure enters cooldown'
);
select ok(
  (select (response_json ->> 'retry_after_seconds')::integer between 899 and 900
   from failed_claim_results where attempt = 5),
  'the cooldown result carries a bounded dynamic Retry-After value'
);
select is(
  (select count(*) from private.claim_attempt_windows
   where failure_count = 5 and blocked_until > statement_timestamp()),
  2::bigint,
  'source and device buckets are both persisted and blocked'
);

select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a1', 32), 'hex'), decode(repeat('b2', 32), 'hex'),
    decode(repeat('d1', 32), 'hex'), '64000000-0000-4000-8000-000000000001',
    decode(repeat('d2', 32), 'hex'), '62000000-0000-4000-8000-000000000002',
    '63000000-0000-4000-8000-000000000002', decode(repeat('d3', 32), 'hex'),
    '{}'::jsonb, '{}'::jsonb
  ) ->> '_problem',
  'rate_limited',
  'a blocked source cannot evade cooldown by rotating device identity'
);
select is(
  (select count(*) from private.claim_attempt_windows where key_kind = 'device'),
  1::bigint,
  'a blocked source does not create attacker-controlled device buckets'
);
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a2', 32), 'hex'), decode(repeat('b1', 32), 'hex'),
    decode(repeat('d4', 32), 'hex'), '64000000-0000-4000-8000-000000000002',
    decode(repeat('d5', 32), 'hex'), '62000000-0000-4000-8000-000000000001',
    '63000000-0000-4000-8000-000000000003', decode(repeat('d6', 32), 'hex'),
    '{}'::jsonb, '{}'::jsonb
  ) ->> '_problem',
  'rate_limited',
  'a blocked device cannot evade cooldown by changing source'
);
select is(
  (select count(*) from private.claim_attempt_windows where key_kind = 'source'),
  1::bigint,
  'a blocked device does not create additional source buckets'
);

update private.cloud_limits set enabled = false where singleton;
select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a3', 32), 'hex'), decode(repeat('b3', 32), 'hex'),
    decode(repeat('d7', 32), 'hex'), '64000000-0000-4000-8000-000000000003',
    decode(repeat('d8', 32), 'hex'), '62000000-0000-4000-8000-000000000003',
    '63000000-0000-4000-8000-000000000004', decode(repeat('d9', 32), 'hex'),
    '{}'::jsonb, '{}'::jsonb
  ) ->> '_problem',
  'claim_unavailable',
  'the optional DIY switch disables claim cooldowns'
);
select is(
  (select count(*) from private.claim_attempt_windows),
  2::bigint,
  'disabled cooldowns create no attempt buckets'
);
update private.cloud_limits set enabled = true where singleton;

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('e1', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);

create temporary table successful_claim (response_json jsonb not null);
insert into successful_claim values (
  api.consume_device_claim_gateway_v1(
    decode(repeat('a4', 32), 'hex'), decode(repeat('b4', 32), 'hex'),
    decode(repeat('e1', 32), 'hex'), '65000000-0000-4000-8000-000000000001',
    decode(repeat('e2', 32), 'hex'), '66000000-0000-4000-8000-000000000001',
    '67000000-0000-4000-8000-000000000001', decode(repeat('e3', 32), 'hex'),
    jsonb_build_object(
      'device_id', '66000000-0000-4000-8000-000000000001',
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
  (select response_json ->> 'disposition' from successful_claim),
  'claimed',
  'a valid claim still succeeds through the accounting gateway'
);
select is(
  (select attempt_count from private.device_claims where code_digest = decode(repeat('e1', 32), 'hex')),
  1,
  'a successful first consume records one claim attempt'
);

insert into private.claim_attempt_windows (
  key_kind, attempt_key, failure_count, blocked_until
) values
  ('source', decode(repeat('a4', 32), 'hex'), 5, statement_timestamp() + interval '15 minutes'),
  ('device', decode(repeat('b4', 32), 'hex'), 5, statement_timestamp() + interval '15 minutes');

select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a4', 32), 'hex'), decode(repeat('b4', 32), 'hex'),
    decode(repeat('e1', 32), 'hex'), '65000000-0000-4000-8000-000000000001',
    decode(repeat('e2', 32), 'hex'), '66000000-0000-4000-8000-000000000001',
    '67000000-0000-4000-8000-000000000001', decode(repeat('e3', 32), 'hex'),
    '{}'::jsonb, '{}'::jsonb
  ),
  (select response_json from successful_claim),
  'an exact committed replay bypasses a later cooldown'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('f1', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);

select is(
  api.consume_device_claim_gateway_v1(
    decode(repeat('a5', 32), 'hex'), decode(repeat('b5', 32), 'hex'),
    decode(repeat('f1', 32), 'hex'), '68000000-0000-4000-8000-000000000001',
    decode(repeat('f2', 32), 'hex'), '66000000-0000-4000-8000-000000000001',
    '67000000-0000-4000-8000-000000000002', decode(repeat('f3', 32), 'hex'),
    '{}'::jsonb, '{}'::jsonb
  ) ->> '_problem',
  'device_identity_conflict',
  'an identity collision returns a committed internal problem marker'
);
select is(
  (select attempt_count from private.device_claims where code_digest = decode(repeat('f1', 32), 'hex')),
  1,
  'an identity collision persists its claim attempt count'
);
select is(
  (select count(*) from api.collars where device_public_id = '66000000-0000-4000-8000-000000000001'),
  1::bigint,
  'an identity collision does not create a second collar'
);
select is(
  (select count(*) from private.device_credentials where credential_id = '67000000-0000-4000-8000-000000000002'),
  0::bigint,
  'an identity collision leaves no partial credential'
);

select * from finish();
rollback;
