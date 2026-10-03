begin;
create extension if not exists pgtap with schema extensions;

select set_config('request.jwt.claims', jsonb_build_object(
  'sub', '10000000-0000-4000-8000-000000000001',
  'amr', jsonb_build_array(jsonb_build_object(
    'method', 'password',
    'timestamp', extract(epoch from statement_timestamp())
  ))
)::text, true);
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);

select plan(6);

select is(
  (select procedure_row.provolatile::text
   from pg_proc procedure_row
   where procedure_row.oid = 'private.dog_deletion_counts_v1(uuid)'::regprocedure),
  'v',
  'the deletion count fence uses a fresh snapshot after advisory-lock waits'
);

select ok(
  not has_function_privilege('authenticated', 'private.dog_deletion_counts_v1(uuid)', 'execute')
  and not has_function_privilege('service_role', 'private.dog_deletion_counts_v1(uuid)', 'execute'),
  'the fenced inventory helper remains internal'
);

insert into api.dogs (id, name, created_by)
values ('f3000000-0000-4000-8000-000000000001', 'Retention fence fixture', '10000000-0000-4000-8000-000000000001');
insert into api.dog_memberships (dog_id, user_id, role)
values ('f3000000-0000-4000-8000-000000000001', '10000000-0000-4000-8000-000000000001', 'owner');
insert into api.collars (id, device_public_id, dog_id, state, revoked_at)
values
  ('f4000000-0000-4000-8000-000000000001', 'f5000000-0000-4000-8000-000000000001', 'f3000000-0000-4000-8000-000000000001', 'active', null),
  ('f4000000-0000-4000-8000-000000000002', 'f5000000-0000-4000-8000-000000000002', 'f3000000-0000-4000-8000-000000000001', 'revoked', statement_timestamp());
insert into private.device_credentials (credential_id, collar_id, secret_digest, state)
values ('f6000000-0000-4000-8000-000000000001', 'f4000000-0000-4000-8000-000000000001', decode(repeat('f1', 32), 'hex'), 'active');

set local role authenticated;
select lives_ok(
  $$ select api.request_dog_deletion_v1(
       'f3000000-0000-4000-8000-000000000001',
       'fa000000-0000-4000-8000-000000000001',
       'dog-delete-v1'
     ) $$,
  'a fresh deletion request closes ingress and snapshots the dog inventory'
);
reset role;

select is(
  (select initial_counts ->> 'collars' from private.deletion_jobs),
  '2',
  'the snapshot includes both active and previously revoked collars'
);

select is(
  (
    select count(*)
    from api.collars collar
    where collar.dog_id = 'f3000000-0000-4000-8000-000000000001'
      and exists (
        select 1
        from pg_locks lock_row
        where lock_row.locktype = 'advisory'
          and lock_row.pid = pg_backend_pid()
          and lock_row.granted
          and lock_row.mode = 'ExclusiveLock'
          and lock_row.objsubid = 1
          and lock_row.classid = (
            (pg_catalog.hashtextextended('dog-rgb:telemetry:' || collar.id::text, 0) >> 32)
            & 4294967295
          )::oid
          and lock_row.objid = (
            pg_catalog.hashtextextended('dog-rgb:telemetry:' || collar.id::text, 0)
            & 4294967295
          )::oid
      )
  ),
  2::bigint,
  'the deletion request holds retention fences for every collar, including one already revoked'
);

select ok(
  position('dog_deletion_counts_v1' in (
    select procedure_row.prosrc from pg_proc procedure_row
    where procedure_row.oid = 'private.request_dog_deletion_for_actor_v1(uuid,uuid,uuid,text,timestamptz)'::regprocedure
  )) > 0
  and position('dog_deletion_counts_v1' in (
    select procedure_row.prosrc from pg_proc procedure_row
    where procedure_row.oid = 'private.replay_dog_deletion_tombstone_v1(jsonb)'::regprocedure
  )) > 0,
  'fresh requests and restore replays both use the same fenced inventory helper'
);

select * from finish();
rollback;
