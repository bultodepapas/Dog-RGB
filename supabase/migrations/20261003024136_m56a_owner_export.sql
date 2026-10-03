-- M5.6a v1: owner-only exports are materialized by one bounded, stable RPC.
-- Every source read shares the calling statement snapshot. The function fails
-- as a whole when a row or byte limit is exceeded; it never returns a prefix.

create or replace function api.export_dog_data_v1(
  p_dog_id uuid,
  p_recording_id uuid default null
)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
set statement_timeout = '12s'
as $$
declare
  v_user_id uuid := auth.uid();
  v_dog api.dogs%rowtype;
  v_recording api.recordings%rowtype;
  v_recording_collar_id uuid;
  v_collar_count bigint;
  v_recording_count bigint;
  v_point_count bigint;
  v_loss_count bigint;
  v_daily_summary_count bigint;
  v_recording_summary_count bigint;
  v_config_head_count bigint;
  v_config_reported_count bigint;
  v_config_revision_count bigint;
  v_total_rows bigint;
  v_dynamic_bytes bigint;
  v_has_oversized_json boolean;
  v_dog_json jsonb;
  v_collars_json jsonb;
  v_recordings_json jsonb;
  v_daily_summaries_json jsonb;
  v_recording_summaries_json jsonb;
  v_config_heads_json jsonb;
  v_config_reported_json jsonb;
  v_config_revisions_json jsonb;
  v_points_json jsonb;
  v_losses_json jsonb;
  v_result jsonb;
begin
  if v_user_id is null or not exists (
    select 1 from auth.users u where u.id = v_user_id and u.deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;

  select d.* into v_dog
  from api.dogs d
  where d.id = p_dog_id and d.deleted_at is null;

  if not found or not exists (
    select 1 from api.dog_memberships m
    where m.dog_id = p_dog_id and m.user_id = v_user_id and m.role = 'owner'
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  if p_recording_id is not null then
    select r.* into v_recording
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where r.id = p_recording_id and c.dog_id = p_dog_id;

    if not found then
      raise exception using errcode = '42501', message = 'not_authorized';
    end if;
    v_recording_collar_id := v_recording.collar_id;
  end if;

  select count(*) into v_collar_count from (
    select 1 from api.collars c
    where c.dog_id = p_dog_id
      and (p_recording_id is null or c.id = v_recording_collar_id)
    limit 101
  ) bounded;

  select count(*) into v_recording_count from (
    select 1 from api.recordings r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id
      and (p_recording_id is null or r.id = p_recording_id)
    limit 10001
  ) bounded;

  select count(*) into v_point_count from (
    select 1 from api.telemetry_points p
    join api.collars c on c.id = p.collar_id
    where c.dog_id = p_dog_id
      and (p_recording_id is null or (
        p.collar_id = v_recording.collar_id and p.boot_sequence = v_recording.boot_sequence
      ))
    limit 25001
  ) bounded;

  select count(*) into v_loss_count from (
    select 1 from private.telemetry_loss_markers l
    join api.collars c on c.id = l.collar_id
    where c.dog_id = p_dog_id
      and (p_recording_id is null or (
        l.collar_id = v_recording.collar_id and l.boot_sequence = v_recording.boot_sequence
      ))
    limit 10001
  ) bounded;

  select count(*) into v_daily_summary_count from (
    select 1 from api.daily_summaries s
    where p_recording_id is null and s.dog_id = p_dog_id
    limit 5001
  ) bounded;

  select count(*) into v_recording_summary_count from (
    select 1 from api.recording_summaries s
    join api.recordings r on r.id = s.recording_id
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id
      and (p_recording_id is null or r.id = p_recording_id)
    limit 10001
  ) bounded;

  select count(*) into v_config_head_count from (
    select 1 from api.config_resource_heads h
    join api.collars c on c.id = h.collar_id
    where c.dog_id = p_dog_id and p_recording_id is null
    limit 1001
  ) bounded;

  select count(*) into v_config_reported_count from (
    select 1 from api.config_reported r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id and p_recording_id is null
    limit 1001
  ) bounded;

  select count(*) into v_config_revision_count from (
    select 1 from api.config_revisions r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id and p_recording_id is null
    limit 10001
  ) bounded;

  v_total_rows := v_collar_count + v_recording_count + v_point_count + v_loss_count
    + v_daily_summary_count + v_recording_summary_count + v_config_head_count
    + v_config_reported_count + v_config_revision_count;

  -- v1 is intentionally bounded for low-memory DIY deployments. Owners can
  -- download a recording's retained telemetry separately when the dog bundle
  -- exceeds these limits. No endpoint silently truncates retained history.
  if v_collar_count > 100
    or v_recording_count > 10000
    or v_point_count > 25000
    or v_loss_count > 10000
    or v_daily_summary_count > 5000
    or v_recording_summary_count > 10000
    or v_config_head_count > 1000
    or v_config_reported_count > 1000
    or v_config_revision_count > 10000
    or v_total_rows > 50000 then
    raise exception using errcode = '54000', message = 'export_limit_exceeded';
  end if;

  -- Keep arbitrary JSON payloads from defeating the row and response bounds.
  -- The total is checked before constructing the returned arrays.
  select coalesce(sum(octet_length(x.value::text)), 0), coalesce(bool_or(octet_length(x.value::text) > 65536), false)
  into v_dynamic_bytes, v_has_oversized_json
  from (
    select c.capability_manifest as value
    from api.collars c
    where c.dog_id = p_dog_id
      and (p_recording_id is null or c.id = v_recording_collar_id)
      and c.capability_manifest is not null
    union all
    select h.body
    from api.config_resource_heads h
    join api.collars c on c.id = h.collar_id
    where c.dog_id = p_dog_id and p_recording_id is null
    union all
    select r.body
    from api.config_revisions r
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id and p_recording_id is null
    union all
    select s.phase_durations
    from api.recording_summaries s
    join api.recordings r on r.id = s.recording_id
    join api.collars c on c.id = r.collar_id
    where c.dog_id = p_dog_id
      and (p_recording_id is null or r.id = p_recording_id)
      and s.phase_durations is not null
  ) x;

  if v_has_oversized_json or v_dynamic_bytes > 8388608 then
    raise exception using errcode = '54000', message = 'export_limit_exceeded';
  end if;

  v_dog_json := jsonb_build_object(
    'id', v_dog.id,
    'name', v_dog.name,
    'timezone', v_dog.timezone,
    'timezone_effective_at', v_dog.timezone_effective_at,
    'breed', v_dog.breed,
    'birth_date', v_dog.birth_date,
    'weight_kg', v_dog.weight_kg,
    'created_at', v_dog.created_at,
    'updated_at', v_dog.updated_at
  );

  select coalesce(jsonb_agg(jsonb_build_object(
    'id', c.id,
    'display_name', c.display_name,
    'state', c.state,
    'hardware_revision', c.hardware_revision,
    'firmware_version', c.firmware_version,
    'protocol_version', c.protocol_version,
    'telemetry_schema', c.telemetry_schema,
    'config_schema', c.config_schema,
    'capability_manifest', c.capability_manifest,
    'linked_at', c.linked_at,
    'last_sync_at', c.last_sync_at,
    'revoked_at', c.revoked_at,
    'created_at', c.created_at,
    'updated_at', c.updated_at
  ) order by c.created_at, c.id), '[]'::jsonb)
  into v_collars_json
  from api.collars c
  where c.dog_id = p_dog_id
    and (p_recording_id is null or c.id = v_recording_collar_id);

  select coalesce(jsonb_agg(jsonb_build_object(
    'id', r.id,
    'collar_id', r.collar_id,
    'boot_sequence', r.boot_sequence,
    'started_at', r.started_at,
    'ended_at', r.ended_at,
    'timezone_at_start', r.timezone_at_start,
    'state', r.state,
    'first_point_sequence', r.first_point_sequence,
    'last_point_sequence', r.last_point_sequence,
    'point_count', r.point_count,
    'min_lat_e7', r.min_lat_e7,
    'max_lat_e7', r.max_lat_e7,
    'min_lon_e7', r.min_lon_e7,
    'max_lon_e7', r.max_lon_e7,
    'clock_quality', r.clock_quality,
    'telemetry_schema', r.telemetry_schema,
    'firmware_version', r.firmware_version,
    'created_at', r.created_at,
    'updated_at', r.updated_at
  ) order by r.started_at nulls last, r.created_at, r.id), '[]'::jsonb)
  into v_recordings_json
  from api.recordings r
  join api.collars c on c.id = r.collar_id
  where c.dog_id = p_dog_id
    and (p_recording_id is null or r.id = p_recording_id);

  select coalesce(jsonb_agg(jsonb_build_object(
    'dog_id', s.dog_id,
    'local_date', s.local_date,
    'timezone', s.timezone,
    'observed_s', s.observed_s,
    'moving_s', s.moving_s,
    'inactive_s', s.inactive_s,
    'unknown_s', s.unknown_s,
    'distance_m', s.distance_m,
    'average_observed_cmps', s.average_observed_cmps,
    'average_moving_cmps', s.average_moving_cmps,
    'filtered_max_speed_cmps', s.filtered_max_speed_cmps,
    'valid_points', s.valid_points,
    'warning_points', s.warning_points,
    'gap_count', s.gap_count,
    'dropped_points', s.dropped_points,
    'coverage_ratio', s.coverage_ratio,
    'algorithm_version', s.algorithm_version,
    'summary_status', s.summary_status,
    'source_received_at', s.source_received_at,
    'window_start', s.window_start,
    'window_end', s.window_end,
    'source_schema_min', s.source_schema_min,
    'source_schema_max', s.source_schema_max,
    'source_revision', s.source_revision,
    'computed_at', s.computed_at
  ) order by s.local_date, s.algorithm_version), '[]'::jsonb)
  into v_daily_summaries_json
  from api.daily_summaries s
  where p_recording_id is null and s.dog_id = p_dog_id;

  select coalesce(jsonb_agg(jsonb_build_object(
    'recording_id', s.recording_id,
    'observed_s', s.observed_s,
    'moving_s', s.moving_s,
    'inactive_s', s.inactive_s,
    'unknown_s', s.unknown_s,
    'distance_m', s.distance_m,
    'average_observed_cmps', s.average_observed_cmps,
    'average_moving_cmps', s.average_moving_cmps,
    'filtered_max_speed_cmps', s.filtered_max_speed_cmps,
    'valid_points', s.valid_points,
    'warning_points', s.warning_points,
    'gap_count', s.gap_count,
    'dropped_points', s.dropped_points,
    'coverage_ratio', s.coverage_ratio,
    'phase_durations', s.phase_durations,
    'algorithm_version', s.algorithm_version,
    'summary_status', s.summary_status,
    'source_revision', s.source_revision,
    'source_received_at', s.source_received_at,
    'window_start', s.window_start,
    'window_end', s.window_end,
    'window_scope', s.window_scope,
    'source_schema_min', s.source_schema_min,
    'source_schema_max', s.source_schema_max,
    'computed_at', s.computed_at
  ) order by s.recording_id, s.algorithm_version), '[]'::jsonb)
  into v_recording_summaries_json
  from api.recording_summaries s
  join api.recordings r on r.id = s.recording_id
  join api.collars c on c.id = r.collar_id
  where c.dog_id = p_dog_id
    and (p_recording_id is null or r.id = p_recording_id);

  select coalesce(jsonb_agg(jsonb_build_object(
    'collar_id', h.collar_id,
    'resource_key', h.resource_key,
    'resource_schema', h.resource_schema,
    'server_version', h.server_version,
    'body', h.body,
    'accepted_hlc_physical_ms', h.accepted_hlc_physical_ms,
    'accepted_hlc_logical', h.accepted_hlc_logical,
    'updated_at', h.updated_at
  ) order by h.collar_id, h.resource_key), '[]'::jsonb)
  into v_config_heads_json
  from api.config_resource_heads h
  join api.collars c on c.id = h.collar_id
  where c.dog_id = p_dog_id and p_recording_id is null;

  select coalesce(jsonb_agg(jsonb_build_object(
    'collar_id', r.collar_id,
    'resource_key', r.resource_key,
    'reported_server_version', r.reported_server_version,
    'status', r.status,
    'error_code', r.error_code,
    'firmware_version', r.firmware_version,
    'config_schema', r.config_schema,
    'device_applied_at', r.device_applied_at,
    'cloud_received_at', r.cloud_received_at
  ) order by r.collar_id, r.resource_key), '[]'::jsonb)
  into v_config_reported_json
  from api.config_reported r
  join api.collars c on c.id = r.collar_id
  where c.dog_id = p_dog_id and p_recording_id is null;

  select coalesce(jsonb_agg(jsonb_build_object(
    'id', r.id,
    'collar_id', r.collar_id,
    'resource_key', r.resource_key,
    'resource_schema', r.resource_schema,
    'base_server_version', r.base_server_version,
    'origin', r.origin,
    'submitted_hlc_physical_ms', r.submitted_hlc_physical_ms,
    'submitted_hlc_logical', r.submitted_hlc_logical,
    'submitted_time_quality', r.submitted_time_quality,
    'accepted_hlc_physical_ms', r.accepted_hlc_physical_ms,
    'accepted_hlc_logical', r.accepted_hlc_logical,
    'ordering_mode', r.ordering_mode,
    'server_version', r.server_version,
    'body', r.body,
    'disposition', r.disposition,
    'rejection_code', r.rejection_code,
    'received_at', r.received_at
  ) order by r.received_at, r.id), '[]'::jsonb)
  into v_config_revisions_json
  from api.config_revisions r
  join api.collars c on c.id = r.collar_id
  where c.dog_id = p_dog_id and p_recording_id is null;

  select coalesce(jsonb_agg(jsonb_build_object(
    'recording_id', r.id,
    'collar_id', p.collar_id,
    'boot_sequence', p.boot_sequence,
    'chunk_sequence', p.chunk_sequence,
    'point_sequence', p.point_sequence,
    'recorded_at', p.recorded_at,
    'received_at', p.received_at,
    'lat_e7', p.lat_e7,
    'lon_e7', p.lon_e7,
    'latitude', case when p.lat_e7 is null then null else p.lat_e7::double precision / 10000000.0 end,
    'longitude', case when p.lon_e7 is null then null else p.lon_e7::double precision / 10000000.0 end,
    'reported_speed_cmps', p.reported_speed_cmps,
    'satellites', p.satellites,
    'flags', p.flags,
    'time_quality', p.time_quality,
    'telemetry_schema', p.telemetry_schema,
    'firmware_version', p.firmware_version
  ) order by p.collar_id, p.boot_sequence, p.point_sequence), '[]'::jsonb)
  into v_points_json
  from api.telemetry_points p
  join api.collars c on c.id = p.collar_id
  left join api.recordings r
    on r.collar_id = p.collar_id and r.boot_sequence = p.boot_sequence
  where c.dog_id = p_dog_id
    and (p_recording_id is null or (
      p.collar_id = v_recording.collar_id and p.boot_sequence = v_recording.boot_sequence
    ));

  select coalesce(jsonb_agg(jsonb_build_object(
    'recording_id', r.id,
    'collar_id', l.collar_id,
    'boot_sequence', l.boot_sequence,
    'id', l.id,
    'first_missing_point_sequence', l.first_missing_point_sequence,
    'last_missing_point_sequence', l.last_missing_point_sequence,
    'dropped_points', l.dropped_points,
    'reason', l.reason,
    'recorded_at', l.recorded_at,
    'recorded_utc_ms', l.recorded_utc_ms
  ) order by l.collar_id, l.boot_sequence, l.first_missing_point_sequence, l.id), '[]'::jsonb)
  into v_losses_json
  from private.telemetry_loss_markers l
  join api.collars c on c.id = l.collar_id
  left join api.recordings r
    on r.collar_id = l.collar_id and r.boot_sequence = l.boot_sequence
  where c.dog_id = p_dog_id
    and (p_recording_id is null or (
      l.collar_id = v_recording.collar_id and l.boot_sequence = v_recording.boot_sequence
    ));

  v_result := jsonb_build_object(
    'schema_version', 1,
    'complete', true,
    'export_type', case when p_recording_id is null then 'dog_data' else 'recording_geojson_source' end,
    'snapshot_at', transaction_timestamp(),
    'timezone', v_dog.timezone,
    'units', jsonb_build_object(
      'distance', 'm',
      'speed', 'cm/s',
      'weight', 'kg',
      'coordinates', 'WGS84 degrees; integer e7 also included',
      'timestamps', 'UTC ISO-8601; local-day summaries include their source timezone'
    ),
    'dog', v_dog_json,
    'collars', v_collars_json,
    'recordings', v_recordings_json,
    'daily_summaries', v_daily_summaries_json,
    'recording_summaries', v_recording_summaries_json,
    'configuration', jsonb_build_object(
      'resource_heads', v_config_heads_json,
      'device_reported', v_config_reported_json,
      'revisions', v_config_revisions_json
    ),
    'telemetry_points', v_points_json,
    'loss_markers', v_losses_json
  );

  if octet_length(convert_to(v_result::text, 'UTF8')) > 16777216 then
    raise exception using errcode = '54000', message = 'export_limit_exceeded';
  end if;

  return v_result;
end
$$;

revoke all on function api.export_dog_data_v1(uuid, uuid) from public, anon, authenticated;
grant execute on function api.export_dog_data_v1(uuid, uuid) to authenticated;
