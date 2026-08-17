-- Telemetry correctness is database-owned. Edge validation is intentionally
-- duplicated here so alternate service clients cannot create ambiguous history.

alter table private.telemetry_loss_markers
  add column boot_sequence bigint,
  add column recorded_utc_ms bigint;

-- The previous RPC could not persist a contract-valid loss marker because it
-- read obsolete JSON field names. Keep a deterministic namespace for any rows
-- inserted manually before this migration, then make the contract field required.
update private.telemetry_loss_markers
set boot_sequence = 0
where boot_sequence is null;

alter table private.telemetry_loss_markers
  alter column boot_sequence set not null,
  add constraint telemetry_loss_markers_boot_sequence_check
    check (boot_sequence between 0 and 4294967295) not valid,
  add constraint telemetry_loss_markers_recorded_utc_ms_check
    check (recorded_utc_ms is null or recorded_utc_ms between 0 and 4102444800000) not valid,
  add constraint telemetry_loss_markers_exact_range_count_check
    check (dropped_points::bigint = last_missing_point_sequence - first_missing_point_sequence + 1) not valid;

alter table private.telemetry_loss_markers
  validate constraint telemetry_loss_markers_boot_sequence_check;
alter table private.telemetry_loss_markers
  validate constraint telemetry_loss_markers_recorded_utc_ms_check;
alter table private.telemetry_loss_markers
  validate constraint telemetry_loss_markers_exact_range_count_check;

alter table private.device_daily_summaries
  add constraint device_daily_summaries_exact_duration_check
    check (moving_s + inactive_s = observed_s) not valid,
  add constraint device_daily_summaries_positive_window_check
    check (window_end > window_start) not valid,
  add constraint device_daily_summaries_observed_within_window_check
    check (observed_s <= floor(extract(epoch from window_end - window_start))) not valid;

alter table private.device_daily_summaries
  validate constraint device_daily_summaries_exact_duration_check;
alter table private.device_daily_summaries
  validate constraint device_daily_summaries_positive_window_check;
alter table private.device_daily_summaries
  validate constraint device_daily_summaries_observed_within_window_check;

create index telemetry_chunks_stream_range_idx
  on private.telemetry_chunks
    (collar_id, boot_sequence, first_point_sequence, last_point_sequence);
create unique index telemetry_chunks_one_final_per_stream_idx
  on private.telemetry_chunks (collar_id, boot_sequence)
  where is_final;
create index telemetry_loss_markers_stream_range_idx
  on private.telemetry_loss_markers
    (collar_id, boot_sequence, first_missing_point_sequence, last_missing_point_sequence);

create or replace function private.lock_telemetry_collar_v1(p_collar_id uuid)
returns void
language sql
volatile
strict
set search_path = ''
as $$
  select pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended('dog-rgb:telemetry:' || p_collar_id::text, 0)
  )
$$;

create or replace function private.enforce_telemetry_chunk_invariants_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_existing private.telemetry_chunks%rowtype;
begin
  perform private.lock_telemetry_collar_v1(new.collar_id);

  select * into v_existing
  from private.telemetry_chunks
  where collar_id = new.collar_id
    and boot_sequence = new.boot_sequence
    and chunk_sequence = new.chunk_sequence;

  if found then
    if v_existing.first_point_sequence = new.first_point_sequence
       and v_existing.last_point_sequence = new.last_point_sequence
       and v_existing.point_count = new.point_count
       and v_existing.content_sha256 = new.content_sha256
       and v_existing.is_final = new.is_final then
      -- Skip later BEFORE triggers as well as the physical insert. A replay must
      -- not consume quota or rewrite receipt metadata.
      return null;
    end if;
    raise exception using errcode = '23505', message = 'chunk_identity_conflict';
  end if;

  if exists (
    select 1
    from private.telemetry_chunks c
    where c.collar_id = new.collar_id
      and c.boot_sequence = new.boot_sequence
      and c.first_point_sequence <= new.last_point_sequence
      and c.last_point_sequence >= new.first_point_sequence
  ) then
    raise exception using errcode = '22023', message = 'chunk_point_overlap';
  end if;

  if exists (
    select 1
    from private.telemetry_loss_markers l
    where l.collar_id = new.collar_id
      and l.boot_sequence = new.boot_sequence
      and l.first_missing_point_sequence <= new.last_point_sequence
      and l.last_missing_point_sequence >= new.first_point_sequence
  ) then
    raise exception using errcode = '22023', message = 'chunk_loss_overlap';
  end if;

  if exists (
    select 1
    from private.telemetry_chunks c
    where c.collar_id = new.collar_id
      and c.boot_sequence = new.boot_sequence
      and c.is_final
      and new.chunk_sequence > c.chunk_sequence
  ) then
    raise exception using errcode = '22023', message = 'chunk_after_final';
  end if;

  if new.is_final and exists (
    select 1
    from private.telemetry_chunks c
    where c.collar_id = new.collar_id
      and c.boot_sequence = new.boot_sequence
      and (c.is_final or c.chunk_sequence > new.chunk_sequence)
  ) then
    raise exception using errcode = '22023', message = 'final_chunk_not_terminal';
  end if;

  return new;
end
$$;

create or replace function private.enforce_telemetry_loss_invariants_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_existing private.telemetry_loss_markers%rowtype;
  v_marker jsonb;
  v_markers_text text := pg_catalog.current_setting('dog_rgb.loss_markers', true);
begin
  -- device_sync_v1 predates the frozen marker field names. The narrow gateway
  -- stores the original marker array transaction-locally so this trigger can
  -- persist the contract-only boot and clock fields atomically.
  if new.boot_sequence is null and coalesce(v_markers_text, '') <> '' then
    select marker into v_marker
    from jsonb_array_elements(v_markers_text::jsonb) as item(marker)
    where marker ->> 'marker_id' = new.id::text
    limit 1;
    if found then
      new.boot_sequence := (v_marker ->> 'boot_sequence')::bigint;
      new.recorded_utc_ms := nullif(v_marker ->> 'recorded_utc_ms', '')::bigint;
    end if;
  end if;

  perform private.lock_telemetry_collar_v1(new.collar_id);

  select * into v_existing
  from private.telemetry_loss_markers
  where id = new.id;
  if found then
    if v_existing.collar_id = new.collar_id
       and v_existing.boot_sequence = new.boot_sequence
       and v_existing.first_missing_point_sequence = new.first_missing_point_sequence
       and v_existing.last_missing_point_sequence = new.last_missing_point_sequence
       and v_existing.dropped_points = new.dropped_points
       and v_existing.reason = new.reason
       and v_existing.recorded_utc_ms is not distinct from new.recorded_utc_ms then
      return null;
    end if;
    raise exception using errcode = '23505', message = 'loss_marker_identity_conflict';
  end if;

  if exists (
    select 1
    from private.telemetry_loss_markers l
    where l.collar_id = new.collar_id
      and l.boot_sequence = new.boot_sequence
      and l.first_missing_point_sequence <= new.last_missing_point_sequence
      and l.last_missing_point_sequence >= new.first_missing_point_sequence
  ) then
    raise exception using errcode = '22023', message = 'loss_marker_range_overlap';
  end if;

  if exists (
    select 1
    from private.telemetry_chunks c
    where c.collar_id = new.collar_id
      and c.boot_sequence = new.boot_sequence
      and c.first_point_sequence <= new.last_missing_point_sequence
      and c.last_point_sequence >= new.first_missing_point_sequence
  ) then
    raise exception using errcode = '22023', message = 'loss_marker_chunk_overlap';
  end if;

  return new;
end
$$;

create or replace function private.enforce_device_summary_invariants_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_existing private.device_daily_summaries%rowtype;
begin
  perform private.lock_telemetry_collar_v1(new.collar_id);

  if new.timezone !~ '^[A-Za-z][A-Za-z0-9._+-]*(/[A-Za-z0-9._+-]+)+$'
     or not exists (select 1 from pg_catalog.pg_timezone_names where name = new.timezone) then
    raise exception using errcode = '22023', message = 'summary_timezone_invalid';
  end if;

  select * into v_existing
  from private.device_daily_summaries
  where summary_id = new.summary_id;
  if found then
    if v_existing.collar_id = new.collar_id
       and v_existing.local_date = new.local_date
       and v_existing.timezone = new.timezone
       and v_existing.source_revision = new.source_revision
       and v_existing.window_start = new.window_start
       and v_existing.window_end = new.window_end
       and v_existing.observed_s = new.observed_s
       and v_existing.moving_s = new.moving_s
       and v_existing.inactive_s = new.inactive_s
       and v_existing.distance_m = new.distance_m
       and v_existing.max_speed_cmps is not distinct from new.max_speed_cmps
       and v_existing.valid_points = new.valid_points
       and v_existing.gap_count = new.gap_count
       and v_existing.dropped_points = new.dropped_points
       and v_existing.time_quality = new.time_quality then
      return null;
    end if;
    raise exception using errcode = '23505', message = 'summary_identity_conflict';
  end if;

  if exists (
    select 1
    from private.device_daily_summaries s
    where s.collar_id = new.collar_id
      and s.local_date = new.local_date
      and s.source_revision = new.source_revision
  ) then
    raise exception using errcode = '23505', message = 'summary_revision_identity_conflict';
  end if;

  return new;
end
$$;

create or replace function private.reconcile_recording_completeness_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_collar_id uuid := new.collar_id;
  v_boot_sequence bigint := new.boot_sequence;
  v_first bigint;
  v_last bigint;
  v_points bigint;
  v_has_final boolean;
  v_has_loss boolean;
begin
  select min(first_point_sequence), max(last_point_sequence), sum(point_count), bool_or(is_final)
  into v_first, v_last, v_points, v_has_final
  from private.telemetry_chunks
  where collar_id = v_collar_id and boot_sequence = v_boot_sequence;

  select exists (
    select 1 from private.telemetry_loss_markers
    where collar_id = v_collar_id and boot_sequence = v_boot_sequence
  ) into v_has_loss;

  update api.recordings
  set first_point_sequence = v_first,
      last_point_sequence = v_last,
      point_count = coalesce(v_points, 0)::integer,
      state = case
        when not coalesce(v_has_final, false) then 'open'
        when v_has_loss or v_last - v_first + 1 <> v_points then 'incomplete'
        else 'closed'
      end,
      updated_at = statement_timestamp()
  where collar_id = v_collar_id
    and boot_sequence = v_boot_sequence
    and state <> 'legacy';
  return new;
end
$$;

revoke execute on function private.lock_telemetry_collar_v1(uuid)
  from public, anon, authenticated, service_role;
revoke execute on function private.enforce_telemetry_chunk_invariants_v1()
  from public, anon, authenticated, service_role;
revoke execute on function private.enforce_telemetry_loss_invariants_v1()
  from public, anon, authenticated, service_role;
revoke execute on function private.enforce_device_summary_invariants_v1()
  from public, anon, authenticated, service_role;
revoke execute on function private.reconcile_recording_completeness_v1()
  from public, anon, authenticated, service_role;

create trigger dog_rgb_telemetry_invariants
before insert on private.telemetry_chunks
for each row execute function private.enforce_telemetry_chunk_invariants_v1();

create trigger dog_rgb_loss_marker_invariants
before insert on private.telemetry_loss_markers
for each row execute function private.enforce_telemetry_loss_invariants_v1();

create trigger dog_rgb_device_summary_invariants
before insert on private.device_daily_summaries
for each row execute function private.enforce_device_summary_invariants_v1();

create constraint trigger dog_rgb_reconcile_recording_from_chunk
after insert on private.telemetry_chunks
deferrable initially deferred
for each row execute function private.reconcile_recording_completeness_v1();

create constraint trigger dog_rgb_reconcile_recording_from_loss
after insert on private.telemetry_loss_markers
deferrable initially deferred
for each row execute function private.reconcile_recording_completeness_v1();

-- Preserve the public RPC signature while translating the frozen loss-marker
-- contract into the field names used by the original internal transaction.
create or replace function api.device_sync_gateway_v1(
  p_credential_id uuid,
  p_secret_digest bytea,
  p_request_id uuid,
  p_request_sha256 bytea,
  p_request jsonb
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_credential private.device_credentials%rowtype;
  v_original_markers jsonb := coalesce(p_request #> '{upload,loss_markers}', '[]'::jsonb);
  v_normalized_markers jsonb;
  v_normalized_request jsonb;
begin
  select * into v_credential
  from private.device_credentials
  where credential_id = p_credential_id
  for update;
  if not found or not private.secure_digest_equal(v_credential.secret_digest, p_secret_digest) then
    raise exception using errcode = '28000', message = 'invalid_device_credential';
  end if;
  if v_credential.state = 'revoked' then
    raise exception using errcode = '42501', message = 'device_revoked';
  end if;
  if v_credential.state = 'expired'
     or (v_credential.valid_until is not null and v_credential.valid_until <= statement_timestamp()) then
    raise exception using errcode = '28000', message = 'device_credential_expired';
  end if;
  if v_credential.state not in ('active', 'rotating') then
    raise exception using errcode = '28000', message = 'invalid_device_credential';
  end if;

  select coalesce(jsonb_agg(
    (marker - 'marker_id' - 'lost_points') || jsonb_build_object(
      'loss_id', marker -> 'marker_id',
      'dropped_points', marker -> 'lost_points'
    ) order by ordinality
  ), '[]'::jsonb)
  into v_normalized_markers
  from jsonb_array_elements(v_original_markers) with ordinality as item(marker, ordinality);

  perform pg_catalog.set_config('dog_rgb.loss_markers', v_original_markers::text, true);
  v_normalized_request := jsonb_set(
    p_request,
    '{upload,loss_markers}',
    v_normalized_markers,
    false
  );

  return api.device_sync_v1(
    p_credential_id,
    p_secret_digest,
    p_request_id,
    p_request_sha256,
    v_normalized_request
  );
end
$$;

revoke execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated;
grant execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  to service_role;
