begin;
select plan(33);

create temporary table retention_clock as
select
  pg_catalog.date_trunc('day', statement_timestamp(), 'UTC') as as_of,
  pg_catalog.date_trunc('day', statement_timestamp(), 'UTC') - interval '1 year' as cutoff;

select is(
  (
    select count(*)
    from pg_class relation
    where relation.oid in (
      'private.telemetry_retention_watermarks'::regclass,
      'private.retention_jobs'::regclass,
      'private.retention_receipts'::regclass
    )
      and relation.relrowsecurity
  ),
  3::bigint,
  'every retention state table has RLS enabled'
);

select ok(
  not has_table_privilege('anon', 'private.retention_jobs', 'select')
  and not has_table_privilege('authenticated', 'private.retention_jobs', 'select')
  and not has_table_privilege('service_role', 'private.retention_jobs', 'select')
  and not has_table_privilege('service_role', 'private.retention_receipts', 'select'),
  'browser and service roles cannot inspect retention state directly'
);

select ok(
  has_function_privilege(
    'service_role',
    'private.enqueue_raw_telemetry_retention_v1(timestamptz)',
    'execute'
  )
  and has_function_privilege(
    'service_role',
    'private.process_raw_telemetry_retention_batch_v1(integer)',
    'execute'
  )
  and not has_function_privilege(
    'authenticated',
    'private.enqueue_raw_telemetry_retention_v1(timestamptz)',
    'execute'
  )
  and not has_function_privilege(
    'authenticated',
    'private.process_raw_telemetry_retention_batch_v1(integer)',
    'execute'
  ),
  'only the service worker can enqueue or process retention jobs'
);

select ok(
  exists (
    select 1
    from pg_index index_state
    join pg_class relation on relation.oid = index_state.indexrelid
    where relation.relname = 'telemetry_points_retention_fallback_v1_idx'
      and index_state.indisvalid
  ),
  'the exceptional retention cutoff has a valid partial supporting index'
);

select is(
  (
    select count(*)
    from information_schema.columns
    where table_schema = 'private'
      and table_name in (
        'telemetry_retention_watermarks', 'retention_jobs', 'retention_receipts'
      )
      and column_name ~ '(latitude|longitude|coordinate|payload|body|email|dog_name|secret)'
  ),
  0::bigint,
  'retention state and receipts have no sensitive payload columns'
);

select is(
  private.telemetry_retention_basis_v1(
    '2025-01-01 00:00:00+00', '2025-01-02 00:00:00+00'
  ),
  '2025-01-01 00:00:00+00'::timestamptz,
  'a plausible observation uses its recorded instant'
);

select is(
  private.telemetry_retention_basis_v1(
    '2025-01-03 00:00:00+00', '2025-01-02 00:00:00+00'
  ),
  '2025-01-02 00:00:00+00'::timestamptz,
  'a future observation falls back to authenticated receipt time'
);

insert into api.collars (
  id, device_public_id, dog_id, display_name, state
) values (
  'e4000000-0000-4000-8000-000000000001',
  'e5000000-0000-4000-8000-000000000001',
  '30000000-0000-4000-8000-000000000003',
  'Retention collar',
  'active'
);

insert into private.telemetry_chunks (
  collar_id, boot_sequence, chunk_sequence, first_point_sequence,
  last_point_sequence, point_count, content_sha256, received_at,
  request_id, is_final
)
select
  'e4000000-0000-4000-8000-000000000001',
  1,
  chunk_sequence,
  first_point_sequence,
  first_point_sequence + point_count - 1,
  point_count,
  extensions.digest(('retention-chunk-' || chunk_sequence)::bytea, 'sha256'),
  clock.cutoff,
  ('e6000000-0000-4000-8000-00000000000' || chunk_sequence)::uuid,
  chunk_sequence = 3
from retention_clock clock
cross join (values
  (0::bigint, 0::bigint, 3::integer),
  (1::bigint, 3::bigint, 1::integer),
  (2::bigint, 4::bigint, 1::integer),
  (3::bigint, 5::bigint, 1::integer)
) chunks(chunk_sequence, first_point_sequence, point_count);

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, received_at,
  flags, time_quality, telemetry_schema, firmware_version, chunk_sequence
)
select
  'e4000000-0000-4000-8000-000000000001', 1, point_sequence,
  recorded_at, received_at, 0, time_quality, 3, 'retention-test', chunk_sequence
from retention_clock clock
cross join lateral (values
  (0::bigint, clock.cutoff - interval '1 microsecond', clock.cutoff - interval '1 microsecond', 'gnss_trusted'::text, 0::bigint),
  (1::bigint, clock.cutoff, clock.cutoff, 'gnss_trusted'::text, 0::bigint),
  (2::bigint, clock.cutoff + interval '1 microsecond', clock.cutoff + interval '1 microsecond', 'gnss_trusted'::text, 0::bigint),
  (3::bigint, null::timestamptz, clock.cutoff, 'unknown'::text, 1::bigint),
  (4::bigint, clock.cutoff + interval '1 day', clock.cutoff, 'gnss_trusted'::text, 2::bigint),
  (5::bigint, clock.cutoff + interval '1 day', clock.cutoff + interval '1 day', 'gnss_trusted'::text, 3::bigint)
) points(point_sequence, recorded_at, received_at, time_quality, chunk_sequence);

select lives_ok(
  $$select private.enqueue_raw_telemetry_retention_v1((select as_of from retention_clock))$$,
  'the service primitive can enqueue the deterministic UTC-day cutoff'
);

select is(
  (select count(*) from private.retention_jobs),
  1::bigint,
  'one active collar produces one retention job'
);

select is(
  (
    private.enqueue_raw_telemetry_retention_v1(
      (select as_of from retention_clock)
    ) ->> 'created_jobs'
  )::integer,
  0,
  'exact enqueue replay creates no duplicate job'
);

select is(
  (select count(*) from private.retention_jobs),
  1::bigint,
  'the replay-safe job key remains unique'
);

select is(
  (select cutoff from private.retention_jobs),
  (select cutoff from retention_clock),
  'the job cutoff is exactly twelve calendar months before the UTC run day'
);

select lives_ok(
  $$select private.process_raw_telemetry_retention_batch_v1(2)$$,
  'the first bounded point batch completes'
);

select is(
  (
    select count(*)
    from api.telemetry_points point, retention_clock clock
    where point.collar_id = 'e4000000-0000-4000-8000-000000000001'
      and coalesce(
        private.telemetry_retention_basis_v1(point.recorded_at, point.received_at),
        point.received_at
      ) <= clock.cutoff
  ),
  2::bigint,
  'deadline minus one and exact deadline are deleted first, leaving two fallback candidates'
);

select ok(
  exists (
    select 1 from api.telemetry_points point, retention_clock clock
    where point.point_sequence = 2 and point.recorded_at = clock.cutoff + interval '1 microsecond'
  )
  and (
    select status = 'pending' and stage = 'purge_points'
      and telemetry_points_deleted = 2 and attempt_count = 1
    from private.retention_jobs
  ),
  'deadline plus one remains and progress is committed without early completion'
);

create function pg_temp.fail_retention_delete_v1()
returns trigger
language plpgsql
as $$
begin
  raise exception using errcode = 'P0001', message = 'forced_retention_failure';
end
$$;
create trigger retention_test_fail_delete
before delete on api.telemetry_points
for each statement execute function pg_temp.fail_retention_delete_v1();

select lives_ok(
  $$select private.process_raw_telemetry_retention_batch_v1(2)$$,
  'a failed data batch is converted into retryable job state'
);

select ok(
  (
    select status = 'failed' and last_error_code = 'P0001'
      and telemetry_points_deleted = 2 and attempt_count = 2
    from private.retention_jobs
  )
  and (
    select count(*) from api.telemetry_points
    where collar_id = 'e4000000-0000-4000-8000-000000000001'
  ) = 4,
  'the failure rolls back point deletion while retaining only a safe SQLSTATE'
);

drop trigger retention_test_fail_delete on api.telemetry_points;
update private.retention_jobs set next_attempt_at = statement_timestamp();

select lives_ok(
  $$select private.process_raw_telemetry_retention_batch_v1(2)$$,
  'the same failed job resumes from its durable progress'
);

select is(
  (
    select count(*)
    from api.telemetry_points point, retention_clock clock
    where point.collar_id = 'e4000000-0000-4000-8000-000000000001'
      and coalesce(
        private.telemetry_retention_basis_v1(point.recorded_at, point.received_at),
        point.received_at
      ) <= clock.cutoff
  ),
  0::bigint,
  'retry removes unknown-time and implausible-future points at the exact cutoff'
);

select ok(
  (
    select status = 'pending' and stage = 'purge_chunks'
      and telemetry_points_deleted = 4 and attempt_count = 3
    from private.retention_jobs
  )
  and (
    select count(*) from api.telemetry_points
    where collar_id = 'e4000000-0000-4000-8000-000000000001'
  ) = 2,
  'point completion advances to chunk cleanup while preserving both fresh points'
);

select lives_ok(
  $$select private.process_raw_telemetry_retention_batch_v1(2)$$,
  'the bounded orphan-chunk batch completes the job'
);

select ok(
  (
    select array_agg(chunk_sequence order by chunk_sequence) = array[0::bigint, 3::bigint]
    from private.telemetry_chunks
    where collar_id = 'e4000000-0000-4000-8000-000000000001'
  ),
  'only chunks still backed by retained points survive'
);

select ok(
  (
    select status = 'completed' and stage = 'completed' and attempt_count = 4
      and telemetry_points_deleted = 4 and telemetry_chunks_deleted = 2
      and completed_at is not null and last_error_code is null
    from private.retention_jobs
  ),
  'the completed job reports exact cumulative bounded work'
);

select ok(
  (
    select reject_at_or_before = clock.cutoff
      and purged_at_or_before = clock.cutoff
    from private.telemetry_retention_watermarks watermark, retention_clock clock
    where watermark.collar_id = 'e4000000-0000-4000-8000-000000000001'
  ),
  'the anti-resurrection and fully-purged watermarks agree only after completion'
);

select ok(
  (
    select count(*) = 1 and octet_length((array_agg(receipt_sha256))[1]) = 32
      and max(telemetry_points_deleted) = 4
      and max(telemetry_chunks_deleted) = 2
    from private.retention_receipts
  ),
  'one coordinate-free hashed receipt binds the completed counts'
);

select throws_ok(
  $$
    insert into api.telemetry_points (
      collar_id, boot_sequence, point_sequence, recorded_at, received_at,
      flags, time_quality, telemetry_schema, firmware_version, chunk_sequence
    )
    select 'e4000000-0000-4000-8000-000000000001', 1, 6,
      cutoff, cutoff, 0, 'gnss_trusted', 3, 'retention-test', 4
    from retention_clock
  $$,
  '22023',
  'telemetry_expired_by_retention',
  'an expired replay cannot resurrect purged location'
);

select lives_ok(
  $$
    insert into api.telemetry_points (
      collar_id, boot_sequence, point_sequence, recorded_at, received_at,
      flags, time_quality, telemetry_schema, firmware_version, chunk_sequence
    )
    select 'e4000000-0000-4000-8000-000000000001', 1, 6,
      cutoff + interval '1 microsecond', cutoff + interval '1 microsecond',
      0, 'gnss_trusted', 3, 'retention-test', 4
    from retention_clock
  $$,
  'the anti-resurrection watermark preserves deadline plus one'
);

select throws_ok(
  $$select private.enqueue_raw_telemetry_retention_v1(statement_timestamp() + interval '1 day')$$,
  '22023',
  'invalid_retention_as_of',
  'the worker cannot advance retention with an arbitrary future clock'
);

select throws_ok(
  $$select private.process_raw_telemetry_retention_batch_v1(0)$$,
  '22023',
  'invalid_retention_batch_size',
  'zero-sized batches fail before claiming work'
);

select lives_ok(
  $$select private.enqueue_raw_telemetry_retention_v1('2024-02-29 23:59:59+00')$$,
  'a historical leap-day drill can be enqueued deterministically'
);

select is(
  (
    select cutoff
    from private.retention_jobs
    where cutoff = '2023-02-28 00:00:00+00'::timestamptz
    limit 1
  ),
  '2023-02-28 00:00:00+00'::timestamptz,
  'calendar-year retention maps leap day to the prior February boundary'
);

select ok(
  exists (
    select 1 from pg_trigger
    where tgname = 'dog_rgb_reject_expired_telemetry' and not tgisinternal
  )
  and exists (
    select 1 from pg_trigger
    where tgname = 'dog_rgb_lock_telemetry_on_collar_revoke' and not tgisinternal
  ),
  'anti-resurrection and deletion-race serialization triggers are installed'
);

select is(
  (
    select count(*)
    from pg_policies
    where schemaname = 'private'
      and tablename in (
        'telemetry_retention_watermarks', 'retention_jobs', 'retention_receipts'
      )
  ),
  0::bigint,
  'retention state has no direct RLS policy surface'
);

select * from finish();
rollback;
