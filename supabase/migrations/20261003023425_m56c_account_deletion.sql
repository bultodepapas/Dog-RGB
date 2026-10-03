-- M5.6c: a password-reauthenticated account deletion request drains the
-- existing dog deletion queue before the Auth user may be removed.

create table private.account_deletion_requests (
  request_id uuid primary key,
  user_id uuid references auth.users(id) on delete set null,
  requester_sha256 bytea not null check (octet_length(requester_sha256) = 32),
  confirmation_version text not null check (confirmation_version = 'account-delete-v1'),
  scope_sha256 bytea not null check (octet_length(scope_sha256) = 32),
  status text not null check (status in ('pending', 'blocked', 'ready', 'completed')),
  blocker_code text check (blocker_code is null or char_length(blocker_code) between 1 and 64),
  owned_dog_count integer not null default 0 check (owned_dog_count >= 0),
  detached_membership_count integer not null default 0 check (detached_membership_count >= 0),
  requested_at timestamptz not null default statement_timestamp(),
  updated_at timestamptz not null default statement_timestamp(),
  ready_at timestamptz,
  completed_at timestamptz,
  receipt_sha256 bytea check (receipt_sha256 is null or octet_length(receipt_sha256) = 32),
  constraint account_deletion_completed_at_check
    check ((status = 'completed') = (completed_at is not null)),
  constraint account_deletion_receipt_check
    check ((status in ('ready', 'completed')) = (receipt_sha256 is not null))
);

create unique index account_deletion_one_active_per_user_idx
  on private.account_deletion_requests (requester_sha256)
  where status in ('pending', 'ready');
create index account_deletion_requests_user_id_idx
  on private.account_deletion_requests (user_id);

create table private.account_deletion_job_links (
  request_id uuid not null references private.account_deletion_requests(request_id) on delete restrict,
  job_id uuid not null references private.deletion_jobs(id) on delete restrict,
  dog_id uuid not null,
  source text not null check (source in ('requested', 'already_pending')),
  primary key (request_id, job_id),
  unique (request_id, dog_id)
);

create index account_deletion_job_links_job_id_idx
  on private.account_deletion_job_links (job_id);

alter table private.account_deletion_requests enable row level security;
alter table private.account_deletion_job_links enable row level security;
revoke all on private.account_deletion_requests from public, anon, authenticated, service_role;
revoke all on private.account_deletion_job_links from public, anon, authenticated, service_role;

create or replace function private.account_deletion_active_v1(p_user_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1
    from private.account_deletion_requests request
    where request.requester_sha256 = extensions.digest(p_user_id::text, 'sha256')
      and request.status in ('pending', 'ready')
  )
$$;

revoke all on function private.account_deletion_active_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function private.current_account_active_v1()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select private.account_deletion_active_v1((select auth.uid()))
$$;

revoke all on function private.current_account_active_v1()
  from public, anon, service_role;
grant execute on function private.current_account_active_v1()
  to authenticated;

create or replace function private.lock_account_mutation_v1(p_user_id uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
begin
  if p_user_id is null then return; end if;
  perform pg_catalog.pg_advisory_xact_lock_shared(
    pg_catalog.hashtextextended(p_user_id::text, 1886942205)
  );
  if private.account_deletion_active_v1(p_user_id) then
    raise exception using errcode = '55000', message = 'account_deletion_pending';
  end if;
end
$$;

revoke all on function private.lock_account_mutation_v1(uuid)
  from public, anon, authenticated, service_role;

-- Take the account lock before the existing configuration RPC locks its
-- collar row. The INSERT trigger below remains a defense in depth for any
-- other web-origin config writer.
alter function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  set schema private;
alter function private.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  rename to mutate_config_resource_unlocked_v1;

create or replace function api.mutate_config_resource_v1(
  p_collar_id uuid,
  p_resource_key text,
  p_resource_schema integer,
  p_mutation_id uuid,
  p_base_server_version bigint,
  p_body jsonb,
  p_body_sha256 bytea
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  perform private.lock_account_mutation_v1(v_user_id);
  return private.mutate_config_resource_unlocked_v1(
    p_collar_id, p_resource_key, p_resource_schema, p_mutation_id,
    p_base_server_version, p_body, p_body_sha256
  );
end
$$;

revoke all on function private.mutate_config_resource_unlocked_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  from public, anon, authenticated, service_role;
revoke all on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  from public, anon, service_role;
grant execute on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  to authenticated;

-- Close the three write paths that can create or change a dog-owned resource
-- while account deletion is pending. Shared advisory locks serialize these
-- writes against the account request's exclusive identity lock.
create or replace function private.guard_account_deletion_mutation_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid;
begin
  if tg_table_schema = 'api' and tg_table_name = 'dogs' then
    v_user_id := new.created_by;
  elsif tg_table_schema = 'private' and tg_table_name = 'device_claims' then
    v_user_id := new.requested_by;
  elsif tg_table_schema = 'api' and tg_table_name = 'config_revisions' then
    if new.origin <> 'web' then return new; end if;
    v_user_id := new.actor_user_id;
  else
    return new;
  end if;
  perform private.lock_account_mutation_v1(v_user_id);
  return new;
end
$$;

revoke all on function private.guard_account_deletion_mutation_v1()
  from public, anon, authenticated, service_role;

create trigger dogs_account_deletion_write_guard
before insert on api.dogs
for each row execute function private.guard_account_deletion_mutation_v1();

create trigger device_claims_account_deletion_write_guard
before insert on private.device_claims
for each row execute function private.guard_account_deletion_mutation_v1();

create trigger config_revisions_account_deletion_write_guard
before insert on api.config_revisions
for each row execute function private.guard_account_deletion_mutation_v1();

create or replace function private.member_role(p_dog_id uuid)
returns text
language sql
stable
security definer
set search_path = ''
as $$
  select membership.role
  from api.dog_memberships as membership
  join api.dogs as dog on dog.id = membership.dog_id
  where membership.dog_id = p_dog_id
    and membership.user_id = (select auth.uid())
    and dog.deleted_at is null
    and not private.account_deletion_active_v1((select auth.uid()))
$$;

revoke execute on function private.member_role(uuid) from public, anon;
grant execute on function private.member_role(uuid) to authenticated;

drop policy if exists profiles_select_self on api.profiles;
create policy profiles_select_self on api.profiles
  for select to authenticated
  using (
    user_id = (select auth.uid())
    and not private.current_account_active_v1()
  );
drop policy if exists profiles_update_self on api.profiles;
create policy profiles_update_self on api.profiles
  for update to authenticated
  using (
    user_id = (select auth.uid())
    and not private.current_account_active_v1()
  )
  with check (
    user_id = (select auth.uid())
    and not private.current_account_active_v1()
  );

-- Use one deterministic, bounded snapshot for both preview and request-time
-- confirmation. More than 100 affected dogs/memberships is blocked for manual
-- resolution instead of silently truncating the confirmed inventory.
create or replace function private.account_deletion_scope_v1(p_user_id uuid)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_owned jsonb;
  v_detach jsonb;
  v_prior_jobs jsonb;
  v_blocked_dogs jsonb;
  v_blockers jsonb;
  v_material jsonb;
begin
  select coalesce(jsonb_agg(jsonb_build_object(
      'dog_id', owned.dog_id,
      'name', owned.name,
      'other_member_count', owned.other_member_count,
      'other_owner_count', owned.other_owner_count
    ) order by owned.dog_id), '[]'::jsonb)
  into v_owned
  from (
    select dog.id as dog_id, dog.name,
      count(other_membership.user_id)::integer as other_member_count,
      count(other_membership.user_id) filter (where other_membership.role = 'owner')::integer as other_owner_count
    from api.dogs dog
    join api.dog_memberships own_membership
      on own_membership.dog_id = dog.id and own_membership.user_id = p_user_id
      and own_membership.role = 'owner'
    left join api.dog_memberships other_membership
      on other_membership.dog_id = dog.id and other_membership.user_id <> p_user_id
    where dog.deleted_at is null
    group by dog.id, dog.name
  ) owned;

  select coalesce(jsonb_agg(jsonb_build_object(
      'dog_id', detached.dog_id,
      'name', detached.name,
      'role', detached.role
    ) order by detached.dog_id), '[]'::jsonb)
  into v_detach
  from (
    select dog.id as dog_id, dog.name, membership.role
    from api.dog_memberships membership
    join api.dogs dog on dog.id = membership.dog_id
    where membership.user_id = p_user_id
      and membership.role in ('editor', 'viewer')
      and dog.deleted_at is null
  ) detached;

  select coalesce(jsonb_agg(jsonb_build_object(
      'job_id', prior.job_id,
      'dog_id', prior.dog_id,
      'status', prior.status
    ) order by prior.dog_id, prior.job_id), '[]'::jsonb)
  into v_prior_jobs
  from (
    select dog.id as dog_id, job.id as job_id, job.status
    from api.dogs dog
    join private.deletion_tombstones tombstone
      on tombstone.scope = 'dog' and tombstone.scope_id = dog.id
      and tombstone.requested_by_sha256 = extensions.digest(p_user_id::text, 'sha256')
    join private.deletion_jobs job on job.tombstone_id = tombstone.id
    where dog.created_by = p_user_id
      and dog.deleted_at is not null
      and job.status <> 'completed'
  ) prior;

  select coalesce(jsonb_agg(blocked.dog_id order by blocked.dog_id), '[]'::jsonb)
  into v_blocked_dogs
  from (
    select dog.id as dog_id
    from api.dogs dog
    where dog.created_by = p_user_id
      and not exists (
        select 1 from api.dog_memberships membership
        where membership.dog_id = dog.id
          and membership.user_id = p_user_id
          and membership.role = 'owner'
          and dog.deleted_at is null
      )
      and not exists (
        select 1
        from private.deletion_tombstones tombstone
        join private.deletion_jobs job on job.tombstone_id = tombstone.id
        where tombstone.scope = 'dog'
          and tombstone.scope_id = dog.id
          and tombstone.requested_by_sha256 = extensions.digest(p_user_id::text, 'sha256')
          and job.status <> 'completed'
      )
  ) blocked;

  select coalesce(jsonb_agg(blocker.code order by blocker.code), '[]'::jsonb)
  into v_blockers
  from (
    select 'creator_not_in_deletion_set'::text as code
    where jsonb_array_length(v_blocked_dogs) > 0
    union all
    select 'inventory_limit'::text
    where jsonb_array_length(v_owned) + jsonb_array_length(v_detach) + jsonb_array_length(v_prior_jobs) > 100
  ) blocker;

  v_material := jsonb_build_object(
    'version', 'account-delete-v1',
    'actor', p_user_id::text,
    'owned_dogs', v_owned,
    'memberships_to_detach', v_detach,
    'already_pending_jobs', v_prior_jobs,
    'blocked_creator_dog_ids', v_blocked_dogs,
    'blockers', v_blockers
  );
  return jsonb_build_object(
    'owned_dogs', v_owned,
    'memberships_to_detach', v_detach,
    'already_pending_jobs', v_prior_jobs,
    'blocked_creator_dog_ids', v_blocked_dogs,
    'blockers', v_blockers,
    'scope_material', v_material,
    'scope_sha256', private.base64url_encode(extensions.digest(convert_to(v_material::text, 'UTF8'), 'sha256'))
  );
end
$$;

revoke all on function private.account_deletion_scope_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function private.account_deletion_result_v1(p_request_id uuid)
returns jsonb
language sql
stable
security definer
set search_path = ''
as $$
  select jsonb_strip_nulls(jsonb_build_object(
    'schema_version', 'account-deletion-v1',
    'request_id', request.request_id,
    'status', case
      when request.status = 'pending' and not exists (
        select 1 from private.account_deletion_job_links link
        join private.deletion_jobs job on job.id = link.job_id
        where link.request_id = request.request_id and job.status <> 'completed'
      ) then 'ready'
      else request.status
    end,
    'blocker_code', request.blocker_code,
    'owned_dog_count', request.owned_dog_count,
    'detached_membership_count', request.detached_membership_count,
    'dog_jobs', coalesce((
      select jsonb_agg(jsonb_build_object(
        'job_id', job.id,
        'dog_id', link.dog_id,
        'status', job.status,
        'attempts', job.attempt_count,
        'last_error_code', job.last_error_code,
        'requested_at', job.requested_at,
        'completed_at', job.completed_at
      ) order by job.requested_at, job.id)
      from private.account_deletion_job_links link
      join private.deletion_jobs job on job.id = link.job_id
      where link.request_id = request.request_id
    ), '[]'::jsonb),
    'requested_at', request.requested_at,
    'updated_at', request.updated_at,
    'ready_at', request.ready_at,
    'completed_at', request.completed_at,
    'receipt_sha256', case when request.receipt_sha256 is not null then private.base64url_encode(request.receipt_sha256) end
  ))
  from private.account_deletion_requests request
  where request.request_id = p_request_id
$$;

revoke all on function private.account_deletion_result_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function api.preview_my_account_deletion_v1()
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_scope jsonb;
begin
  if v_user_id is null or not exists (
    select 1 from auth.users where id = v_user_id and deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if private.account_deletion_active_v1(v_user_id) then
    raise exception using errcode = '55000', message = 'account_deletion_pending';
  end if;
  v_scope := private.account_deletion_scope_v1(v_user_id);
  return jsonb_build_object(
    'schema_version', 'account-deletion-preview-v1',
    'request_id', extensions.gen_random_uuid(),
    'confirmation_version', 'account-delete-v1',
    'confirmation_phrase', 'ELIMINAR CUENTA Y TODOS LOS PERROS',
    'scope_sha256', v_scope ->> 'scope_sha256',
    'owned_dogs', v_scope -> 'owned_dogs',
    'memberships_to_detach', v_scope -> 'memberships_to_detach',
    'already_pending_jobs', v_scope -> 'already_pending_jobs',
    'blocked_creator_dog_ids', v_scope -> 'blocked_creator_dog_ids',
    'blockers', v_scope -> 'blockers'
  );
end
$$;

revoke all on function api.preview_my_account_deletion_v1() from public, anon, service_role;
grant execute on function api.preview_my_account_deletion_v1() to authenticated;

create or replace function api.get_my_account_deletion_v1()
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_request_id uuid;
  v_result jsonb;
begin
  if v_user_id is null or not exists (
    select 1 from auth.users where id = v_user_id and deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  select request_id into v_request_id
  from private.account_deletion_requests
  where requester_sha256 = extensions.digest(v_user_id::text, 'sha256')
  order by requested_at desc, request_id desc
  limit 1;
  if v_request_id is null then
    return jsonb_build_object('schema_version', 'account-deletion-v1', 'status', 'none');
  end if;
  v_result := private.account_deletion_result_v1(v_request_id);
  return v_result;
end
$$;

revoke all on function api.get_my_account_deletion_v1() from public, anon, service_role;
grant execute on function api.get_my_account_deletion_v1() to authenticated;

create or replace function api.retry_my_account_deletion_v1()
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_request_id uuid;
begin
  if v_user_id is null or not exists (
    select 1 from auth.users where id = v_user_id and deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  select request_id into v_request_id
  from private.account_deletion_requests
  where requester_sha256 = extensions.digest(v_user_id::text, 'sha256')
  order by requested_at desc, request_id desc
  limit 1;
  if v_request_id is null then
    raise exception using errcode = 'P0002', message = 'account_deletion_not_found';
  end if;
  if not exists (
    select 1 from private.account_deletion_requests
    where request_id = v_request_id and status = 'pending'
  ) then
    raise exception using errcode = '55000', message = 'account_deletion_not_retryable';
  end if;
  update private.deletion_jobs job
  set status = 'pending', last_error_code = null, next_attempt_at = statement_timestamp()
  where job.status = 'failed'
    and job.id in (
      select link.job_id from private.account_deletion_job_links link
      where link.request_id = v_request_id
    );
  update private.account_deletion_requests
  set updated_at = statement_timestamp()
  where request_id = v_request_id;
  return private.account_deletion_result_v1(v_request_id);
end
$$;

revoke all on function api.retry_my_account_deletion_v1() from public, anon, service_role;
grant execute on function api.retry_my_account_deletion_v1() to authenticated;

create or replace function api.request_account_deletion_v1(
  p_user_id uuid,
  p_request_id uuid,
  p_confirmation_version text,
  p_scope_sha256 text,
  p_confirmation_phrase text,
  p_recent_password_at bigint
)
returns jsonb
language plpgsql
security definer
set search_path = ''
set timezone = 'UTC'
as $$
declare
  v_user_sha256 bytea;
  v_scope jsonb;
  v_existing private.account_deletion_requests%rowtype;
  v_active_request uuid;
  v_dog jsonb;
  v_job jsonb;
  v_dog_id uuid;
  v_job_id uuid;
  v_detached_count integer;
begin
  if p_user_id is null or p_request_id is null
     or p_confirmation_version is distinct from 'account-delete-v1'
     or p_scope_sha256 is null or p_scope_sha256 !~ '^[A-Za-z0-9_-]{43}$'
     or p_confirmation_phrase is distinct from 'ELIMINAR CUENTA Y TODOS LOS PERROS'
  then
    raise exception using errcode = '22023', message = 'invalid_account_deletion_request';
  end if;
  if not exists (select 1 from auth.users where id = p_user_id and deleted_at is null) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if p_recent_password_at is null
     or p_recent_password_at > floor(extract(epoch from statement_timestamp()))::bigint
     or p_recent_password_at < floor(extract(epoch from statement_timestamp() - interval '5 minutes'))::bigint then
    raise exception using errcode = '28000', message = 'reauthentication_required';
  end if;

  v_user_sha256 := extensions.digest(p_user_id::text, 'sha256');
  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(p_user_id::text, 1886942205)
  );

  select * into v_existing
  from private.account_deletion_requests
  where request_id = p_request_id;
  if found then
    if v_existing.requester_sha256 <> v_user_sha256
       or v_existing.scope_sha256 <> extensions.digest(convert_to(p_scope_sha256, 'UTF8'), 'sha256')
       or v_existing.confirmation_version <> p_confirmation_version then
      raise exception using errcode = '23505', message = 'request_id_reused';
    end if;
    return private.account_deletion_result_v1(p_request_id);
  end if;

  select request_id into v_active_request
  from private.account_deletion_requests
  where requester_sha256 = v_user_sha256 and status in ('pending', 'ready')
  limit 1;
  if v_active_request is not null then
    raise exception using errcode = '55000', message = 'account_deletion_pending';
  end if;

  -- Lock the current account-owned dog set and memberships before comparing
  -- the browser preview. Concurrent dog deletion either finishes first and
  -- changes the hash, or waits until this request has installed its tombstones.
  perform dog.id
  from api.dogs dog
  where dog.created_by = p_user_id
     or exists (select 1 from api.dog_memberships membership where membership.dog_id = dog.id and membership.user_id = p_user_id)
  order by dog.id
  for update of dog;
  perform membership.dog_id
  from api.dog_memberships membership
  where membership.user_id = p_user_id
     or membership.dog_id in (
       select own_membership.dog_id from api.dog_memberships own_membership
       where own_membership.user_id = p_user_id and own_membership.role = 'owner'
     )
  order by membership.dog_id, membership.user_id
  for update;

  v_scope := private.account_deletion_scope_v1(p_user_id);
  if v_scope ->> 'scope_sha256' is distinct from p_scope_sha256 then
    raise exception using errcode = 'PT409', message = 'account_deletion_scope_changed';
  end if;

  insert into private.account_deletion_requests (
    request_id, user_id, requester_sha256, confirmation_version, scope_sha256,
    status, blocker_code, owned_dog_count, detached_membership_count
  ) values (
    p_request_id, p_user_id, v_user_sha256, p_confirmation_version,
    extensions.digest(convert_to(p_scope_sha256, 'UTF8'), 'sha256'),
    case when jsonb_array_length(v_scope -> 'blockers') > 0 then 'blocked' else 'pending' end,
    case when jsonb_array_length(v_scope -> 'blockers') > 0 then v_scope -> 'blockers' ->> 0 else null end,
    jsonb_array_length(v_scope -> 'owned_dogs'),
    jsonb_array_length(v_scope -> 'memberships_to_detach')
  );

  if jsonb_array_length(v_scope -> 'blockers') > 0 then
    return private.account_deletion_result_v1(p_request_id);
  end if;

  for v_dog in select value from jsonb_array_elements(v_scope -> 'owned_dogs') loop
    v_dog_id := (v_dog ->> 'dog_id')::uuid;
    v_job_id := (private.request_dog_deletion_for_actor_v1(
      p_user_id, v_dog_id, extensions.gen_random_uuid(), 'dog-delete-v1',
      to_timestamp(p_recent_password_at::double precision)
    ) ->> 'job_id')::uuid;
    insert into private.account_deletion_job_links (request_id, job_id, dog_id, source)
    values (p_request_id, v_job_id, v_dog_id, 'requested');
  end loop;

  for v_job in select value from jsonb_array_elements(v_scope -> 'already_pending_jobs') loop
    v_job_id := (v_job ->> 'job_id')::uuid;
    v_dog_id := (v_job ->> 'dog_id')::uuid;
    insert into private.account_deletion_job_links (request_id, job_id, dog_id, source)
    values (p_request_id, v_job_id, v_dog_id, 'already_pending')
    on conflict (request_id, dog_id) do nothing;
  end loop;

  delete from api.dog_memberships membership
  where membership.user_id = p_user_id and membership.role in ('editor', 'viewer');
  get diagnostics v_detached_count = row_count;
  update private.account_deletion_requests
  set detached_membership_count = v_detached_count,
      updated_at = statement_timestamp()
  where request_id = p_request_id;
  return private.account_deletion_result_v1(p_request_id);
end
$$;

revoke all on function api.request_account_deletion_v1(uuid, uuid, text, text, text, bigint)
  from public, anon, authenticated;
grant execute on function api.request_account_deletion_v1(uuid, uuid, text, text, text, bigint)
  to service_role;

create or replace function api.prepare_account_deletion_finalization_v1(p_user_id uuid)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_request private.account_deletion_requests%rowtype;
begin
  if p_user_id is null or not exists (
    select 1 from auth.users where id = p_user_id and deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  select * into v_request
  from private.account_deletion_requests request
  where request.requester_sha256 = extensions.digest(p_user_id::text, 'sha256')
  order by request.requested_at desc, request.request_id desc
  limit 1
  for update;
  if not found then
    raise exception using errcode = 'P0002', message = 'account_deletion_not_found';
  end if;
  if v_request.status = 'blocked' then
    return private.account_deletion_result_v1(v_request.request_id);
  end if;
  if v_request.status not in ('pending', 'ready') or exists (
    select 1 from private.account_deletion_job_links link
    join private.deletion_jobs job on job.id = link.job_id
    where link.request_id = v_request.request_id and job.status <> 'completed'
  ) or exists (select 1 from api.dogs where created_by = p_user_id)
     or exists (select 1 from api.dog_memberships where user_id = p_user_id) then
    return private.account_deletion_result_v1(v_request.request_id);
  end if;

  if v_request.status <> 'ready' then
    update private.account_deletion_requests
    set status = 'ready', blocker_code = null, ready_at = statement_timestamp(), updated_at = statement_timestamp(),
        receipt_sha256 = extensions.digest(convert_to(concat_ws('|', 'account-deletion-receipt-v1',
          request_id::text, requested_at::text, encode(scope_sha256, 'hex'), owned_dog_count::text,
          detached_membership_count::text), 'UTF8'), 'sha256')
    where request_id = v_request.request_id;
  end if;
  return private.account_deletion_result_v1(v_request.request_id);
end
$$;

revoke all on function api.prepare_account_deletion_finalization_v1(uuid)
  from public, anon, authenticated;
grant execute on function api.prepare_account_deletion_finalization_v1(uuid)
  to service_role;

create or replace function api.get_account_deletion_receipt_v1(p_user_id uuid, p_request_id uuid)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_result jsonb;
begin
  if p_user_id is null or p_request_id is null then
    raise exception using errcode = '22023', message = 'invalid_account_deletion_receipt_request';
  end if;
  select private.account_deletion_result_v1(request.request_id)
  into v_result
  from private.account_deletion_requests request
  where request.request_id = p_request_id
    and request.requester_sha256 = extensions.digest(p_user_id::text, 'sha256')
    and request.status = 'completed';
  if v_result is null then
    raise exception using errcode = '55000', message = 'account_deletion_not_completed';
  end if;
  return v_result;
end
$$;

revoke all on function api.get_account_deletion_receipt_v1(uuid, uuid)
  from public, anon, authenticated;
grant execute on function api.get_account_deletion_receipt_v1(uuid, uuid)
  to service_role;

create or replace function private.guard_auth_user_delete_for_account_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_sha256 bytea := extensions.digest(old.id::text, 'sha256');
  v_request private.account_deletion_requests%rowtype;
begin
  select * into v_request
  from private.account_deletion_requests request
  where request.requester_sha256 = v_user_sha256
    and request.status <> 'completed'
  order by request.requested_at desc, request.request_id desc
  limit 1;
  -- Preserve established Auth deletion behavior for accounts that never
  -- opted into the durable account-deletion workflow.
  if not found then return old; end if;
  if v_request.status in ('pending', 'blocked') then
    raise exception using errcode = '55000', message = 'account_deletion_not_ready';
  end if;
  if v_request.status <> 'ready'
     or exists (select 1 from api.dogs where created_by = old.id)
     or exists (select 1 from api.dog_memberships where user_id = old.id)
     or exists (
       select 1 from private.account_deletion_requests request
       where request.requester_sha256 = v_user_sha256
         and request.request_id = v_request.request_id
         and exists (
           select 1 from private.account_deletion_job_links link
           join private.deletion_jobs job on job.id = link.job_id
           where link.request_id = request.request_id and job.status <> 'completed'
         )
     ) then
    raise exception using errcode = '55000', message = 'account_deletion_not_ready';
  end if;
  return old;
end
$$;

revoke all on function private.guard_auth_user_delete_for_account_v1()
  from public, anon, authenticated, service_role;

create trigger auth_user_account_deletion_guard
before delete on auth.users
for each row execute function private.guard_auth_user_delete_for_account_v1();

create or replace function private.complete_account_deletion_after_auth_delete_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  update private.account_deletion_requests
  set status = 'completed', completed_at = statement_timestamp(), updated_at = statement_timestamp()
  where requester_sha256 = extensions.digest(old.id::text, 'sha256')
    and status = 'ready';
  return old;
end
$$;

revoke all on function private.complete_account_deletion_after_auth_delete_v1()
  from public, anon, authenticated, service_role;

create trigger auth_user_account_deletion_receipt
after delete on auth.users
for each row execute function private.complete_account_deletion_after_auth_delete_v1();
