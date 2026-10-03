begin;
create extension if not exists pgtap with schema extensions;
select set_config('request.jwt.claims', jsonb_build_object('amr',
  jsonb_build_array(jsonb_build_object('method','password','timestamp',extract(epoch from statement_timestamp()))))::text, true);

select plan(41);

create function pg_temp.m123_capabilities()
returns jsonb
language sql
immutable
as $$
  select '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7]}'::jsonb
$$;

create function pg_temp.m123_device(
  p_device_id uuid,
  p_capability_hash bytea,
  p_firmware_version text
)
returns jsonb
language sql
immutable
as $$
  select jsonb_build_object(
    'device_id', p_device_id,
    'hardware_revision', 'xiao-s3-r1',
    'firmware_version', p_firmware_version,
    'protocol_version', 1,
    'telemetry_schema', 3,
    'config_schema', 7,
    'capability_hash', private.base64url_encode(p_capability_hash)
  )
$$;

create function pg_temp.m123_consume_claim(
  p_code_digest bytea,
  p_request_id uuid,
  p_request_sha256 bytea,
  p_device_public_id uuid,
  p_credential_id uuid,
  p_secret_digest bytea,
  p_device jsonb
)
returns jsonb
language sql
as $$
  select api.consume_device_claim_gateway_v1(
    extensions.digest(convert_to('m123-source:' || encode(p_code_digest, 'hex'), 'UTF8'), 'sha256'),
    extensions.digest(convert_to('m123-device:' || p_device_public_id::text || ':' || encode(p_code_digest, 'hex'), 'UTF8'), 'sha256'),
    p_code_digest,
    p_request_id,
    p_request_sha256,
    p_device_public_id,
    p_credential_id,
    p_secret_digest,
    p_device,
    pg_temp.m123_capabilities()
  )
$$;

create function pg_temp.m123_sync_request(
  p_request_id uuid,
  p_device_id uuid,
  p_capability_hash bytea,
  p_firmware_version text,
  p_chunk_sequence bigint,
  p_point_sequence bigint,
  p_point jsonb
)
returns jsonb
language sql
as $$
  select jsonb_build_object(
    'protocol_version', 1,
    'request_id', p_request_id,
    'device', pg_temp.m123_device(p_device_id, p_capability_hash, p_firmware_version)
      || jsonb_build_object('boot_sequence', 42),
    'clock', jsonb_build_object('utc_ms', null, 'quality', 'unknown', 'uncertainty_ms', null),
    'capabilities', pg_temp.m123_capabilities(),
    'diagnostics', jsonb_build_object(
      'outbox_chunks', 1,
      'outbox_points', 1,
      'outbox_used_bytes', 64,
      'outbox_capacity_bytes', 4096,
      'oldest_unacknowledged_utc_ms', floor(extract(epoch from statement_timestamp() - interval '1 minute') * 1000)::bigint,
      'dropped_points_total', 0,
      'last_error_code', null
    ),
    'upload', jsonb_build_object(
      'chunks', jsonb_build_array(jsonb_build_object(
        'telemetry_schema', 3,
        'boot_sequence', 42,
        'chunk_sequence', p_chunk_sequence,
        'first_point_sequence', p_point_sequence,
        'point_count', 1,
        'time_quality', 4,
        'content_sha256', private.base64url_encode(extensions.digest(
          private.track_v3_point_bytes(p_point), 'sha256'
        )),
        'is_final', false,
        'points', jsonb_build_array(p_point)
      )),
      'summaries', '[]'::jsonb,
      'loss_markers', '[]'::jsonb
    ),
    'configuration', jsonb_build_object('mutations', '[]'::jsonb, 'reported', '[]'::jsonb)
  )
$$;

select has_index(
  'api', 'collars', 'collars_one_active_per_dog_idx',
  'a database index enforces at most one active collar per dog'
);
select ok(
  coalesce(pg_catalog.pg_get_expr(index.indpred, index.indrelid), '') = '(state = ''active''::text)',
  'the active-collar constraint applies only to state=active'
)
from pg_catalog.pg_index as index
join pg_catalog.pg_class as index_class on index_class.oid = index.indexrelid
join pg_catalog.pg_namespace as namespace on namespace.oid = index_class.relnamespace
where namespace.nspname = 'api'
  and index_class.relname = 'collars_one_active_per_dog_idx';
select ok(
  position('from api.dogs as dog' in lower(pg_get_functiondef(
    'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure
  ))) < position('from api.collars as collar' in lower(pg_get_functiondef(
    'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure
  )))
  and position(
    'for update' in substring(
      lower(pg_get_functiondef('api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure))
      from position('from api.dogs as dog' in lower(pg_get_functiondef(
        'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure
      )))
    )
  ) < position(
    'from api.collars as collar' in substring(
      lower(pg_get_functiondef('api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure))
      from position('from api.dogs as dog' in lower(pg_get_functiondef(
        'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)'::regprocedure
      )))
    )
  ),
  'claim transactions lock the dog before checking or mutating collar rows'
);
select ok(
  has_function_privilege(
    'service_role',
    'api.consume_device_claim_gateway_v1(bytea,bytea,bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  )
  and not has_function_privilege(
    'service_role',
    'api.consume_device_claim_v1(bytea,uuid,bytea,uuid,uuid,bytea,jsonb,jsonb)',
    'execute'
  )
  and has_function_privilege(
    'service_role',
    'api.device_revoke_v1(uuid,bytea,uuid,bytea,uuid,text)',
    'execute'
  ),
  'the gateway retains its narrow service grants and callers cannot bypass claim accounting'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    'a1230000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'm123-owner-two@example.test',
    extensions.crypt('local-m123-owner-two-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    'a1230000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'm123-editor@example.test',
    extensions.crypt('local-m123-editor-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    'a1230000-0000-4000-8000-000000000003',
    'authenticated', 'authenticated', 'm123-viewer@example.test',
    extensions.crypt('local-m123-viewer-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  )
on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values
  ('30000000-0000-4000-8000-000000000003', 'a1230000-0000-4000-8000-000000000001', 'owner'),
  ('30000000-0000-4000-8000-000000000003', 'a1230000-0000-4000-8000-000000000002', 'editor'),
  ('30000000-0000-4000-8000-000000000003', 'a1230000-0000-4000-8000-000000000003', 'viewer')
on conflict (dog_id, user_id) do update set role = excluded.role;

insert into api.dogs (id, name, timezone, created_by)
values (
  '30000000-0000-4000-8000-000000000004',
  'M1.23 transfer target',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
);
insert into api.dog_memberships (dog_id, user_id, role)
values ('30000000-0000-4000-8000-000000000004', 'a1230000-0000-4000-8000-000000000001', 'owner');

create temporary table m123_claim_results (
  attempt integer primary key,
  response_json jsonb not null
) on commit drop;
create temporary table m123_sync_results (
  attempt integer primary key,
  request_id uuid not null,
  request_json jsonb not null,
  request_sha256 bytea not null,
  response_json jsonb not null
) on commit drop;

select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  decode(repeat('a1', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
delete from api.dog_memberships
where dog_id = '30000000-0000-4000-8000-000000000003'
  and user_id = '10000000-0000-4000-8000-000000000001';
select throws_ok(
  $$
    select pg_temp.m123_consume_claim(
      decode(repeat('a1', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000109',
      decode(repeat('b9', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000001',
      'd1230000-0000-4000-8000-000000000209',
      decode(repeat('c9', 32), 'hex'),
      pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('39', 32), 'hex'), 'm123-removed-owner')
    )
  $$,
  '42501', 'not_authorized',
  'a removed claim issuer cannot pair against a still-issued claim'
);
insert into api.dog_memberships (dog_id, user_id, role) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  'owner'
);
update auth.users
set deleted_at = statement_timestamp()
where id = '10000000-0000-4000-8000-000000000001';
select throws_ok(
  $$
    select pg_temp.m123_consume_claim(
      decode(repeat('a1', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000110',
      decode(repeat('ba', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000001',
      'd1230000-0000-4000-8000-000000000210',
      decode(repeat('ca', 32), 'hex'),
      pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('3a', 32), 'hex'), 'm123-deleted-owner')
    )
  $$,
  '42501', 'not_authorized',
  'a soft-deleted claim issuer cannot complete a still-issued claim'
);
update auth.users set deleted_at = null
where id = '10000000-0000-4000-8000-000000000001';
insert into m123_claim_results values (
  1,
  pg_temp.m123_consume_claim(
    decode(repeat('a1', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000101',
    decode(repeat('b1', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000201',
    decode(repeat('c1', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('31', 32), 'hex'), 'm123-first')
  )
);
select is(
  (select response_json ->> 'disposition' from m123_claim_results where attempt = 1),
  'claimed',
  'an unused device UUID can be enrolled through the existing claim gateway'
);
select throws_ok(
  $$
    insert into api.collars (device_public_id, dog_id, state)
    values (
      'd1230000-0000-4000-8000-000000000099',
      '30000000-0000-4000-8000-000000000003',
      'active'
    )
  $$,
  '23505',
  'duplicate key value violates unique constraint "collars_one_active_per_dog_idx"',
  'direct writes cannot create a second active collar even outside claim RPCs'
);

create temporary table m123_initial_replay as
select pg_temp.m123_sync_request(
  'd1230000-0000-4000-8000-000000000301',
  'd1230000-0000-4000-8000-000000000001',
  decode(repeat('31', 32), 'hex'),
  'm123-first',
  7,
  0,
  jsonb_build_array(468123456, -740123456,
    floor(extract(epoch from statement_timestamp())::bigint), 125, 9, 7)
) as request_json;
insert into m123_sync_results
select 1,
  (request.request_json ->> 'request_id')::uuid,
  request.request_json,
  extensions.digest(convert_to(request.request_json::text, 'UTF8'), 'sha256'),
  api.device_sync_gateway_v1(
    'd1230000-0000-4000-8000-000000000201',
    decode(repeat('c1', 32), 'hex'),
    (request.request_json ->> 'request_id')::uuid,
    extensions.digest(convert_to(request.request_json::text, 'UTF8'), 'sha256'),
    request.request_json
  )
from m123_initial_replay as request;
select is(
  (select response_json #>> '{telemetry,accepted_chunks,0,chunk_sequence}' from m123_sync_results where attempt = 1),
  '7',
  'the original credential can commit the first boot/chunk before revoke'
);

create temporary table m123_identity as
select
  (response_json ->> 'collar_id')::uuid as collar_id,
  linked_at
from m123_claim_results cross join api.collars
where attempt = 1
  and api.collars.id = (m123_claim_results.response_json ->> 'collar_id')::uuid;
insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state, valid_from, valid_until
)
select
  'd1230000-0000-4000-8000-000000000202',
  identity.collar_id,
  decode(repeat('c2', 32), 'hex'),
  'expired',
  statement_timestamp() - interval '2 days',
  statement_timestamp() - interval '1 day'
from m123_identity as identity;
insert into private.telemetry_retention_watermarks (collar_id, reject_at_or_before)
select collar_id, '2026-01-01 00:00:00+00' from m123_identity;
insert into private.retention_jobs (
  id, data_class, collar_id, cutoff, status, requested_at, next_attempt_at
)
select
  'd1230000-0000-4000-8000-000000000401',
  'raw_telemetry_v1',
  collar_id,
  '2026-01-01 00:00:00+00',
  'pending',
  statement_timestamp(),
  statement_timestamp()
from m123_identity;
create temporary table m123_history_before as
select recording.id as recording_id, recording.collar_id, recording.boot_sequence,
       point.point_sequence, point.lat_e7, point.lon_e7
from api.recordings as recording
join api.telemetry_points as point
  on point.collar_id = recording.collar_id
 and point.boot_sequence = recording.boot_sequence
where recording.collar_id = (select collar_id from m123_identity)
  and recording.boot_sequence = 42;

create temporary table m123_old_revoke as
select api.device_revoke_v1(
  'd1230000-0000-4000-8000-000000000201',
  decode(repeat('c1', 32), 'hex'),
  'd1230000-0000-4000-8000-000000000501',
  decode(repeat('e1', 32), 'hex'),
  'd1230000-0000-4000-8000-000000000001',
  'local_unlink'
) as response_json;
select is(
  (select response_json ->> 'disposition' from m123_old_revoke),
  'newly_revoked',
  'the active device credential can revoke its enrollment'
);
select is(
  (select state from api.collars where id = (select collar_id from m123_identity)),
  'revoked',
  'revoke closes the collar before re-enrollment'
);

select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  'a1230000-0000-4000-8000-000000000002',
  decode(repeat('a2', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
select throws_ok(
  $$
    select pg_temp.m123_consume_claim(
      decode(repeat('a2', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000102',
      decode(repeat('b2', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000001',
      'd1230000-0000-4000-8000-000000000203',
      decode(repeat('c3', 32), 'hex'),
      pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('32', 32), 'hex'), 'm123-editor')
    )
  $$,
  '42501', 'not_authorized',
  'an editor cannot re-enroll an existing device identity'
);
select is(
  (select state from private.device_claims where code_digest = decode(repeat('a2', 32), 'hex')),
  'issued',
  'a denied editor attempt does not consume the owner claim transaction'
);
update private.device_claims
set state = 'cancelled'
where code_digest = decode(repeat('a2', 32), 'hex');
select throws_ok(
  $$
    select api.issue_device_claim_v1(
      '30000000-0000-4000-8000-000000000003',
      'a1230000-0000-4000-8000-000000000003',
      decode(repeat('a3', 32), 'hex'),
      statement_timestamp() + interval '10 minutes'
    )
  $$,
  '42501', 'not_authorized',
  'a viewer cannot issue a claim for re-enrollment'
);

select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  'a1230000-0000-4000-8000-000000000001',
  decode(repeat('a4', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
insert into m123_claim_results values (
  2,
  pg_temp.m123_consume_claim(
    decode(repeat('a4', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000104',
    decode(repeat('b4', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000204',
    decode(repeat('c4', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('34', 32), 'hex'), 'm123-second')
  )
);
select is(
  (select response_json ->> 'disposition' from m123_claim_results where attempt = 2),
  'claimed',
  'a different current owner can re-enroll without being the historical claim issuer'
);
select is(
  (
    select row(
      collar.id, collar.device_public_id, collar.dog_id, collar.state,
      collar.revoked_at, collar.firmware_version
    )::text
    from api.collars as collar
    where collar.id = (select collar_id from m123_identity)
  ),
  '(' || (select collar_id::text from m123_identity) || ',d1230000-0000-4000-8000-000000000001,30000000-0000-4000-8000-000000000003,active,,m123-second)',
  're-enrollment reuses the same collar row and dog while refreshing enrollment metadata'
);
select is(
  (
    select count(*)
    from private.device_credentials as credential
    where credential.collar_id = (select collar_id from m123_identity)
      and credential.state = 'active'
  ),
  1::bigint,
  'exactly one credential is active after re-enrollment'
);
select is(
  (
    select count(*)
    from private.device_credentials as credential
    where credential.collar_id = (select collar_id from m123_identity)
      and credential.state = 'revoked'
      and credential.revoked_at is not null
  ),
  2::bigint,
  'both the prior credential and an expired historical credential become revoked tombstones'
);
select is(
  pg_temp.m123_consume_claim(
    decode(repeat('a4', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000104',
    decode(repeat('b4', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000204',
    decode(repeat('c4', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('34', 32), 'hex'), 'm123-second')
  ),
  (select response_json from m123_claim_results where attempt = 2),
  'a lost claim response can be replayed exactly without adding a credential'
);
select throws_ok(
  $$
    select pg_temp.m123_consume_claim(
      decode(repeat('a4', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000104',
      decode(repeat('b5', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000001',
      'd1230000-0000-4000-8000-000000000204',
      decode(repeat('c4', 32), 'hex'),
      pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('34', 32), 'hex'), 'm123-second')
    )
  $$,
  '23505', 'request_id_conflict',
  'the consumed claim request identity cannot be replayed with different content'
);

select throws_ok(
  $$
    select api.device_sync_gateway_v1(
      'd1230000-0000-4000-8000-000000000201', decode(repeat('c1', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000301',
      decode(repeat('f1', 32), 'hex'), '{}'::jsonb
    )
  $$,
  '42501', 'device_revoked',
  'the old credential cannot sync after re-enrollment even while its device retains pending outbox data'
);
select is(
  api.device_revoke_v1(
    'd1230000-0000-4000-8000-000000000201',
    decode(repeat('c1', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000501',
    decode(repeat('e1', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'local_unlink'
  ),
  (select response_json from m123_old_revoke),
  'the exact old revoke request replays its bounded original receipt'
);
select is(
  (select state from api.collars where id = (select collar_id from m123_identity)),
  'active',
  'an exact old revoke replay leaves the newer enrollment active'
);
create temporary table m123_old_tombstone as
select api.device_revoke_v1(
  'd1230000-0000-4000-8000-000000000201',
  decode(repeat('c1', 32), 'hex'),
  'd1230000-0000-4000-8000-000000000502',
  decode(repeat('e2', 32), 'hex'),
  'd1230000-0000-4000-8000-000000000001',
  'factory_reset'
) as response_json;
select is(
  (select response_json ->> 'disposition' from m123_old_tombstone),
  'already_revoked',
  'a new old-credential revoke request receives only an already-revoked tombstone'
);
select is(
  (
    select row(collar.state, credential.state)::text
    from api.collars as collar
    join private.device_credentials as credential on credential.collar_id = collar.id
    where collar.id = (select collar_id from m123_identity)
      and credential.credential_id = 'd1230000-0000-4000-8000-000000000204'
  ),
  '(active,active)',
  'old revoke retries cannot revoke the newer enrollment or credential'
);
select is(
  (select response_json from m123_old_tombstone) ->> 'revoked_at',
  (select response_json from m123_old_revoke) ->> 'revoked_at',
  'the old tombstone reports the old credential revoke time after collar reactivation'
);
select is(
  api.device_sync_gateway_v1(
    'd1230000-0000-4000-8000-000000000204',
    decode(repeat('c4', 32), 'hex'),
    (select request_id from m123_sync_results where attempt = 1),
    (select request_sha256 from m123_sync_results where attempt = 1),
    (select request_json from m123_sync_results where attempt = 1)
  ),
  (select response_json from m123_sync_results where attempt = 1),
  'the new credential can replay an old exact sync receipt on the retained collar identity'
);

create temporary table m123_pending_outbox as
select pg_temp.m123_sync_request(
  'd1230000-0000-4000-8000-000000000302',
  'd1230000-0000-4000-8000-000000000001',
  decode(repeat('34', 32), 'hex'),
  'm123-second',
  8,
  1,
  jsonb_build_array(468123500, -740123400,
    floor(extract(epoch from statement_timestamp())::bigint), 140, 9, 7)
) as request_json;
insert into m123_sync_results
select 2,
  (request.request_json ->> 'request_id')::uuid,
  request.request_json,
  extensions.digest(convert_to(request.request_json::text, 'UTF8'), 'sha256'),
  api.device_sync_gateway_v1(
    'd1230000-0000-4000-8000-000000000204',
    decode(repeat('c4', 32), 'hex'),
    (request.request_json ->> 'request_id')::uuid,
    extensions.digest(convert_to(request.request_json::text, 'UTF8'), 'sha256'),
    request.request_json
  )
from m123_pending_outbox as request;
select is(
  (select response_json #>> '{telemetry,accepted_chunks,0,chunk_sequence}' from m123_sync_results where attempt = 2),
  '8',
  'a fresh request under the new credential accepts the old outbox chunk identity'
);
select is(
  (
    select row(recording.id, recording.boot_sequence, recording.first_point_sequence,
               recording.last_point_sequence, recording.point_count)::text
    from api.recordings as recording
    where recording.collar_id = (select collar_id from m123_identity)
      and recording.boot_sequence = 42
  ),
  '(' || (select recording_id::text from m123_history_before limit 1) || ',42,0,1,2)',
  'boot and point sequence history continues on the same recording after re-enrollment'
);
select is(
  (
    select count(*)
    from api.telemetry_points as point
    where point.collar_id = (select collar_id from m123_identity)
      and point.boot_sequence = 42
      and point.point_sequence in (0, 1)
  ),
  2::bigint,
  'both the pre-revoke point and re-enrolled outbox point remain under their original IDs'
);
select is(
  (
    select count(*)
    from private.telemetry_retention_watermarks as watermark
    join private.retention_jobs as job on job.collar_id = watermark.collar_id
    where watermark.collar_id = (select collar_id from m123_identity)
      and watermark.reject_at_or_before = '2026-01-01 00:00:00+00'
      and job.id = 'd1230000-0000-4000-8000-000000000401'
      and job.status = 'pending'
  ),
  1::bigint,
  'retention watermarks and queued purge jobs stay attached to the stable collar'
);

select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000004',
  'a1230000-0000-4000-8000-000000000001',
  decode(repeat('a5', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
select is(
  pg_temp.m123_consume_claim(
    decode(repeat('a5', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000105',
    decode(repeat('b5', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000205',
    decode(repeat('c5', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('35', 32), 'hex'), 'm123-transfer')
  ) ->> '_problem',
  'device_identity_conflict',
  'the same hardware UUID cannot transfer to another dog'
);
select is(
  (select state from private.device_claims where code_digest = decode(repeat('a5', 32), 'hex')),
  'issued',
  'a deferred cross-dog transfer attempt leaves its fresh claim unused'
);
select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  'a1230000-0000-4000-8000-000000000001',
  decode(repeat('a6', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
select throws_ok(
  $$
    select pg_temp.m123_consume_claim(
      decode(repeat('a6', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000106',
      decode(repeat('b6', 32), 'hex'),
      'd1230000-0000-4000-8000-000000000002',
      'd1230000-0000-4000-8000-000000000206',
      decode(repeat('c6', 32), 'hex'),
      pg_temp.m123_device('d1230000-0000-4000-8000-000000000002', decode(repeat('36', 32), 'hex'), 'm123-extra')
    )
  $$,
  'P0001', 'active_collar_exists',
  'the serialized claim path refuses a second distinct active collar for the dog'
);
update private.device_claims
set state = 'cancelled'
where code_digest = decode(repeat('a6', 32), 'hex');

select is(
  api.device_revoke_v1(
    'd1230000-0000-4000-8000-000000000204',
    decode(repeat('c4', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000503',
    decode(repeat('e3', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'local_unlink'
  ) ->> 'disposition',
  'newly_revoked',
  'the current credential can revoke the second enrollment before another attempt'
);
select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  'a1230000-0000-4000-8000-000000000001',
  decode(repeat('a7', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
select is(
  pg_temp.m123_consume_claim(
    decode(repeat('a7', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000107',
    decode(repeat('b7', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000207',
    decode(repeat('c1', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('37', 32), 'hex'), 'm123-reused-secret')
  ) ->> '_problem',
  'device_identity_conflict',
  'a fresh claim cannot reuse the digest of a revoked credential'
);
select is(
  (
    select row(collar.state, count(credential.credential_id))::text
    from api.collars as collar
    left join private.device_credentials as credential on credential.collar_id = collar.id
      and credential.credential_id = 'd1230000-0000-4000-8000-000000000207'
    where collar.id = (select collar_id from m123_identity)
    group by collar.state
  ),
  '(revoked,0)',
  'reused-secret rejection rolls back collar reactivation and inserts no credential'
);
update private.device_claims
set state = 'cancelled'
where code_digest = decode(repeat('a7', 32), 'hex');

select api.issue_device_claim_v1(
  '30000000-0000-4000-8000-000000000003',
  'a1230000-0000-4000-8000-000000000001',
  decode(repeat('a8', 32), 'hex'),
  statement_timestamp() + interval '10 minutes'
);
insert into m123_claim_results values (
  3,
  pg_temp.m123_consume_claim(
    decode(repeat('a8', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000108',
    decode(repeat('b8', 32), 'hex'),
    'd1230000-0000-4000-8000-000000000001',
    'd1230000-0000-4000-8000-000000000208',
    decode(repeat('c8', 32), 'hex'),
    pg_temp.m123_device('d1230000-0000-4000-8000-000000000001', decode(repeat('38', 32), 'hex'), 'm123-third')
  )
);
select is(
  (select response_json ->> 'disposition' from m123_claim_results where attempt = 3),
  'claimed',
  'a later fresh credential can re-enroll after a rejected old-secret reuse'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', 'a1230000-0000-4000-8000-000000000001', true);
create temporary table m123_delete_response as
select api.request_dog_deletion_v1(
  '30000000-0000-4000-8000-000000000003',
  'd1230000-0000-4000-8000-000000000601',
  'dog-delete-v1'
) as response_json;
reset role;
select is(
  (select dog.deleted_at is not null
   from api.dogs as dog where dog.id = '30000000-0000-4000-8000-000000000003'),
  true,
  'dog deletion still marks the re-enrolled dog before physical cleanup'
);
select is(
  (
    select row(collar.state, credential.state, credential.revoked_at is not null)::text
    from api.collars as collar
    join private.device_credentials as credential on credential.collar_id = collar.id
      and credential.credential_id = 'd1230000-0000-4000-8000-000000000208'
    where collar.id = (select collar_id from m123_identity)
  ),
  '(revoked,revoked,t)',
  'dog deletion revokes the latest credential and collar before cascade processing'
);
select is(
  (
    select (job.initial_counts ->> 'telemetry_retention_watermarks')
           || ':' || (job.initial_counts ->> 'retention_jobs')
    from private.deletion_jobs as job
    where job.id = ((select response_json ->> 'job_id' from m123_delete_response))::uuid
  ),
  '1:1',
  'the deletion inventory still includes the retained collar retention fence'
);

select * from finish();
rollback;
