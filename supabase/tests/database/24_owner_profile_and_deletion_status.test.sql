begin;
create extension if not exists pgtap with schema extensions;
-- Model a recently password-authenticated signed JWT for destructive owner calls.
select set_config('request.jwt.claims', jsonb_build_object('amr',
  jsonb_build_array(jsonb_build_object('method','password','timestamp',extract(epoch from statement_timestamp()))))::text, true);
select plan(13);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(api.rename_dog_v1('30000000-0000-4000-8000-000000000003', '  Nube  '),
  '30000000-0000-4000-8000-000000000003'::uuid, 'owner can rename');
select is((select name from api.dogs where id='30000000-0000-4000-8000-000000000003'), 'Nube', 'name is trimmed');
select throws_ok($$select api.rename_dog_v1('30000000-0000-4000-8000-000000000003', repeat('a',81))$$,
  '22023', 'invalid_dog_profile', 'long names denied');
select is(api.list_my_deletion_jobs_v1(), '[]'::jsonb, 'no invented deletion state');
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select throws_ok($$select api.rename_dog_v1('30000000-0000-4000-8000-000000000003', 'Editor')$$,
  '42501', 'not_authorized', 'editor cannot rename');
select throws_ok($$select api.retry_my_deletion_job_v1('ffffffff-ffff-4fff-8fff-ffffffffffff')$$,
  '42501', 'not_authorized', 'job retry cannot probe another requester');
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select set_config('request.jwt.claims', '{"user_metadata":{"amr":[{"method":"password","timestamp":9999999999}]}}', true);
select throws_ok($$select api.request_dog_deletion_v1('30000000-0000-4000-8000-000000000003',
  'dd000000-0000-4000-8000-000000000025', 'dog-delete-v1')$$, '28000', 'reauthentication_required', 'user metadata cannot supply password proof');
select set_config('request.jwt.claims', jsonb_build_object('amr', jsonb_build_array(jsonb_build_object(
  'method','password','timestamp',extract(epoch from statement_timestamp() - interval '6 minutes'))))::text, true);
select throws_ok($$select api.request_dog_deletion_v1('30000000-0000-4000-8000-000000000003',
  'dd000000-0000-4000-8000-000000000025', 'dog-delete-v1')$$, '28000', 'reauthentication_required', 'old password proof is denied');
select is(api.list_my_deletion_jobs_v1(), '[]'::jsonb, 'failed reauthentication has no deletion side effects');
select set_config('request.jwt.claims', jsonb_build_object('amr', jsonb_build_array(jsonb_build_object(
  'method','password','timestamp',extract(epoch from statement_timestamp()))))::text, true);
select lives_ok($$select api.request_dog_deletion_v1('30000000-0000-4000-8000-000000000003',
  'dd000000-0000-4000-8000-000000000024', 'dog-delete-v1')$$, 'owner requests deletion');
select is(jsonb_array_length(api.list_my_deletion_jobs_v1()), 1, 'requester sees job after membership removed');
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(api.list_my_deletion_jobs_v1(), '[]'::jsonb, 'other member cannot see requester jobs');
select set_config('request.jwt.claim.sub', 'ffffffff-ffff-4fff-8fff-ffffffffffff', true);
select throws_ok($$select api.list_my_deletion_jobs_v1()$$, '28000', 'authentication_required', 'deleted/nonexistent Auth rejected');
reset role;
select * from finish();
rollback;
