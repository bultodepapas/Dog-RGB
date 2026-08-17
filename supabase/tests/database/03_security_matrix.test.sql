begin;
create extension if not exists pgtap with schema extensions;

select plan(9);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname = 'api' and c.relkind in ('r', 'p') and not c.relrowsecurity
  ),
  0::bigint,
  'every exposed api table has RLS enabled'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname in ('api', 'private') and c.relkind in ('r', 'p')
      and has_table_privilege('anon', c.oid, 'SELECT,INSERT,UPDATE,DELETE,TRUNCATE,REFERENCES,TRIGGER')
  ),
  0::bigint,
  'anonymous has no effective table privilege in cloud schemas'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
    where n.nspname in ('api', 'private') and c.relkind in ('r', 'p')
      and has_table_privilege('authenticated', c.oid, 'INSERT,UPDATE,DELETE,TRUNCATE,REFERENCES,TRIGGER')
  ),
  0::bigint,
  'authenticated users have no table-wide write privileges'
);

select ok(
  has_column_privilege('authenticated', 'api.profiles', 'display_name', 'UPDATE')
  and has_column_privilege('authenticated', 'api.profiles', 'default_timezone', 'UPDATE')
  and has_column_privilege('authenticated', 'api.profiles', 'units', 'UPDATE'),
  'profile self-service is restricted to the intended columns'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_proc p
    join pg_catalog.pg_namespace n on n.oid = p.pronamespace
    where n.nspname in ('api', 'private')
      and has_function_privilege('anon', p.oid, 'EXECUTE')
  ),
  0::bigint,
  'anonymous cannot execute any api or private function'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_proc p
    join pg_catalog.pg_namespace n on n.oid = p.pronamespace
    where n.nspname in ('api', 'private')
      and p.proname in (
        'issue_device_claim_v1', 'consume_device_claim_v1', 'device_sync_v1', 'device_sync_gateway_v1',
        'device_revoke_v1', 'recompute_dirty_summaries_v1', 'secure_digest_equal'
      )
      and has_function_privilege('authenticated', p.oid, 'EXECUTE')
  ),
  0::bigint,
  'authenticated users cannot execute device or worker transaction functions'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_proc p
    join pg_catalog.pg_namespace n on n.oid = p.pronamespace
    where n.nspname in ('api', 'private') and p.prosecdef
      and not coalesce(p.proconfig, '{}'::text[]) @> array['search_path=""']
  ),
  0::bigint,
  'every SECURITY DEFINER function fixes an empty search_path'
);

select is(
  (
    select count(*)
    from pg_catalog.pg_views v
    where v.schemaname = 'api'
  ),
  0::bigint,
  'the foundation exposes no view that could bypass RLS'
);

set local role anon;
select throws_ok(
  $$ select count(*) from api.dogs $$,
  '42501',
  null,
  'anonymous Data API access fails before row filtering'
);

select * from finish();
rollback;
