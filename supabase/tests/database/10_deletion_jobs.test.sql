begin;
create extension if not exists pgtap with schema extensions;

select plan(34);

select is(
  (
    select count(*)
    from pg_class relation
    where relation.oid in (
      'private.deletion_tombstones'::regclass,
      'private.deletion_jobs'::regclass,
      'private.deletion_receipts'::regclass
    )
      and relation.relrowsecurity
  ),
  3::bigint,
  'all deletion workflow tables have defense-in-depth RLS'
);

select ok(
  not has_table_privilege('authenticated', 'private.deletion_jobs', 'select')
  and not has_table_privilege('service_role', 'private.deletion_jobs', 'select'),
  'browser and service roles cannot inspect deletion rows directly'
);
select ok(
  has_function_privilege(
    'authenticated', 'api.request_dog_deletion_v1(uuid,uuid,text)', 'execute'
  ) and has_function_privilege(
    'authenticated', 'api.get_deletion_job_v1(uuid)', 'execute'
  ),
  'authenticated users receive only the narrow request and status RPCs'
);
select ok(
  has_function_privilege(
    'service_role', 'private.process_dog_deletion_batch_v1(integer)', 'execute'
  ) and not has_function_privilege(
    'authenticated', 'private.process_dog_deletion_batch_v1(integer)', 'execute'
  ),
  'only service_role can execute the bounded deletion worker'
);
select is(
  (
    select count(*)
    from pg_policies
    where schemaname = 'api'
      and tablename = 'dogs'
      and policyname = 'dogs_delete_owner'
  ),
  0::bigint,
  'the direct dog DELETE policy cannot bypass jobs and tombstones'
);

insert into api.dogs (id, name, timezone, created_by)
values (
  '30000000-0000-4000-8000-000000000004',
  'Survivor',
  'America/Bogota',
  '20000000-0000-4000-8000-000000000002'
);
insert into api.dog_memberships (dog_id, user_id, role)
values
  (
    '30000000-0000-4000-8000-000000000003',
    '20000000-0000-4000-8000-000000000002',
    'editor'
  ),
  (
    '30000000-0000-4000-8000-000000000004',
    '20000000-0000-4000-8000-000000000002',
    'owner'
  );

insert into api.collars (id, device_public_id, dog_id, state)
values
  (
    'd4000000-0000-4000-8000-000000000001',
    'd5000000-0000-4000-8000-000000000001',
    '30000000-0000-4000-8000-000000000003',
    'active'
  ),
  (
    'd4000000-0000-4000-8000-000000000002',
    'd5000000-0000-4000-8000-000000000002',
    '30000000-0000-4000-8000-000000000004',
    'active'
  );

insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state
)
values
  (
    'd6000000-0000-4000-8000-000000000001',
    'd4000000-0000-4000-8000-000000000001',
    decode(repeat('d1', 32), 'hex'),
    'active'
  ),
  (
    'd6000000-0000-4000-8000-000000000002',
    'd4000000-0000-4000-8000-000000000002',
    decode(repeat('d2', 32), 'hex'),
    'active'
  );

insert into private.device_claims (
  id, dog_id, requested_by, code_digest, expires_at
)
values (
  'd7000000-0000-4000-8000-000000000001',
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('d3', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
)
select
  'd4000000-0000-4000-8000-000000000001',
  1,
  point_sequence,
  '2026-08-17 12:00:00+00'::timestamptz + point_sequence * interval '1 second',
  47110000 + point_sequence::integer,
  -740721000 + point_sequence::integer,
  100,
  8,
  13,
  'gnss_trusted',
  3,
  'deletion-job-fixture',
  1
from generate_series(1, 7) as generated(point_sequence);

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
)
select
  'd4000000-0000-4000-8000-000000000002',
  1,
  point_sequence,
  '2026-08-17 13:00:00+00'::timestamptz + point_sequence * interval '1 second',
  47120000 + point_sequence::integer,
  -740722000 + point_sequence::integer,
  100,
  8,
  13,
  'gnss_trusted',
  3,
  'deletion-job-survivor',
  1
from generate_series(1, 2) as generated(point_sequence);

create temporary table deletion_job_capture (result jsonb not null) on commit drop;
grant select, insert, delete on deletion_job_capture to authenticated;

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true
);
select throws_ok(
  $$
    select api.request_dog_deletion_v1(
      '30000000-0000-4000-8000-000000000003',
      'da000000-0000-4000-8000-000000000001',
      'dog-delete-v1'
    )
  $$,
  '42501',
  'not_authorized',
  'an editor cannot request dog deletion'
);

select set_config(
  'request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true
);
select lives_ok(
  $$
    select api.request_dog_deletion_v1(
      '30000000-0000-4000-8000-000000000003',
      'da000000-0000-4000-8000-000000000002',
      'dog-delete-v1'
    )
  $$,
  'the owner can create a deletion tombstone and job atomically'
);
insert into deletion_job_capture (result)
select api.request_dog_deletion_v1(
  '30000000-0000-4000-8000-000000000003',
  'da000000-0000-4000-8000-000000000002',
  'dog-delete-v1'
);
reset role;

select is(
  (select count(*) from private.deletion_tombstones),
  1::bigint,
  'an exact request replay creates no second tombstone'
);
select is(
  (select count(*) from private.deletion_jobs),
  1::bigint,
  'an exact request replay creates no second job'
);
select is(
  (
    select initial_counts ->> 'telemetry_points'
    from private.deletion_jobs
  ),
  '7',
  'the job snapshots the exact initial telemetry count before purge'
);
select is(
  (
    select initial_counts ->> 'dog_memberships'
    from private.deletion_jobs
  ),
  '2',
  'the coordinate-free inventory includes all memberships before access closes'
);
select ok(
  (select deleted_at is not null from api.dogs where id = '30000000-0000-4000-8000-000000000003')
  and not exists (
    select 1 from api.dog_memberships
    where dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  'request commit marks the dog deleting and removes every active membership'
);
select ok(
  (select state = 'revoked' from api.collars where id = 'd4000000-0000-4000-8000-000000000001')
  and (select state = 'revoked' from private.device_credentials where credential_id = 'd6000000-0000-4000-8000-000000000001'),
  'request commit revokes collar ingress before background purge'
);

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true
);
select is((select count(*) from api.dogs), 0::bigint, 'the requester cannot read a deleting dog');
select is((select count(*) from api.telemetry_points), 0::bigint, 'the requester cannot read its deleting route');
select is(
  api.get_deletion_job_v1((select (result ->> 'job_id')::uuid from deletion_job_capture)) ->> 'status',
  'pending',
  'the requester can read only its coordinate-free job status'
);
select throws_ok(
  $$
    select api.request_dog_deletion_v1(
      '30000000-0000-4000-8000-000000000099',
      'da000000-0000-4000-8000-000000000002',
      'dog-delete-v1'
    )
  $$,
  '23505',
  'request_id_reused',
  'the request ID cannot be reused for another scope'
);

select set_config(
  'request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true
);
select throws_ok(
  format(
    'select api.get_deletion_job_v1(%L::uuid)',
    (select result ->> 'job_id' from deletion_job_capture)
  ),
  '42501',
  'not_authorized',
  'another user cannot inspect the deletion job'
);
reset role;

set local role service_role;
select lives_ok(
  $$ select private.process_dog_deletion_batch_v1(3) $$,
  'the first service batch succeeds'
);
reset role;
select ok(
  (select count(*) from api.telemetry_points where collar_id = 'd4000000-0000-4000-8000-000000000001') = 4
  and (select telemetry_points_deleted = 3 and attempt_count = 1 and status = 'pending' from private.deletion_jobs),
  'the first transaction deletes exactly three points and records progress'
);

set local role service_role;
select lives_ok(
  $$ select private.process_dog_deletion_batch_v1(3) $$,
  'the second service batch succeeds'
);
reset role;
select ok(
  (select count(*) from api.telemetry_points where collar_id = 'd4000000-0000-4000-8000-000000000001') = 1
  and (select telemetry_points_deleted = 6 and attempt_count = 2 and status = 'pending' from private.deletion_jobs),
  'the second transaction advances without holding one monolithic delete'
);

update private.deletion_jobs
set initial_counts = jsonb_set(initial_counts, '{telemetry_points}', '8'::jsonb);
set local role service_role;
select lives_ok(
  $$ select private.process_dog_deletion_batch_v1(3) $$,
  'a failed finalization is converted into retryable job state'
);
reset role;
select ok(
  (select count(*) from api.telemetry_points where collar_id = 'd4000000-0000-4000-8000-000000000001') = 1
  and (select telemetry_points_deleted = 6 and attempt_count = 3 and status = 'failed' and last_error_code = '23514' from private.deletion_jobs),
  'failure rolls back the data batch while preserving a safe error code and attempt'
);

update private.deletion_jobs
set initial_counts = jsonb_set(initial_counts, '{telemetry_points}', '7'::jsonb),
    next_attempt_at = statement_timestamp();
set local role service_role;
select lives_ok(
  $$ select private.process_dog_deletion_batch_v1(3) $$,
  'the exact failed job retries to completion'
);
reset role;

select is(
  private.dog_deletion_counts_v1('30000000-0000-4000-8000-000000000003'),
  jsonb_build_object(
    'dogs', 0, 'dog_memberships', 0, 'collars', 0, 'device_claims', 0,
    'daily_summaries', 0, 'dirty_summary_days', 0, 'device_credentials', 0,
    'sync_requests', 0, 'recordings', 0, 'telemetry_chunks', 0,
    'telemetry_points', 0, 'telemetry_loss_markers', 0,
    'device_daily_summaries', 0, 'config_revisions', 0,
    'config_resource_heads', 0, 'config_reported', 0, 'config_hlc_state', 0,
    'telemetry_retention_watermarks', 0, 'retention_jobs', 0,
    'retention_receipts', 0,
    'recording_summaries', 0
  ),
  'finalization leaves every dog-scoped active data class empty'
);
select ok(
  (select status = 'completed' and stage = 'completed' and attempt_count = 4
      and telemetry_points_deleted = 7 and completed_at is not null
    from private.deletion_jobs),
  'the completed job records bounded progress and completion time'
);
select ok(
  (select octet_length(receipt_sha256) = 32
      and deleted_counts ->> 'telemetry_points' = '7'
      and deleted_counts ->> 'dog_memberships' = '2'
    from private.deletion_receipts),
  'the durable receipt contains hashes and counts rather than deleted content'
);
select ok(
  exists (select 1 from api.dogs where id = '30000000-0000-4000-8000-000000000004')
  and (select count(*) from api.telemetry_points where collar_id = 'd4000000-0000-4000-8000-000000000002') = 2
  and (select state = 'active' from private.device_credentials where credential_id = 'd6000000-0000-4000-8000-000000000002'),
  'the other dog, telemetry, and credential remain unchanged'
);
select is(
  (
    select count(*)
    from information_schema.columns
    where table_schema = 'private'
      and table_name in ('deletion_tombstones', 'deletion_jobs', 'deletion_receipts')
      and column_name ~ '(latitude|longitude|coordinate|payload|body|email|dog_name|secret)'
  ),
  0::bigint,
  'tombstone, job, and receipt schemas have no content or coordinate columns'
);

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true
);
select is(
  api.request_dog_deletion_v1(
    '30000000-0000-4000-8000-000000000003',
    'da000000-0000-4000-8000-000000000002',
    'dog-delete-v1'
  ) ->> 'status',
  'completed',
  'exact request replay remains stable after active dog data is gone'
);
reset role;

set local role service_role;
select is(
  private.process_dog_deletion_batch_v1(3) ->> 'disposition',
  'idle',
  'the worker is idempotently idle after all ready jobs complete'
);
reset role;

select is(
  (select count(*) from private.deletion_tombstones),
  1::bigint,
  'one append-only tombstone survives active-data deletion'
);
select is(
  (select count(*) from private.deletion_receipts),
  1::bigint,
  'one coordinate-free receipt survives active-data deletion'
);

select * from finish();
rollback;
