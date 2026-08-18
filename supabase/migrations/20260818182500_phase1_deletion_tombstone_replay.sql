-- Coordinate-free deletion tombstone export and fail-closed restore replay.
-- Export storage/signing remains an external operations responsibility; these
-- primitives deliberately expose no dog content, coordinates, or user PII.

-- The original tombstone digest renders timestamptz as text. Pinning UTC makes
-- new digests independent of the caller's session timezone and lets restores
-- verify them deterministically.
alter function api.request_dog_deletion_v1(uuid, uuid, text)
  set timezone = 'UTC';

create or replace function private.deletion_tombstone_sha256_v1(
  p_scope_id uuid,
  p_request_id uuid,
  p_requested_at timestamptz,
  p_request_sha256 bytea
)
returns bytea
language sql
stable
strict
set search_path = ''
set timezone = 'UTC'
as $$
  select extensions.digest(
    convert_to(concat_ws('|', 'dog-tombstone-v1', p_scope_id::text,
      p_request_id::text, p_requested_at::text,
      private.base64url_encode(p_request_sha256)), 'utf8'),
    'sha256'
  )
$$;

revoke execute on function private.deletion_tombstone_sha256_v1(
  uuid, uuid, timestamptz, bytea
) from public, anon, authenticated, service_role;

create or replace function private.deletion_replay_sha256_v1(
  p_request_id uuid,
  p_request_sha256 bytea,
  p_scope_id uuid,
  p_confirmation_version text,
  p_requested_by_sha256 bytea,
  p_requested_at timestamptz,
  p_tombstone_sha256 bytea
)
returns bytea
language sql
immutable
strict
set search_path = ''
as $$
  select extensions.digest(
    convert_to(concat_ws('|',
      'dog-deletion-tombstone-export-v1',
      p_request_id::text,
      private.base64url_encode(p_request_sha256),
      'dog',
      p_scope_id::text,
      p_confirmation_version,
      private.base64url_encode(p_requested_by_sha256),
      to_char(p_requested_at at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
      private.base64url_encode(p_tombstone_sha256)
    ), 'utf8'),
    'sha256'
  )
$$;

revoke execute on function private.deletion_replay_sha256_v1(
  uuid, bytea, uuid, text, bytea, timestamptz, bytea
) from public, anon, authenticated, service_role;

create or replace function private.export_deletion_tombstones_v1(
  p_after_requested_at timestamptz default null,
  p_after_request_id uuid default null,
  p_limit integer default 100
)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
set timezone = 'UTC'
as $$
declare
  v_result jsonb;
begin
  if p_limit not between 1 and 1000 then
    raise exception using errcode = '22023', message = 'invalid_tombstone_export_limit';
  end if;
  if (p_after_requested_at is null) <> (p_after_request_id is null) then
    raise exception using errcode = '22023', message = 'invalid_tombstone_export_cursor';
  end if;

  with candidates as (
    select tombstone.*
    from private.deletion_tombstones tombstone
    where p_after_requested_at is null
       or (tombstone.requested_at, tombstone.request_id)
          > (p_after_requested_at, p_after_request_id)
    order by tombstone.requested_at, tombstone.request_id
    limit p_limit + 1
  ), page as (
    select *
    from candidates
    order by requested_at, request_id
    limit p_limit
  ), encoded as (
    select
      requested_at,
      request_id,
      jsonb_build_object(
        'schema_version', 'dog-deletion-tombstone-v1',
        'request_id', request_id,
        'request_sha256', private.base64url_encode(request_sha256),
        'scope', scope,
        'scope_id', scope_id,
        'confirmation_version', confirmation_version,
        'requested_by_sha256', private.base64url_encode(requested_by_sha256),
        'requested_at', to_char(
          requested_at at time zone 'UTC',
          'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'
        ),
        'tombstone_sha256', private.base64url_encode(tombstone_sha256),
        'replay_sha256', private.base64url_encode(
          private.deletion_replay_sha256_v1(
            request_id, request_sha256, scope_id, confirmation_version,
            requested_by_sha256, requested_at, tombstone_sha256
          )
        )
      ) as item
    from page
  )
  select jsonb_build_object(
    'schema_version', 'dog-deletion-tombstone-export-v1',
    'items', coalesce(
      (select jsonb_agg(item order by requested_at, request_id) from encoded),
      '[]'::jsonb
    ),
    'has_more', (select count(*) > p_limit from candidates),
    'next_cursor', (
      select jsonb_build_object(
        'requested_at', to_char(
          requested_at at time zone 'UTC',
          'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'
        ),
        'request_id', request_id
      )
      from page
      order by requested_at desc, request_id desc
      limit 1
    )
  ) into v_result;

  return v_result;
end
$$;

revoke execute on function private.export_deletion_tombstones_v1(
  timestamptz, uuid, integer
) from public, anon, authenticated;
grant execute on function private.export_deletion_tombstones_v1(
  timestamptz, uuid, integer
) to service_role;

create or replace function private.replay_dog_deletion_tombstone_v1(
  p_tombstone jsonb
)
returns jsonb
language plpgsql
security definer
set search_path = ''
set timezone = 'UTC'
as $$
declare
  v_request_id uuid;
  v_request_sha256 bytea;
  v_scope_id uuid;
  v_confirmation_version text;
  v_requested_by_sha256 bytea;
  v_requested_at timestamptz;
  v_tombstone_sha256 bytea;
  v_replay_sha256 bytea;
  v_expected_request_sha256 bytea;
  v_existing record;
  v_tombstone_id uuid;
  v_job_id uuid;
  v_initial_counts jsonb;
begin
  if p_tombstone is null or jsonb_typeof(p_tombstone) <> 'object'
     or (select count(*) from jsonb_object_keys(p_tombstone)) <> 10
     or not p_tombstone ?& array[
       'schema_version', 'request_id', 'request_sha256', 'scope', 'scope_id',
       'confirmation_version', 'requested_by_sha256', 'requested_at',
       'tombstone_sha256', 'replay_sha256'
     ] then
    raise exception using errcode = '22023', message = 'invalid_deletion_tombstone';
  end if;

  begin
    v_request_id := (p_tombstone ->> 'request_id')::uuid;
    v_request_sha256 := private.base64url_decode(p_tombstone ->> 'request_sha256');
    v_scope_id := (p_tombstone ->> 'scope_id')::uuid;
    v_confirmation_version := p_tombstone ->> 'confirmation_version';
    v_requested_by_sha256 := private.base64url_decode(
      p_tombstone ->> 'requested_by_sha256'
    );
    v_requested_at := (p_tombstone ->> 'requested_at')::timestamptz;
    v_tombstone_sha256 := private.base64url_decode(
      p_tombstone ->> 'tombstone_sha256'
    );
    v_replay_sha256 := private.base64url_decode(p_tombstone ->> 'replay_sha256');
  exception when others then
    raise exception using errcode = '22023', message = 'invalid_deletion_tombstone';
  end;

  if p_tombstone ->> 'schema_version' <> 'dog-deletion-tombstone-v1'
     or p_tombstone ->> 'scope' <> 'dog'
     or v_confirmation_version <> 'dog-delete-v1'
     or octet_length(v_request_sha256) <> 32
     or octet_length(v_requested_by_sha256) <> 32
     or octet_length(v_tombstone_sha256) <> 32
     or octet_length(v_replay_sha256) <> 32
     or private.base64url_encode(v_request_sha256)
        <> p_tombstone ->> 'request_sha256'
     or private.base64url_encode(v_requested_by_sha256)
        <> p_tombstone ->> 'requested_by_sha256'
     or private.base64url_encode(v_tombstone_sha256)
        <> p_tombstone ->> 'tombstone_sha256'
     or private.base64url_encode(v_replay_sha256)
        <> p_tombstone ->> 'replay_sha256'
     or to_char(v_requested_at at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"')
        <> p_tombstone ->> 'requested_at' then
    raise exception using errcode = '22023', message = 'invalid_deletion_tombstone';
  end if;

  v_expected_request_sha256 := extensions.digest(
    convert_to(concat_ws('|', 'dog-delete-v1', v_scope_id::text,
      v_confirmation_version), 'utf8'),
    'sha256'
  );
  if not private.secure_digest_equal(
       v_request_sha256, v_expected_request_sha256
     )
     or not private.secure_digest_equal(
       v_tombstone_sha256,
       private.deletion_tombstone_sha256_v1(
         v_scope_id, v_request_id, v_requested_at, v_request_sha256
       )
     )
     or not private.secure_digest_equal(
       v_replay_sha256,
       private.deletion_replay_sha256_v1(
         v_request_id, v_request_sha256, v_scope_id, v_confirmation_version,
         v_requested_by_sha256, v_requested_at, v_tombstone_sha256
       )
     ) then
    raise exception using errcode = '22023', message = 'invalid_deletion_tombstone_hash';
  end if;

  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(v_request_id::text, 1146113073)
  );

  select job.id as job_id, tombstone.*
  into v_existing
  from private.deletion_tombstones tombstone
  join private.deletion_jobs job on job.tombstone_id = tombstone.id
  where tombstone.request_id = v_request_id;

  if found then
    if v_existing.scope <> 'dog'
       or v_existing.scope_id <> v_scope_id
       or v_existing.confirmation_version <> v_confirmation_version
       or v_existing.requested_at <> v_requested_at
       or not private.secure_digest_equal(
         v_existing.request_sha256, v_request_sha256
       )
       or not private.secure_digest_equal(
         v_existing.requested_by_sha256, v_requested_by_sha256
       )
       or not private.secure_digest_equal(
         v_existing.tombstone_sha256, v_tombstone_sha256
       ) then
      raise exception using errcode = '23505', message = 'deletion_tombstone_conflict';
    end if;
    return private.deletion_job_result_v1(v_existing.job_id)
      || jsonb_build_object('disposition', 'already_present');
  end if;

  -- A valid tombstone can only be applied to a restore point where the scope
  -- still exists. If neither active data nor the original tombstone exists,
  -- stop traffic activation and investigate the restore/export pairing.
  perform 1
  from api.dogs
  where id = v_scope_id
    and deleted_at is null
  for update;
  if not found then
    raise exception using errcode = 'P0002', message = 'deletion_scope_missing';
  end if;

  update private.device_credentials credential
  set state = 'revoked',
      revoked_at = coalesce(credential.revoked_at, v_requested_at)
  where credential.state in ('active', 'rotating')
    and exists (
      select 1
      from api.collars collar
      where collar.id = credential.collar_id
        and collar.dog_id = v_scope_id
    );

  update api.collars
  set state = 'revoked',
      revoked_at = coalesce(revoked_at, v_requested_at),
      updated_at = greatest(updated_at, v_requested_at)
  where dog_id = v_scope_id
    and state in ('pending', 'active');

  v_initial_counts := private.dog_deletion_counts_v1(v_scope_id);

  insert into private.deletion_tombstones (
    request_id, request_sha256, scope, scope_id, confirmation_version,
    requested_by_sha256, requested_at, tombstone_sha256
  ) values (
    v_request_id, v_request_sha256, 'dog', v_scope_id,
    v_confirmation_version, v_requested_by_sha256, v_requested_at,
    v_tombstone_sha256
  ) returning id into v_tombstone_id;

  insert into private.deletion_jobs (
    tombstone_id, initial_counts, requested_at, next_attempt_at
  ) values (
    v_tombstone_id, v_initial_counts, v_requested_at, statement_timestamp()
  ) returning id into v_job_id;

  update api.dogs
  set deleted_at = v_requested_at,
      updated_at = greatest(updated_at, v_requested_at)
  where id = v_scope_id;

  delete from api.dog_memberships where dog_id = v_scope_id;

  return private.deletion_job_result_v1(v_job_id)
    || jsonb_build_object('disposition', 'replayed');
end
$$;

revoke execute on function private.replay_dog_deletion_tombstone_v1(jsonb)
  from public, anon, authenticated;
grant execute on function private.replay_dog_deletion_tombstone_v1(jsonb)
  to service_role;
