begin;
create extension if not exists pgtap with schema extensions;
select plan(49);

select ok(has_function_privilege('authenticated', 'api.preview_my_account_deletion_v1()', 'execute'),
  'authenticated users can preview only their own deletion inventory');
select ok(has_function_privilege('authenticated', 'api.get_my_account_deletion_v1()', 'execute'),
  'authenticated users can read their durable account deletion status');
select ok(has_function_privilege('authenticated', 'api.retry_my_account_deletion_v1()', 'execute'),
  'authenticated users can retry only their own account purge jobs');
select ok(not has_function_privilege('authenticated', 'api.request_account_deletion_v1(uuid,uuid,text,text,text,bigint)', 'execute'),
  'clients cannot call the account initiation service RPC directly');
select ok(has_function_privilege('service_role', 'api.request_account_deletion_v1(uuid,uuid,text,text,text,bigint)', 'execute'),
  'the trusted Edge service can initiate account deletion');
select ok(has_function_privilege('service_role', 'api.prepare_account_deletion_finalization_v1(uuid)', 'execute'),
  'only the trusted service can prepare final Auth deletion');
select ok(has_function_privilege('service_role', 'api.get_account_deletion_receipt_v1(uuid,uuid)', 'execute'),
  'only the trusted service can retrieve a completed receipt after Auth removal');
select ok(not has_function_privilege('anon', 'api.preview_my_account_deletion_v1()', 'execute'),
  'anonymous users cannot enumerate account deletion inventory');
select ok(has_function_privilege('authenticated', 'private.current_account_active_v1()', 'execute'),
  'profile policies use a caller-bound account status helper');

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  ('00000000-0000-0000-0000-000000000000', 'af560000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'm56-owner@example.test', extensions.crypt('local-m56-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', 'af560000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'm56-coowner@example.test', extensions.crypt('local-m56-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', 'af560000-0000-4000-8000-000000000003',
    'authenticated', 'authenticated', 'm56-member@example.test', extensions.crypt('local-m56-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', ''),
  ('00000000-0000-0000-0000-000000000000', 'af560000-0000-4000-8000-000000000004',
    'authenticated', 'authenticated', 'm56-blocked@example.test', extensions.crypt('local-m56-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}', statement_timestamp(), statement_timestamp(), '', '', '', '');

insert into api.profiles (user_id) values
  ('af560000-0000-4000-8000-000000000001'), ('af560000-0000-4000-8000-000000000002'),
  ('af560000-0000-4000-8000-000000000003'), ('af560000-0000-4000-8000-000000000004')
on conflict (user_id) do nothing;

insert into api.dogs (id, name, timezone, created_by) values
  ('af561000-0000-4000-8000-000000000001', 'Owned shared', 'America/Bogota', 'af560000-0000-4000-8000-000000000001'),
  ('af561000-0000-4000-8000-000000000002', 'Owned solo', 'America/Bogota', 'af560000-0000-4000-8000-000000000001'),
  ('af561000-0000-4000-8000-000000000003', 'Editor dog', 'America/Bogota', 'af560000-0000-4000-8000-000000000002'),
  ('af561000-0000-4000-8000-000000000004', 'Viewer dog', 'America/Bogota', 'af560000-0000-4000-8000-000000000003'),
  ('af561000-0000-4000-8000-000000000005', 'Unresolved creator', 'America/Bogota', 'af560000-0000-4000-8000-000000000004');
insert into api.dog_memberships (dog_id, user_id, role) values
  ('af561000-0000-4000-8000-000000000001', 'af560000-0000-4000-8000-000000000001', 'owner'),
  ('af561000-0000-4000-8000-000000000001', 'af560000-0000-4000-8000-000000000002', 'owner'),
  ('af561000-0000-4000-8000-000000000001', 'af560000-0000-4000-8000-000000000003', 'viewer'),
  ('af561000-0000-4000-8000-000000000002', 'af560000-0000-4000-8000-000000000001', 'owner'),
  ('af561000-0000-4000-8000-000000000003', 'af560000-0000-4000-8000-000000000002', 'owner'),
  ('af561000-0000-4000-8000-000000000003', 'af560000-0000-4000-8000-000000000001', 'editor'),
  ('af561000-0000-4000-8000-000000000004', 'af560000-0000-4000-8000-000000000003', 'owner'),
  ('af561000-0000-4000-8000-000000000004', 'af560000-0000-4000-8000-000000000001', 'viewer'),
  ('af561000-0000-4000-8000-000000000005', 'af560000-0000-4000-8000-000000000002', 'owner'),
  ('af561000-0000-4000-8000-000000000005', 'af560000-0000-4000-8000-000000000004', 'editor');

select set_config('request.jwt.claim.sub', 'af560000-0000-4000-8000-000000000001', true);
create temporary table m56_owner_preview on commit drop as
  select api.preview_my_account_deletion_v1() as body;
select is((select body ->> 'schema_version' from m56_owner_preview), 'account-deletion-preview-v1',
  'preview has a versioned contract');
select is((select jsonb_array_length(body -> 'owned_dogs') from m56_owner_preview), 2,
  'preview includes every currently owned dog');
select is((select (dog ->> 'other_member_count')::integer
  from m56_owner_preview, jsonb_array_elements(body -> 'owned_dogs') dog
  where dog ->> 'dog_id' = 'af561000-0000-4000-8000-000000000001'), 2,
  'shared-dog preview counts all affected non-requesting members');
select is((select (dog ->> 'other_owner_count')::integer
  from m56_owner_preview, jsonb_array_elements(body -> 'owned_dogs') dog
  where dog ->> 'dog_id' = 'af561000-0000-4000-8000-000000000001'), 1,
  'shared-dog preview identifies its other owner');
select is((select jsonb_array_length(body -> 'memberships_to_detach') from m56_owner_preview), 2,
  'preview lists viewer and editor memberships to detach');
select is((select jsonb_array_length(body -> 'blockers') from m56_owner_preview), 0,
  'ordinary complete ownership has no preflight blocker');
select ok((select body ->> 'scope_sha256' ~ '^[A-Za-z0-9_-]{43}$' from m56_owner_preview),
  'preview returns a bounded scope fingerprint');

select throws_ok($$
  select api.request_account_deletion_v1(
    'af560000-0000-4000-8000-000000000001',
    (select (body ->> 'request_id')::uuid from m56_owner_preview),
    'account-delete-v1', (select body ->> 'scope_sha256' from m56_owner_preview),
    'ELIMINAR CUENTA Y TODOS LOS PERROS',
    floor(extract(epoch from statement_timestamp() - interval '6 minutes'))::bigint
  )
$$, '28000', 'reauthentication_required', 'service start rejects stale password reauthentication');
select is((select count(*) from private.account_deletion_requests), 0::bigint,
  'failed reauthentication creates no durable or destructive state');

create temporary table m56_owner_request on commit drop as
  select api.request_account_deletion_v1(
    'af560000-0000-4000-8000-000000000001',
    (select (body ->> 'request_id')::uuid from m56_owner_preview),
    'account-delete-v1', (select body ->> 'scope_sha256' from m56_owner_preview),
    'ELIMINAR CUENTA Y TODOS LOS PERROS', floor(extract(epoch from statement_timestamp()))::bigint
  ) as body;
select is((select body ->> 'status' from m56_owner_request), 'pending',
  'request durably starts the existing dog jobs');
select is((select count(*) from api.dogs where id in (
  'af561000-0000-4000-8000-000000000001', 'af561000-0000-4000-8000-000000000002') and deleted_at is not null),
  2::bigint, 'all owned dogs close access before physical purge');
select is((select count(*) from api.dog_memberships where user_id = 'af560000-0000-4000-8000-000000000001'),
  0::bigint, 'account memberships are removed at initiation');
select is((select count(*) from private.account_deletion_job_links
  where request_id = (select (body ->> 'request_id')::uuid from m56_owner_request)),
  2::bigint, 'account status links both pre-existing worker jobs');
select is((select count(*) from api.dog_memberships
  where dog_id = 'af561000-0000-4000-8000-000000000001'), 0::bigint,
  'co-owners and viewers are removed from a dog the account owns');
select is((select count(*) from api.dogs where id = 'af561000-0000-4000-8000-000000000003'),
  1::bigint, 'viewer/editor dogs remain intact');
select is((select count(*) from auth.users where id = 'af560000-0000-4000-8000-000000000001'),
  1::bigint, 'Auth identity remains until every purge job completes');

set local role authenticated;
select set_config('request.jwt.claim.sub', 'af560000-0000-4000-8000-000000000001', true);
select is((select count(*) from api.profiles where user_id = 'af560000-0000-4000-8000-000000000001'),
  0::bigint, 'pending deletion blocks reads of the caller profile');
select throws_ok($$select api.create_dog_v1('After deletion', 'America/Bogota')$$,
  '55000', 'account_deletion_pending', 'pending account cannot create another dog');
select is(api.retry_my_account_deletion_v1() ->> 'status', 'pending',
  'retry without failed jobs is a harmless pending-state refresh');
reset role;

select throws_ok($$delete from auth.users where id = 'af560000-0000-4000-8000-000000000001'$$,
  '55000', 'account_deletion_not_ready', 'Auth removal is refused before every worker job completes');
update private.deletion_jobs
set status = 'failed', last_error_code = '40001', next_attempt_at = statement_timestamp() + interval '1 hour'
where id = (select job_id from private.account_deletion_job_links
  where request_id = (select (body ->> 'request_id')::uuid from m56_owner_request) limit 1);
set local role authenticated;
select set_config('request.jwt.claim.sub', 'af560000-0000-4000-8000-000000000001', true);
create temporary table m56_retry on commit drop as
  select api.retry_my_account_deletion_v1() as body;
select is((select body ->> 'status' from m56_retry), 'pending', 'retry preserves the pending account state');
reset role;
select is((select count(*) from private.deletion_jobs where status = 'pending'
  and id in (select job_id from private.account_deletion_job_links
    where request_id = (select (body ->> 'request_id')::uuid from m56_owner_request))),
  2::bigint, 'account retry releases all failed child jobs to the existing worker');

select lives_ok($$select private.process_dog_deletion_batch_v1(5000)$$, 'existing worker completes first dog purge');
select lives_ok($$select private.process_dog_deletion_batch_v1(5000)$$, 'existing worker completes second dog purge');
select is((select count(*) from private.deletion_jobs job
  join private.account_deletion_job_links link on link.job_id = job.id
  where link.request_id = (select (body ->> 'request_id')::uuid from m56_owner_request)
    and job.status = 'completed'), 2::bigint, 'account status waits for every existing dog job');
select is((select count(*) from api.dogs where id in (
  'af561000-0000-4000-8000-000000000001', 'af561000-0000-4000-8000-000000000002')),
  0::bigint, 'worker removes dog data only after its bounded job completes');
select is((select count(*) from api.dog_memberships
  where dog_id in ('af561000-0000-4000-8000-000000000003', 'af561000-0000-4000-8000-000000000004')
    and user_id = 'af560000-0000-4000-8000-000000000001'), 0::bigint,
  'viewer/editor membership removal does not delete those other dogs');
select is(api.get_my_account_deletion_v1() ->> 'status', 'ready',
  'durable status becomes ready when every linked dog purge completes');
create temporary table m56_ready on commit drop as
  select api.prepare_account_deletion_finalization_v1('af560000-0000-4000-8000-000000000001') as body;
select is((select body ->> 'status' from m56_ready), 'ready',
  'trusted readiness RPC seals the account receipt before Auth removal');
select ok((select body ->> 'receipt_sha256' ~ '^[A-Za-z0-9_-]{43}$' from m56_ready),
  'readiness persists a bounded receipt fingerprint');
select lives_ok($$delete from auth.users where id = 'af560000-0000-4000-8000-000000000001'$$,
  'Auth can be removed only after readiness closes every reference');
select is(api.get_account_deletion_receipt_v1(
  'af560000-0000-4000-8000-000000000001',
  (select (body ->> 'request_id')::uuid from m56_owner_request)
) ->> 'status', 'completed', 'Auth delete trigger stores the durable completion receipt');
select throws_ok($$select api.get_my_account_deletion_v1()$$,
  '28000', 'authentication_required', 'status RPC rejects a session whose Auth user was removed');

select set_config('request.jwt.claim.sub', 'af560000-0000-4000-8000-000000000004', true);
create temporary table m56_blocked_preview on commit drop as
  select api.preview_my_account_deletion_v1() as body;
select ok((select body -> 'blockers' ? 'creator_not_in_deletion_set' from m56_blocked_preview),
  'creator-without-owner membership is reported before destructive work');
create temporary table m56_blocked_request on commit drop as
  select api.request_account_deletion_v1(
    'af560000-0000-4000-8000-000000000004',
    (select (body ->> 'request_id')::uuid from m56_blocked_preview),
    'account-delete-v1', (select body ->> 'scope_sha256' from m56_blocked_preview),
    'ELIMINAR CUENTA Y TODOS LOS PERROS', floor(extract(epoch from statement_timestamp()))::bigint
  ) as body;
select is((select body ->> 'status' from m56_blocked_request), 'blocked',
  'unresolvable creator reference is durably blocked without auto-transfer');
select is((select count(*) from api.dogs where id = 'af561000-0000-4000-8000-000000000005' and deleted_at is null),
  1::bigint, 'blocked preflight leaves the creator dog unchanged');
select is((select count(*) from api.dog_memberships
  where dog_id = 'af561000-0000-4000-8000-000000000005'
    and user_id = 'af560000-0000-4000-8000-000000000004'), 1::bigint,
  'blocked preflight leaves memberships unchanged');
select ok(not private.account_deletion_active_v1('af560000-0000-4000-8000-000000000004'),
  'a preflight blocker does not install the pending-access lock');
select throws_ok($$delete from auth.users where id = 'af560000-0000-4000-8000-000000000004'$$,
  '55000', 'account_deletion_not_ready', 'Auth removal is refused for an unresolved creator reference');
select is((select count(*) from private.account_deletion_job_links
  where request_id = (select (body ->> 'request_id')::uuid from m56_blocked_request)),
  0::bigint, 'a blocked account has no queued deletion jobs');

reset role;
select * from finish();
rollback;
