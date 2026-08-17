create or replace function private.reverse_bytes(p_value bytea)
returns bytea
language sql
immutable
strict
set search_path = ''
as $$
  select decode(
    string_agg(lpad(to_hex(get_byte(p_value, position)), 2, '0'), '' order by position desc),
    'hex'
  )
  from generate_series(0, octet_length(p_value) - 1) as position
$$;

create or replace function private.track_v3_point_bytes(p_point jsonb)
returns bytea
language sql
immutable
strict
set search_path = ''
as $$
  select
    private.reverse_bytes(pg_catalog.int4send((p_point ->> 0)::integer)) ||
    private.reverse_bytes(pg_catalog.int4send((p_point ->> 1)::integer)) ||
    private.reverse_bytes(substring(pg_catalog.int8send((p_point ->> 2)::bigint) from 5 for 4)) ||
    private.reverse_bytes(substring(pg_catalog.int4send((p_point ->> 3)::integer) from 3 for 2)) ||
    decode(lpad(to_hex((p_point ->> 4)::integer), 2, '0'), 'hex') ||
    decode(lpad(to_hex((p_point ->> 5)::integer), 2, '0'), 'hex')
$$;

revoke execute on function private.reverse_bytes(bytea) from public, anon, authenticated, service_role;
revoke execute on function private.track_v3_point_bytes(jsonb) from public, anon, authenticated, service_role;

create or replace function private.apply_device_mutation_v1(
  p_collar_id uuid,
  p_device_id uuid,
  p_mutation jsonb,
  p_received_ms bigint
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_mutation_id uuid := (p_mutation ->> 'mutation_id')::uuid;
  v_resource_key text := p_mutation ->> 'resource_key';
  v_resource_schema integer := (p_mutation ->> 'resource_schema')::integer;
  v_body jsonb := p_mutation -> 'body';
  v_hash bytea := private.base64url_decode(p_mutation ->> 'body_sha256');
  v_origin text := p_mutation ->> 'origin';
  v_quality text := p_mutation ->> 'time_quality';
  v_submitted_ms bigint := (p_mutation #>> '{authored_hlc,physical_ms}')::bigint;
  v_submitted_logical bigint := (p_mutation #>> '{authored_hlc,logical}')::bigint;
  v_submitted_actor uuid := (p_mutation #>> '{authored_hlc,actor_id}')::uuid;
  v_accepted_ms bigint;
  v_accepted_logical bigint;
  v_accepted_actor uuid;
  v_ordering text;
  v_head api.config_resource_heads%rowtype;
  v_existing api.config_revisions%rowtype;
  v_revision_id uuid := extensions.gen_random_uuid();
  v_disposition text;
  v_rejection text;
  v_version bigint;
  v_wins boolean;
begin
  select * into v_existing
  from api.config_revisions
  where collar_id = p_collar_id and mutation_id = v_mutation_id;
  if found then
    if v_existing.body_sha256 <> v_hash then
      raise exception using errcode = '23505', message = 'mutation_id_conflict';
    end if;
    return jsonb_build_object(
      'mutation_id', v_mutation_id,
      'resource_key', v_resource_key,
      'disposition', v_existing.disposition,
      'replayed', true,
      'server_version', v_existing.server_version,
      'ordering', case when v_existing.disposition = 'rejected' then null else v_existing.ordering_mode end,
      'accepted_hlc', case when v_existing.disposition = 'rejected' then null else jsonb_build_object(
        'physical_ms', v_existing.accepted_hlc_physical_ms,
        'logical', v_existing.accepted_hlc_logical,
        'actor_id', v_existing.accepted_actor_id
      ) end,
      'error_code', v_existing.rejection_code
    );
  end if;

  select * into v_head
  from api.config_resource_heads
  where collar_id = p_collar_id and resource_key = v_resource_key
  for update;

  if v_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted')
     and abs(v_submitted_ms - p_received_ms) <= 600000 then
    v_accepted_ms := v_submitted_ms;
    v_accepted_logical := v_submitted_logical;
    v_accepted_actor := v_submitted_actor;
    v_ordering := 'authored';
  else
    v_accepted_ms := greatest(p_received_ms, coalesce(v_head.accepted_hlc_physical_ms, 0));
    v_accepted_logical := case
      when found and v_accepted_ms = v_head.accepted_hlc_physical_ms
        then v_head.accepted_hlc_logical + 1
      else 0
    end;
    if v_accepted_logical > 4294967295 then
      raise exception using errcode = '22003', message = 'hlc_logical_overflow';
    end if;
    v_accepted_actor := p_device_id;
    v_ordering := 'fallback_received';
  end if;

  if v_resource_key not in (
    'brightness', 'visual_mode', 'simple_effect', 'speed_profile', 'gps_quality', 'geofence_policy'
  ) or v_resource_schema <= 0 or jsonb_typeof(v_body) <> 'object'
     or octet_length(v_hash) <> 32 or pg_column_size(v_body) > 4096 then
    v_rejection := 'invalid_resource';
  elsif v_resource_key = 'brightness' and (
    (select count(*) from jsonb_object_keys(v_body)) <> 1
    or not (v_body ? 'brightness')
    or (v_body ->> 'brightness')::integer not between 1 and 255
  ) then
    v_rejection := 'invalid_brightness';
  end if;

  v_wins := v_rejection is null and (
    not found or row(v_accepted_ms, v_accepted_logical, v_accepted_actor)
      > row(v_head.accepted_hlc_physical_ms, v_head.accepted_hlc_logical, v_head.accepted_actor_id)
  );
  v_disposition := case
    when v_rejection is not null then 'rejected'
    when v_wins then 'winning'
    else 'superseded'
  end;
  v_version := case when v_wins then coalesce(v_head.server_version, 0) + 1 else null end;

  insert into api.config_revisions (
    id, collar_id, resource_key, mutation_id, resource_schema,
    base_server_version, origin, actor_device_id,
    submitted_hlc_physical_ms, submitted_hlc_logical, submitted_actor_id,
    submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
    accepted_actor_id, ordering_mode, server_version, body, body_sha256,
    disposition, rejection_code
  ) values (
    v_revision_id, p_collar_id, v_resource_key, v_mutation_id, v_resource_schema,
    nullif(p_mutation ->> 'base_server_version', '')::bigint, v_origin, p_device_id,
    v_submitted_ms, v_submitted_logical, v_submitted_actor, v_quality,
    v_accepted_ms, v_accepted_logical, v_accepted_actor, v_ordering,
    v_version, v_body, v_hash, v_disposition, v_rejection
  );

  if v_wins then
    insert into api.config_resource_heads (
      collar_id, resource_key, resource_schema, server_version, body,
      body_sha256, winning_revision_id, accepted_hlc_physical_ms,
      accepted_hlc_logical, accepted_actor_id
    ) values (
      p_collar_id, v_resource_key, v_resource_schema, v_version, v_body,
      v_hash, v_revision_id, v_accepted_ms, v_accepted_logical, v_accepted_actor
    ) on conflict (collar_id, resource_key) do update
      set resource_schema = excluded.resource_schema,
          server_version = excluded.server_version,
          body = excluded.body,
          body_sha256 = excluded.body_sha256,
          winning_revision_id = excluded.winning_revision_id,
          accepted_hlc_physical_ms = excluded.accepted_hlc_physical_ms,
          accepted_hlc_logical = excluded.accepted_hlc_logical,
          accepted_actor_id = excluded.accepted_actor_id,
          updated_at = statement_timestamp();
  end if;

  return jsonb_build_object(
    'mutation_id', v_mutation_id,
    'resource_key', v_resource_key,
    'disposition', v_disposition,
    'replayed', false,
    'server_version', v_version,
    'ordering', case when v_rejection is not null then null else v_ordering end,
    'accepted_hlc', case when v_rejection is not null then null else jsonb_build_object(
      'physical_ms', v_accepted_ms,
      'logical', v_accepted_logical,
      'actor_id', v_accepted_actor
    ) end,
    'error_code', v_rejection
  );
end
$$;

create or replace function api.device_sync_v1(
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
  v_collar api.collars%rowtype;
  v_receipt private.sync_requests%rowtype;
  v_chunk jsonb;
  v_summary jsonb;
  v_loss jsonb;
  v_report jsonb;
  v_mutation jsonb;
  v_point jsonb;
  v_ordinal bigint;
  v_chunk_hash bytea;
  v_calculated_chunk_hash bytea;
  v_existing_hash bytea;
  v_new_chunk boolean;
  v_recording_id uuid;
  v_received_at timestamptz := statement_timestamp();
  v_received_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
  v_time_quality text;
  v_accepted_chunks jsonb := '[]'::jsonb;
  v_accepted_summary_ids jsonb := '[]'::jsonb;
  v_accepted_loss_marker_ids jsonb := '[]'::jsonb;
  v_mutation_results jsonb := '[]'::jsonb;
  v_response jsonb;
begin
  if octet_length(p_secret_digest) <> 32 or octet_length(p_request_sha256) <> 32
     or jsonb_typeof(p_request) <> 'object' or pg_column_size(p_request) > 131072
     or (p_request ->> 'protocol_version')::integer <> 1
     or (p_request ->> 'request_id')::uuid <> p_request_id then
    raise exception using errcode = '22023', message = 'invalid_sync_request';
  end if;

  select * into v_credential
  from private.device_credentials
  where credential_id = p_credential_id
  for update;
  if not found or not private.secure_digest_equal(v_credential.secret_digest, p_secret_digest)
     or v_credential.state <> 'active'
     or (v_credential.valid_until is not null and v_credential.valid_until <= v_received_at) then
    raise exception using errcode = '28000', message = 'invalid_device_credential';
  end if;

  select * into v_collar from api.collars
  where id = v_credential.collar_id
  for update;
  if v_collar.state <> 'active'
     or v_collar.device_public_id <> (p_request #>> '{device,device_id}')::uuid then
    raise exception using errcode = '28000', message = 'device_identity_mismatch';
  end if;

  select * into v_receipt from private.sync_requests
  where collar_id = v_collar.id and request_id = p_request_id;
  if found then
    if v_receipt.request_sha256 <> p_request_sha256 then
      raise exception using errcode = '23505', message = 'request_id_conflict';
    end if;
    if v_receipt.status = 'committed' then
      return v_receipt.response_json;
    end if;
    raise exception using errcode = '40001', message = 'request_in_progress';
  end if;

  insert into private.sync_requests (
    collar_id, request_id, request_sha256, protocol_version, status
  ) values (v_collar.id, p_request_id, p_request_sha256, 1, 'processing');

  update private.device_credentials
  set last_used_at = v_received_at
  where credential_id = p_credential_id;
  update api.collars
  set last_sync_at = v_received_at,
      firmware_version = left(p_request #>> '{device,firmware_version}', 64),
      hardware_revision = left(p_request #>> '{device,hardware_revision}', 64),
      protocol_version = (p_request #>> '{device,protocol_version}')::integer,
      telemetry_schema = (p_request #>> '{device,telemetry_schema}')::integer,
      config_schema = (p_request #>> '{device,config_schema}')::integer,
      updated_at = v_received_at
  where id = v_collar.id;

  for v_chunk in select value from jsonb_array_elements(coalesce(p_request #> '{upload,chunks}', '[]'::jsonb))
  loop
    if jsonb_array_length(v_chunk -> 'points') <> (v_chunk ->> 'point_count')::integer
       or (v_chunk ->> 'point_count')::integer not between 1 and 96 then
      raise exception using errcode = '22023', message = 'invalid_chunk_point_count';
    end if;
    v_chunk_hash := private.base64url_decode(v_chunk ->> 'content_sha256');
    select extensions.digest(
      string_agg(private.track_v3_point_bytes(value), ''::bytea order by ordinality),
      'sha256'
    ) into v_calculated_chunk_hash
    from jsonb_array_elements(v_chunk -> 'points') with ordinality;
    if not private.secure_digest_equal(v_chunk_hash, v_calculated_chunk_hash) then
      raise exception using errcode = '22023', message = 'telemetry_hash_mismatch';
    end if;
    select content_sha256 into v_existing_hash
    from private.telemetry_chunks
    where collar_id = v_collar.id
      and boot_sequence = (v_chunk ->> 'boot_sequence')::bigint
      and chunk_sequence = (v_chunk ->> 'chunk_sequence')::bigint;
    if found and v_existing_hash <> v_chunk_hash then
      raise exception using errcode = '23505', message = 'chunk_identity_conflict';
    end if;
    v_new_chunk := not found;

    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, received_at,
      request_id, is_final
    ) values (
      v_collar.id,
      (v_chunk ->> 'boot_sequence')::bigint,
      (v_chunk ->> 'chunk_sequence')::bigint,
      (v_chunk ->> 'first_point_sequence')::bigint,
      (v_chunk ->> 'first_point_sequence')::bigint + (v_chunk ->> 'point_count')::integer - 1,
      (v_chunk ->> 'point_count')::integer,
      v_chunk_hash, v_received_at, p_request_id,
      coalesce((v_chunk ->> 'is_final')::boolean, false)
    ) on conflict (collar_id, boot_sequence, chunk_sequence) do nothing;

    select id into v_recording_id from api.recordings
    where collar_id = v_collar.id and boot_sequence = (v_chunk ->> 'boot_sequence')::bigint
    for update;
    if not found then
      insert into api.recordings (
        collar_id, boot_sequence, timezone_at_start, state,
        first_point_sequence, last_point_sequence, point_count, clock_quality,
        telemetry_schema, firmware_version
      ) values (
        v_collar.id, (v_chunk ->> 'boot_sequence')::bigint,
        (select d.timezone from api.dogs d where d.id = v_collar.dog_id),
        case when coalesce((v_chunk ->> 'is_final')::boolean, false) then 'closed' else 'open' end,
        (v_chunk ->> 'first_point_sequence')::bigint,
        (v_chunk ->> 'first_point_sequence')::bigint + (v_chunk ->> 'point_count')::integer - 1,
        (v_chunk ->> 'point_count')::integer,
        case (v_chunk ->> 'time_quality')::integer
          when 0 then 'unknown' when 1 then 'approximate_persisted'
          when 2 then 'server_anchored' when 3 then 'sntp_synced'
          when 4 then 'gnss_trusted' when 5 then 'legacy_minute'
        end,
        (v_chunk ->> 'telemetry_schema')::integer,
        left(p_request #>> '{device,firmware_version}', 64)
      ) returning id into v_recording_id;
    elsif v_new_chunk then
      update api.recordings
      set first_point_sequence = least(first_point_sequence, (v_chunk ->> 'first_point_sequence')::bigint),
          last_point_sequence = greatest(last_point_sequence,
            (v_chunk ->> 'first_point_sequence')::bigint + (v_chunk ->> 'point_count')::integer - 1),
          point_count = point_count + (v_chunk ->> 'point_count')::integer,
          state = case when coalesce((v_chunk ->> 'is_final')::boolean, false) then 'closed' else state end,
          updated_at = v_received_at
      where id = v_recording_id;
    end if;

    if v_new_chunk then
      v_time_quality := case (v_chunk ->> 'time_quality')::integer
        when 0 then 'unknown' when 1 then 'approximate_persisted'
        when 2 then 'server_anchored' when 3 then 'sntp_synced'
        when 4 then 'gnss_trusted' when 5 then 'legacy_minute'
      end;
      for v_point, v_ordinal in
        select value, ordinality
        from jsonb_array_elements(v_chunk -> 'points') with ordinality
      loop
        insert into api.telemetry_points (
          collar_id, boot_sequence, point_sequence, recorded_at, received_at,
          lat_e7, lon_e7, reported_speed_cmps, satellites, flags,
          time_quality, telemetry_schema, firmware_version, chunk_sequence
        ) values (
          v_collar.id,
          (v_chunk ->> 'boot_sequence')::bigint,
          (v_chunk ->> 'first_point_sequence')::bigint + v_ordinal - 1,
          case when (v_point ->> 2)::bigint = 0 then null
            else to_timestamp((v_point ->> 2)::double precision) end,
          v_received_at,
          case when ((v_point ->> 5)::integer & 1) = 1 then (v_point ->> 0)::integer else null end,
          case when ((v_point ->> 5)::integer & 1) = 1 then (v_point ->> 1)::integer else null end,
          nullif((v_point ->> 3)::integer, 65535),
          (v_point ->> 4)::smallint,
          (v_point ->> 5)::integer,
          v_time_quality,
          (v_chunk ->> 'telemetry_schema')::integer,
          left(p_request #>> '{device,firmware_version}', 64),
          (v_chunk ->> 'chunk_sequence')::bigint
        ) on conflict (collar_id, boot_sequence, point_sequence) do nothing;
      end loop;
    end if;

    v_accepted_chunks := v_accepted_chunks || jsonb_build_array(jsonb_build_object(
      'boot_sequence', (v_chunk ->> 'boot_sequence')::bigint,
      'chunk_sequence', (v_chunk ->> 'chunk_sequence')::bigint,
      'accepted_point_count', (v_chunk ->> 'point_count')::integer,
      'through_point_sequence',
        (v_chunk ->> 'first_point_sequence')::bigint + (v_chunk ->> 'point_count')::integer - 1,
      'content_sha256', v_chunk ->> 'content_sha256'
    ));
  end loop;

  for v_summary in select value from jsonb_array_elements(coalesce(p_request #> '{upload,summaries}', '[]'::jsonb))
  loop
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      max_speed_cmps, valid_points, gap_count, dropped_points, time_quality
    ) values (
      (v_summary ->> 'summary_id')::uuid, v_collar.id, p_request_id,
      (v_summary ->> 'local_date')::date, v_summary ->> 'timezone',
      (v_summary ->> 'source_revision')::bigint,
      (v_summary ->> 'window_start')::timestamptz,
      (v_summary ->> 'window_end')::timestamptz,
      (v_summary ->> 'observed_s')::bigint, (v_summary ->> 'moving_s')::bigint,
      (v_summary ->> 'inactive_s')::bigint, (v_summary ->> 'distance_m')::bigint,
      nullif(v_summary ->> 'max_speed_cmps', '')::integer,
      (v_summary ->> 'valid_points')::integer, (v_summary ->> 'gap_count')::integer,
      (v_summary ->> 'dropped_points')::integer, v_summary ->> 'time_quality'
    ) on conflict (summary_id) do nothing;
    v_accepted_summary_ids := v_accepted_summary_ids ||
      jsonb_build_array(v_summary ->> 'summary_id');
    insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
    values (v_collar.dog_id, (v_summary ->> 'local_date')::date, v_summary ->> 'timezone', 'device_sync')
    on conflict (dog_id, local_date, timezone) do update
      set last_marked_at = statement_timestamp(), reason = excluded.reason;
  end loop;

  for v_loss in select value from jsonb_array_elements(coalesce(p_request #> '{upload,loss_markers}', '[]'::jsonb))
  loop
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason
    ) values (
      (v_loss ->> 'loss_id')::uuid, v_collar.id, p_request_id,
      (v_loss ->> 'first_missing_point_sequence')::bigint,
      (v_loss ->> 'last_missing_point_sequence')::bigint,
      (v_loss ->> 'dropped_points')::integer,
      left(coalesce(v_loss ->> 'reason', 'storage_pressure'), 64)
    ) on conflict (id) do nothing;
    v_accepted_loss_marker_ids := v_accepted_loss_marker_ids ||
      jsonb_build_array(v_loss ->> 'loss_id');
  end loop;

  if exists (
    select 1
    from jsonb_array_elements(coalesce(p_request #> '{configuration,mutations}', '[]'::jsonb)) m
    group by m.value ->> 'local_sequence'
    having count(*) > 1
  ) then
    raise exception using errcode = '22023', message = 'duplicate_local_sequence';
  end if;

  for v_mutation in
    select value
    from jsonb_array_elements(coalesce(p_request #> '{configuration,mutations}', '[]'::jsonb)) with ordinality m(value, ordinality)
    order by
      case when value ->> 'time_quality' in ('server_anchored', 'sntp_synced', 'gnss_trusted') then 0 else 1 end,
      case when value ->> 'time_quality' not in ('server_anchored', 'sntp_synced', 'gnss_trusted')
        then (value ->> 'local_sequence')::bigint else ordinality end
  loop
    v_mutation_results := v_mutation_results || jsonb_build_array(
      private.apply_device_mutation_v1(v_collar.id, v_collar.device_public_id, v_mutation, v_received_ms)
    );
  end loop;

  for v_report in select value from jsonb_array_elements(coalesce(p_request #> '{configuration,reported}', '[]'::jsonb))
  loop
    insert into api.config_reported (
      collar_id, resource_key, reported_server_version, reported_body_sha256,
      status, error_code, firmware_version, config_schema, device_applied_at,
      cloud_received_at
    ) values (
      v_collar.id, v_report ->> 'resource_key', (v_report ->> 'server_version')::bigint,
      private.base64url_decode(v_report ->> 'body_sha256'), v_report ->> 'status',
      v_report ->> 'error_code', left(p_request #>> '{device,firmware_version}', 64),
      (p_request #>> '{device,config_schema}')::integer,
      nullif(v_report ->> 'device_applied_at', '')::timestamptz, v_received_at
    ) on conflict (collar_id, resource_key) do update
      set reported_server_version = excluded.reported_server_version,
          reported_body_sha256 = excluded.reported_body_sha256,
          status = excluded.status, error_code = excluded.error_code,
          firmware_version = excluded.firmware_version,
          config_schema = excluded.config_schema,
          device_applied_at = excluded.device_applied_at,
          cloud_received_at = excluded.cloud_received_at;
  end loop;

  v_response := jsonb_build_object(
    'protocol_version', 1,
    'request_id', p_request_id,
    'server_time', v_received_at,
    'server_hlc', jsonb_build_object(
      'physical_ms', v_received_ms,
      'logical', 0,
      'actor_id', '00000000-0000-4000-8000-000000000001'::uuid
    ),
    'accepted_capability_hash', private.base64url_encode(v_collar.capability_hash),
    'telemetry', jsonb_build_object(
      'accepted_chunks', v_accepted_chunks,
      'rejected_chunks', '[]'::jsonb,
      'accepted_summary_ids', v_accepted_summary_ids,
      'rejected_summaries', '[]'::jsonb,
      'accepted_loss_marker_ids', v_accepted_loss_marker_ids,
      'rejected_loss_markers', '[]'::jsonb
    ),
    'configuration', jsonb_build_object(
      'outcomes', v_mutation_results,
      'desired_resources', coalesce((
        select jsonb_agg(jsonb_build_object(
          'resource_key', h.resource_key,
          'resource_schema', h.resource_schema,
          'server_version', h.server_version,
          'accepted_hlc', jsonb_build_object(
            'physical_ms', h.accepted_hlc_physical_ms,
            'logical', h.accepted_hlc_logical,
            'actor_id', h.accepted_actor_id
          ),
          'body', h.body,
          'body_sha256', private.base64url_encode(h.body_sha256)
        ) order by h.resource_key)
        from api.config_resource_heads h where h.collar_id = v_collar.id
      ), '[]'::jsonb)
    ),
    'next_sync_after_seconds', 900
  );

  update private.sync_requests
  set status = 'committed', committed_at = statement_timestamp(), response_json = v_response
  where collar_id = v_collar.id and request_id = p_request_id;
  return v_response;
end
$$;

revoke execute on function private.apply_device_mutation_v1(uuid, uuid, jsonb, bigint)
  from public, anon, authenticated, service_role;
revoke execute on function api.device_sync_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated;
grant execute on function api.device_sync_v1(uuid, bytea, uuid, bytea, jsonb)
  to service_role;
