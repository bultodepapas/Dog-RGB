begin;
create extension if not exists pgtap with schema extensions;

select plan(41);

create function pg_temp.brightness_hash(p_brightness integer)
returns bytea
language sql
immutable
as $$
  select extensions.digest(
    convert_to(format('{"brightness":%s}', p_brightness), 'UTF8'),
    'sha256'
  )
$$;

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname = 'api'
      and c.relname in ('collars', 'config_resource_heads', 'config_reported')
      and c.relrowsecurity
  ),
  3::bigint,
  'all brightness read tables retain RLS'
);
select is(
  (
    select count(*)
    from unnest(array[
      'api.collars', 'api.config_resource_heads', 'api.config_reported'
    ]) as table_name
    where has_table_privilege('authenticated', table_name, 'select')
  ),
  3::bigint,
  'authenticated sessions have only the required brightness read grants'
);
select is(
  (
    select count(*)
    from unnest(array[
      'api.collars', 'api.config_resource_heads', 'api.config_reported'
    ]) as table_name
    where has_table_privilege('anon', table_name, 'select')
  ),
  0::bigint,
  'anonymous sessions cannot read brightness state'
);
select ok(
  has_function_privilege(
    'authenticated',
    'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)',
    'execute'
  ),
  'authenticated users retain the existing mutation RPC grant'
);
select ok(
  not has_function_privilege(
    'anon',
    'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)',
    'execute'
  ),
  'anonymous callers cannot execute the mutation RPC'
);
select ok(
  position(
    'for update of c' in lower(pg_get_functiondef(
      'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)'::regprocedure
    ))
  ) < position(
    'select * into v_existing' in lower(pg_get_functiondef(
      'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)'::regprocedure
    ))
  ),
  'the stable active-collar row is locked before replay and optional-head reads'
);
select ok(
  pg_get_functiondef(
    'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)'::regprocedure
  ) like '%invalid_body_sha256%'
  and pg_get_functiondef(
    'api.mutate_config_resource_v1(uuid,text,integer,uuid,bigint,jsonb,bytea)'::regprocedure
  ) like '%25[0-5]%'
  ,
  'the RPC binds the canonical brightness body to an exact SHA-256 digest'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    '19000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'brightness-editor@example.test',
    extensions.crypt('local-brightness-editor-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '19000000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'brightness-viewer@example.test',
    extensions.crypt('local-brightness-viewer-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}', '{}',
    statement_timestamp(), statement_timestamp(), '', '', '', ''
  )
on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values
  ('30000000-0000-4000-8000-000000000003', '19000000-0000-4000-8000-000000000001', 'editor'),
  ('30000000-0000-4000-8000-000000000003', '19000000-0000-4000-8000-000000000002', 'viewer')
on conflict (dog_id, user_id) do update set role = excluded.role;

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at
) values
  (
    '89000000-0000-4000-8000-000000000001',
    '89100000-0000-4000-8000-000000000001',
    '30000000-0000-4000-8000-000000000003',
    'Brightness owner fixture', 'active', '2026-08-25 12:00:00+00'
  ),
  (
    '89000000-0000-4000-8000-000000000002',
    '89100000-0000-4000-8000-000000000002',
    '30000000-0000-4000-8000-000000000003',
    'Brightness editor fixture', 'active', '2026-08-25 11:00:00+00'
  ),
  (
    '89000000-0000-4000-8000-000000000003',
    '89100000-0000-4000-8000-000000000003',
    '30000000-0000-4000-8000-000000000003',
    'Brightness inactive fixture', 'retired', '2026-08-25 10:00:00+00'
  );

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000001', 'brightness', 1,
    '89200000-0000-4000-8000-000000000001', 0,
    '{"brightness":96}'::jsonb, pg_temp.brightness_hash(96)
  ) ->> 'disposition',
  'winning',
  'an owner creates the first brightness head'
);
select is(
  (select server_version from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  1::bigint,
  'the first head has version one'
);
select is(
  (select body ->> 'brightness' from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  '96',
  'the exact one-key body becomes desired state'
);
select is(
  (select body_sha256 from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  pg_temp.brightness_hash(96),
  'the desired head stores the canonical body digest'
);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000001', 'brightness', 1,
    '89200000-0000-4000-8000-000000000001', 0,
    '{"brightness":96}'::jsonb, pg_temp.brightness_hash(96)
  ) ->> 'disposition',
  'winning',
  'an exact winning mutation replay returns its original disposition'
);
select is(
  (select count(*) from api.config_revisions
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  1::bigint,
  'an exact winning replay creates no duplicate receipt'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000001', 0,
      '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '23505', 'mutation_id_conflict',
  'the same mutation ID cannot identify a different canonical body'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000001', 1,
      '{"brightness":96}'::jsonb, pg_temp.brightness_hash(96)
    )
  $$,
  '23505', 'mutation_id_conflict',
  'the replay fingerprint includes base server version'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000002', 1,
      '{"brightness":97}'::jsonb, decode(repeat('00', 32), 'hex')
    )
  $$,
  '22023', 'invalid_body_sha256',
  'a supplied digest must belong to the canonical submitted body'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000003', 1,
      '{"brightness":96.5}'::jsonb, pg_temp.brightness_hash(96)
    )
  $$,
  '22023', 'invalid_brightness',
  'fractional JSON brightness cannot be rounded into the accepted range'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      null, 1, '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '22023', 'invalid_config_resource',
  'a null mutation ID is rejected before any write'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000014', null,
      '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '22023', 'invalid_config_resource',
  'a null base version cannot impersonate an explicit zero base'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000015', -1,
      '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '22023', 'invalid_config_resource',
  'a negative base version is rejected before head inspection'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000016', 1,
      null, pg_temp.brightness_hash(97)
    )
  $$,
  '22023', 'invalid_config_resource',
  'a null body is rejected as a bounded invalid resource'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000017', 1,
      '{"brightness":97}'::jsonb, null
    )
  $$,
  '22023', 'invalid_config_resource',
  'a null digest is rejected before canonical hash comparison'
);
reset role;

set local role authenticated;
select set_config('request.jwt.claim.sub', '19000000-0000-4000-8000-000000000002', true);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000004', 1,
      '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '42501', 'not_authorized',
  'a viewer cannot mutate brightness'
);
reset role;

set local role authenticated;
select set_config('request.jwt.claim.sub', '19000000-0000-4000-8000-000000000001', true);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000002', 'brightness', 1,
    '89200000-0000-4000-8000-000000000005', 0,
    '{"brightness":64}'::jsonb, pg_temp.brightness_hash(64)
  ) ->> 'disposition',
  'winning',
  'an editor can mutate an active collar'
);
reset role;

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000003', 'brightness', 1,
      '89200000-0000-4000-8000-000000000006', 0,
      '{"brightness":64}'::jsonb, pg_temp.brightness_hash(64)
    )
  $$,
  '42501', 'not_authorized',
  'an inactive collar cannot be mutated'
);
reset role;

create temporary table m111_before_noop as
select h.server_version, h.winning_revision_id, h.updated_at,
       s.physical_ms, s.logical
from api.config_resource_heads h
join private.config_hlc_state s on s.collar_id = h.collar_id
where h.collar_id = '89000000-0000-4000-8000-000000000001'
  and h.resource_key = 'brightness';

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000001', 'brightness', 1,
    '89200000-0000-4000-8000-000000000007', 1,
    '{"brightness":96}'::jsonb, pg_temp.brightness_hash(96)
  ) ->> 'disposition',
  'unchanged',
  'a new-ID identical canonical value returns a bounded no-op disposition'
);
reset role;

select is(
  (select server_version from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  1::bigint,
  'a no-op does not advance the desired head version'
);
select is(
  (select winning_revision_id from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  (select winning_revision_id from m111_before_noop),
  'a no-op does not replace the winning revision'
);
select is(
  (select updated_at from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  (select updated_at from m111_before_noop),
  'a no-op does not touch the head timestamp'
);
select is(
  (select row(physical_ms, logical) from private.config_hlc_state
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  (select row(physical_ms, logical) from m111_before_noop),
  'a no-op does not advance the collar HLC'
);
select is(
  (select count(*) from api.config_revisions
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  2::bigint,
  'a no-op persists exactly one idempotency receipt'
);
select is(
  (select disposition || ':' || server_version::text
    from api.config_revisions
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and mutation_id = '89200000-0000-4000-8000-000000000007'),
  'superseded:1',
  'the existing superseded revision shape stores the no-op receipt at the unchanged version'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000001', 'brightness', 1,
    '89200000-0000-4000-8000-000000000007', 1,
    '{"brightness":96}'::jsonb, pg_temp.brightness_hash(96)
  ) ->> 'disposition',
  'unchanged',
  'an exact no-op replay returns the original bounded disposition'
);
reset role;
select is(
  (select count(*) from api.config_revisions
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  2::bigint,
  'an exact no-op replay creates no duplicate receipt'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000007', 1,
      '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
    )
  $$,
  '23505', 'mutation_id_conflict',
  'a no-op mutation ID cannot later identify a different body'
);
select is(
  api.mutate_config_resource_v1(
    '89000000-0000-4000-8000-000000000001', 'brightness', 1,
    '89200000-0000-4000-8000-000000000008', 1,
    '{"brightness":97}'::jsonb, pg_temp.brightness_hash(97)
  ) ->> 'server_version',
  '2',
  'a genuinely changed brightness advances exactly one version'
);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '89000000-0000-4000-8000-000000000001', 'brightness', 1,
      '89200000-0000-4000-8000-000000000009', 1,
      '{"brightness":98}'::jsonb, pg_temp.brightness_hash(98)
    )
  $$,
  'PT409', 'stale_base_server_version',
  'a stale base never overwrites the newer desired value'
);
reset role;
select is(
  (select body ->> 'brightness' from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'
      and resource_key = 'brightness'),
  '97',
  'the stale attempt leaves the winning brightness unchanged'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  (select count(*) from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  0::bigint,
  'a non-member cannot read another dog brightness head through RLS'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '19000000-0000-4000-8000-000000000002', true);
select is(
  (select count(*) from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  1::bigint,
  'a viewer can read the authorized brightness head through RLS'
);
reset role;
set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  (select count(*) from api.config_resource_heads
    where collar_id = '89000000-0000-4000-8000-000000000001'),
  1::bigint,
  'an owner can read the authorized brightness head through RLS'
);
reset role;

select * from finish();
rollback;
