begin;
create extension if not exists pgtap with schema extensions;

select plan(19);

select set_eq(
  $$
    select format('%I.%I', namespace.nspname, relation.relname)
    from pg_catalog.pg_class relation
    join pg_catalog.pg_namespace namespace on namespace.oid = relation.relnamespace
    where namespace.nspname = 'api'
      and relation.relkind in ('r', 'p')
      and has_table_privilege('authenticated', relation.oid, 'select')
  $$,
  $$
    values
      ('api.collars'),
      ('api.config_reported'),
      ('api.config_resource_heads'),
      ('api.config_revisions'),
      ('api.daily_summaries'),
      ('api.dog_memberships'),
      ('api.dogs'),
      ('api.profiles'),
      ('api.recording_summaries'),
      ('api.recordings'),
      ('api.telemetry_points')
  $$,
  'authenticated Data API reads are frozen to the exact eleven api tables'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_class relation
    join pg_catalog.pg_namespace namespace on namespace.oid = relation.relnamespace
    where namespace.nspname = 'api'
      and relation.relkind in ('r', 'p')
      and has_table_privilege('authenticated', relation.oid, 'select')
      and relation.relrowsecurity
  ),
  11::bigint,
  'all eleven authenticated Data API tables retain RLS'
);

select set_eq(
  $$
    select procedure.oid::regprocedure::text
    from pg_catalog.pg_proc procedure
    join pg_catalog.pg_namespace namespace on namespace.oid = procedure.pronamespace
    where namespace.nspname = 'api'
      and has_function_privilege('authenticated', procedure.oid, 'execute')
  $$,
  $$
    values
      ('api.create_dog_v1(text,text)'),
      ('api.get_deletion_job_v1(uuid)'),
      ('api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)'),
      ('api.request_dog_deletion_v1(uuid,uuid,text)'),
      ('api.revoke_collar_v1(uuid)')
  $$,
  'authenticated RPC execution is frozen to the exact five user functions'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_proc procedure
    join pg_catalog.pg_namespace namespace on namespace.oid = procedure.pronamespace
    where namespace.nspname = 'api'
      and has_function_privilege('authenticated', procedure.oid, 'execute')
      and procedure.prosecdef
      and coalesce(procedure.proconfig, '{}'::text[]) @> array['search_path=""']
  ),
  5::bigint,
  'all five authenticated RPCs remain security-definer functions with an empty search path'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_proc procedure
    join pg_catalog.pg_namespace namespace on namespace.oid = procedure.pronamespace
    where namespace.nspname = 'api'
      and has_function_privilege('anon', procedure.oid, 'execute')
  ),
  0::bigint,
  'anonymous callers cannot execute any api RPC'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values (
  '00000000-0000-0000-0000-000000000000',
  '9a140000-0000-4000-8000-000000000001',
  'authenticated', 'authenticated', 'm114-deleted-user@example.test',
  extensions.crypt('local-m114-deleted-password', extensions.gen_salt('bf')),
  statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
  statement_timestamp(), statement_timestamp(), '', '', '', ''
);

insert into api.dogs (id, name, timezone, created_by)
values (
  '9b140000-0000-4000-8000-000000000001',
  'M1.14 deletion receipt fixture',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
);

insert into api.dog_memberships (dog_id, user_id, role) values
  (
    '9b140000-0000-4000-8000-000000000001',
    '10000000-0000-4000-8000-000000000001',
    'owner'
  ),
  (
    '9b140000-0000-4000-8000-000000000001',
    '9a140000-0000-4000-8000-000000000001',
    'owner'
  ),
  (
    '30000000-0000-4000-8000-000000000003',
    '9a140000-0000-4000-8000-000000000001',
    'owner'
  );

insert into api.collars (id, device_public_id, dog_id, display_name, state, linked_at)
values (
  '9d140000-0000-4000-8000-000000000001',
  '9e140000-0000-4000-8000-000000000001',
  '30000000-0000-4000-8000-000000000003',
  'M1.14 stale-user target',
  'active',
  statement_timestamp()
);

insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state
) values (
  '9f140000-0000-4000-8000-000000000001',
  '9d140000-0000-4000-8000-000000000001',
  decode(repeat('a1', 32), 'hex'),
  'active'
);

create temporary table m114_deletion_capture (result jsonb not null) on commit drop;
grant select, insert on m114_deletion_capture to authenticated;

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '9a140000-0000-4000-8000-000000000001', true
);
select lives_ok(
  $$
    insert into pg_temp.m114_deletion_capture (result)
    select api.request_dog_deletion_v1(
      '9b140000-0000-4000-8000-000000000001',
      '9c140000-0000-4000-8000-000000000001',
      'dog-delete-v1'
    )
  $$,
  'a current co-owner creates the retained deletion job before Auth deletion'
);
reset role;

select is(
  (select count(*) from private.deletion_jobs job
    where job.id = (select (result ->> 'job_id')::uuid from m114_deletion_capture)),
  1::bigint,
  'the deletion job exists independently of active dog membership'
);

delete from auth.users
where id = '9a140000-0000-4000-8000-000000000001';

select ok(
  not exists (
    select 1 from auth.users
    where id = '9a140000-0000-4000-8000-000000000001'
  ) and not exists (
    select 1 from api.dog_memberships
    where user_id = '9a140000-0000-4000-8000-000000000001'
  ),
  'hard Auth deletion removes the user and every remaining membership'
);

create temporary table m114_state_before on commit drop as
select jsonb_build_object(
  'forbidden_dogs', (
    select count(*) from api.dogs where name = 'M1.14 forbidden stale creation'
  ),
  'memberships', (
    select count(*) from api.dog_memberships
    where user_id = '9a140000-0000-4000-8000-000000000001'
  ),
  'collar', (
    select jsonb_build_object('state', state, 'revoked_at', revoked_at)
    from api.collars where id = '9d140000-0000-4000-8000-000000000001'
  ),
  'credential', (
    select jsonb_build_object('state', state, 'revoked_at', revoked_at)
    from private.device_credentials
    where credential_id = '9f140000-0000-4000-8000-000000000001'
  ),
  'config_revisions', (
    select count(*) from api.config_revisions
    where collar_id = '9d140000-0000-4000-8000-000000000001'
  ),
  'config_heads', (
    select count(*) from api.config_resource_heads
    where collar_id = '9d140000-0000-4000-8000-000000000001'
  ),
  'config_hlc', (
    select count(*) from private.config_hlc_state
    where collar_id = '9d140000-0000-4000-8000-000000000001'
  ),
  'tombstone', (
    select to_jsonb(tombstone)
    from private.deletion_tombstones tombstone
    where tombstone.request_id = '9c140000-0000-4000-8000-000000000001'
  ),
  'job', (
    select to_jsonb(job)
    from private.deletion_jobs job
    where job.id = (select (result ->> 'job_id')::uuid from m114_deletion_capture)
  )
) as snapshot;

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '9a140000-0000-4000-8000-000000000001', true
);

select throws_ok(
  $$ select api.create_dog_v1('M1.14 forbidden stale creation', 'America/Bogota') $$,
  '28000', 'authentication_required',
  'a deleted Auth subject cannot create a dog'
);

select throws_ok(
  $$
    select api.request_dog_deletion_v1(
      '9b140000-0000-4000-8000-000000000001',
      '9c140000-0000-4000-8000-000000000001',
      'dog-delete-v1'
    )
  $$,
  '28000', 'authentication_required',
  'a deleted Auth subject cannot replay its retained deletion request'
);

select throws_ok(
  format(
    'select api.get_deletion_job_v1(%L::uuid)',
    (select result ->> 'job_id' from m114_deletion_capture)
  ),
  '28000', 'authentication_required',
  'a deleted Auth subject cannot read its retained deletion job'
);

select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '9d140000-0000-4000-8000-000000000001',
      'brightness', 1,
      '9a150000-0000-4000-8000-000000000001', 0,
      '{"brightness":64}'::jsonb,
      extensions.digest(convert_to('{"brightness":64}', 'UTF8'), 'sha256')
    )
  $$,
  '42501', 'not_authorized',
  'membership removal keeps a deleted Auth subject out of configuration mutation'
);

select throws_ok(
  $$ select api.revoke_collar_v1('9d140000-0000-4000-8000-000000000001') $$,
  '42501', 'not_authorized',
  'membership removal keeps a deleted Auth subject out of collar revocation'
);
reset role;

insert into api.dog_memberships (dog_id, user_id, role)
values (
  '30000000-0000-4000-8000-000000000003',
  '20000000-0000-4000-8000-000000000002',
  'editor'
);

set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true
);
select throws_ok(
  $$ select api.revoke_collar_v1('9d140000-0000-4000-8000-000000000001') $$,
  '42501', 'not_authorized',
  'an editor cannot revoke a collar through the direct RPC'
);
select throws_ok(
  $$
    select api.request_dog_deletion_v1(
      '9b140000-0000-4000-8000-000000000001',
      '9c140000-0000-4000-8000-000000000001',
      'dog-delete-v1'
    )
  $$,
  '42501', 'not_authorized',
  'another identity receives the bounded denial for an existing deletion request ID'
);
select throws_ok(
  $$
    select api.request_dog_deletion_v1(
      '9b140000-0000-4000-8000-000000000001',
      '9c140000-0000-4000-8000-000000000099',
      'dog-delete-v1'
    )
  $$,
  '42501', 'not_authorized',
  'another identity receives the same bounded denial for a missing deletion request ID'
);
reset role;

select is(
  jsonb_build_object(
    'forbidden_dogs', (
      select count(*) from api.dogs where name = 'M1.14 forbidden stale creation'
    ),
    'memberships', (
      select count(*) from api.dog_memberships
      where user_id = '9a140000-0000-4000-8000-000000000001'
    ),
    'collar', (
      select jsonb_build_object('state', state, 'revoked_at', revoked_at)
      from api.collars where id = '9d140000-0000-4000-8000-000000000001'
    ),
    'credential', (
      select jsonb_build_object('state', state, 'revoked_at', revoked_at)
      from private.device_credentials
      where credential_id = '9f140000-0000-4000-8000-000000000001'
    ),
    'config_revisions', (
      select count(*) from api.config_revisions
      where collar_id = '9d140000-0000-4000-8000-000000000001'
    ),
    'config_heads', (
      select count(*) from api.config_resource_heads
      where collar_id = '9d140000-0000-4000-8000-000000000001'
    ),
    'config_hlc', (
      select count(*) from private.config_hlc_state
      where collar_id = '9d140000-0000-4000-8000-000000000001'
    ),
    'tombstone', (
      select to_jsonb(tombstone)
      from private.deletion_tombstones tombstone
      where tombstone.request_id = '9c140000-0000-4000-8000-000000000001'
    ),
    'job', (
      select to_jsonb(job)
      from private.deletion_jobs job
      where job.id = (select (result ->> 'job_id')::uuid from m114_deletion_capture)
    )
  ),
  (select snapshot from m114_state_before),
  'all five stale-user RPC attempts leave user, collar, configuration, tombstone, and job state unchanged'
);

select is(
  (select count(*) from private.deletion_tombstones
    where request_id = '9c140000-0000-4000-8000-000000000001'),
  1::bigint,
  'the denied stale replay creates no duplicate deletion tombstone'
);

select is(
  (select count(*) from private.deletion_jobs job
    where job.id = (select (result ->> 'job_id')::uuid from m114_deletion_capture)),
  1::bigint,
  'the retained deletion job remains exactly one row after stale access denial'
);

select * from finish();
rollback;
