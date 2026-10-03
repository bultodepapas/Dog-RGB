-- M4A v1: truthful, bounded cloud summaries. No scheduler is installed here.

alter table api.daily_summaries
  alter column observed_s drop not null,
  alter column moving_s drop not null,
  alter column inactive_s drop not null,
  alter column unknown_s drop not null,
  alter column distance_m drop not null,
  alter column valid_points drop not null,
  alter column warning_points drop not null,
  alter column gap_count drop not null,
  alter column dropped_points drop not null,
  alter column coverage_ratio drop not null,
  add column summary_status text not null default 'available'
    check (summary_status in (
      'available', 'insufficient_retained_data', 'insufficient_time_evidence',
      'source_limit_exceeded'
    )),
  add column source_received_at timestamptz,
  add column window_start timestamptz,
  add column window_end timestamptz,
  add column source_schema_min integer,
  add column source_schema_max integer,
  add constraint daily_summaries_window_pair_check
    check ((window_start is null) = (window_end is null)
      and (window_start is null or window_end >= window_start)),
  add constraint daily_summaries_schema_range_check
    check (
      (source_schema_min is null and source_schema_max is null)
      or (source_schema_min > 0 and source_schema_max >= source_schema_min)
    ),
  add constraint daily_summaries_truthful_state_check
    check (
      (summary_status = 'available'
        and observed_s is not null and moving_s is not null
        and inactive_s is not null and unknown_s is not null
        and valid_points is not null and warning_points is not null
        and gap_count is not null and dropped_points is not null
        and coverage_ratio is not null
        and (window_start is null or (
          window_end >= window_start
          and observed_s + unknown_s =
            floor(extract(epoch from window_end)) - floor(extract(epoch from window_start))
          and moving_s + inactive_s = observed_s
        )))
      or (summary_status <> 'available'
        and observed_s is null and moving_s is null and inactive_s is null
        and unknown_s is null and distance_m is null
        and average_observed_cmps is null and average_moving_cmps is null
        and filtered_max_speed_cmps is null and valid_points is null
        and warning_points is null and gap_count is null and dropped_points is null
        and coverage_ratio is null)
    );

alter table api.recording_summaries
  alter column observed_s drop not null,
  alter column moving_s drop not null,
  alter column inactive_s drop not null,
  alter column unknown_s drop not null,
  alter column distance_m drop not null,
  alter column valid_points drop not null,
  alter column warning_points drop not null,
  alter column gap_count drop not null,
  alter column dropped_points drop not null,
  alter column coverage_ratio drop not null,
  add column summary_status text not null default 'available'
    check (summary_status in (
      'available', 'insufficient_retained_data', 'insufficient_time_evidence',
      'source_limit_exceeded'
    )),
  add column source_revision bigint not null default 0 check (source_revision >= 0),
  add column source_received_at timestamptz,
  add column window_start timestamptz,
  add column window_end timestamptz,
  add column window_scope text check (window_scope in ('recording_bounds', 'trusted_observation_span')),
  add column source_schema_min integer,
  add column source_schema_max integer,
  add constraint recording_summaries_window_pair_check
    check ((window_start is null) = (window_end is null)
      and (window_start is null or window_end >= window_start)),
  add constraint recording_summaries_schema_range_check
    check (
      (source_schema_min is null and source_schema_max is null)
      or (source_schema_min > 0 and source_schema_max >= source_schema_min)
    ),
  add constraint recording_summaries_truthful_state_check
    check (
      (summary_status = 'available'
        and observed_s is not null and moving_s is not null
        and inactive_s is not null and unknown_s is not null
        and valid_points is not null and warning_points is not null
        and gap_count is not null and dropped_points is not null
        and coverage_ratio is not null
        and (window_start is null or (
          window_end >= window_start
          and observed_s + unknown_s =
            floor(extract(epoch from window_end)) - floor(extract(epoch from window_start))
          and moving_s + inactive_s = observed_s
        )))
      or (summary_status <> 'available'
        and observed_s is null and moving_s is null and inactive_s is null
        and unknown_s is null and distance_m is null
        and average_observed_cmps is null and average_moving_cmps is null
        and filtered_max_speed_cmps is null and valid_points is null
        and warning_points is null and gap_count is null and dropped_points is null
        and coverage_ratio is null)
    );

-- M4A v1 freezes duration in seconds, distance in rounded meters, and speed in
-- cm/s. Moving evidence is 20..1111 cm/s, distance segments need >=3m, gaps
-- over 65s stay unknown, and daily/recording inputs cap at 100k/250k points.
-- Flags remain evidence only. Approximate and legacy clocks stay in history
-- but do not create duration, distance, or speed claims.
create or replace function private.compute_telemetry_summary_v1(
  p_points jsonb,
  p_losses jsonb,
  p_window_start timestamptz,
  p_window_end timestamptz,
  p_max_points integer
)
returns jsonb
language plpgsql
stable
set search_path = ''
set timezone = 'UTC'
as $$
declare
  v_start_s bigint;
  v_end_s bigint;
  v_window_s bigint;
  v_point_count integer;
  v_loss_count integer;
  v_valid_points integer := 0;
  v_warning_points integer := 0;
  v_warning_keys text[] := array[]::text[];
  v_gap_count integer := 0;
  v_dropped_points bigint := 0;
  v_moving_s bigint := 0;
  v_inactive_s bigint := 0;
  v_distance_m numeric := 0;
  v_distance_segments integer := 0;
  v_max_speed integer;
  v_intervals jsonb := '[]'::jsonb;
  v_source_schema_min integer;
  v_source_schema_max integer;
  v_current record;
  v_delta_s bigint;
  v_overlap_s bigint;
  v_lat1 double precision;
  v_lon1 double precision;
  v_lat2 double precision;
  v_lon2 double precision;
  v_delta_lat double precision;
  v_delta_lon double precision;
  v_haversine double precision;
  v_segment_m double precision;
  v_segment_speed double precision;
  v_status text;
begin
  v_start_s := floor(extract(epoch from p_window_start))::bigint;
  v_end_s := floor(extract(epoch from p_window_end))::bigint;
  v_window_s := v_end_s - v_start_s;
  if p_window_start is null or p_window_end is null or v_window_s <= 0 then
    return jsonb_build_object(
      'summary_status', 'insufficient_time_evidence',
      'observed_s', null, 'moving_s', null, 'inactive_s', null, 'unknown_s', null,
      'distance_m', null, 'average_observed_cmps', null, 'average_moving_cmps', null,
      'filtered_max_speed_cmps', null, 'valid_points', null, 'warning_points', null,
      'gap_count', null, 'dropped_points', null, 'coverage_ratio', null,
      'source_schema_min', null, 'source_schema_max', null
    );
  end if;

  select count(*)::integer, min((value ->> 'telemetry_schema')::integer),
         max((value ->> 'telemetry_schema')::integer)
  into v_point_count, v_source_schema_min, v_source_schema_max
  from jsonb_array_elements(coalesce(p_points, '[]'::jsonb));

  select count(*)::integer into v_loss_count
  from (
    select distinct value ->> 'id' as marker_id
    from jsonb_array_elements(coalesce(p_losses, '[]'::jsonb))
    where value ->> 'id' is not null
  ) markers;

  if v_point_count > p_max_points or v_loss_count > p_max_points then
    return jsonb_build_object(
      'summary_status', 'source_limit_exceeded',
      'observed_s', null, 'moving_s', null, 'inactive_s', null, 'unknown_s', null,
      'distance_m', null, 'average_observed_cmps', null, 'average_moving_cmps', null,
      'filtered_max_speed_cmps', null, 'valid_points', null, 'warning_points', null,
      'gap_count', null, 'dropped_points', null, 'coverage_ratio', null,
      'source_schema_min', v_source_schema_min, 'source_schema_max', v_source_schema_max
    );
  end if;

  with raw_points as (
    select
      value,
      nullif(value ->> 'recorded_s', '')::bigint as recorded_s,
      coalesce((value ->> 'flags')::integer, 0) as flags,
      nullif(value ->> 'reported_speed_cmps', '')::integer as speed_cmps,
      nullif(value ->> 'lat_e7', '')::integer as lat_e7,
      nullif(value ->> 'lon_e7', '')::integer as lon_e7,
      value ->> 'collar_id' as collar_id,
      (value ->> 'boot_sequence')::bigint as boot_sequence,
      (value ->> 'point_sequence')::bigint as point_sequence,
      value ->> 'time_quality' as time_quality,
      (value ->> 'telemetry_schema')::integer as telemetry_schema
    from jsonb_array_elements(coalesce(p_points, '[]'::jsonb))
  ), eligible_time as (
    select *,
      case
        when flags & 16 = 0 and flags & 32 = 0
          and flags & 2 <> 0 and speed_cmps between 20 and 1111 then 'moving'
        when flags & 16 = 0 and flags & 32 = 0
          and flags & 8 <> 0 and (speed_cmps is null or speed_cmps < 20) then 'stationary'
        else null
      end as evidence_kind
    from raw_points
    where recorded_s is not null
      and time_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted')
      and telemetry_schema = 3
      and flags & 4 <> 0
      and flags & 64 = 0
  ), ordered as (
    select *,
      lag(recorded_s) over stream as previous_s,
      lag(flags) over stream as previous_flags,
      lag(speed_cmps) over stream as previous_speed_cmps,
      lag(lat_e7) over stream as previous_lat_e7,
      lag(lon_e7) over stream as previous_lon_e7,
      lag(evidence_kind) over stream as previous_kind,
      lag(point_sequence) over stream as previous_sequence
    from eligible_time
    window stream as (
      partition by collar_id, boot_sequence
      order by recorded_s, point_sequence
    )
  )
  select count(*) filter (
           where evidence_kind is not null and recorded_s >= v_start_s and recorded_s < v_end_s
         )::integer,
         count(*) filter (
           where flags & 32 <> 0 and recorded_s >= v_start_s and recorded_s < v_end_s
         )::integer
  into v_valid_points, v_gap_count
  from eligible_time;

  select coalesce(array_agg(distinct
    coalesce(value ->> 'collar_id', '') || ':'
      || coalesce(value ->> 'boot_sequence', '') || ':'
      || coalesce(value ->> 'point_sequence', '')
  ) filter (
    where value ->> 'time_quality' not in ('server_anchored', 'sntp_synced', 'gnss_trusted')
      or coalesce((value ->> 'telemetry_schema')::integer, 0) <> 3
      or coalesce((value ->> 'flags')::integer, 0) & 4 = 0
      or coalesce((value ->> 'flags')::integer, 0) & (16 | 32 | 64) <> 0
      or not (
        (coalesce((value ->> 'flags')::integer, 0) & 2 <> 0
          and coalesce((value ->> 'reported_speed_cmps')::integer, -1) between 20 and 1111)
        or (coalesce((value ->> 'flags')::integer, 0) & 8 <> 0
          and ((value ->> 'reported_speed_cmps') is null
            or (value ->> 'reported_speed_cmps')::integer < 20))
      )
      or (value ->> 'reported_speed_cmps')::integer > 1111
  ), array[]::text[])
  into v_warning_keys
  from jsonb_array_elements(coalesce(p_points, '[]'::jsonb));

  with unique_losses as (
    select distinct on (value ->> 'id')
      nullif(coalesce(value ->> 'dropped_points', value ->> 'lost_points'), '')::bigint as dropped_points
    from jsonb_array_elements(coalesce(p_losses, '[]'::jsonb))
    order by value ->> 'id'
  )
  select coalesce(sum(dropped_points), 0)
  into v_dropped_points
  from unique_losses
  where dropped_points > 0;

  for v_current in
    with raw_points as (
      select
        value,
        nullif(value ->> 'recorded_s', '')::bigint as recorded_s,
        coalesce((value ->> 'flags')::integer, 0) as flags,
        nullif(value ->> 'reported_speed_cmps', '')::integer as speed_cmps,
        nullif(value ->> 'lat_e7', '')::integer as lat_e7,
        nullif(value ->> 'lon_e7', '')::integer as lon_e7,
        value ->> 'collar_id' as collar_id,
        (value ->> 'boot_sequence')::bigint as boot_sequence,
        (value ->> 'point_sequence')::bigint as point_sequence,
        value ->> 'time_quality' as time_quality,
        (value ->> 'telemetry_schema')::integer as telemetry_schema
      from jsonb_array_elements(coalesce(p_points, '[]'::jsonb))
    ), eligible_time as (
      select *,
        case
          when flags & 16 = 0 and flags & 32 = 0
            and flags & 2 <> 0 and speed_cmps between 20 and 1111 then 'moving'
          when flags & 16 = 0 and flags & 32 = 0
            and flags & 8 <> 0 and (speed_cmps is null or speed_cmps < 20) then 'stationary'
          else null
        end as evidence_kind
      from raw_points
      where recorded_s is not null
        and time_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted')
        and telemetry_schema = 3
        and flags & 4 <> 0
        and flags & 64 = 0
    ), ordered as (
      select *,
        lag(recorded_s) over stream as previous_s,
        lag(flags) over stream as previous_flags,
        lag(speed_cmps) over stream as previous_speed_cmps,
        lag(lat_e7) over stream as previous_lat_e7,
        lag(lon_e7) over stream as previous_lon_e7,
        lag(evidence_kind) over stream as previous_kind,
        lag(point_sequence) over stream as previous_sequence
      from eligible_time
      window stream as (
        partition by collar_id, boot_sequence
        order by recorded_s, point_sequence
      )
    )
    select * from ordered
    order by collar_id, boot_sequence, recorded_s, point_sequence
  loop
    if v_current.evidence_kind = 'moving'
       and v_current.recorded_s >= v_start_s and v_current.recorded_s < v_end_s then
      if v_current.speed_cmps is not null and v_current.speed_cmps <= 1111 then
        v_max_speed := greatest(coalesce(v_max_speed, 0), v_current.speed_cmps);
      end if;
    end if;

    if v_current.previous_s is null then
      continue;
    end if;
    v_delta_s := v_current.recorded_s - v_current.previous_s;
    v_overlap_s := greatest(
      0,
      least(v_current.recorded_s, v_end_s) - greatest(v_current.previous_s, v_start_s)
    );

    if v_current.point_sequence > v_current.previous_sequence + 1 and v_overlap_s > 0 then
      v_gap_count := v_gap_count + 1;
    end if;
    if v_delta_s > 65 and v_overlap_s > 0 then
      v_gap_count := v_gap_count + 1;
    end if;
    if v_delta_s <= 0 or v_delta_s > 65 then
      if v_delta_s <= 0 then
        v_warning_keys := array_append(
          v_warning_keys,
          coalesce(v_current.collar_id, '') || ':'
            || coalesce(v_current.boot_sequence::text, '') || ':'
            || coalesce(v_current.point_sequence::text, '')
        );
      end if;
      continue;
    end if;
    if v_current.flags & 32 <> 0 or v_current.previous_flags & 32 <> 0 then
      if v_overlap_s > 0 then v_gap_count := v_gap_count + 1; end if;
      continue;
    end if;

    if v_current.evidence_kind = v_current.previous_kind
       and v_current.evidence_kind is not null and v_overlap_s > 0 then
      v_intervals := v_intervals || jsonb_build_array(jsonb_build_object(
        'start', greatest(v_current.previous_s, v_start_s),
        'end', least(v_current.recorded_s, v_end_s),
        'kind', v_current.evidence_kind
      ));
    end if;

    if v_current.evidence_kind = 'moving'
       and v_current.previous_kind = 'moving'
       and v_current.flags & 16 = 0 and v_current.previous_flags & 16 = 0
       and v_current.flags & 32 = 0 and v_current.previous_flags & 32 = 0
       and v_current.flags & 1 <> 0 and v_current.previous_flags & 1 <> 0
       and v_current.lat_e7 between -900000000 and 900000000
       and v_current.lon_e7 between -1800000000 and 1800000000
       and v_current.previous_lat_e7 between -900000000 and 900000000
       and v_current.previous_lon_e7 between -1800000000 and 1800000000 then
      v_lat1 := radians(v_current.previous_lat_e7::double precision / 10000000.0);
      v_lon1 := radians(v_current.previous_lon_e7::double precision / 10000000.0);
      v_lat2 := radians(v_current.lat_e7::double precision / 10000000.0);
      v_lon2 := radians(v_current.lon_e7::double precision / 10000000.0);
      v_delta_lat := v_lat2 - v_lat1;
      v_delta_lon := v_lon2 - v_lon1;
      v_haversine := greatest(0, least(1, power(sin(v_delta_lat / 2), 2)
        + cos(v_lat1) * cos(v_lat2) * power(sin(v_delta_lon / 2), 2)));
      v_segment_m := 6371008.8 * 2 * atan2(sqrt(v_haversine), sqrt(1 - v_haversine));
      v_segment_speed := v_segment_m * 100 / v_delta_s;
      if v_current.previous_s + v_delta_s / 2.0 >= v_start_s
         and v_current.previous_s + v_delta_s / 2.0 < v_end_s then
        if v_segment_m >= 3 and v_segment_speed <= 1111 then
          v_distance_m := v_distance_m + v_segment_m;
          v_distance_segments := v_distance_segments + 1;
          v_max_speed := greatest(coalesce(v_max_speed, 0), round(v_segment_speed)::integer);
        elsif v_segment_speed > 1111 then
          v_warning_keys := array_append(
            v_warning_keys,
            coalesce(v_current.collar_id, '') || ':'
              || coalesce(v_current.boot_sequence::text, '') || ':'
              || coalesce(v_current.point_sequence::text, '')
          );
          v_warning_keys := array_append(
            v_warning_keys,
            coalesce(v_current.collar_id, '') || ':'
              || coalesce(v_current.boot_sequence::text, '') || ':'
              || coalesce(v_current.previous_sequence::text, '')
          );
        end if;
      end if;
    end if;
  end loop;

  select count(distinct warning_key)::integer into v_warning_points
  from unnest(v_warning_keys) as warning(warning_key);

  with intervals as (
    select
      (value ->> 'start')::bigint as interval_start,
      (value ->> 'end')::bigint as interval_end,
      value ->> 'kind' as kind
    from jsonb_array_elements(v_intervals)
  ), events as (
    select interval_start as event_s,
      case when kind = 'moving' then 1 else 0 end as moving_delta,
      case when kind = 'stationary' then 1 else 0 end as inactive_delta
    from intervals
    union all
    select interval_end as event_s,
      case when kind = 'moving' then -1 else 0 end as moving_delta,
      case when kind = 'stationary' then -1 else 0 end as inactive_delta
    from intervals
  ), grouped_events as (
    select event_s, sum(moving_delta) as moving_delta,
      sum(inactive_delta) as inactive_delta
    from events
    group by event_s
  ), segments as (
    select event_s,
      lead(event_s) over (order by event_s) as next_event_s,
      sum(moving_delta) over (order by event_s rows unbounded preceding) as moving_count,
      sum(inactive_delta) over (order by event_s rows unbounded preceding) as inactive_count
    from grouped_events
  )
  select
    coalesce(sum(next_event_s - event_s) filter (
      where next_event_s > event_s and moving_count > 0 and inactive_count = 0
    ), 0),
    coalesce(sum(next_event_s - event_s) filter (
      where next_event_s > event_s and inactive_count > 0 and moving_count = 0
    ), 0)
  into v_moving_s, v_inactive_s
  from segments;

  if v_valid_points = 0 then
    v_status := 'insufficient_time_evidence';
  else
    v_status := 'available';
  end if;

  if v_status <> 'available' then
    return jsonb_build_object(
      'summary_status', v_status,
      'observed_s', null, 'moving_s', null, 'inactive_s', null, 'unknown_s', null,
      'distance_m', null, 'average_observed_cmps', null, 'average_moving_cmps', null,
      'filtered_max_speed_cmps', null, 'valid_points', null, 'warning_points', null,
      'gap_count', null, 'dropped_points', null, 'coverage_ratio', null,
      'source_schema_min', v_source_schema_min, 'source_schema_max', v_source_schema_max
    );
  end if;

  return jsonb_build_object(
    'summary_status', 'available',
    'observed_s', v_moving_s + v_inactive_s,
    'moving_s', v_moving_s,
    'inactive_s', v_inactive_s,
    'unknown_s', v_window_s - v_moving_s - v_inactive_s,
    'distance_m', case when v_distance_segments > 0
      then round(v_distance_m)::bigint else null end,
    'average_observed_cmps', case when v_distance_segments > 0 and v_moving_s + v_inactive_s > 0
      then round(v_distance_m * 100 / (v_moving_s + v_inactive_s))::integer else null end,
    'average_moving_cmps', case when v_distance_segments > 0 and v_moving_s > 0
      then round(v_distance_m * 100 / v_moving_s)::integer else null end,
    'filtered_max_speed_cmps', v_max_speed,
    'valid_points', v_valid_points,
    'warning_points', v_warning_points,
    'gap_count', v_gap_count,
    'dropped_points', v_dropped_points,
    'coverage_ratio', round((v_moving_s + v_inactive_s)::numeric / v_window_s, 6),
    'source_schema_min', v_source_schema_min,
    'source_schema_max', v_source_schema_max
  );
end
$$;

revoke execute on function private.compute_telemetry_summary_v1(jsonb, jsonb, timestamptz, timestamptz, integer)
  from public, anon, authenticated, service_role;

-- New points and loss markers invalidate both the civil day and their
-- recording. The receipt-local day is only a queue key for clockless evidence;
-- it is never treated as that observation's event date by the producer.
create or replace function private.mark_telemetry_summary_dirty_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  update api.recordings recording
  set updated_at = statement_timestamp()
  from (
    select distinct collar_id, boot_sequence from new_points
  ) source
  where recording.collar_id = source.collar_id
    and recording.boot_sequence = source.boot_sequence;

  insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
  select distinct affected.dog_id, marked_dates.local_date, affected.timezone, 'telemetry_points'
  from (
    select dog.id as dog_id, dog.timezone,
      case when point.recorded_at is null
        then (point.received_at at time zone dog.timezone)::date
        else (point.recorded_at at time zone dog.timezone)::date end as local_date,
      point.recorded_at,
      (point.recorded_at at time zone dog.timezone)::date as event_date
    from new_points point
    join api.collars collar on collar.id = point.collar_id
    join api.dogs dog on dog.id = collar.dog_id
  ) affected
  cross join lateral (
    select affected.local_date as local_date
    union
    select affected.event_date - 1
    where affected.recorded_at is not null
      and affected.recorded_at < ((affected.event_date::timestamp at time zone affected.timezone) + interval '65 seconds')
    union
    select affected.event_date + 1
    where affected.recorded_at is not null
      and affected.recorded_at >= (((affected.event_date + 1)::timestamp at time zone affected.timezone) - interval '65 seconds')
  ) marked_dates
  on conflict (dog_id, local_date, timezone) do update
    set last_marked_at = statement_timestamp(), reason = excluded.reason;

  return null;
end
$$;

revoke execute on function private.mark_telemetry_summary_dirty_v1() from public, anon, authenticated, service_role;

create trigger dog_rgb_mark_telemetry_summary_dirty_v1
after insert on api.telemetry_points
referencing new table as new_points
for each statement execute function private.mark_telemetry_summary_dirty_v1();

create or replace function private.mark_loss_summary_dirty_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  update api.recordings recording
  set updated_at = statement_timestamp()
  from (
    select distinct collar_id, boot_sequence from new_losses
  ) source
  where recording.collar_id = source.collar_id
    and recording.boot_sequence = source.boot_sequence;

  insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
  select distinct collar.dog_id,
    case when loss.recorded_utc_ms is null
      then (loss.recorded_at at time zone dog.timezone)::date
      else (to_timestamp(loss.recorded_utc_ms / 1000.0) at time zone dog.timezone)::date end,
    dog.timezone,
    'telemetry_loss'
  from new_losses loss
  join api.collars collar on collar.id = loss.collar_id
  join api.dogs dog on dog.id = collar.dog_id
  on conflict (dog_id, local_date, timezone) do update
    set last_marked_at = statement_timestamp(), reason = excluded.reason;

  return null;
end
$$;

revoke execute on function private.mark_loss_summary_dirty_v1() from public, anon, authenticated, service_role;

create trigger dog_rgb_mark_loss_summary_dirty_v1
after insert on private.telemetry_loss_markers
referencing new table as new_losses
for each statement execute function private.mark_loss_summary_dirty_v1();

create index telemetry_loss_markers_event_time_v1_idx
  on private.telemetry_loss_markers (collar_id, recorded_utc_ms, id)
  where recorded_utc_ms is not null;
create index telemetry_loss_markers_receipt_time_v1_idx
  on private.telemetry_loss_markers (collar_id, recorded_at, id)
  where recorded_utc_ms is null;

create or replace function private.recompute_dirty_summaries_v1(p_limit integer default 4)
returns integer
language plpgsql
security definer
set search_path = ''
set statement_timeout = '10s'
as $$
declare
  v_dirty private.dirty_summary_days%rowtype;
  v_dog_timezone text;
  v_day_start timestamptz;
  v_day_end timestamptz;
  v_as_of timestamptz := statement_timestamp();
  v_computed_at timestamptz;
  v_points jsonb;
  v_losses jsonb;
  v_result jsonb;
  v_daily_count integer;
  v_source_received_at timestamptz;
  v_source_revision bigint;
  v_daily_schema_min integer;
  v_daily_schema_max integer;
  v_existing_source_at timestamptz;
  v_retention_incomplete boolean;
  v_recording api.recordings%rowtype;
  v_recording_points jsonb;
  v_recording_losses jsonb;
  v_recording_count integer;
  v_recording_source_at timestamptz;
  v_recording_start timestamptz;
  v_recording_end timestamptz;
  v_window_scope text;
  v_more_recordings boolean;
  v_processed_recordings integer;
  v_consumed integer := 0;
begin
  if p_limit is null or p_limit not between 1 and 4 then
    raise exception using errcode = '22023', message = 'invalid_batch_limit';
  end if;

  for v_dirty in
    select dirty.*
    from private.dirty_summary_days dirty
    order by dirty.first_marked_at, dirty.dog_id, dirty.local_date, dirty.timezone
    for update of dirty skip locked
    limit p_limit
  loop
    v_computed_at := statement_timestamp();
    select dog.timezone into v_dog_timezone
    from api.dogs dog where dog.id = v_dirty.dog_id;
    if v_dog_timezone is null then
      delete from private.dirty_summary_days
      where (dog_id, local_date, timezone) = (v_dirty.dog_id, v_dirty.local_date, v_dirty.timezone);
      v_consumed := v_consumed + 1;
      continue;
    end if;

    v_day_start := v_dirty.local_date::timestamp at time zone v_dirty.timezone;
    v_day_end := case
      when v_dirty.local_date = (v_as_of at time zone v_dirty.timezone)::date then v_as_of
      else (v_dirty.local_date + 1)::timestamp at time zone v_dirty.timezone
    end;
    v_day_end := greatest(v_day_start, v_day_end);

    select count(*)::integer,
      coalesce(jsonb_agg(source.point_json order by source.collar_id, source.boot_sequence,
        source.recorded_s nulls last, source.point_sequence), '[]'::jsonb),
      max(source.received_at), min(source.telemetry_schema), max(source.telemetry_schema)
    into v_daily_count, v_points, v_source_received_at,
      v_daily_schema_min, v_daily_schema_max
    from (
      select
        jsonb_build_object(
          'collar_id', point.collar_id,
          'boot_sequence', point.boot_sequence,
          'point_sequence', point.point_sequence,
          'recorded_s', floor(extract(epoch from point.recorded_at))::bigint,
          'lat_e7', point.lat_e7,
          'lon_e7', point.lon_e7,
          'reported_speed_cmps', point.reported_speed_cmps,
          'flags', point.flags,
          'time_quality', point.time_quality,
          'telemetry_schema', point.telemetry_schema
        ) as point_json,
        point.collar_id, point.boot_sequence, point.recorded_at,
        floor(extract(epoch from point.recorded_at))::bigint as recorded_s,
        point.point_sequence, point.received_at, point.telemetry_schema
      from api.telemetry_points point
      join api.collars collar on collar.id = point.collar_id
      where collar.dog_id = v_dirty.dog_id
        and (
          point.recorded_at >= v_day_start - interval '65 seconds'
          and point.recorded_at < v_day_end + interval '65 seconds'
          or point.recorded_at is null
            and (point.received_at at time zone v_dirty.timezone)::date = v_dirty.local_date
        )
      order by point.collar_id, point.boot_sequence, point.recorded_at nulls last, point.point_sequence
      limit 100001
    ) source;

    select coalesce(jsonb_agg(jsonb_build_object(
        'id', loss_source.id,
        'dropped_points', loss_source.dropped_points
      ) order by loss_source.id), '[]'::jsonb)
    into v_losses
    from (
      select * from (
      (
        select loss.id, loss.dropped_points
        from private.telemetry_loss_markers loss
        join api.collars collar on collar.id = loss.collar_id
        where collar.dog_id = v_dirty.dog_id
          and loss.recorded_utc_ms >= floor(extract(epoch from v_day_start) * 1000)::bigint
          and loss.recorded_utc_ms < floor(extract(epoch from v_day_end) * 1000)::bigint
        order by loss.collar_id, loss.recorded_utc_ms, loss.id
        limit 100001
      )
      union all
      (
        select loss.id, loss.dropped_points
        from private.telemetry_loss_markers loss
        join api.collars collar on collar.id = loss.collar_id
        where collar.dog_id = v_dirty.dog_id
          and loss.recorded_utc_ms is null
          and loss.recorded_at >= v_day_start
          and loss.recorded_at < v_day_end
        order by loss.collar_id, loss.recorded_at, loss.id
        limit 100001
      )
      ) candidates
      order by candidates.id
      limit 100001
    ) loss_source;

    v_source_received_at := greatest(
      coalesce(v_source_received_at, v_dirty.last_marked_at),
      v_dirty.last_marked_at
    );
    v_source_revision := floor(extract(epoch from v_source_received_at) * 1000000)::bigint;

    select exists (
      select 1
      from private.telemetry_retention_watermarks watermark
      join api.collars collar on collar.id = watermark.collar_id
      join api.recordings recording on recording.collar_id = collar.id
      where collar.dog_id = v_dirty.dog_id
        and watermark.purged_at_or_before >= v_day_start
        and (recording.started_at is null or recording.started_at <= watermark.purged_at_or_before)
        and (recording.ended_at is null or recording.ended_at >= v_day_start)
    ) into v_retention_incomplete;

    select api.daily_summaries.source_received_at into v_existing_source_at
    from api.daily_summaries
    where dog_id = v_dirty.dog_id and local_date = v_dirty.local_date
      and timezone = v_dirty.timezone and algorithm_version = 1;
    if not found or v_existing_source_at is null
       or v_existing_source_at < v_dirty.last_marked_at
       or v_dirty.local_date = (v_as_of at time zone v_dirty.timezone)::date then
      v_source_revision := floor(extract(epoch from v_source_received_at) * 1000000)::bigint;
      v_result := private.compute_telemetry_summary_v1(
        v_points, v_losses, v_day_start, v_day_end, 100000
      );
      if v_retention_incomplete then
        v_result := jsonb_set(v_result, '{summary_status}', '"insufficient_retained_data"'::jsonb);
        v_result := v_result || jsonb_build_object(
          'observed_s', null, 'moving_s', null, 'inactive_s', null, 'unknown_s', null,
          'distance_m', null, 'average_observed_cmps', null, 'average_moving_cmps', null,
          'filtered_max_speed_cmps', null, 'valid_points', null, 'warning_points', null,
          'gap_count', null, 'dropped_points', null, 'coverage_ratio', null
        );
      end if;

      insert into api.daily_summaries (
        dog_id, local_date, timezone, observed_s, moving_s, inactive_s, unknown_s,
        distance_m, average_observed_cmps, average_moving_cmps, filtered_max_speed_cmps,
        valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
        algorithm_version, source_revision, computed_at, summary_status, source_received_at,
        window_start, window_end, source_schema_min, source_schema_max
      ) values (
        v_dirty.dog_id, v_dirty.local_date, v_dirty.timezone,
        (v_result ->> 'observed_s')::bigint, (v_result ->> 'moving_s')::bigint,
        (v_result ->> 'inactive_s')::bigint, (v_result ->> 'unknown_s')::bigint,
        (v_result ->> 'distance_m')::bigint,
        (v_result ->> 'average_observed_cmps')::integer,
        (v_result ->> 'average_moving_cmps')::integer,
        (v_result ->> 'filtered_max_speed_cmps')::integer,
        (v_result ->> 'valid_points')::integer, (v_result ->> 'warning_points')::integer,
        (v_result ->> 'gap_count')::integer, (v_result ->> 'dropped_points')::integer,
        (v_result ->> 'coverage_ratio')::numeric, 1, v_source_revision, v_computed_at,
        v_result ->> 'summary_status', v_source_received_at, v_day_start, v_day_end,
        (v_result ->> 'source_schema_min')::integer, (v_result ->> 'source_schema_max')::integer
      ) on conflict (dog_id, local_date, algorithm_version) do update set
        timezone = excluded.timezone,
        observed_s = excluded.observed_s,
        moving_s = excluded.moving_s,
        inactive_s = excluded.inactive_s,
        unknown_s = excluded.unknown_s,
        distance_m = excluded.distance_m,
        average_observed_cmps = excluded.average_observed_cmps,
        average_moving_cmps = excluded.average_moving_cmps,
        filtered_max_speed_cmps = excluded.filtered_max_speed_cmps,
        valid_points = excluded.valid_points,
        warning_points = excluded.warning_points,
        gap_count = excluded.gap_count,
        dropped_points = excluded.dropped_points,
        coverage_ratio = excluded.coverage_ratio,
        source_revision = excluded.source_revision,
        computed_at = excluded.computed_at,
        summary_status = excluded.summary_status,
        source_received_at = excluded.source_received_at,
        window_start = excluded.window_start,
        window_end = excluded.window_end,
        source_schema_min = excluded.source_schema_min,
        source_schema_max = excluded.source_schema_max;
    end if;

    v_processed_recordings := 0;
    for v_recording in
      select recording.*
      from api.recordings recording
      join api.collars collar on collar.id = recording.collar_id
      left join api.recording_summaries summary
        on summary.recording_id = recording.id and summary.algorithm_version = 1
      where collar.dog_id = v_dirty.dog_id
        and (
          recording.point_count > 0
          or exists (
            select 1 from private.telemetry_loss_markers loss
            where loss.collar_id = recording.collar_id
              and loss.boot_sequence = recording.boot_sequence
          )
        )
        and (summary.recording_id is null or summary.source_received_at is null
          or summary.source_received_at < recording.updated_at)
      order by recording.updated_at, recording.id
      for update of recording skip locked
      limit 1
    loop
      v_processed_recordings := v_processed_recordings + 1;
      select count(*)::integer,
        coalesce(jsonb_agg(source.point_json order by source.point_sequence), '[]'::jsonb),
        max(source.received_at),
        min(source.recorded_at) filter (
          where source.time_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted')
            and source.telemetry_schema = 3 and source.flags & 4 <> 0 and source.flags & 64 = 0
        ),
        max(source.recorded_at) filter (
          where source.time_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted')
            and source.telemetry_schema = 3 and source.flags & 4 <> 0 and source.flags & 64 = 0
        )
      into v_recording_count, v_recording_points, v_recording_source_at,
        v_recording_start, v_recording_end
      from (
        select
          jsonb_build_object(
            'collar_id', point.collar_id,
            'boot_sequence', point.boot_sequence,
            'point_sequence', point.point_sequence,
            'recorded_s', floor(extract(epoch from point.recorded_at))::bigint,
            'lat_e7', point.lat_e7,
            'lon_e7', point.lon_e7,
            'reported_speed_cmps', point.reported_speed_cmps,
            'flags', point.flags,
            'time_quality', point.time_quality,
            'telemetry_schema', point.telemetry_schema
          ) as point_json,
          point.point_sequence, point.received_at, point.recorded_at, point.telemetry_schema,
          point.time_quality, point.flags
        from api.telemetry_points point
        where point.collar_id = v_recording.collar_id
          and point.boot_sequence = v_recording.boot_sequence
        order by point.point_sequence
        limit 250001
      ) source;

      select coalesce(jsonb_agg(jsonb_build_object(
          'id', loss_source.id,
          'dropped_points', loss_source.dropped_points
        ) order by loss_source.id), '[]'::jsonb)
      into v_recording_losses
      from (
        select loss.id, loss.dropped_points
        from private.telemetry_loss_markers loss
        where loss.collar_id = v_recording.collar_id
          and loss.boot_sequence = v_recording.boot_sequence
        order by loss.first_missing_point_sequence, loss.last_missing_point_sequence, loss.id
        limit 250001
      ) loss_source;

      v_recording_start := coalesce(v_recording.started_at, v_recording_start);
      v_recording_end := case
        when v_recording.ended_at is not null then least(v_recording.ended_at, v_as_of)
        when v_recording.state = 'open' then v_as_of
        else v_recording_end
      end;
      v_window_scope := case
        when v_recording.started_at is not null then 'recording_bounds'
        else 'trusted_observation_span'
      end;
      if v_recording_start is null or v_recording_end is null then
        v_recording_start := null;
        v_recording_end := null;
      else
        v_recording_end := greatest(v_recording_start, v_recording_end);
      end if;
      v_recording_source_at := coalesce(v_recording.updated_at, v_recording_source_at, v_computed_at);
      v_source_revision := floor(extract(epoch from v_recording_source_at) * 1000000)::bigint;
      v_result := private.compute_telemetry_summary_v1(
        v_recording_points, v_recording_losses, v_recording_start, v_recording_end, 250000
      );

      select watermark.purged_at_or_before >= coalesce(v_recording.started_at, v_recording_start)
      into v_retention_incomplete
      from private.telemetry_retention_watermarks watermark
      where watermark.collar_id = v_recording.collar_id;
      v_retention_incomplete := coalesce(v_retention_incomplete, false);
      if v_retention_incomplete then
        v_result := jsonb_set(v_result, '{summary_status}', '"insufficient_retained_data"'::jsonb);
        v_result := v_result || jsonb_build_object(
          'observed_s', null, 'moving_s', null, 'inactive_s', null, 'unknown_s', null,
          'distance_m', null, 'average_observed_cmps', null, 'average_moving_cmps', null,
          'filtered_max_speed_cmps', null, 'valid_points', null, 'warning_points', null,
          'gap_count', null, 'dropped_points', null, 'coverage_ratio', null
        );
      end if;

      insert into api.recording_summaries (
        recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
        average_observed_cmps, average_moving_cmps, filtered_max_speed_cmps,
        valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
        algorithm_version, computed_at, summary_status, source_revision, source_received_at,
        window_start, window_end, window_scope, source_schema_min, source_schema_max
      ) values (
        v_recording.id, (v_result ->> 'observed_s')::bigint,
        (v_result ->> 'moving_s')::bigint, (v_result ->> 'inactive_s')::bigint,
        (v_result ->> 'unknown_s')::bigint, (v_result ->> 'distance_m')::bigint,
        (v_result ->> 'average_observed_cmps')::integer,
        (v_result ->> 'average_moving_cmps')::integer,
        (v_result ->> 'filtered_max_speed_cmps')::integer,
        (v_result ->> 'valid_points')::integer, (v_result ->> 'warning_points')::integer,
        (v_result ->> 'gap_count')::integer, (v_result ->> 'dropped_points')::integer,
        (v_result ->> 'coverage_ratio')::numeric, 1, v_computed_at,
        v_result ->> 'summary_status', v_source_revision, v_recording_source_at,
        v_recording_start, v_recording_end, v_window_scope,
        (v_result ->> 'source_schema_min')::integer, (v_result ->> 'source_schema_max')::integer
      ) on conflict (recording_id, algorithm_version) do update set
        observed_s = excluded.observed_s,
        moving_s = excluded.moving_s,
        inactive_s = excluded.inactive_s,
        unknown_s = excluded.unknown_s,
        distance_m = excluded.distance_m,
        average_observed_cmps = excluded.average_observed_cmps,
        average_moving_cmps = excluded.average_moving_cmps,
        filtered_max_speed_cmps = excluded.filtered_max_speed_cmps,
        valid_points = excluded.valid_points,
        warning_points = excluded.warning_points,
        gap_count = excluded.gap_count,
        dropped_points = excluded.dropped_points,
        coverage_ratio = excluded.coverage_ratio,
        computed_at = excluded.computed_at,
        summary_status = excluded.summary_status,
        source_revision = excluded.source_revision,
        source_received_at = excluded.source_received_at,
        window_start = excluded.window_start,
        window_end = excluded.window_end,
        window_scope = excluded.window_scope,
        source_schema_min = excluded.source_schema_min,
        source_schema_max = excluded.source_schema_max;
    end loop;

    select exists (
      select 1
      from api.recordings recording
      join api.collars collar on collar.id = recording.collar_id
      left join api.recording_summaries summary
        on summary.recording_id = recording.id and summary.algorithm_version = 1
      where collar.dog_id = v_dirty.dog_id
        and (recording.point_count > 0 or exists (
          select 1 from private.telemetry_loss_markers loss
          where loss.collar_id = recording.collar_id
            and loss.boot_sequence = recording.boot_sequence
        ))
        and (summary.recording_id is null or summary.source_received_at is null
          or summary.source_received_at < recording.updated_at)
    ) into v_more_recordings;

    if not v_more_recordings then
      delete from private.dirty_summary_days
      where (dog_id, local_date, timezone) = (v_dirty.dog_id, v_dirty.local_date, v_dirty.timezone);
      v_consumed := v_consumed + 1;
    end if;
  end loop;

  return v_consumed;
end
$$;

revoke execute on function private.recompute_dirty_summaries_v1(integer)
  from public, anon, authenticated;
grant execute on function private.recompute_dirty_summaries_v1(integer) to service_role;

create or replace function api.summary_freshness_v1(
  p_dog_id uuid,
  p_local_date date default null,
  p_recording_id uuid default null
)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_pending boolean := false;
  v_status text;
  v_source_received_at timestamptz;
  v_computed_at timestamptz;
  v_algorithm_version integer;
  v_window_start timestamptz;
  v_window_end timestamptz;
  v_summary_status text;
  v_source_schema_min integer;
  v_source_schema_max integer;
  v_timezone text;
  v_dog_timezone text;
  v_queue_timezone text;
  v_recording_is_open boolean;
  v_recording_updated_at timestamptz;
  v_recording_window_start timestamptz;
  v_recording_window_end timestamptz;
begin
  if ((p_local_date is null) = (p_recording_id is null)) then
    raise exception using errcode = '22023', message = 'invalid_freshness_scope';
  end if;
  if v_user_id is null or not exists (
    select 1 from auth.users auth_user
    where auth_user.id = v_user_id and auth_user.deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if not private.can_read_dog(p_dog_id) then
    return null;
  end if;

  if p_local_date is not null then
    select dog.timezone into v_dog_timezone
    from api.dogs dog where dog.id = p_dog_id;
    v_timezone := v_dog_timezone;
    select coalesce(bool_or(true), false), max(dirty.last_marked_at), max(dirty.timezone)
    into v_pending, v_source_received_at, v_queue_timezone
    from private.dirty_summary_days dirty
    where dirty.dog_id = p_dog_id and dirty.local_date = p_local_date;

    select summary.summary_status, summary.source_received_at, summary.computed_at,
      summary.algorithm_version, summary.window_start, summary.window_end,
      summary.source_schema_min, summary.source_schema_max, summary.timezone
    into v_summary_status, v_source_received_at, v_computed_at,
      v_algorithm_version, v_window_start, v_window_end,
      v_source_schema_min, v_source_schema_max, v_timezone
    from api.daily_summaries summary
    where summary.dog_id = p_dog_id and summary.local_date = p_local_date
    order by summary.algorithm_version desc
    limit 1;

    if v_pending then
      v_status := 'pending';
      select max(dirty.last_marked_at) into v_source_received_at
      from private.dirty_summary_days dirty
      where dirty.dog_id = p_dog_id and dirty.local_date = p_local_date;
    elsif v_summary_status is null then
      v_status := 'unavailable';
    elsif v_source_received_at is null or v_window_start is null or v_window_end is null then
      v_status := 'stale';
    elsif v_timezone is distinct from v_dog_timezone then
      v_status := 'stale';
    elsif p_local_date = (statement_timestamp() at time zone v_dog_timezone)::date
      and v_window_end < statement_timestamp() then
      -- No scheduler is installed: a current-day row is a snapshot and ages
      -- stale as its denominator grows, even without another telemetry write.
      v_status := 'stale';
    else
      v_status := v_summary_status;
    end if;
    v_timezone := case when v_timezone is distinct from v_dog_timezone
      then v_dog_timezone else coalesce(v_timezone, v_queue_timezone, v_dog_timezone) end;
    return jsonb_build_object(
      'scope', 'daily', 'dog_id', p_dog_id, 'local_date', p_local_date,
      'timezone', v_timezone, 'status', v_status, 'pending', v_pending,
      'summary_status', v_summary_status,
      'source_received_at', v_source_received_at, 'computed_at', v_computed_at,
      'algorithm_version', v_algorithm_version,
      'window_start', v_window_start, 'window_end', v_window_end,
      'source_schema_min', v_source_schema_min, 'source_schema_max', v_source_schema_max
    );
  end if;

  select recording.updated_at, recording.started_at,
    recording.ended_at, dog.timezone, recording.state = 'open'
  into v_recording_updated_at, v_recording_window_start,
    v_recording_window_end, v_timezone, v_recording_is_open
  from api.recordings recording
  join api.collars collar on collar.id = recording.collar_id
  join api.dogs dog on dog.id = collar.dog_id
  where recording.id = p_recording_id and collar.dog_id = p_dog_id;
  if not found then return null; end if;

  select summary.summary_status, summary.source_received_at, summary.computed_at,
    summary.algorithm_version, summary.window_start, summary.window_end,
    summary.source_schema_min, summary.source_schema_max
  into v_summary_status, v_source_received_at, v_computed_at,
    v_algorithm_version, v_window_start, v_window_end,
    v_source_schema_min, v_source_schema_max
  from api.recording_summaries summary
  where summary.recording_id = p_recording_id
  order by summary.algorithm_version desc
  limit 1;

  v_recording_window_start := coalesce(v_recording_window_start, v_window_start);
  v_recording_window_end := coalesce(v_recording_window_end, v_window_end);

  v_pending := exists (
    select 1 from private.dirty_summary_days dirty
    where dirty.dog_id = p_dog_id
      and (
        v_recording_window_start is null
        or dirty.local_date >= (v_recording_window_start at time zone v_timezone)::date
      )
      and (
        v_recording_window_end is null
        or dirty.local_date <= (v_recording_window_end at time zone v_timezone)::date
      )
  ) and (
    v_summary_status is null
    or v_source_received_at is null
    or v_recording_updated_at > v_source_received_at
  );
  if v_summary_status is null then
    v_status := case when v_pending then 'pending' else 'unavailable' end;
  elsif v_pending then
    v_status := 'pending';
    v_source_received_at := v_recording_updated_at;
  elsif v_source_received_at is null or v_window_start is null or v_window_end is null then
    v_status := 'stale';
  elsif v_recording_is_open and v_window_end < statement_timestamp() then
    -- Like today's civil-day summary, an open recording window ages with time.
    v_status := 'stale';
  else
    v_status := v_summary_status;
  end if;
  return jsonb_build_object(
    'scope', 'recording', 'dog_id', p_dog_id, 'recording_id', p_recording_id,
    'status', v_status, 'pending', v_pending,
    'summary_status', v_summary_status,
    'source_received_at', v_source_received_at, 'computed_at', v_computed_at,
    'algorithm_version', v_algorithm_version,
    'window_start', v_window_start, 'window_end', v_window_end,
    'source_schema_min', v_source_schema_min, 'source_schema_max', v_source_schema_max
  );
end
$$;

revoke execute on function api.summary_freshness_v1(uuid, date, uuid) from public, anon, service_role;
grant execute on function api.summary_freshness_v1(uuid, date, uuid) to authenticated;

comment on function api.summary_freshness_v1(uuid, date, uuid) is
  'Membership-authorized freshness only; never reads or returns telemetry points.';
comment on function private.recompute_dirty_summaries_v1(integer) is
  'Manual-only M4A producer. Four dirty days per call, one recording per day iteration, 10s timeout, 100k daily and 250k recording point caps. No schedule.';
