begin;
create extension if not exists pgtap with schema extensions;

select plan(35);

select has_column('private', 'telemetry_loss_markers', 'boot_sequence', 'loss markers retain their boot namespace');
select has_column('private', 'telemetry_loss_markers', 'recorded_utc_ms', 'loss markers retain the device clock evidence');
select ok(
  not has_function_privilege('service_role', 'private.lock_telemetry_collar_v1(uuid)', 'execute'),
  'service role cannot invoke the telemetry serialization primitive'
);
select ok(
  not has_function_privilege('service_role', 'private.enforce_telemetry_chunk_invariants_v1()', 'execute'),
  'the chunk invariant trigger is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.enforce_telemetry_loss_invariants_v1()', 'execute'),
  'the loss invariant trigger is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.enforce_device_summary_invariants_v1()', 'execute'),
  'the summary invariant trigger is not an exposed RPC'
);
select ok(
  not has_function_privilege('service_role', 'private.reconcile_recording_completeness_v1()', 'execute'),
  'the deferred recording reconciler is not an exposed RPC'
);

update private.cloud_limits set enabled = false where singleton;
insert into api.collars (id, device_public_id, dog_id, state)
values (
  '70000000-0000-4000-8000-000000000007',
  '71000000-0000-4000-8000-000000000007',
  '30000000-0000-4000-8000-000000000003',
  'active'
);
insert into api.recordings (
  id, collar_id, boot_sequence, timezone_at_start, state, point_count,
  clock_quality, telemetry_schema, firmware_version
) values
  (
    '72000000-0000-4000-8000-000000000001',
    '70000000-0000-4000-8000-000000000007', 1, 'America/Bogota', 'open', 0,
    'gnss_trusted', 3, '1.0.0'
  ),
  (
    '72000000-0000-4000-8000-000000000002',
    '70000000-0000-4000-8000-000000000007', 2, 'America/Bogota', 'open', 0,
    'gnss_trusted', 3, '1.0.0'
  );

select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 1, 0, 1, 2,
      decode(repeat('11', 32), 'hex'), '73000000-0000-4000-8000-000000000001'
    )
  $$,
  'a first chunk is accepted'
);
select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 1, 0, 1, 2,
      decode(repeat('11', 32), 'hex'), '73000000-0000-4000-8000-000000000002'
    )
  $$,
  'an exact chunk replay under a new request is accepted without a write'
);
select is(
  (select count(*) from private.telemetry_chunks where collar_id = '70000000-0000-4000-8000-000000000007' and boot_sequence = 1),
  1::bigint,
  'the exact chunk replay does not duplicate storage'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 1, 0, 1, 2,
      decode(repeat('12', 32), 'hex'), '73000000-0000-4000-8000-000000000003'
    )
  $$,
  '23505', 'chunk_identity_conflict',
  'a chunk identity cannot be replayed with different content'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 2, 1, 2, 2,
      decode(repeat('13', 32), 'hex'), '73000000-0000-4000-8000-000000000004'
    )
  $$,
  '22023', 'chunk_point_overlap',
  'different chunk identities cannot overlap point sequences'
);
select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 2, 2, 2, 1,
      decode(repeat('14', 32), 'hex'), '73000000-0000-4000-8000-000000000005'
    )
  $$,
  'an adjacent chunk is accepted'
);
select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id, is_final
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 4, 4, 4, 1,
      decode(repeat('15', 32), 'hex'), '73000000-0000-4000-8000-000000000006', true
    )
  $$,
  'a terminal chunk closes its boot stream'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 5, 5, 5, 1,
      decode(repeat('16', 32), 'hex'), '73000000-0000-4000-8000-000000000007'
    )
  $$,
  '22023', 'chunk_after_final',
  'a chunk cannot occur after a persisted final chunk'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id, is_final
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 3, 3, 3, 1,
      decode(repeat('17', 32), 'hex'), '73000000-0000-4000-8000-000000000008', true
    )
  $$,
  '22023', 'final_chunk_not_terminal',
  'a stream cannot acquire a second final chunk'
);

set constraints all immediate;
select is(
  (select state from api.recordings where id = '72000000-0000-4000-8000-000000000001'),
  'incomplete',
  'a finalized recording with an uncovered point hole is incomplete'
);

select lives_ok(
  $$
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason, recorded_utc_ms
    ) values (
      '74000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '75000000-0000-4000-8000-000000000001', 1, 3, 3, 1,
      'storage_pressure', 1786641065000
    )
  $$,
  'a loss marker can account for an unpersisted point range'
);
select lives_ok(
  $$
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason, recorded_utc_ms
    ) values (
      '74000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '75000000-0000-4000-8000-000000000002', 1, 3, 3, 1,
      'storage_pressure', 1786641065000
    )
  $$,
  'an exact loss-marker replay is accepted without a write'
);
select is(
  (select count(*) from private.telemetry_loss_markers where id = '74000000-0000-4000-8000-000000000001'),
  1::bigint,
  'the exact loss-marker replay does not duplicate storage'
);
select throws_ok(
  $$
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason
    ) values (
      '74000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '75000000-0000-4000-8000-000000000003', 1, 3, 3, 1, 'corrupt_chunk'
    )
  $$,
  '23505', 'loss_marker_identity_conflict',
  'a marker identity cannot be replayed with different content'
);
select throws_ok(
  $$
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason
    ) values (
      '74000000-0000-4000-8000-000000000002',
      '70000000-0000-4000-8000-000000000007',
      '75000000-0000-4000-8000-000000000004', 1, 3, 3, 1, 'corrupt_chunk'
    )
  $$,
  '22023', 'loss_marker_range_overlap',
  'different marker identities cannot overlap missing ranges'
);
select throws_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id
    ) values (
      '70000000-0000-4000-8000-000000000007', 1, 3, 3, 3, 1,
      decode(repeat('18', 32), 'hex'), '73000000-0000-4000-8000-000000000009'
    )
  $$,
  '22023', 'chunk_loss_overlap',
  'a later chunk cannot contradict an acknowledged loss marker'
);
select throws_ok(
  $$
    insert into private.telemetry_loss_markers (
      id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
      last_missing_point_sequence, dropped_points, reason
    ) values (
      '74000000-0000-4000-8000-000000000003',
      '70000000-0000-4000-8000-000000000007',
      '75000000-0000-4000-8000-000000000005', 1, 6, 7, 1, 'corrupt_chunk'
    )
  $$,
  '23514', null,
  'a marker count must equal its inclusive missing range'
);

select lives_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      max_speed_cmps, valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000001', '2026-08-13', 'America/Bogota', 1,
      '2026-08-13T17:10:00Z', '2026-08-13T17:11:05Z', 65, 5, 60, 15,
      140, 3, 1, 1, 'sntp_synced'
    )
  $$,
  'a summary with exact duration accounting is accepted'
);
select lives_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      max_speed_cmps, valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000002', '2026-08-13', 'America/Bogota', 1,
      '2026-08-13T17:10:00Z', '2026-08-13T17:11:05Z', 65, 5, 60, 15,
      140, 3, 1, 1, 'sntp_synced'
    )
  $$,
  'an exact summary replay is accepted without a write'
);
select is(
  (select count(*) from private.device_daily_summaries where summary_id = '76000000-0000-4000-8000-000000000001'),
  1::bigint,
  'the exact summary replay does not duplicate storage'
);
select throws_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      max_speed_cmps, valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000001',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000003', '2026-08-13', 'America/Bogota', 1,
      '2026-08-13T17:10:00Z', '2026-08-13T17:11:05Z', 65, 5, 60, 16,
      140, 3, 1, 1, 'sntp_synced'
    )
  $$,
  '23505', 'summary_identity_conflict',
  'a summary identity cannot be replayed with changed measurements'
);
select throws_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000002',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000004', '2026-08-13', 'America/Bogota', 1,
      '2026-08-13T17:10:00Z', '2026-08-13T17:11:05Z', 65, 5, 60, 15,
      3, 1, 1, 'sntp_synced'
    )
  $$,
  '23505', 'summary_revision_identity_conflict',
  'a date revision cannot be assigned a second summary identity'
);
select throws_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000003',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000005', '2026-08-14', 'America/Bogota', 1,
      '2026-08-14T17:10:00Z', '2026-08-14T17:11:05Z', 64, 5, 60, 15,
      3, 1, 1, 'sntp_synced'
    )
  $$,
  '23514', null,
  'summary motion buckets must exactly equal observed time'
);
select throws_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000004',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000006', '2026-08-15', 'America/Bogota', 1,
      '2026-08-15T17:10:00Z', '2026-08-15T17:11:00Z', 65, 5, 60, 15,
      3, 1, 1, 'sntp_synced'
    )
  $$,
  '23514', null,
  'observed time cannot exceed the summary window'
);
select throws_ok(
  $$
    insert into private.device_daily_summaries (
      summary_id, collar_id, request_id, local_date, timezone, source_revision,
      window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
      valid_points, gap_count, dropped_points, time_quality
    ) values (
      '76000000-0000-4000-8000-000000000005',
      '70000000-0000-4000-8000-000000000007',
      '77000000-0000-4000-8000-000000000007', '2026-08-16', 'Mars/Olympus_Mons', 1,
      '2026-08-16T17:10:00Z', '2026-08-16T17:11:05Z', 65, 5, 60, 15,
      3, 1, 1, 'sntp_synced'
    )
  $$,
  '22023', 'summary_timezone_invalid',
  'summary timezones must exist in the server IANA catalog'
);

select lives_ok(
  $$
    insert into private.telemetry_chunks (
      collar_id, boot_sequence, chunk_sequence, first_point_sequence,
      last_point_sequence, point_count, content_sha256, request_id, is_final
    ) values
      (
        '70000000-0000-4000-8000-000000000007', 2, 0, 0, 0, 1,
        decode(repeat('21', 32), 'hex'), '78000000-0000-4000-8000-000000000001', false
      ),
      (
        '70000000-0000-4000-8000-000000000007', 2, 1, 1, 1, 1,
        decode(repeat('22', 32), 'hex'), '78000000-0000-4000-8000-000000000002', true
      )
  $$,
  'a contiguous stream with one final chunk is accepted'
);
set constraints all immediate;
select is(
  (select state from api.recordings where id = '72000000-0000-4000-8000-000000000002'),
  'closed',
  'a contiguous finalized recording reconciles to closed'
);
select is(
  (select point_count from api.recordings where id = '72000000-0000-4000-8000-000000000002'),
  2,
  'recording point_count is recomputed from accepted non-overlapping chunks'
);

select * from finish();
rollback;
