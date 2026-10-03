-- Shared actor-explicit transaction: public dog requests and the service-only
-- account orchestrator use exactly the same closure/tombstone/job implementation.
create or replace function private.request_dog_deletion_for_actor_v1(
  p_user_id uuid,
  p_dog_id uuid,
  p_request_id uuid,
  p_confirmation_version text,
  p_password_authenticated_at timestamptz
)
returns jsonb
language plpgsql
security definer
set search_path = ''
set timezone = 'UTC'
as $$
declare
  v_user_id uuid := p_user_id;
  v_user_sha256 bytea;
  v_request_sha256 bytea;
  v_existing record;
  v_tombstone_id uuid;
  v_job_id uuid;
  v_requested_at timestamptz := statement_timestamp();
  v_initial_counts jsonb;
begin
  if v_user_id is null or not exists (
    select 1
    from auth.users as auth_user
    where auth_user.id = v_user_id
      and auth_user.deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if p_dog_id is null or p_request_id is null
     or p_confirmation_version is distinct from 'dog-delete-v1' then
    raise exception using errcode = '22023', message = 'invalid_deletion_request';
  end if;

  v_user_sha256 := extensions.digest(v_user_id::text, 'sha256');
  v_request_sha256 := extensions.digest(
    convert_to(concat_ws('|', 'dog-delete-v1', p_dog_id::text, p_confirmation_version), 'utf8'),
    'sha256'
  );

  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(p_request_id::text, 1146113073)
  );

  select job.id as job_id, tombstone.scope_id, tombstone.request_sha256,
         tombstone.confirmation_version, tombstone.requested_by_sha256
  into v_existing
  from private.deletion_tombstones tombstone
  join private.deletion_jobs job on job.tombstone_id = tombstone.id
  where tombstone.request_id = p_request_id;

  if found then
    if v_existing.requested_by_sha256 <> v_user_sha256 then
      raise exception using errcode = '42501', message = 'not_authorized';
    end if;
    if v_existing.scope_id <> p_dog_id
       or v_existing.request_sha256 <> v_request_sha256
       or v_existing.confirmation_version <> p_confirmation_version then
      raise exception using errcode = '23505', message = 'request_id_reused';
    end if;
    return private.deletion_job_result_v1(v_existing.job_id);
  end if;

  perform 1
  from api.dogs dog
  join api.dog_memberships membership on membership.dog_id = dog.id
  where dog.id = p_dog_id
    and dog.deleted_at is null
    and membership.user_id = v_user_id
    and membership.role = 'owner'
  for update of dog;
  if not found then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  -- Exact receipt replay above does not create destructive work. New work
  -- requires proof of a recent password login supplied by a trusted caller.
  if p_password_authenticated_at is null
     or p_password_authenticated_at < v_requested_at - interval '5 minutes'
     or p_password_authenticated_at > v_requested_at + interval '30 seconds' then
    raise exception using errcode = '28000', message = 'reauthentication_required';
  end if;

  update private.device_credentials credential
  set state = 'revoked',
      revoked_at = coalesce(credential.revoked_at, v_requested_at)
  where credential.state in ('active', 'rotating')
    and exists (
      select 1
      from api.collars collar
      where collar.id = credential.collar_id
        and collar.dog_id = p_dog_id
    );

  update api.collars
  set state = 'revoked',
      revoked_at = coalesce(revoked_at, v_requested_at),
      updated_at = v_requested_at
  where dog_id = p_dog_id
    and state in ('pending', 'active');

  v_initial_counts := private.dog_deletion_counts_v1(p_dog_id);

  insert into private.deletion_tombstones (
    request_id, request_sha256, scope, scope_id, confirmation_version,
    requested_by_sha256, requested_at, tombstone_sha256
  ) values (
    p_request_id,
    v_request_sha256,
    'dog',
    p_dog_id,
    p_confirmation_version,
    v_user_sha256,
    v_requested_at,
    extensions.digest(
      convert_to(concat_ws('|', 'dog-tombstone-v1', p_dog_id::text,
        p_request_id::text, v_requested_at::text, private.base64url_encode(v_request_sha256)), 'utf8'),
      'sha256'
    )
  )
  returning id into v_tombstone_id;

  insert into private.deletion_jobs (
    tombstone_id, initial_counts, requested_at, next_attempt_at
  ) values (
    v_tombstone_id, v_initial_counts, v_requested_at, v_requested_at
  )
  returning id into v_job_id;

  update api.dogs
  set deleted_at = v_requested_at,
      updated_at = v_requested_at
  where id = p_dog_id;

  delete from api.dog_memberships where dog_id = p_dog_id;

  return private.deletion_job_result_v1(v_job_id);
end
$$;


revoke all on function private.request_dog_deletion_for_actor_v1(uuid,uuid,uuid,text,timestamptz)
  from public, anon, authenticated, service_role;

create or replace function api.request_dog_deletion_v1(
  p_dog_id uuid, p_request_id uuid, p_confirmation_version text
) returns jsonb
language plpgsql security definer set search_path = '' set timezone = 'UTC'
as $$
declare
  v_password_at timestamptz;
begin
  -- AMR is a signed Auth claim; user_metadata is deliberately not consulted.
  select max(case when factor ->> 'method' = 'password'
    and factor ->> 'timestamp' ~ '^[0-9]{1,12}(\.[0-9]{1,6})?$'
    then to_timestamp((factor ->> 'timestamp')::double precision) end)
  into v_password_at
  from jsonb_array_elements(case when jsonb_typeof(auth.jwt() -> 'amr') = 'array'
    then auth.jwt() -> 'amr' else '[]'::jsonb end) factor;
  return private.request_dog_deletion_for_actor_v1(
    auth.uid(), p_dog_id, p_request_id, p_confirmation_version, v_password_at
  );
end
$$;
revoke all on function api.request_dog_deletion_v1(uuid,uuid,text) from public, anon, authenticated, service_role;
grant execute on function api.request_dog_deletion_v1(uuid,uuid,text) to authenticated;
