begin;
create extension if not exists pgtap with schema extensions;

select plan(32);

create function pg_temp.device_mutation(
  p_id uuid,
  p_sequence bigint,
  p_resource_key text,
  p_body jsonb,
  p_physical_ms bigint,
  p_logical bigint,
  p_quality text,
  p_device_id uuid
)
returns jsonb
language sql
as $$
  select jsonb_build_object(
    'mutation_id', p_id,
    'local_sequence', p_sequence,
    'resource_key', p_resource_key,
    'resource_schema', 1,
    'base_server_version', 0,
    'authored_hlc', jsonb_build_object(
      'physical_ms', p_physical_ms,
      'logical', p_logical,
      'actor_id', p_device_id
    ),
    'time_quality', p_quality,
    'origin', 'ap',
    'body', p_body,
    'body_sha256', private.base64url_encode(extensions.digest(p_body::text, 'sha256'))
  )
$$;

select ok(
  not has_table_privilege('service_role', 'private.config_hlc_state', 'select'),
  'the collar-wide server clock is not exposed to service role'
);
select ok(
  not has_function_privilege('service_role', 'private.advance_config_hlc_v1(uuid,bigint,bigint,bigint)', 'execute'),
  'the HLC transition primitive is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.normalize_device_mutations_v1(jsonb,bigint)', 'execute'),
  'the mutation trust classifier is not an exposed RPC'
);

insert into api.collars (id, device_public_id, dog_id, state)
values
  (
    '80000000-0000-4000-8000-000000000001',
    'f1111111-1111-4111-8111-111111111111',
    '30000000-0000-4000-8000-000000000003', 'active'
  ),
  (
    '80000000-0000-4000-8000-000000000002',
    'f2222222-2222-4222-8222-222222222222',
    '30000000-0000-4000-8000-000000000003', 'active'
  ),
  (
    '80000000-0000-4000-8000-000000000003',
    'f3333333-3333-4333-8333-333333333333',
    '30000000-0000-4000-8000-000000000003', 'active'
  ),
  (
    '80000000-0000-4000-8000-000000000004',
    'f4444444-4444-4444-8444-444444444444',
    '30000000-0000-4000-8000-000000000003', 'active'
  );

select is(
  private.normalize_device_mutations_v1(
    jsonb_build_array(pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000001', 9, 'brightness', '{"brightness":90}',
      1600001, 0, 'sntp_synced', 'f1111111-1111-4111-8111-111111111111'
    )),
    1000000
  ) #>> '{0,time_quality}',
  'unknown',
  'trusted-labelled time beyond the inclusive skew window enters fallback ordering'
);
select is(
  private.normalize_device_mutations_v1(
    jsonb_build_array(pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000002', 9, 'brightness', '{"brightness":90}',
      1600000, 0, 'sntp_synced', 'f1111111-1111-4111-8111-111111111111'
    )),
    1000000
  ) #>> '{0,time_quality}',
  'sntp_synced',
  'the exact positive skew boundary remains authored ordering'
);
select is(
  private.normalize_device_mutations_v1(
    jsonb_build_array(pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000003', 9, 'brightness', '{"brightness":90}',
      1600001, 0, 'gnss_trusted', 'f1111111-1111-4111-8111-111111111111'
    )),
    1000000
  ) #>> '{0,submitted_time_quality}',
  'gnss_trusted',
  'fallback classification retains the submitted quality for audit'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select lives_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000001', 'brightness', 1,
      '82000000-0000-4000-8000-000000000001', 0,
      '{"brightness":80}'::jsonb, decode(repeat('21', 32), 'hex')
    )
  $$,
  'a current web form creates the first resource head'
);
reset role;

select is(
  (
    private.apply_device_mutation_v1(
      '80000000-0000-4000-8000-000000000001',
      'f1111111-1111-4111-8111-111111111111',
      pg_temp.device_mutation(
        '81000000-0000-4000-8000-000000000011', 1, 'brightness', '{"brightness":100}',
        (select accepted_hlc_physical_ms + 1 from api.config_resource_heads
          where collar_id = '80000000-0000-4000-8000-000000000001' and resource_key = 'brightness'),
        0, 'sntp_synced', 'f1111111-1111-4111-8111-111111111111'
      ),
      (select accepted_hlc_physical_ms + 1 from api.config_resource_heads
        where collar_id = '80000000-0000-4000-8000-000000000001' and resource_key = 'brightness')
    ) ->> 'disposition'
  ),
  'winning',
  'a later trusted AP mutation wins over the web head'
);
select is(
  (select body ->> 'brightness' from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000001' and resource_key = 'brightness'),
  '100',
  'web then trusted AP converges to the AP body'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select lives_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000001', 'brightness', 1,
      '82000000-0000-4000-8000-000000000002', 2,
      '{"brightness":70}'::jsonb, decode(repeat('22', 32), 'hex')
    )
  $$,
  'a fresh web form can deliberately supersede the AP head'
);
reset role;
select is(
  (select body ->> 'brightness' from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000001' and resource_key = 'brightness'),
  '70',
  'trusted AP then web converges to the later web body'
);
select is(
  (select server_version from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000001' and resource_key = 'brightness'),
  3::bigint,
  'server_version increments once per winning revision'
);

select is(
  private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000001',
    'f1111111-1111-4111-8111-111111111111',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000012', 2, 'gps_quality',
      '{"min_fix_quality":1}'::jsonb, 0, 0, 'unknown',
      'f1111111-1111-4111-8111-111111111111'
    ),
    floor(extract(epoch from clock_timestamp()) * 1000)::bigint
  ) ->> 'disposition',
  'winning',
  'an unrelated AP resource wins independently'
);
select is(
  (select count(*) from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000001'),
  2::bigint,
  'brightness and GPS heads survive as separate LWW registers'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select lives_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000002', 'brightness', 1,
      '82000000-0000-4000-8000-000000000003', 0,
      '{"brightness":80}'::jsonb, decode(repeat('23', 32), 'hex')
    )
  $$,
  'the actor tie-break scenario starts with a web head'
);
reset role;

select is(
  private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000002',
    'f2222222-2222-4222-8222-222222222222',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000013', 1, 'brightness', '{"brightness":90}',
      (select accepted_hlc_physical_ms from api.config_resource_heads
        where collar_id = '80000000-0000-4000-8000-000000000002' and resource_key = 'brightness'),
      (select accepted_hlc_logical from api.config_resource_heads
        where collar_id = '80000000-0000-4000-8000-000000000002' and resource_key = 'brightness'),
      'gnss_trusted', 'f2222222-2222-4222-8222-222222222222'
    ),
    (select accepted_hlc_physical_ms from api.config_resource_heads
      where collar_id = '80000000-0000-4000-8000-000000000002' and resource_key = 'brightness')
  ) ->> 'disposition',
  'winning',
  'actor UUID bytes deterministically break an equal physical/logical tie'
);
select is(
  (select accepted_actor_id from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000002' and resource_key = 'brightness'),
  'f2222222-2222-4222-8222-222222222222'::uuid,
  'the lexicographically larger actor owns the tied head'
);

select is(
  private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000002',
    'f2222222-2222-4222-8222-222222222222',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000014', 2, 'brightness', '{"brightness":120}',
      0, 1, 'unknown', 'f2222222-2222-4222-8222-222222222222'
    ),
    floor(extract(epoch from clock_timestamp()) * 1000)::bigint
  ) #>> '{accepted_hlc,actor_id}',
  '00000000-0000-4000-8000-0000000000ff',
  'unknown-time fallback uses the normative server actor'
);
select is(
  (select body ->> 'brightness' from api.config_resource_heads
    where collar_id = '80000000-0000-4000-8000-000000000002' and resource_key = 'brightness'),
  '120',
  'an unknown-time AP edit accepted after a web edit wins at receipt time'
);
select is(
  (select ordering_mode from api.config_revisions
    where collar_id = '80000000-0000-4000-8000-000000000002'
      and mutation_id = '81000000-0000-4000-8000-000000000014'),
  'fallback_received',
  'fallback ordering remains visible in the audit revision'
);

select is(
  private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000003',
    'f3333333-3333-4333-8333-333333333333',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000021', 7, 'brightness', '{"brightness":101}',
      0, 0, 'unknown', 'f3333333-3333-4333-8333-333333333333'
    ), 2000
  ) #>> '{accepted_hlc,logical}',
  '0',
  'the first fallback mutation starts the collar server clock'
);
select is(
  private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000003',
    'f3333333-3333-4333-8333-333333333333',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000022', 8, 'gps_quality',
      '{"min_fix_quality":1}'::jsonb, 0, 0, 'unknown',
      'f3333333-3333-4333-8333-333333333333'
    ), 2000
  ) #>> '{accepted_hlc,logical}',
  '1',
  'fallback HLC increases across different resource heads'
);
select is(
  (private.apply_device_mutation_v1(
    '80000000-0000-4000-8000-000000000003',
    'f3333333-3333-4333-8333-333333333333',
    pg_temp.device_mutation(
      '81000000-0000-4000-8000-000000000022', 8, 'gps_quality',
      '{"min_fix_quality":1}'::jsonb, 0, 0, 'unknown',
      'f3333333-3333-4333-8333-333333333333'
    ), 3000
  ) ->> 'replayed'),
  'true',
  'an exact mutation replay returns the persisted outcome before ticking'
);
select is(
  (select logical from private.config_hlc_state
    where collar_id = '80000000-0000-4000-8000-000000000003'),
  1::bigint,
  'an exact mutation replay does not advance the collar server clock'
);
select is(
  (select count(*) from api.config_revisions
    where collar_id = '80000000-0000-4000-8000-000000000003'),
  2::bigint,
  'an exact mutation replay creates no second revision'
);
select throws_ok(
  $$
    select private.apply_device_mutation_v1(
      '80000000-0000-4000-8000-000000000003',
      'f3333333-3333-4333-8333-333333333333',
      jsonb_set(
        pg_temp.device_mutation(
          '81000000-0000-4000-8000-000000000022', 8, 'gps_quality',
          '{"min_fix_quality":2}'::jsonb, 0, 0, 'unknown',
          'f3333333-3333-4333-8333-333333333333'
        ),
        '{body_sha256}', to_jsonb(repeat('A', 43))
      ), 3000
    )
  $$,
  '23505', 'mutation_id_conflict',
  'a mutation identity cannot be replayed with another body hash'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select lives_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000004', 'brightness', 1,
      '82000000-0000-4000-8000-000000000011', 0,
      '{"brightness":60}'::jsonb, decode(repeat('31', 32), 'hex')
    )
  $$,
  'the first sequential web mutation succeeds'
);
select lives_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000004', 'brightness', 1,
      '82000000-0000-4000-8000-000000000012', 1,
      '{"brightness":61}'::jsonb, decode(repeat('32', 32), 'hex')
    )
  $$,
  'the second sequential web mutation succeeds'
);
reset role;
select ok(
  (
    select row(newer.accepted_hlc_physical_ms, newer.accepted_hlc_logical, newer.accepted_actor_id)
      > row(older.accepted_hlc_physical_ms, older.accepted_hlc_logical, older.accepted_actor_id)
    from api.config_revisions older
    join api.config_revisions newer on newer.collar_id = older.collar_id
    where older.mutation_id = '82000000-0000-4000-8000-000000000011'
      and newer.mutation_id = '82000000-0000-4000-8000-000000000012'
  ),
  'sequential web writes receive strictly increasing HLCs even inside one millisecond'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select throws_ok(
  $$
    select api.mutate_config_resource_v1(
      '80000000-0000-4000-8000-000000000004', 'brightness', 1,
      '82000000-0000-4000-8000-000000000013', 0,
      '{"brightness":62}'::jsonb, decode(repeat('33', 32), 'hex')
    )
  $$,
  '40001', 'stale_base_server_version',
  'a stale web form is rejected before it can become an LWW mutation'
);
reset role;
select is(
  (select count(*) from api.config_revisions
    where collar_id = '80000000-0000-4000-8000-000000000004'),
  2::bigint,
  'a stale web form leaves no revision behind'
);

insert into private.config_hlc_state (collar_id, physical_ms, logical)
values ('80000000-0000-4000-8000-000000000004', 4102444800000, 4294967295)
on conflict (collar_id) do update
set physical_ms = excluded.physical_ms, logical = excluded.logical;
select throws_ok(
  $$
    select private.advance_config_hlc_v1(
      '80000000-0000-4000-8000-000000000004', 4102444800000
    )
  $$,
  '22003', 'hlc_logical_overflow',
  'the server clock fails closed instead of wrapping at the wire maximum'
);

select * from finish();
rollback;
