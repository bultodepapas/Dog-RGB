begin;
create extension if not exists pgtap with schema extensions;

select plan(23);

select ok(
  has_function_privilege(
    'authenticated',
    'api.create_dog_v1(text,text)',
    'execute'
  ),
  'authenticated users can execute the dog-creation RPC'
);
select ok(
  not has_function_privilege('anon', 'api.create_dog_v1(text,text)', 'execute'),
  'anonymous users cannot execute the dog-creation RPC'
);

set local role anon;
select throws_ok(
  $$ select api.create_dog_v1('Anon dog', 'America/Bogota') $$,
  '42501',
  null,
  'an anonymous RPC call is denied by function privileges'
);

reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '', true);
select throws_ok(
  $$ select api.create_dog_v1('No subject', 'America/Bogota') $$,
  '28000',
  'authentication_required',
  'an authenticated role without a JWT subject is rejected'
);

select set_config(
  'request.jwt.claim.sub',
  '10000000-0000-4000-8000-000000000001',
  true
);
select throws_ok(
  $$ select api.create_dog_v1('', 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects an empty name'
);
select throws_ok(
  $$ select api.create_dog_v1('   ', 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects a name containing only ordinary spaces'
);
select throws_ok(
  $$ select api.create_dog_v1(E'\t\n', 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects a name containing only control whitespace'
);
select throws_ok(
  $$ select api.create_dog_v1(E'\u00a0\u2003\ufeff', 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects a name containing only Unicode whitespace'
);
select throws_ok(
  $$ select api.create_dog_v1(repeat('🐕', 81), 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC bounds names by Unicode code points'
);
select throws_ok(
  $$ select api.create_dog_v1('Wrong zone', 'Mars/Olympus') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects an unknown timezone'
);
select throws_ok(
  $$ select api.create_dog_v1(null, 'America/Bogota') $$,
  '22023',
  'invalid_dog_profile',
  'the RPC rejects a null name with the bounded contract error'
);

reset role;
select is(
  (select count(*) from api.dogs),
  1::bigint,
  'invalid dog profiles create no dog rows'
);
select is(
  (select count(*) from api.dog_memberships),
  1::bigint,
  'invalid dog profiles create no membership rows'
);

create temporary table m15_created_dog (dog_id uuid not null);
grant insert, select on table m15_created_dog to authenticated;

set local role authenticated;
select lives_ok(
  $$
    insert into pg_temp.m15_created_dog (dog_id)
    select api.create_dog_v1('  Mora 🐕  ', 'America/Bogota')
  $$,
  'an authenticated user can create one dog through the RPC'
);

reset role;
select ok(
  (select dog_id::text from m15_created_dog)
    ~ '^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$',
  'the RPC returns a canonical UUID'
);
select is(
  (
    select count(*)
    from api.dogs d
    join m15_created_dog result on result.dog_id = d.id
    where d.name = 'Mora 🐕'
      and d.timezone = 'America/Bogota'
      and d.created_by = '10000000-0000-4000-8000-000000000001'
  ),
  1::bigint,
  'dog creation stores only the normalized name, fixed timezone, and caller identity'
);
select is(
  (
    select count(*)
    from api.dog_memberships dm
    join m15_created_dog result on result.dog_id = dm.dog_id
    where dm.user_id = '10000000-0000-4000-8000-000000000001'
      and dm.role = 'owner'
  ),
  1::bigint,
  'dog creation atomically grants exactly one owner membership'
);
select is(
  (
    select units
    from api.profiles
    where user_id = '10000000-0000-4000-8000-000000000001'
  ),
  'metric',
  'dog creation leaves the profile-level metric default unchanged'
);

create or replace function private.m15_force_membership_failure()
returns trigger
language plpgsql
set search_path = ''
as $$
begin
  raise exception using errcode = 'P0001', message = 'forced_membership_failure';
end
$$;
create trigger m15_force_membership_failure
before insert on api.dog_memberships
for each row execute function private.m15_force_membership_failure();

set local role authenticated;
select set_config(
  'request.jwt.claim.sub',
  '10000000-0000-4000-8000-000000000001',
  true
);
select throws_ok(
  $$ select api.create_dog_v1('Must roll back', 'America/Bogota') $$,
  'P0001',
  'forced_membership_failure',
  'a membership insertion failure aborts dog creation'
);

reset role;
select is(
  (select count(*) from api.dogs),
  2::bigint,
  'a membership failure rolls back the preceding dog row'
);
select is(
  (select count(*) from api.dog_memberships),
  2::bigint,
  'a membership failure leaves the membership set unchanged'
);
drop trigger m15_force_membership_failure on api.dog_memberships;
drop function private.m15_force_membership_failure();

set local role authenticated;
select set_config(
  'request.jwt.claim.sub',
  '90000000-0000-4000-8000-000000000009',
  true
);
select throws_ok(
  $$ select api.create_dog_v1('Deleted user', 'America/Bogota') $$,
  '23503',
  null,
  'a stale subject absent from Auth cannot create a dog'
);

reset role;
select is(
  (select count(*) from api.dogs),
  2::bigint,
  'a stale subject failure leaves no dog row'
);

select * from finish();
rollback;
