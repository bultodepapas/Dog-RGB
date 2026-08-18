-- Owner-authorized, replay-safe dog deletion primitives.
-- Scheduling remains deliberately disabled: a service worker must invoke one
-- bounded batch per transaction after the export/product gates are complete.

create table private.deletion_tombstones (
  id uuid primary key default extensions.gen_random_uuid(),
  request_id uuid not null unique,
  request_sha256 bytea not null check (octet_length(request_sha256) = 32),
  scope text not null check (scope = 'dog'),
  scope_id uuid not null,
  confirmation_version text not null
    check (confirmation_version = 'dog-delete-v1'),
  requested_by_sha256 bytea not null check (octet_length(requested_by_sha256) = 32),
  requested_at timestamptz not null,
  tombstone_sha256 bytea not null unique check (octet_length(tombstone_sha256) = 32)
);

create table private.deletion_jobs (
  id uuid primary key default extensions.gen_random_uuid(),
  tombstone_id uuid not null unique
    references private.deletion_tombstones(id) on delete restrict,
  status text not null default 'pending'
    check (status in ('pending', 'processing', 'failed', 'completed')),
  stage text not null default 'purge_telemetry'
    check (stage in ('purge_telemetry', 'finalize', 'completed')),
  initial_counts jsonb not null check (jsonb_typeof(initial_counts) = 'object'),
  telemetry_points_deleted bigint not null default 0
    check (telemetry_points_deleted >= 0),
  attempt_count integer not null default 0 check (attempt_count >= 0),
  requested_at timestamptz not null,
  last_attempt_at timestamptz,
  next_attempt_at timestamptz not null,
  last_error_code text check (
    last_error_code is null or char_length(last_error_code) between 1 and 64
  ),
  completed_at timestamptz,
  check ((status = 'completed') = (stage = 'completed')),
  check ((status = 'completed') = (completed_at is not null)),
  check (status = 'failed' or last_error_code is null)
);

create table private.deletion_receipts (
  job_id uuid primary key references private.deletion_jobs(id) on delete restrict,
  tombstone_id uuid not null unique
    references private.deletion_tombstones(id) on delete restrict,
  completed_at timestamptz not null,
  deleted_counts jsonb not null check (jsonb_typeof(deleted_counts) = 'object'),
  receipt_sha256 bytea not null unique check (octet_length(receipt_sha256) = 32)
);

create index deletion_tombstones_scope_replay_idx
  on private.deletion_tombstones (scope, scope_id, requested_at, id);
create index deletion_jobs_ready_idx
  on private.deletion_jobs (next_attempt_at, requested_at, id)
  where status in ('pending', 'processing', 'failed');

alter table private.deletion_tombstones enable row level security;
alter table private.deletion_jobs enable row level security;
alter table private.deletion_receipts enable row level security;

revoke all on private.deletion_tombstones from public, anon, authenticated;
revoke all on private.deletion_jobs from public, anon, authenticated;
revoke all on private.deletion_receipts from public, anon, authenticated;

-- A dog enters an inaccessible deleting state before any physical purge. All
-- existing policies delegate through this helper, so one change closes every
-- user-visible dog/collar/route/config path while the worker makes progress.
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
$$;

revoke execute on function private.member_role(uuid)
  from public, anon;
grant execute on function private.member_role(uuid)
  to authenticated;

-- Direct browser deletion must never bypass the durable tombstone/job path.
drop policy if exists dogs_delete_owner on api.dogs;

create or replace function private.dog_deletion_counts_v1(p_dog_id uuid)
returns jsonb
language sql
stable
set search_path = ''
as $$
  select jsonb_build_object(
    'dogs', (select count(*) from api.dogs where id = p_dog_id),
    'dog_memberships', (select count(*) from api.dog_memberships where dog_id = p_dog_id),
    'collars', (select count(*) from api.collars where dog_id = p_dog_id),
    'device_claims', (select count(*) from private.device_claims where dog_id = p_dog_id),
    'daily_summaries', (select count(*) from api.daily_summaries where dog_id = p_dog_id),
    'dirty_summary_days', (select count(*) from private.dirty_summary_days where dog_id = p_dog_id),
    'device_credentials', (select count(*) from private.device_credentials row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'sync_requests', (select count(*) from private.sync_requests row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'recordings', (select count(*) from api.recordings row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'telemetry_chunks', (select count(*) from private.telemetry_chunks row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'telemetry_points', (select count(*) from api.telemetry_points row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'telemetry_loss_markers', (select count(*) from private.telemetry_loss_markers row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'device_daily_summaries', (select count(*) from private.device_daily_summaries row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'config_revisions', (select count(*) from api.config_revisions row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'config_resource_heads', (select count(*) from api.config_resource_heads row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'config_reported', (select count(*) from api.config_reported row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'config_hlc_state', (select count(*) from private.config_hlc_state row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'recording_summaries', (
      select count(*)
      from api.recording_summaries row_value
      join api.recordings recording on recording.id = row_value.recording_id
      join api.collars collar on collar.id = recording.collar_id
      where collar.dog_id = p_dog_id
    )
  )
$$;

revoke execute on function private.dog_deletion_counts_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function private.deletion_job_result_v1(p_job_id uuid)
returns jsonb
language sql
stable
set search_path = ''
as $$
  select jsonb_strip_nulls(jsonb_build_object(
    'job_id', job.id,
    'scope', tombstone.scope,
    'scope_id', tombstone.scope_id,
    'status', job.status,
    'stage', job.stage,
    'requested_at', job.requested_at,
    'attempt_count', job.attempt_count,
    'telemetry_points_deleted', job.telemetry_points_deleted,
    'next_attempt_at', case when job.status <> 'completed' then job.next_attempt_at end,
    'last_error_code', job.last_error_code,
    'completed_at', job.completed_at,
    'tombstone_sha256', private.base64url_encode(tombstone.tombstone_sha256),
    'receipt_sha256', private.base64url_encode(receipt.receipt_sha256)
  ))
  from private.deletion_jobs job
  join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
  left join private.deletion_receipts receipt on receipt.job_id = job.id
  where job.id = p_job_id
$$;

revoke execute on function private.deletion_job_result_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function api.request_dog_deletion_v1(
  p_dog_id uuid,
  p_request_id uuid,
  p_confirmation_version text
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_user_sha256 bytea;
  v_request_sha256 bytea;
  v_existing record;
  v_tombstone_id uuid;
  v_job_id uuid;
  v_requested_at timestamptz := statement_timestamp();
  v_initial_counts jsonb;
begin
  if v_user_id is null then
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

  -- Serialize exact request replay before reading the durable receipt. Hash
  -- collisions only serialize unrelated requests; they cannot merge identity.
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
      raise exception using errcode = '40001', message = 'deletion_request_conflict';
    end if;
    if v_existing.scope_id <> p_dog_id
       or v_existing.request_sha256 <> v_request_sha256
       or v_existing.confirmation_version <> p_confirmation_version then
      raise exception using errcode = '23505', message = 'request_id_reused';
    end if;
    return private.deletion_job_result_v1(v_existing.job_id);
  end if;

  -- Serialize against another delete request for the same dog. Authorization is
  -- evaluated before deleted_at is set, using the durable membership row.
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

  -- Wait for any credential-locked sync transaction, then close future ingress
  -- before capturing the deletion inventory.
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

  -- Membership removal closes authorization paths implemented by narrow RPCs
  -- as well as ordinary RLS reads. Job status remains bound to the requester's
  -- one-way fingerprint instead of an active membership.
  delete from api.dog_memberships where dog_id = p_dog_id;

  return private.deletion_job_result_v1(v_job_id);
end
$$;

revoke execute on function api.request_dog_deletion_v1(uuid, uuid, text)
  from public, anon;
grant execute on function api.request_dog_deletion_v1(uuid, uuid, text)
  to authenticated;

create or replace function api.get_deletion_job_v1(p_job_id uuid)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_result jsonb;
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;

  select private.deletion_job_result_v1(job.id)
  into v_result
  from private.deletion_jobs job
  join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
  where job.id = p_job_id
    and tombstone.requested_by_sha256 = extensions.digest(v_user_id::text, 'sha256');

  if v_result is null then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;
  return v_result;
end
$$;

revoke execute on function api.get_deletion_job_v1(uuid)
  from public, anon;
grant execute on function api.get_deletion_job_v1(uuid)
  to authenticated;

create or replace function private.process_dog_deletion_batch_v1(
  p_batch_size integer default 5000
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_job private.deletion_jobs%rowtype;
  v_tombstone private.deletion_tombstones%rowtype;
  v_attempted_at timestamptz := statement_timestamp();
  v_deleted bigint := 0;
  v_deleted_total bigint := 0;
  v_remaining boolean;
  v_remaining_count bigint;
  v_receipt_sha256 bytea;
  v_error_state text;
begin
  if p_batch_size not between 1 and 10000 then
    raise exception using errcode = '22023', message = 'invalid_deletion_batch_size';
  end if;

  select job.*
  into v_job
  from private.deletion_jobs job
  where job.status in ('pending', 'processing', 'failed')
    and job.next_attempt_at <= v_attempted_at
  order by job.requested_at, job.id
  limit 1
  for update skip locked;

  if not found then
    return jsonb_build_object('disposition', 'idle');
  end if;

  select * into strict v_tombstone
  from private.deletion_tombstones
  where id = v_job.tombstone_id;

  update private.deletion_jobs
  set status = 'processing',
      attempt_count = attempt_count + 1,
      last_attempt_at = v_attempted_at,
      last_error_code = null
  where id = v_job.id;

  begin
    with target as (
      select point.ctid
      from api.telemetry_points point
      join api.collars collar on collar.id = point.collar_id
      where collar.dog_id = v_tombstone.scope_id
      order by point.collar_id, point.boot_sequence, point.point_sequence
      limit p_batch_size
      for update of point skip locked
    ), deleted as (
      delete from api.telemetry_points point
      using target
      where point.ctid = target.ctid
      returning 1
    )
    select count(*) into v_deleted from deleted;

    update private.deletion_jobs
    set telemetry_points_deleted = telemetry_points_deleted + v_deleted
    where id = v_job.id;

    select telemetry_points_deleted
    into v_deleted_total
    from private.deletion_jobs
    where id = v_job.id;

    select exists (
      select 1
      from api.telemetry_points point
      join api.collars collar on collar.id = point.collar_id
      where collar.dog_id = v_tombstone.scope_id
    ) into v_remaining;

    if v_remaining then
      update private.deletion_jobs
      set status = 'pending',
          stage = 'purge_telemetry',
          next_attempt_at = v_attempted_at
      where id = v_job.id;
      return private.deletion_job_result_v1(v_job.id)
        || jsonb_build_object('batch_points_deleted', v_deleted);
    end if;

    if v_deleted_total <> (v_job.initial_counts ->> 'telemetry_points')::bigint then
      raise exception using errcode = '23514', message = 'telemetry_deletion_count_mismatch';
    end if;

    update private.deletion_jobs
    set stage = 'finalize'
    where id = v_job.id;

    delete from api.dogs where id = v_tombstone.scope_id;

    select coalesce(sum(value::bigint), 0)
    into v_remaining_count
    from jsonb_each_text(private.dog_deletion_counts_v1(v_tombstone.scope_id));
    if v_remaining_count <> 0 then
      raise exception using errcode = '23514', message = 'deletion_scope_not_empty';
    end if;

    v_receipt_sha256 := extensions.digest(
      convert_to(concat_ws('|', 'dog-deletion-receipt-v1', v_job.id::text,
        v_tombstone.tombstone_sha256::text, v_attempted_at::text,
        v_job.initial_counts::text), 'utf8'),
      'sha256'
    );

    insert into private.deletion_receipts (
      job_id, tombstone_id, completed_at, deleted_counts, receipt_sha256
    ) values (
      v_job.id, v_tombstone.id, v_attempted_at, v_job.initial_counts, v_receipt_sha256
    )
    on conflict (job_id) do nothing;

    update private.deletion_jobs
    set status = 'completed',
        stage = 'completed',
        next_attempt_at = v_attempted_at,
        completed_at = v_attempted_at,
        last_error_code = null
    where id = v_job.id;

    return private.deletion_job_result_v1(v_job.id)
      || jsonb_build_object('batch_points_deleted', v_deleted);
  exception when others then
    get stacked diagnostics v_error_state = returned_sqlstate;
    update private.deletion_jobs
    set status = 'failed',
        next_attempt_at = v_attempted_at + interval '5 minutes',
        last_error_code = left(v_error_state, 64)
    where id = v_job.id;
    return private.deletion_job_result_v1(v_job.id);
  end;
end
$$;

revoke execute on function private.process_dog_deletion_batch_v1(integer)
  from public, anon, authenticated;
grant execute on function private.process_dog_deletion_batch_v1(integer)
  to service_role;
