\set ON_ERROR_STOP on

begin;

create temporary table restore_manifest_tables (
  relation_name text primary key,
  row_count bigint not null,
  content_sha256 text not null
) on commit drop;

do $$
declare
  relation_row record;
  relation_count bigint;
  relation_hash text;
begin
  for relation_row in
    select namespace.nspname as schema_name, relation.relname as table_name
    from pg_class relation
    join pg_namespace namespace on namespace.oid = relation.relnamespace
    where namespace.nspname in ('api', 'private')
      and relation.relkind in ('r', 'p')
    order by namespace.nspname, relation.relname
  loop
    execute format('select count(*) from %I.%I', relation_row.schema_name, relation_row.table_name)
      into relation_count;
    execute format(
      $query$
        select encode(
          extensions.digest(
            coalesce(string_agg(row_text, E'\n' order by row_text), ''),
            'sha256'
          ),
          'hex'
        )
        from (
          select to_jsonb(table_row)::text as row_text
          from %I.%I table_row
        ) rows_to_hash
      $query$,
      relation_row.schema_name,
      relation_row.table_name
    ) into relation_hash;

    insert into restore_manifest_tables (relation_name, row_count, content_sha256)
    values (
      format('%I.%I', relation_row.schema_name, relation_row.table_name),
      relation_count,
      relation_hash
    );
  end loop;
end
$$;

select jsonb_build_object(
  'manifest_version', 1,
  'table_counts', (
    select jsonb_object_agg(relation_name, row_count order by relation_name)
    from restore_manifest_tables
  ),
  'table_hashes', (
    select jsonb_object_agg(relation_name, content_sha256 order by relation_name)
    from restore_manifest_tables
  ),
  'auth', jsonb_build_object(
    'users', (select count(*) from auth.users),
    'identities', (select count(*) from auth.identities),
    'profiles_without_user', (
      select count(*)
      from api.profiles profile
      left join auth.users auth_user on auth_user.id = profile.user_id
      where auth_user.id is null
    ),
    'dogs_without_creator', (
      select count(*)
      from api.dogs dog
      left join auth.users auth_user on auth_user.id = dog.created_by
      where auth_user.id is null
    ),
    'memberships_without_user', (
      select count(*)
      from api.dog_memberships membership
      left join auth.users auth_user on auth_user.id = membership.user_id
      where auth_user.id is null
    ),
    'content_sha256', (
      select encode(
        extensions.digest(
          coalesce(string_agg(row_text, E'\n' order by row_text), ''),
          'sha256'
        ),
        'hex'
      )
      from (
        select 'users:' || to_jsonb(auth_user)::text as row_text from auth.users auth_user
        union all
        select 'identities:' || to_jsonb(identity_row)::text from auth.identities identity_row
      ) auth_rows
    )
  ),
  'route_sha256', (
    select encode(
      extensions.digest(
        coalesce(string_agg(to_jsonb(point_row)::text, E'\n' order by
          point_row.collar_id, point_row.boot_sequence, point_row.point_sequence), ''),
        'sha256'
      ),
      'hex'
    )
    from api.telemetry_points point_row
  ),
  'config_heads_sha256', (
    select encode(
      extensions.digest(
        coalesce(string_agg(to_jsonb(head_row)::text, E'\n' order by
          head_row.collar_id, head_row.resource_key), ''),
        'sha256'
      ),
      'hex'
    )
    from api.config_resource_heads head_row
  ),
  'rls_sha256', (
    select encode(
      extensions.digest(
        coalesce(string_agg(catalog_row, E'\n' order by catalog_row), ''),
        'sha256'
      ),
      'hex'
    )
    from (
      select to_jsonb(row_value)::text as catalog_row
      from (
        select
          namespace.nspname as schema_name,
          relation.relname as relation_name,
          relation.relrowsecurity,
          relation.relforcerowsecurity,
          concat_ws(',',
            has_table_privilege('anon', relation.oid, 'select')::text,
            has_table_privilege('anon', relation.oid, 'insert')::text,
            has_table_privilege('anon', relation.oid, 'update')::text,
            has_table_privilege('anon', relation.oid, 'delete')::text,
            has_table_privilege('authenticated', relation.oid, 'select')::text,
            has_table_privilege('authenticated', relation.oid, 'insert')::text,
            has_table_privilege('authenticated', relation.oid, 'update')::text,
            has_table_privilege('authenticated', relation.oid, 'delete')::text,
            has_table_privilege('service_role', relation.oid, 'select')::text,
            has_table_privilege('service_role', relation.oid, 'insert')::text,
            has_table_privilege('service_role', relation.oid, 'update')::text,
            has_table_privilege('service_role', relation.oid, 'delete')::text,
            has_table_privilege('postgres', relation.oid, 'select')::text,
            has_table_privilege('postgres', relation.oid, 'insert')::text,
            has_table_privilege('postgres', relation.oid, 'update')::text,
            has_table_privilege('postgres', relation.oid, 'delete')::text
          ) as effective_privileges
        from pg_class relation
        join pg_namespace namespace on namespace.oid = relation.relnamespace
        where namespace.nspname in ('api', 'private')
          and relation.relkind in ('r', 'p', 'v', 'm')
        union all
        select
          policy.schemaname,
          policy.tablename || ':' || policy.policyname,
          false,
          false,
          concat_ws('|', policy.permissive, policy.roles::text, policy.cmd,
            coalesce(policy.qual, ''), coalesce(policy.with_check, ''))
        from pg_policies policy
        where policy.schemaname in ('api', 'private')
      ) row_value
    ) security_catalog
  ),
  'functions_sha256', (
    select encode(
      extensions.digest(
        coalesce(string_agg(function_row, E'\n' order by function_row), ''),
        'sha256'
      ),
      'hex'
    )
    from (
      select concat_ws('|',
        namespace.nspname,
        procedure.proname,
        pg_get_function_identity_arguments(procedure.oid),
        procedure.prosecdef::text,
        coalesce(procedure.proacl::text, ''),
        coalesce(procedure.proconfig::text, ''),
        pg_get_functiondef(procedure.oid)
      ) as function_row
      from pg_proc procedure
      join pg_namespace namespace on namespace.oid = procedure.pronamespace
      where namespace.nspname in ('api', 'private')
    ) functions_to_hash
  ),
  'migrations_sha256', (
    select encode(
      extensions.digest(
        coalesce(string_agg(version || ':' || coalesce(name, ''), E'\n' order by version), ''),
        'sha256'
      ),
      'hex'
    )
    from supabase_migrations.schema_migrations
  ),
  'extensions', (
    select jsonb_object_agg(extension.extname, extension.extversion order by extension.extname)
    from pg_extension extension
  )
)::text;

rollback;
