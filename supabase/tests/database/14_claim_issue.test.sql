begin;
create extension if not exists pgtap with schema extensions;

select plan(25);

select hasnt_column(
  'private', 'device_claims', 'code',
  'claim rows have no plaintext code column'
);
select ok(
  not has_table_privilege('anon', 'private.device_claims', 'select'),
  'anonymous callers cannot read claim digests'
);
select ok(
  not has_table_privilege('authenticated', 'private.device_claims', 'select'),
  'authenticated callers cannot read claim digests'
);
select ok(
  has_function_privilege(
    'service_role',
    'api.issue_device_claim_v1(uuid,uuid,bytea,timestamptz,integer)',
    'execute'
  ),
  'only the trusted Edge service can execute claim issuance'
);
select ok(
  not has_function_privilege(
    'authenticated',
    'api.issue_device_claim_v1(uuid,uuid,bytea,timestamptz,integer)',
    'execute'
  ),
  'browser users cannot bypass the claim Edge Function'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    '14000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'claim-editor@example.test',
    extensions.crypt('local-claim-editor-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '14000000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'claim-viewer@example.test',
    extensions.crypt('local-claim-viewer-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  );

insert into api.dog_memberships (dog_id, user_id, role) values
  (
    '30000000-0000-4000-8000-000000000003',
    '14000000-0000-4000-8000-000000000001',
    'editor'
  ),
  (
    '30000000-0000-4000-8000-000000000003',
    '14000000-0000-4000-8000-000000000002',
    'viewer'
  );

select ok(
  api.issue_device_claim_v1(
    '30000000-0000-4000-8000-000000000003',
    '10000000-0000-4000-8000-000000000001',
    decode(repeat('d1', 32), 'hex'),
    statement_timestamp() + interval '15 minutes'
  ) is not null,
  'an owner can issue a claim at the exact TTL boundary'
);
select is(
  (
    select octet_length(code_digest)
    from private.device_claims
    where code_digest = decode(repeat('d1', 32), 'hex')
  ),
  32,
  'the database stores exactly one 32-byte digest'
);
select ok(
  (
    select response_json is null
      and consumed_by_device_id is null
      and consumed_at is null
      and max_attempts = 5
    from private.device_claims
    where code_digest = decode(repeat('d1', 32), 'hex')
  ),
  'issuance persists no pairing response, device, or plaintext recovery state'
);
select is(
  (
    select expires_at - created_at
    from private.device_claims
    where code_digest = decode(repeat('d1', 32), 'hex')
  ),
  interval '15 minutes',
  'the exact boundary claim expires 15 minutes after creation'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('d2', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '23505',
  'active_claim_exists',
  'a second active claim for the dog is rejected'
);

update private.device_claims set state = 'cancelled' where state = 'issued';
select lives_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '14000000-0000-4000-8000-000000000001',
      decode(repeat('d3', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  'an editor can issue a claim'
);
update private.device_claims set state = 'cancelled' where state = 'issued';

select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '14000000-0000-4000-8000-000000000002',
      decode(repeat('d4', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '42501',
  'not_authorized',
  'a viewer cannot issue a claim'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '20000000-0000-4000-8000-000000000002',
      decode(repeat('d5', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '42501',
  'not_authorized',
  'a non-member cannot issue a claim'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '34000000-0000-4000-8000-000000000004',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('d6', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '42501',
  'not_authorized',
  'a nonexistent dog is indistinguishable from other denied dogs'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('d7', 32), 'hex'),
      statement_timestamp() + interval '15 minutes 1 millisecond'
    )
  $$,
  '22023',
  'invalid_claim',
  'an expiry beyond 15 minutes is rejected'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('d8', 32), 'hex'),
      statement_timestamp()
    )
  $$,
  '22023',
  'invalid_claim',
  'a non-future expiry is rejected'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('d9', 32), 'hex'),
      statement_timestamp() + interval '10 minutes',
      6
    )
  $$,
  '22023',
  'invalid_claim',
  'more than five attempts is rejected'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('da', 31), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '22023',
  'invalid_claim',
  'a non-32-byte digest is rejected'
);

insert into private.device_claims (
  dog_id, requested_by, code_digest, expires_at
) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('db', 32), 'hex'),
  statement_timestamp() - interval '1 second'
);
select lives_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('dc', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  'an expired active row is replaced by a new claim'
);
select is(
  (
    select state
    from private.device_claims
    where code_digest = decode(repeat('db', 32), 'hex')
  ),
  'expired',
  'replacement records the previous issued row as expired'
);
select is(
  (
    select count(*)
    from private.device_claims
    where dog_id = '30000000-0000-4000-8000-000000000003'
      and state = 'issued'
  ),
  1::bigint,
  'exactly one claim remains active after replacement'
);
select is(
  (
    select requested_by
    from private.device_claims
    where code_digest = decode(repeat('dc', 32), 'hex')
  ),
  '10000000-0000-4000-8000-000000000001'::uuid,
  'the replacement records the authenticated requester'
);
select is(
  (select count(*) from api.collars),
  0::bigint,
  'claim issuance does not create a collar'
);
select is(
  (select count(*) from private.device_credentials),
  0::bigint,
  'claim issuance does not create a device credential'
);
select is(
  (
    select count(*)
    from private.device_claims
    where code_digest in (
      decode(repeat('d4', 32), 'hex'),
      decode(repeat('d5', 32), 'hex'),
      decode(repeat('d6', 32), 'hex'),
      decode(repeat('d7', 32), 'hex'),
      decode(repeat('d8', 32), 'hex'),
      decode(repeat('d9', 32), 'hex'),
      decode(repeat('da', 31), 'hex')
    )
  ),
  0::bigint,
  'denied and invalid issue attempts leave no partial claim row'
);

select * from finish();
rollback;
