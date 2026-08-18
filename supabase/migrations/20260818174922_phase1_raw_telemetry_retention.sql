-- Replay-safe, bounded raw telemetry retention primitives.
--
-- No cron schedule is installed here. Production scheduling remains an
-- explicit hosted-environment decision after load, backup/replay, and alerting
-- gates pass. A trusted service worker enqueues one UTC-day cutoff and executes
-- one bounded transaction at a time.

create or replace function private.telemetry_retention_basis_v1(
  p_recorded_at timestamptz,
  p_received_at timestamptz
)
returns timestamptz
language sql
immutable
returns null on null input
parallel safe
set search_path = ''
as $$
  select case
    -- A timestamp materially ahead of authenticated receipt time cannot keep
    -- location immortal. Unknown time is represented by a null recorded_at and
    -- is handled by the trigger/index caller with received_at.
    when p_recorded_at - p_received_at > interval '10 minutes'
      then p_received_at
    else p_recorded_at
  end
$$;

revoke execute on function private.telemetry_retention_basis_v1(timestamptz, timestamptz)
  from public, anon, authenticated, service_role;

create or replace function private.telemetry_uses_received_at_v1(
  p_recorded_at timestamptz,
  p_received_at timestamptz
)
returns boolean
language sql
immutable
parallel safe
set search_path = ''
as $$
  select p_recorded_at is null
    or p_recorded_at - p_received_at > interval '10 minutes'
$$;

revoke execute on function private.telemetry_uses_received_at_v1(timestamptz, timestamptz)
  from public, anon, authenticated, service_role;

-- Plausible timestamps already use telemetry_points_collar_time_idx. Only the
-- exceptional unknown/future rows need another access path; indexing every
-- point a fourth time exceeded the accepted capacity budget by 82.4 MB/million.
create index telemetry_points_retention_fallback_v1_idx
  on api.telemetry_points (collar_id, received_at, boot_sequence, point_sequence)
  where private.telemetry_uses_received_at_v1(recorded_at, received_at);

create table private.telemetry_retention_watermarks (
  collar_id uuid primary key references api.collars(id) on delete cascade,
  reject_at_or_before timestamptz not null,
  purged_at_or_before timestamptz,
  updated_at timestamptz not null default statement_timestamp(),
  check (
    purged_at_or_before is null
    or purged_at_or_before <= reject_at_or_before
  )
);

create table private.retention_jobs (
  id uuid primary key default extensions.gen_random_uuid(),
  data_class text not null check (data_class = 'raw_telemetry_v1'),
  collar_id uuid not null references api.collars(id) on delete cascade,
  cutoff timestamptz not null,
  status text not null default 'pending'
    check (status in ('pending', 'processing', 'failed', 'completed')),
  stage text not null default 'purge_points'
    check (stage in ('purge_points', 'purge_chunks', 'completed')),
  telemetry_points_deleted bigint not null default 0
    check (telemetry_points_deleted >= 0),
  telemetry_chunks_deleted bigint not null default 0
    check (telemetry_chunks_deleted >= 0),
  attempt_count integer not null default 0 check (attempt_count >= 0),
  requested_at timestamptz not null,
  last_attempt_at timestamptz,
  next_attempt_at timestamptz not null,
  last_error_code text check (
    last_error_code is null or char_length(last_error_code) between 1 and 64
  ),
  completed_at timestamptz,
  unique (data_class, collar_id, cutoff),
  check (cutoff <= requested_at),
  check ((status = 'completed') = (stage = 'completed')),
  check ((status = 'completed') = (completed_at is not null)),
  check (status = 'failed' or last_error_code is null)
);

create table private.retention_receipts (
  job_id uuid primary key references private.retention_jobs(id) on delete cascade,
  completed_at timestamptz not null,
  cutoff timestamptz not null,
  telemetry_points_deleted bigint not null check (telemetry_points_deleted >= 0),
  telemetry_chunks_deleted bigint not null check (telemetry_chunks_deleted >= 0),
  receipt_sha256 bytea not null unique check (octet_length(receipt_sha256) = 32)
);

create index retention_jobs_ready_v1_idx
  on private.retention_jobs (next_attempt_at, cutoff, collar_id, id)
  where status in ('pending', 'processing', 'failed');
create index retention_jobs_collar_id_v1_idx
  on private.retention_jobs (collar_id);

alter table private.telemetry_retention_watermarks enable row level security;
alter table private.retention_jobs enable row level security;
alter table private.retention_receipts enable row level security;

revoke all on private.telemetry_retention_watermarks
  from public, anon, authenticated, service_role;
revoke all on private.retention_jobs
  from public, anon, authenticated, service_role;
revoke all on private.retention_receipts
  from public, anon, authenticated, service_role;

-- Keep the dog-deletion manifest exhaustive as the schema grows. These rows
-- cascade through a collar, but they still belong in the pre-delete inventory
-- and the final coordinate-free receipt.
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
    'telemetry_retention_watermarks', (select count(*) from private.telemetry_retention_watermarks row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'retention_jobs', (select count(*) from private.retention_jobs row_value join api.collars collar on collar.id = row_value.collar_id where collar.dog_id = p_dog_id),
    'retention_receipts', (
      select count(*)
      from private.retention_receipts row_value
      join private.retention_jobs job on job.id = row_value.job_id
      join api.collars collar on collar.id = job.collar_id
      where collar.dog_id = p_dog_id
    ),
    'recording_summaries', (
      select count(*)
      from api.recording_summaries row_value
      join api.recordings recording on recording.id = row_value.recording_id
      join api.collars collar on collar.id = recording.collar_id
      where collar.dog_id = p_dog_id
    )
  )
$$;

-- Chunk insertion already takes this same per-collar advisory lock. Taking it
-- again for direct point writes makes the watermark race-safe even when a
-- privileged maintenance path bypasses the normal sync transaction.
create or replace function private.reject_expired_telemetry_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_reject_at_or_before timestamptz;
  v_basis timestamptz;
begin
  perform private.lock_telemetry_collar_v1(new.collar_id);

  select watermark.reject_at_or_before
  into v_reject_at_or_before
  from private.telemetry_retention_watermarks watermark
  where watermark.collar_id = new.collar_id;

  v_basis := coalesce(
    private.telemetry_retention_basis_v1(new.recorded_at, new.received_at),
    new.received_at
  );

  if v_reject_at_or_before is not null and v_basis <= v_reject_at_or_before then
    raise exception using
      errcode = '22023',
      message = 'telemetry_expired_by_retention';
  end if;

  return new;
end
$$;

revoke execute on function private.reject_expired_telemetry_v1()
  from public, anon, authenticated, service_role;

create trigger dog_rgb_reject_expired_telemetry
before insert on api.telemetry_points
for each row execute function private.reject_expired_telemetry_v1();

-- Owner-requested dog deletion snapshots point counts after revoking collars.
-- Acquiring the telemetry lock on that state transition prevents a retention
-- batch from committing between the snapshot and the deletion job creation.
create or replace function private.lock_telemetry_on_collar_revoke_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  if old.state <> 'revoked' and new.state = 'revoked' then
    perform private.lock_telemetry_collar_v1(new.id);
  end if;
  return new;
end
$$;

revoke execute on function private.lock_telemetry_on_collar_revoke_v1()
  from public, anon, authenticated, service_role;

create trigger dog_rgb_lock_telemetry_on_collar_revoke
before update of state on api.collars
for each row execute function private.lock_telemetry_on_collar_revoke_v1();

create or replace function private.retention_job_result_v1(p_job_id uuid)
returns jsonb
language sql
stable
set search_path = ''
as $$
  select jsonb_strip_nulls(jsonb_build_object(
    'job_id', job.id,
    'data_class', job.data_class,
    'collar_id', job.collar_id,
    'cutoff', job.cutoff,
    'status', job.status,
    'stage', job.stage,
    'attempt_count', job.attempt_count,
    'telemetry_points_deleted', job.telemetry_points_deleted,
    'telemetry_chunks_deleted', job.telemetry_chunks_deleted,
    'next_attempt_at', case when job.status <> 'completed' then job.next_attempt_at end,
    'last_error_code', job.last_error_code,
    'completed_at', job.completed_at,
    'receipt_sha256', private.base64url_encode(receipt.receipt_sha256)
  ))
  from private.retention_jobs job
  left join private.retention_receipts receipt on receipt.job_id = job.id
  where job.id = p_job_id
$$;

revoke execute on function private.retention_job_result_v1(uuid)
  from public, anon, authenticated, service_role;

create or replace function private.enqueue_raw_telemetry_retention_v1(
  p_as_of timestamptz default statement_timestamp()
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_as_of timestamptz;
  v_cutoff timestamptz;
  v_requested_at timestamptz := statement_timestamp();
  v_created integer;
  v_total integer;
begin
  if p_as_of is null or p_as_of > v_requested_at + interval '5 minutes' then
    raise exception using errcode = '22023', message = 'invalid_retention_as_of';
  end if;

  -- One deterministic job key per UTC day. Calendar-year subtraction preserves
  -- the intended leap-day behavior of a twelve-month policy.
  v_as_of := pg_catalog.date_trunc('day', p_as_of, 'UTC');
  v_cutoff := v_as_of - interval '1 year';

  insert into private.retention_jobs (
    data_class, collar_id, cutoff, requested_at, next_attempt_at
  )
  select 'raw_telemetry_v1', collar.id, v_cutoff, v_requested_at, v_requested_at
  from api.collars collar
  join api.dogs dog on dog.id = collar.dog_id
  where dog.deleted_at is null
  order by collar.id
  on conflict (data_class, collar_id, cutoff) do nothing;
  get diagnostics v_created = row_count;

  select count(*) into v_total
  from private.retention_jobs job
  where job.data_class = 'raw_telemetry_v1'
    and job.cutoff = v_cutoff;

  return jsonb_build_object(
    'data_class', 'raw_telemetry_v1',
    'as_of', v_as_of,
    'cutoff', v_cutoff,
    'created_jobs', v_created,
    'total_jobs', v_total
  );
end
$$;

revoke execute on function private.enqueue_raw_telemetry_retention_v1(timestamptz)
  from public, anon, authenticated;
grant execute on function private.enqueue_raw_telemetry_retention_v1(timestamptz)
  to service_role;

create or replace function private.process_raw_telemetry_retention_batch_v1(
  p_batch_size integer default 5000
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_job private.retention_jobs%rowtype;
  v_attempted_at timestamptz := statement_timestamp();
  v_deleted bigint := 0;
  v_remaining boolean := false;
  v_receipt_sha256 bytea;
  v_error_state text;
begin
  if p_batch_size not between 1 and 10000 then
    raise exception using errcode = '22023', message = 'invalid_retention_batch_size';
  end if;

  select job.*
  into v_job
  from private.retention_jobs job
  where job.status in ('pending', 'processing', 'failed')
    and job.next_attempt_at <= v_attempted_at
  order by job.cutoff, job.collar_id, job.id
  limit 1
  for update skip locked;

  if not found then
    return jsonb_build_object('disposition', 'idle');
  end if;

  perform private.lock_telemetry_collar_v1(v_job.collar_id);

  update private.retention_jobs
  set status = 'processing',
      attempt_count = attempt_count + 1,
      last_attempt_at = v_attempted_at,
      last_error_code = null
  where id = v_job.id;

  begin
    -- A dog-deletion request owns the stronger operation. Once it has closed
    -- access, retention must not perturb the deletion job's captured counts.
    if not exists (
      select 1
      from api.collars collar
      join api.dogs dog on dog.id = collar.dog_id
      where collar.id = v_job.collar_id
        and dog.deleted_at is null
    ) then
      update private.retention_jobs
      set status = 'completed', stage = 'completed',
          completed_at = v_attempted_at, next_attempt_at = v_attempted_at
      where id = v_job.id;
      return private.retention_job_result_v1(v_job.id)
        || jsonb_build_object('disposition', 'superseded_by_scope_deletion');
    end if;

    insert into private.telemetry_retention_watermarks (
      collar_id, reject_at_or_before, updated_at
    ) values (
      v_job.collar_id, v_job.cutoff, v_attempted_at
    )
    on conflict (collar_id) do update
    set reject_at_or_before = greatest(
          private.telemetry_retention_watermarks.reject_at_or_before,
          excluded.reject_at_or_before
        ),
        updated_at = excluded.updated_at;

    if v_job.stage = 'purge_points' then
      with candidate as (
        (
          select point.ctid, point.recorded_at as retention_basis,
            point.boot_sequence, point.point_sequence
          from api.telemetry_points point
          where point.collar_id = v_job.collar_id
            and not private.telemetry_uses_received_at_v1(
              point.recorded_at, point.received_at
            )
            and point.recorded_at <= v_job.cutoff
          order by point.recorded_at, point.boot_sequence, point.point_sequence
          limit p_batch_size
        )
        union all
        (
          select point.ctid, point.received_at as retention_basis,
            point.boot_sequence, point.point_sequence
          from api.telemetry_points point
          where point.collar_id = v_job.collar_id
            and private.telemetry_uses_received_at_v1(
              point.recorded_at, point.received_at
            )
            and point.received_at <= v_job.cutoff
          order by point.received_at, point.boot_sequence, point.point_sequence
          limit p_batch_size
        )
      ), target as (
        select point.ctid
        from api.telemetry_points point
        join candidate on candidate.ctid = point.ctid
        order by candidate.retention_basis,
          candidate.boot_sequence, candidate.point_sequence
        limit p_batch_size
        for update of point skip locked
      ), deleted as (
        delete from api.telemetry_points point
        using target
        where point.ctid = target.ctid
        returning 1
      )
      select count(*) into v_deleted from deleted;

      update private.retention_jobs
      set telemetry_points_deleted = telemetry_points_deleted + v_deleted
      where id = v_job.id;

      select
        exists (
          select 1
          from api.telemetry_points point
          where point.collar_id = v_job.collar_id
            and not private.telemetry_uses_received_at_v1(
              point.recorded_at, point.received_at
            )
            and point.recorded_at <= v_job.cutoff
        )
        or exists (
          select 1
          from api.telemetry_points point
          where point.collar_id = v_job.collar_id
            and private.telemetry_uses_received_at_v1(
              point.recorded_at, point.received_at
            )
            and point.received_at <= v_job.cutoff
        )
      into v_remaining;

      update private.retention_jobs
      set status = 'pending',
          stage = case when v_remaining then 'purge_points' else 'purge_chunks' end,
          next_attempt_at = v_attempted_at
      where id = v_job.id;

      return private.retention_job_result_v1(v_job.id)
        || jsonb_build_object('batch_points_deleted', v_deleted);
    end if;

    with target as (
      select chunk.ctid
      from private.telemetry_chunks chunk
      where chunk.collar_id = v_job.collar_id
        and not exists (
          select 1
          from api.telemetry_points point
          where point.collar_id = chunk.collar_id
            and point.boot_sequence = chunk.boot_sequence
            and point.chunk_sequence = chunk.chunk_sequence
        )
      order by chunk.boot_sequence, chunk.chunk_sequence
      limit p_batch_size
      for update of chunk skip locked
    ), deleted as (
      delete from private.telemetry_chunks chunk
      using target
      where chunk.ctid = target.ctid
      returning 1
    )
    select count(*) into v_deleted from deleted;

    update private.retention_jobs
    set telemetry_chunks_deleted = telemetry_chunks_deleted + v_deleted
    where id = v_job.id;

    select exists (
      select 1
      from private.telemetry_chunks chunk
      where chunk.collar_id = v_job.collar_id
        and not exists (
          select 1
          from api.telemetry_points point
          where point.collar_id = chunk.collar_id
            and point.boot_sequence = chunk.boot_sequence
            and point.chunk_sequence = chunk.chunk_sequence
        )
    ) into v_remaining;

    if v_remaining then
      update private.retention_jobs
      set status = 'pending', stage = 'purge_chunks', next_attempt_at = v_attempted_at
      where id = v_job.id;
      return private.retention_job_result_v1(v_job.id)
        || jsonb_build_object('batch_chunks_deleted', v_deleted);
    end if;

    update private.telemetry_retention_watermarks
    set purged_at_or_before = greatest(
          coalesce(purged_at_or_before, '-infinity'::timestamptz),
          v_job.cutoff
        ),
        updated_at = v_attempted_at
    where collar_id = v_job.collar_id;

    select extensions.digest(
      convert_to(concat_ws('|',
        'raw-telemetry-retention-receipt-v1',
        job.id::text,
        job.collar_id::text,
        job.cutoff::text,
        job.telemetry_points_deleted::text,
        job.telemetry_chunks_deleted::text,
        v_attempted_at::text
      ), 'utf8'),
      'sha256'
    )
    into v_receipt_sha256
    from private.retention_jobs job
    where job.id = v_job.id;

    insert into private.retention_receipts (
      job_id, completed_at, cutoff, telemetry_points_deleted,
      telemetry_chunks_deleted, receipt_sha256
    )
    select job.id, v_attempted_at, job.cutoff, job.telemetry_points_deleted,
      job.telemetry_chunks_deleted, v_receipt_sha256
    from private.retention_jobs job
    where job.id = v_job.id
    on conflict (job_id) do nothing;

    update private.retention_jobs
    set status = 'completed', stage = 'completed',
        completed_at = v_attempted_at, next_attempt_at = v_attempted_at,
        last_error_code = null
    where id = v_job.id;

    return private.retention_job_result_v1(v_job.id)
      || jsonb_build_object('batch_chunks_deleted', v_deleted);
  exception when others then
    get stacked diagnostics v_error_state = returned_sqlstate;
    update private.retention_jobs
    set status = 'failed',
        next_attempt_at = v_attempted_at + interval '5 minutes',
        last_error_code = left(v_error_state, 64)
    where id = v_job.id;
    return private.retention_job_result_v1(v_job.id);
  end;
end
$$;

revoke execute on function private.process_raw_telemetry_retention_batch_v1(integer)
  from public, anon, authenticated;
grant execute on function private.process_raw_telemetry_retention_batch_v1(integer)
  to service_role;
