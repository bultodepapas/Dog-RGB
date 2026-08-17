begin;
create extension if not exists pgtap with schema extensions;

select plan(18);

select ok(
  not has_table_privilege('service_role', 'private.cloud_limits', 'select'),
  'service role cannot read or change the database-owned limit configuration'
);
select ok(
  not has_function_privilege('service_role', 'private.enforce_sync_rate_limit_v1()', 'execute'),
  'sync rate-limit trigger is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.enforce_telemetry_quota_v1()', 'execute'),
  'telemetry quota trigger is not an exposed RPC'
);
select ok(
  (select enabled from private.cloud_limits where singleton),
  'cloud limits are enabled by default'
);

select ok(
  api.issue_device_claim_v1(
    '30000000-0000-4000-8000-000000000003',
    '10000000-0000-4000-8000-000000000001',
    decode(repeat('a1', 32), 'hex'),
    statement_timestamp() + interval '10 minutes'
  ) is not null,
  'an authorized owner can issue a claim'
);
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('a2', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '23505',
  'active_claim_exists',
  'a dog cannot have two active claims'
);

update private.cloud_limits set claim_issues_per_user_hour = 2 where singleton;
update private.device_claims set state = 'cancelled' where state = 'issued';
select lives_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('a3', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  'a second claim inside the hourly budget succeeds'
);
update private.device_claims set state = 'cancelled' where state = 'issued';
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('a4', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  'P0001',
  'rate_limited_claim_issue',
  'claim issuance is capped per user and hour'
);
update private.cloud_limits set enabled = false where singleton;
select lives_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      '10000000-0000-4000-8000-000000000001',
      decode(repeat('a5', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  'the optional local/DIY switch disables claim limits'
);
update private.device_claims set state = 'cancelled' where state = 'issued';
update private.cloud_limits
set enabled = true, sync_burst_per_collar_minute = 2, sync_sustained_per_collar_hour = 100
where singleton;

insert into api.collars (id, device_public_id, dog_id, state)
values (
  '50000000-0000-4000-8000-000000000005',
  '51000000-0000-4000-8000-000000000005',
  '30000000-0000-4000-8000-000000000003',
  'active'
);

select lives_ok(
  $$
    insert into private.sync_requests (
      collar_id, request_id, request_sha256, protocol_version,
      status, committed_at, response_json
    ) values
      (
        '50000000-0000-4000-8000-000000000005',
        '52000000-0000-4000-8000-000000000001', decode(repeat('b1', 32), 'hex'),
        1, 'committed', statement_timestamp(), '{}'::jsonb
      ),
      (
        '50000000-0000-4000-8000-000000000005',
        '52000000-0000-4000-8000-000000000002', decode(repeat('b2', 32), 'hex'),
        1, 'committed', statement_timestamp(), '{}'::jsonb
      )
  $$,
  'requests inside the collar burst budget are accepted'
);
select throws_ok(
  $$
    insert into private.sync_requests (
      collar_id, request_id, request_sha256, protocol_version, status
    ) values (
      '50000000-0000-4000-8000-000000000005',
      '52000000-0000-4000-8000-000000000003', decode(repeat('b3', 32), 'hex'),
      1, 'processing'
    )
  $$,
  'P0001',
  'rate_limited_sync_burst',
  'a new sync above the collar burst budget is rejected transactionally'
);
select is(
  (select count(*) from private.sync_requests where collar_id = '50000000-0000-4000-8000-000000000005'),
  2::bigint,
  'a rejected sync does not leave a partial request receipt'
);
update private.cloud_limits
set sync_burst_per_collar_minute = 120, sync_sustained_per_collar_hour = 2
where singleton;
select throws_ok(
  $$
    insert into private.sync_requests (
      collar_id, request_id, request_sha256, protocol_version, status
    ) values (
      '50000000-0000-4000-8000-000000000005',
      '52000000-0000-4000-8000-000000000003', decode(repeat('b3', 32), 'hex'),
      1, 'processing'
    )
  $$,
  'P0001',
  'rate_limited_sync_sustained',
  'the sustained collar budget is enforced separately from the burst budget'
);

update private.cloud_limits set points_per_collar_utc_day = 384 where singleton;
select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    )
    select
      '50000000-0000-4000-8000-000000000005', 1, sequence,
      (sequence - 1) * 96, sequence * 96 - 1, 96,
      decode(lpad(to_hex(sequence), 64, '0'), 'hex'),
      ('53000000-0000-4000-8000-' || lpad(sequence::text, 12, '0'))::uuid
    from generate_series(1, 4) as sequence
  $$,
  'telemetry up to the UTC-day point budget is accepted'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '50000000-0000-4000-8000-000000000005', 1, 5, 384, 384, 1,
      decode(repeat('c5', 32), 'hex'), '53000000-0000-4000-8000-000000000005'
    )
  $$,
  'P0001',
  'quota_exceeded',
  'telemetry above the UTC-day point budget is rejected transactionally'
);
select is(
  (select coalesce(sum(point_count), 0) from private.telemetry_chunks where collar_id = '50000000-0000-4000-8000-000000000005'),
  384::bigint,
  'a rejected telemetry chunk does not consume storage'
);
update private.cloud_limits set enabled = false where singleton;
select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '50000000-0000-4000-8000-000000000005', 1, 5, 384, 384, 1,
      decode(repeat('c5', 32), 'hex'), '53000000-0000-4000-8000-000000000005'
    )
  $$,
  'the optional local/DIY switch disables telemetry quota enforcement'
);
select is(
  (select coalesce(sum(point_count), 0) from private.telemetry_chunks where collar_id = '50000000-0000-4000-8000-000000000005'),
  385::bigint,
  'the disabled-limit insert is persisted normally'
);

select * from finish();
rollback;
