begin;
create extension if not exists pgtap with schema extensions;

select plan(67);

select ok(
  has_function_privilege('service_role', 'private.recompute_dirty_summaries_v1(integer)', 'execute')
  and not has_function_privilege('authenticated', 'private.recompute_dirty_summaries_v1(integer)', 'execute'),
  'only the service worker can run the bounded summary producer'
);
select ok(
  has_function_privilege('authenticated', 'api.summary_freshness_v1(uuid,date,uuid)', 'execute')
  and not has_function_privilege('anon', 'api.summary_freshness_v1(uuid,date,uuid)', 'execute')
  and not has_function_privilege('service_role', 'api.summary_freshness_v1(uuid,date,uuid)', 'execute'),
  'freshness is exposed only through the authenticated membership-authorized RPC'
);
select has_column('api', 'daily_summaries', 'summary_status', 'daily summaries carry an explicit calculation status');
select has_column('api', 'recording_summaries', 'source_received_at', 'recording freshness records a source watermark');
select has_column('api', 'recording_summaries', 'window_scope', 'recording summaries identify their window basis');

create temporary table analytics_sql_js_parity as
select private.compute_telemetry_summary_v1(
  jsonb_build_array(
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 1, 'recorded_s', 100, 'lat_e7', 0, 'lon_e7', 0, 'reported_speed_cmps', 0, 'flags', 13, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 2, 'recorded_s', 110, 'lat_e7', 0, 'lon_e7', 0, 'reported_speed_cmps', 0, 'flags', 13, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 3, 'recorded_s', 120, 'lat_e7', 0, 'lon_e7', 0, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 4, 'recorded_s', 130, 'lat_e7', 0, 'lon_e7', 1000, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 5, 'recorded_s', 140, 'lat_e7', 0, 'lon_e7', 2000, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 6, 'recorded_s', 150, 'lat_e7', 0, 'lon_e7', 2000, 'reported_speed_cmps', 0, 'flags', 13, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3)
  ),
  '[]'::jsonb,
  '1970-01-01 00:01:40+00',
  '1970-01-01 00:02:40+00',
  250000
) as result;
select is((select result ->> 'summary_status' from analytics_sql_js_parity), 'available', 'SQL helper matches the JS fixture status');
select is((select (result ->> 'observed_s')::integer from analytics_sql_js_parity), 30, 'SQL/JS parity: observed seconds');
select is((select (result ->> 'moving_s')::integer from analytics_sql_js_parity), 20, 'SQL/JS parity: moving seconds');
select is((select (result ->> 'inactive_s')::integer from analytics_sql_js_parity), 10, 'SQL/JS parity: stationary seconds');
select is((select (result ->> 'unknown_s')::integer from analytics_sql_js_parity), 30, 'SQL/JS parity: unknown seconds');
select is((select (result ->> 'distance_m')::integer from analytics_sql_js_parity), 22, 'SQL/JS parity: rounded distance meters');
select is((select (result ->> 'average_moving_cmps')::integer from analytics_sql_js_parity), 111, 'SQL/JS parity: mean moving speed');
select is((select (result ->> 'filtered_max_speed_cmps')::integer from analytics_sql_js_parity), 111, 'SQL/JS parity: filtered maximum speed');
select is((select (result ->> 'valid_points')::integer from analytics_sql_js_parity), 6, 'SQL/JS parity: usable point count');
select is((select (result ->> 'warning_points')::integer from analytics_sql_js_parity), 0, 'SQL/JS parity: warning point count');
select is((select (result ->> 'coverage_ratio')::numeric from analytics_sql_js_parity), 0.500000::numeric, 'SQL/JS parity: bounded coverage ratio');
create temporary table analytics_missing_gps as
select private.compute_telemetry_summary_v1(
  jsonb_build_array(
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 1, 'recorded_s', 0, 'lat_e7', null, 'lon_e7', null, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 2, 'recorded_s', 10, 'lat_e7', 0, 'lon_e7', 8000, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3),
    jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 3, 'recorded_s', 20, 'lat_e7', 0, 'lon_e7', 8000, 'reported_speed_cmps', 100, 'flags', 7, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3)
  ),
  '[]'::jsonb,
  '1970-01-01 00:00:00+00',
  '1970-01-01 00:00:30+00',
  250000
) as result;
select is((select result ->> 'summary_status' from analytics_missing_gps), 'available', 'moving telemetry remains available without claiming GPS distance');
select is((select (result ->> 'moving_s')::integer from analytics_missing_gps), 20, 'movement evidence can classify elapsed time independently of GPS coordinates');
select is((select result ->> 'distance_m' from analytics_missing_gps), null::text, 'missing GPS segments stay absent rather than zero meters');
select is((select result ->> 'average_moving_cmps' from analytics_missing_gps), null::text, 'missing GPS segments stay absent rather than zero mean speed');
select is(
  private.compute_telemetry_summary_v1(
    jsonb_build_array(jsonb_build_object('collar_id', 'c1', 'boot_sequence', 1, 'point_sequence', 1, 'recorded_s', 0, 'flags', 13, 'time_quality', 'gnss_trusted', 'telemetry_schema', 3)),
    jsonb_build_array(jsonb_build_object('id', 'loss-a', 'dropped_points', 1), jsonb_build_object('id', 'loss-b', 'dropped_points', 1)),
    '1970-01-01 00:00:00+00', '1970-01-01 00:00:10+00', 1
  ) ->> 'summary_status',
  'source_limit_exceeded',
  'too many loss markers also yield an explicit non-partial result'
);

insert into api.dogs (id, name, timezone, created_by) values
  ('30000000-0000-4000-8000-000000000005', 'Analytics DST dog', 'America/New_York', '10000000-0000-4000-8000-000000000001'),
  ('30000000-0000-4000-8000-000000000006', 'Analytics retention dog', 'America/Bogota', '10000000-0000-4000-8000-000000000001'),
  ('30000000-0000-4000-8000-000000000007', 'Analytics live-auth dog', 'America/Bogota', '20000000-0000-4000-8000-000000000002');

insert into api.dog_memberships (dog_id, user_id, role) values
  ('30000000-0000-4000-8000-000000000005', '10000000-0000-4000-8000-000000000001', 'owner'),
  ('30000000-0000-4000-8000-000000000006', '10000000-0000-4000-8000-000000000001', 'owner'),
  ('30000000-0000-4000-8000-000000000007', '20000000-0000-4000-8000-000000000002', 'owner');

insert into api.collars (id, device_public_id, dog_id, state) values
  ('76000000-0000-4000-8000-000000000001', '86000000-0000-4000-8000-000000000001', '30000000-0000-4000-8000-000000000005', 'active'),
  ('76000000-0000-4000-8000-000000000002', '86000000-0000-4000-8000-000000000002', '30000000-0000-4000-8000-000000000006', 'active');

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, ended_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
) values
  (
    '77000000-0000-4000-8000-000000000001',
    '76000000-0000-4000-8000-000000000001', 1,
    '2026-03-08 16:00:00+00', '2026-03-08 16:00:30+00',
    'America/New_York', 'closed', 2, 'gnss_trusted', 3, 'analytics-fixture'
  ),
  (
    '77000000-0000-4000-8000-000000000002',
    '76000000-0000-4000-8000-000000000001', 2,
    '2025-11-02 17:00:00+00', '2025-11-02 17:00:30+00',
    'America/New_York', 'closed', 3, 'gnss_trusted', 3, 'analytics-fixture'
  ),
  (
    '77000000-0000-4000-8000-000000000003',
    '76000000-0000-4000-8000-000000000001', 3,
    null, null, 'America/New_York', 'closed', 1, 'unknown', 3, 'analytics-fixture'
  );

-- Pre-M4 summaries have an algorithm number but no source/window provenance.
-- Keep these legacy rows in place before telemetry arrives so the supported
-- source trigger invalidates them and the worker must replace them.
insert into api.daily_summaries (
  dog_id, local_date, timezone, observed_s, moving_s, inactive_s, unknown_s,
  distance_m, valid_points, warning_points, gap_count, dropped_points,
  coverage_ratio, algorithm_version, source_revision, computed_at
) values (
  '30000000-0000-4000-8000-000000000005', '2026-03-08', 'America/New_York',
  1, 0, 1, 82799, 0, 1, 0, 0, 0, 0.000012, 1, 0, '2026-03-08 16:01:00+00'
);
insert into api.recording_summaries (
  recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
  valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
  algorithm_version, source_revision, computed_at
) values (
  '77000000-0000-4000-8000-000000000001',
  1, 0, 1, 29, 0, 1, 0, 0, 0, 0.033333, 1, 0, '2026-03-08 16:01:00+00'
);

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
) values
  ('76000000-0000-4000-8000-000000000001', 1, 1, '2026-03-08 16:00:00+00', 400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1),
  ('76000000-0000-4000-8000-000000000001', 1, 2, '2026-03-08 16:00:10+00', 400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1),
  ('76000000-0000-4000-8000-000000000001', 2, 1, '2025-11-02 17:00:00+00', 0, -740000000, 100, 8, 7, 'gnss_trusted', 3, 'analytics-fixture', 2),
  ('76000000-0000-4000-8000-000000000001', 2, 2, '2025-11-02 17:00:10+00', 0, -739999000, 100, 8, 7, 'gnss_trusted', 3, 'analytics-fixture', 2),
  ('76000000-0000-4000-8000-000000000001', 2, 3, '2025-11-02 17:00:20+00', 0, -739998000, 100, 8, 7, 'gnss_trusted', 3, 'analytics-fixture', 2),
  ('76000000-0000-4000-8000-000000000001', 3, 1, null, null, null, null, null, 0, 'unknown', 3, 'analytics-fixture', 3);

create temporary table analytics_before_late_upload (source_received_at timestamptz);

set local role service_role;
select lives_ok(
  $$ select private.recompute_dirty_summaries_v1(4) $$,
  'the manual worker computes bounded daily and recording summaries'
);
select lives_ok(
  $$ select private.recompute_dirty_summaries_v1(4) $$,
  'a follow-up batch drains rows retained while recordings were still stale'
);
reset role;
insert into analytics_before_late_upload
select source_received_at
from api.daily_summaries
where dog_id = '30000000-0000-4000-8000-000000000005'
  and local_date = '2026-03-08';

select is(
  (select count(*) from private.dirty_summary_days where dog_id in (
    '30000000-0000-4000-8000-000000000005', '30000000-0000-4000-8000-000000000006'
  )),
  0::bigint,
  'summary upserts and consumed dirty marks commit together'
);
select is(
  (select observed_s from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08' and algorithm_version = 1),
  10::bigint,
  'a source-triggered recompute replaces the pre-M4 daily algorithm result'
);
select ok(
  (select algorithm_version = 1 and source_received_at is not null
      and window_start is not null and window_end is not null
      and source_schema_min = 3 and source_schema_max = 3
   from api.daily_summaries
   where dog_id = '30000000-0000-4000-8000-000000000005'
     and local_date = '2026-03-08' and algorithm_version = 1),
  'the upgraded daily row carries current v1 source and window provenance'
);
select is(
  (select observed_s from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000001' and algorithm_version = 1),
  10::bigint,
  'a source-triggered recompute replaces the pre-M4 recording algorithm result'
);
select ok(
  (select algorithm_version = 1 and source_received_at is not null
      and window_start is not null and window_end is not null
      and window_scope = 'recording_bounds'
      and source_schema_min = 3 and source_schema_max = 3
   from api.recording_summaries
   where recording_id = '77000000-0000-4000-8000-000000000001'
     and algorithm_version = 1),
  'the upgraded recording row carries current v1 source and window provenance'
);

create temporary table analytics_upgrade_first as
select
  to_jsonb(daily) - array['source_received_at', 'source_revision', 'computed_at']::text[] as daily,
  to_jsonb(recording) - array['source_received_at', 'source_revision', 'computed_at']::text[] as recording
from api.daily_summaries daily
cross join api.recording_summaries recording
where daily.dog_id = '30000000-0000-4000-8000-000000000005'
  and daily.local_date = '2026-03-08'
  and daily.algorithm_version = 1
  and recording.recording_id = '77000000-0000-4000-8000-000000000001'
  and recording.algorithm_version = 1;
update api.daily_summaries
set source_received_at = null
where dog_id = '30000000-0000-4000-8000-000000000005'
  and local_date = '2026-03-08' and algorithm_version = 1;
update api.recording_summaries set source_received_at = null
where recording_id = '77000000-0000-4000-8000-000000000001'
  and algorithm_version = 1;
insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
values ('30000000-0000-4000-8000-000000000005', '2026-03-08', 'America/New_York', 'algorithm_replay');
set local role service_role;
select lives_ok(
  $$ select private.recompute_dirty_summaries_v1(4) $$,
  'the producer can replay the same retained inputs after freshness invalidation'
);
reset role;
select is(
  (select to_jsonb(daily) - array['source_received_at', 'source_revision', 'computed_at']::text[]
   from api.daily_summaries daily
   where dog_id = '30000000-0000-4000-8000-000000000005'
     and local_date = '2026-03-08' and algorithm_version = 1),
  (select daily from analytics_upgrade_first),
  'replaying identical source keeps every daily result field deterministic'
);
select is(
  (select to_jsonb(recording) - array['source_received_at', 'source_revision', 'computed_at']::text[]
   from api.recording_summaries recording
   where recording_id = '77000000-0000-4000-8000-000000000001'
     and algorithm_version = 1),
  (select recording from analytics_upgrade_first),
  'replaying identical source keeps every recording result field deterministic'
);
select is(
  (select count(*) from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08'),
  1::bigint,
  'the current daily algorithm replaces in place without duplicate versions'
);
select is(
  (select count(*) from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000001'),
  1::bigint,
  'the current recording algorithm replaces in place without duplicate versions'
);
select is(
  (select summary_status from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08'),
  'available',
  'the spring-forward daily source is available'
);
select is(
  (select floor(extract(epoch from window_end - window_start))::integer from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08'),
  82800,
  'the spring-forward denominator is 23 elapsed hours'
);
select is(
  (select observed_s from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08'),
  10::bigint,
  'daily classified time comes only from adjacent stationary evidence'
);
select is(
  (select summary_status from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2025-11-02'),
  'available',
  'the fall-back daily source is available'
);
select is(
  (select floor(extract(epoch from window_end - window_start))::integer from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2025-11-02'),
  90000,
  'the fall-back denominator is 25 elapsed hours'
);
select is(
  (select observed_s from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000001' and algorithm_version = 1),
  10::bigint,
  'the recording summary uses its explicit 30-second recording bounds'
);
select is(
  (select observed_s from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000002' and algorithm_version = 1),
  20::bigint,
  'the moving recording summary accumulates only valid moving intervals'
);
select is(
  (select distance_m from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000002' and algorithm_version = 1),
  22::bigint,
  'the filtered geodesic distance rounds to whole meters'
);
select is(
  (select average_moving_cmps from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000002' and algorithm_version = 1),
  111,
  'mean moving speed is distance divided by moving seconds in cm/s'
);
select is(
  (select filtered_max_speed_cmps from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000002' and algorithm_version = 1),
  111,
  'filtered maximum speed ignores reported and geodesic outliers above 40 km/h'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', '2026-03-08', null) ->> 'status',
  'available',
  'an owner reads fresh daily-summary metadata'
);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', null, '77000000-0000-4000-8000-000000000002') ->> 'status',
  'available',
  'an owner reads fresh recording-summary metadata'
);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', '2025-11-02', null) ->> 'timezone',
  'America/New_York',
  'daily freshness reports the dog timezone used for the civil window'
);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', '2026-03-08', null) ->> 'pending',
  'false',
  'freshness reports no pending work after an atomic consume'
);
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', '2026-03-08', null),
  null::jsonb,
  'a nonmember cannot read another dog’s freshness record'
);
reset role;

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
) values (
  '76000000-0000-4000-8000-000000000001', 1, 3,
  '2026-03-08 16:00:20+00', 400000000, -740000000,
  0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', '2026-03-08', null) ->> 'status',
  'pending',
  'a late upload re-dirties its event day and hides stale daily metrics'
);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000005', null, '77000000-0000-4000-8000-000000000001') ->> 'status',
  'pending',
  'a late upload also makes the affected recording pending'
);
reset role;

set local role service_role;
select lives_ok($$ select private.recompute_dirty_summaries_v1(4) $$, 'the late event is recomputed');
reset role;
select ok(
  (select summary.source_received_at > before.source_received_at
   from api.daily_summaries summary
   cross join analytics_before_late_upload before
   where summary.dog_id = '30000000-0000-4000-8000-000000000005'
     and summary.local_date = '2026-03-08'),
  'the next recomputation advances its source watermark beyond the previous snapshot'
);
select is(
  (select observed_s from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = '2026-03-08'),
  20::bigint,
  'the late point contributes one newly evidenced ten-second interval'
);
select is(
  (select observed_s from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000001' and algorithm_version = 1),
  20::bigint,
  'the recording summary is recomputed after a late point'
);
insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
) values (
  '76000000-0000-4000-8000-000000000001', 1, 3,
  '2026-03-08 16:00:20+00', 400000000, -740000000,
  0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1
) on conflict (collar_id, boot_sequence, point_sequence) do nothing;
select is(
  (select count(*) from api.telemetry_points where collar_id = '76000000-0000-4000-8000-000000000001' and boot_sequence = 1),
  3::bigint,
  'an exact telemetry retry does not duplicate a source point'
);
select is(
  (select count(*) from private.dirty_summary_days where dog_id = '30000000-0000-4000-8000-000000000005'),
  0::bigint,
  'an exact point retry does not recreate a consumed dirty day'
);

set local role service_role;
select lives_ok($$ select private.recompute_dirty_summaries_v1(4) $$, 'clockless telemetry produces an explicit unavailable result');
reset role;
select is(
  (select summary_status from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = (statement_timestamp() at time zone 'America/New_York')::date),
  'insufficient_time_evidence',
  'unknown event time is never inferred from its receipt time'
);
select is(
  (select observed_s from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000005' and local_date = (statement_timestamp() at time zone 'America/New_York')::date),
  null::bigint,
  'unknown event time stores absence rather than zero duration'
);
select is(
  (select summary_status from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000003' and algorithm_version = 1),
  'insufficient_time_evidence',
  'clockless recording summaries stay explicitly unavailable'
);
select is(
  (select observed_s from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000003' and algorithm_version = 1),
  null::bigint,
  'clockless recording duration is not represented as zero'
);

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, ended_at, timezone_at_start, state,
  point_count, clock_quality, telemetry_schema, firmware_version
) values (
  '77000000-0000-4000-8000-000000000004',
  '76000000-0000-4000-8000-000000000002', 1,
  '2026-06-15 17:00:00+00', '2026-06-15 17:00:30+00',
  'America/Bogota', 'closed', 2, 'gnss_trusted', 3, 'analytics-fixture'
);
insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
) values
  ('76000000-0000-4000-8000-000000000002', 1, 1, '2026-06-15 17:00:00+00', 400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1),
  ('76000000-0000-4000-8000-000000000002', 1, 2, '2026-06-15 17:00:10+00', 400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'analytics-fixture', 1);
insert into private.telemetry_retention_watermarks (
  collar_id, reject_at_or_before, purged_at_or_before
) values (
  '76000000-0000-4000-8000-000000000002',
  '2026-06-16 05:00:00+00', '2026-06-16 05:00:00+00'
);
set local role service_role;
select lives_ok($$ select private.recompute_dirty_summaries_v1(4) $$, 'retention-aware summaries recompute from retained coverage');
reset role;
select is(
  (select summary_status from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000006' and local_date = '2026-06-15'),
  'insufficient_retained_data',
  'a retention watermark prevents a complete daily claim'
);
select is(
  (select observed_s from api.daily_summaries where dog_id = '30000000-0000-4000-8000-000000000006' and local_date = '2026-06-15'),
  null::bigint,
  'retention gaps produce metric absence instead of fabricated zeros'
);
select is(
  (select summary_status from api.recording_summaries where recording_id = '77000000-0000-4000-8000-000000000004' and algorithm_version = 1),
  'insufficient_retained_data',
  'the same retention limit applies to the recording summary'
);

set local role authenticated;
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select is(
  api.summary_freshness_v1('30000000-0000-4000-8000-000000000007', '2026-09-01', null) ->> 'status',
  'unavailable',
  'a currently live authenticated account can query its member dog'
);
reset role;
update auth.users set deleted_at = statement_timestamp()
where id = '20000000-0000-4000-8000-000000000002';
set local role authenticated;
select set_config('request.jwt.claim.sub', '20000000-0000-4000-8000-000000000002', true);
select throws_ok(
  $$ select api.summary_freshness_v1('30000000-0000-4000-8000-000000000007', '2026-09-01', null) $$,
  '28000', 'authentication_required',
  'deleted accounts cannot query freshness even while membership remains'
);

select * from finish();
rollback;
