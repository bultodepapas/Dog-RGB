begin;
create extension if not exists pgtap with schema extensions;

select plan(18);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname = 'api'
      and c.relname in ('collars', 'recordings')
      and c.relrowsecurity
  ),
  2::bigint,
  'both History product tables have RLS enabled'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings']) as table_name
    where has_table_privilege('authenticated', table_name, 'select')
  ),
  2::bigint,
  'authenticated sessions can select both History tables'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings']) as table_name
    where has_table_privilege('anon', table_name, 'select')
  ),
  0::bigint,
  'anonymous sessions have no History table read grant'
);
select is(
  (
    select count(*)
    from unnest(array['api.collars', 'api.recordings']) as table_name
    where has_table_privilege(
      'authenticated', table_name,
      'insert,update,delete,truncate,references,trigger'
    )
  ),
  0::bigint,
  'History adds no authenticated table-wide write grant'
);
select is(
  pg_get_indexdef('api.recordings_history_started_id_idx'::regclass),
  'CREATE INDEX recordings_history_started_id_idx ON api.recordings USING btree (started_at DESC NULLS LAST, id DESC)',
  'History index matches the measured global keyset order without covering columns'
);

insert into api.dog_memberships (dog_id, user_id, role) values (
  '30000000-0000-4000-8000-000000000003',
  '20000000-0000-4000-8000-000000000002',
  'viewer'
);

insert into api.collars (
  id, device_public_id, dog_id, display_name, state, linked_at
) values
  (
    '71000000-0000-4000-8000-000000000001',
    '72000000-0000-4000-8000-000000000001',
    '30000000-0000-4000-8000-000000000003',
    'Collar activo', 'active', '2026-08-20 12:00:00+00'
  ),
  (
    '71000000-0000-4000-8000-000000000002',
    '72000000-0000-4000-8000-000000000002',
    '30000000-0000-4000-8000-000000000003',
    null, 'revoked', '2026-08-19 12:00:00+00'
  );

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version, created_at
)
select
  ('73000000-0000-4000-8000-' || lpad(i::text, 12, '0'))::uuid,
  case when i % 2 = 0
    then '71000000-0000-4000-8000-000000000001'::uuid
    else '71000000-0000-4000-8000-000000000002'::uuid
  end,
  i,
  case when i <= 22
    then '2026-08-25 10:00:00+00'::timestamptz - (((i - 1) / 4) * interval '1 minute')
    else null
  end,
  'America/Bogota',
  case when i % 4 = 0 then 'incomplete' else 'closed' end,
  i * 10,
  case when i <= 22 then 'gnss_trusted' else 'unknown' end,
  3,
  'history-fixture',
  '2026-08-25 11:00:00+00'::timestamptz + (i * interval '1 second')
from generate_series(1, 25) as fixture(i);

select is(
  (
    select count(distinct c.state)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  2::bigint,
  'History contains recordings from active and revoked collars'
);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
      and c.display_name is null
  ),
  13::bigint,
  'nullable collar names remain available for the bounded UI fallback'
);
select is(
  (
    select array_agg(page.id order by page.ordinality)
    from (
      select r.id, row_number() over (
        order by r.started_at desc nulls last, r.id desc
      ) as ordinality
      from api.recordings r
      join api.collars c on c.id = r.collar_id
      where c.dog_id = '30000000-0000-4000-8000-000000000003'
      order by r.started_at desc nulls last, r.id desc
      limit 6
    ) page
  ),
  array[
    '73000000-0000-4000-8000-000000000004',
    '73000000-0000-4000-8000-000000000003',
    '73000000-0000-4000-8000-000000000002',
    '73000000-0000-4000-8000-000000000001',
    '73000000-0000-4000-8000-000000000008',
    '73000000-0000-4000-8000-000000000007'
  ]::uuid[],
  'known timestamps and equal-time UUID ties have exact global order'
);
select is(
  (
    select array_agg(page.id order by page.started_at desc nulls last, page.id desc)
    from (
      select r.id, r.started_at
      from api.recordings r
      join api.collars c on c.id = r.collar_id
      where c.dog_id = '30000000-0000-4000-8000-000000000003'
        and r.started_at is null
    ) page
  ),
  array[
    '73000000-0000-4000-8000-000000000025',
    '73000000-0000-4000-8000-000000000024',
    '73000000-0000-4000-8000-000000000023'
  ]::uuid[],
  'null-time bucket is ordered only by UUID descending'
);
select is(
  (
    select count(*)
    from (
      select r.id
      from api.recordings r
      join api.collars c on c.id = r.collar_id
      where c.dog_id = '30000000-0000-4000-8000-000000000003'
      order by r.started_at desc nulls last, r.id desc
      limit 21
    ) page
  ),
  21::bigint,
  'first History query is bounded to the 20 rows plus one lookahead'
);
select is(
  (
    select page.id
    from (
      select r.id
      from api.recordings r
      join api.collars c on c.id = r.collar_id
      where c.dog_id = '30000000-0000-4000-8000-000000000003'
      order by r.started_at desc nulls last, r.id desc
      limit 21
    ) page
    offset 20
  ),
  '73000000-0000-4000-8000-000000000022'::uuid,
  'the 21st row is lookahead evidence and not the cursor row'
);
select is(
  (
    select array_agg(page.id order by page.started_at desc nulls last, page.id desc)
    from (
      select r.id, r.started_at
      from api.recordings r
      join api.collars c on c.id = r.collar_id
      where c.dog_id = '30000000-0000-4000-8000-000000000003'
        and (
          r.started_at < '2026-08-25 09:56:00+00'
          or (
            r.started_at = '2026-08-25 09:56:00+00'
            and r.id < '73000000-0000-4000-8000-000000000017'
          )
          or r.started_at is null
        )
      order by r.started_at desc nulls last, r.id desc
      limit 21
    ) page
  ),
  array[
    '73000000-0000-4000-8000-000000000022',
    '73000000-0000-4000-8000-000000000021',
    '73000000-0000-4000-8000-000000000025',
    '73000000-0000-4000-8000-000000000024',
    '73000000-0000-4000-8000-000000000023'
  ]::uuid[],
  'known cursor predicate crosses once into the null-time bucket without skips'
);
select is(
  (
    select array_agg(r.id order by r.id desc)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
      and r.started_at is null
      and r.id < '73000000-0000-4000-8000-000000000024'
  ),
  array['73000000-0000-4000-8000-000000000023']::uuid[],
  'unknown cursor predicate stays inside the null-time bucket'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  25::bigint,
  'owner sees the exact authorized History ledger'
);

select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  25::bigint,
  'viewer has the same read-only History visibility'
);

select set_config('request.jwt.claim.sub', '90000000-0000-4000-8000-000000000009', true);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
  ),
  0::bigint,
  'non-member sees no cross-dog History row'
);
select is(
  (
    select count(*)
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = '30000000-0000-4000-8000-000000000003'
      and r.id = '73000000-0000-4000-8000-000000000004'
      and r.collar_id = '71000000-0000-4000-8000-000000000001'
  ),
  0::bigint,
  'forged dog, collar, and recording filters do not bypass RLS'
);

reset role;
set local role anon;
select throws_ok(
  $$ select count(*) from api.recordings $$,
  '42501',
  null,
  'anonymous History read fails before row filtering'
);

select * from finish();
rollback;
