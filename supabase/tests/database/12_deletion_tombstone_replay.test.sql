begin;
create extension if not exists pgtap with schema extensions;

select plan(24);

select ok(
  has_function_privilege(
    'service_role',
    'private.export_deletion_tombstones_v1(timestamptz,uuid,integer)',
    'execute'
  ) and not has_function_privilege(
    'authenticated',
    'private.export_deletion_tombstones_v1(timestamptz,uuid,integer)',
    'execute'
  ),
  'only service_role can call the bounded tombstone exporter'
);
select ok(
  has_function_privilege(
    'service_role', 'private.replay_dog_deletion_tombstone_v1(jsonb)', 'execute'
  ) and not has_function_privilege(
    'authenticated', 'private.replay_dog_deletion_tombstone_v1(jsonb)', 'execute'
  ),
  'only service_role can call restore replay'
);
select ok(
  not has_function_privilege(
    'service_role',
    'private.deletion_replay_sha256_v1(uuid,bytea,uuid,text,bytea,timestamptz,bytea)',
    'execute'
  ),
  'service_role cannot use the internal digest helper directly'
);
select is(
  (
    select config
    from pg_proc procedure_row
    cross join lateral unnest(procedure_row.proconfig) config
    where procedure_row.oid = 'api.request_dog_deletion_v1(uuid,uuid,text)'::regprocedure
      and config = 'TimeZone=UTC'
  ),
  'TimeZone=UTC',
  'new tombstone hashes are pinned to UTC independently of caller timezone'
);

insert into api.dogs (id, name, timezone, created_by)
values (
  'e3000000-0000-4000-8000-000000000001',
  'Replay target',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
);
insert into api.dog_memberships (dog_id, user_id, role)
values (
  'e3000000-0000-4000-8000-000000000001',
  '10000000-0000-4000-8000-000000000001',
  'owner'
);
insert into api.collars (id, device_public_id, dog_id, state)
values (
  'e4000000-0000-4000-8000-000000000001',
  'e5000000-0000-4000-8000-000000000001',
  'e3000000-0000-4000-8000-000000000001',
  'active'
);
insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state
)
values (
  'e6000000-0000-4000-8000-000000000001',
  'e4000000-0000-4000-8000-000000000001',
  decode(repeat('e1', 32), 'hex'),
  'active'
);
insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
)
select
  'e4000000-0000-4000-8000-000000000001', 1, point_sequence,
  '2026-08-18 12:00:00+00'::timestamptz + point_sequence * interval '1 second',
  47110000 + point_sequence::integer, -740721000 + point_sequence::integer,
  100, 8, 13, 'gnss_trusted', 3, 'tombstone-replay-test', 1
from generate_series(1, 3) generated(point_sequence);

create temporary table tombstone_capture (
  export_page jsonb not null,
  item jsonb not null
) on commit drop;
grant select, insert on tombstone_capture to service_role;

set local timezone = 'America/Bogota';
set local role authenticated;
select set_config(
  'request.jwt.claim.sub', '10000000-0000-4000-8000-000000000001', true
);
select lives_ok(
  $$
    select api.request_dog_deletion_v1(
      'e3000000-0000-4000-8000-000000000001',
      'ea000000-0000-4000-8000-000000000001',
      'dog-delete-v1'
    )
  $$,
  'an owner request creates the tombstone while the caller uses another timezone'
);
reset role;

set local role service_role;
insert into tombstone_capture (export_page, item)
select export_page, export_page -> 'items' -> 0
from (
  select private.export_deletion_tombstones_v1(null, null, 100) export_page
) exported;
reset role;

select is(
  (select export_page ->> 'schema_version' from tombstone_capture),
  'dog-deletion-tombstone-export-v1',
  'the export page is explicitly versioned'
);
select is(
  (select jsonb_array_length(export_page -> 'items') from tombstone_capture),
  1,
  'the exporter emits the one durable deletion request'
);
select is(
  (select item ->> 'schema_version' from tombstone_capture),
  'dog-deletion-tombstone-v1',
  'each replay item is independently versioned'
);
select ok(
  (select item ->> 'requested_at' ~ 'Z$' from tombstone_capture),
  'the exported request time uses canonical UTC text'
);
select is(
  (select count(*) from jsonb_object_keys((select item from tombstone_capture))),
  10::bigint,
  'the replay item has an exact bounded field set'
);
select ok(
  not exists (
    select 1
    from jsonb_object_keys((select item from tombstone_capture)) key_name
    where key_name ~ '(lat|lon|coordinate|payload|body|email|dog_name|secret)'
  ),
  'the replay item contains no coordinate, content, email, or secret field'
);
select throws_ok(
  $$
    set local role service_role;
    select private.export_deletion_tombstones_v1(null, null, 0)
  $$,
  '22023',
  'invalid_tombstone_export_limit',
  'the exporter rejects an unbounded/empty page size'
);
reset role;
select throws_ok(
  $$
    set local role service_role;
    select private.export_deletion_tombstones_v1(
      statement_timestamp(), null, 100
    )
  $$,
  '22023',
  'invalid_tombstone_export_cursor',
  'the exporter rejects a partial cursor'
);
reset role;

-- Recreate the pre-deletion state while retaining only the external capture,
-- equivalent to restoring a point immediately before the request.
delete from private.deletion_jobs;
delete from private.deletion_tombstones;
update api.dogs
set deleted_at = null, updated_at = statement_timestamp()
where id = 'e3000000-0000-4000-8000-000000000001';
insert into api.dog_memberships (dog_id, user_id, role)
values (
  'e3000000-0000-4000-8000-000000000001',
  '10000000-0000-4000-8000-000000000001',
  'owner'
);
update api.collars
set state = 'active', revoked_at = null, updated_at = statement_timestamp()
where id = 'e4000000-0000-4000-8000-000000000001';
update private.device_credentials
set state = 'active', revoked_at = null
where credential_id = 'e6000000-0000-4000-8000-000000000001';

set local role service_role;
select throws_ok(
  format(
    'select private.replay_dog_deletion_tombstone_v1(%L::jsonb)',
    jsonb_set(
      (select item from tombstone_capture),
      '{scope_id}',
      to_jsonb('e3000000-0000-4000-8000-000000000099'::text)
    )::text
  ),
  '22023',
  'invalid_deletion_tombstone_hash',
  'a modified scope fails closed before any data changes'
);
select lives_ok(
  format(
    'select private.replay_dog_deletion_tombstone_v1(%L::jsonb)',
    (select item::text from tombstone_capture)
  ),
  'a valid external tombstone replays into the restored state'
);
reset role;

select is(
  (select count(*) from private.deletion_tombstones),
  1::bigint,
  'replay recreates exactly one durable tombstone'
);
select ok(
  (select status = 'pending' and initial_counts ->> 'telemetry_points' = '3'
   from private.deletion_jobs),
  'replay captures the restored scope inventory in a bounded deletion job'
);
select ok(
  (select deleted_at is not null
   from api.dogs where id = 'e3000000-0000-4000-8000-000000000001')
  and not exists (
    select 1 from api.dog_memberships
    where dog_id = 'e3000000-0000-4000-8000-000000000001'
  ),
  'replay closes restored user access before physical purge'
);
select ok(
  (select state = 'revoked'
   from api.collars where id = 'e4000000-0000-4000-8000-000000000001')
  and (select state = 'revoked'
   from private.device_credentials
   where credential_id = 'e6000000-0000-4000-8000-000000000001'),
  'replay closes restored device ingress before physical purge'
);

set local role service_role;
select is(
  private.replay_dog_deletion_tombstone_v1(
    (select item from tombstone_capture)
  ) ->> 'disposition',
  'already_present',
  'exact replay is idempotent'
);
select is(
  private.export_deletion_tombstones_v1(
    ((select export_page -> 'next_cursor' ->> 'requested_at'
      from tombstone_capture))::timestamptz,
    ((select export_page -> 'next_cursor' ->> 'request_id'
      from tombstone_capture))::uuid,
    100
  ) -> 'items',
  '[]'::jsonb,
  'the stable cursor resumes strictly after the exported tombstone'
);
select lives_ok(
  $$ select private.process_dog_deletion_batch_v1(10) $$,
  'the ordinary bounded worker completes a replayed deletion'
);
reset role;

select is(
  private.dog_deletion_counts_v1('e3000000-0000-4000-8000-000000000001'),
  jsonb_build_object(
    'dogs', 0, 'dog_memberships', 0, 'collars', 0, 'device_claims', 0,
    'daily_summaries', 0, 'dirty_summary_days', 0, 'device_credentials', 0,
    'sync_requests', 0, 'recordings', 0, 'telemetry_chunks', 0,
    'telemetry_points', 0, 'telemetry_loss_markers', 0,
    'device_daily_summaries', 0, 'config_revisions', 0,
    'config_resource_heads', 0, 'config_reported', 0, 'config_hlc_state', 0,
    'telemetry_retention_watermarks', 0, 'retention_jobs', 0,
    'retention_receipts', 0, 'recording_summaries', 0
  ),
  'replayed deletion leaves every current dog-scoped data class empty'
);
select ok(
  (select status = 'completed' from private.deletion_jobs)
  and exists (select 1 from private.deletion_receipts),
  'replayed deletion produces the ordinary durable completion receipt'
);

select * from finish();
rollback;
