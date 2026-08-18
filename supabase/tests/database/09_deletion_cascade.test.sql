begin;
create extension if not exists pgtap with schema extensions;

select plan(13);

create temporary table deletion_fixture (
  dog_id uuid primary key,
  owner_id uuid not null,
  collar_id uuid not null,
  recording_id uuid not null,
  claim_id uuid not null,
  credential_id uuid not null,
  request_id uuid not null,
  loss_id uuid not null,
  summary_id uuid not null,
  revision_id uuid not null,
  mutation_id uuid not null,
  is_delete_target boolean not null
) on commit drop;

insert into deletion_fixture values
  (
    'a3000000-0000-4000-8000-000000000001',
    'a1000000-0000-4000-8000-000000000001',
    '91000000-0000-4000-8000-000000000001',
    '92000000-0000-4000-8000-000000000001',
    '93000000-0000-4000-8000-000000000001',
    '94000000-0000-4000-8000-000000000001',
    '95000000-0000-4000-8000-000000000001',
    '96000000-0000-4000-8000-000000000001',
    '97000000-0000-4000-8000-000000000001',
    '98000000-0000-4000-8000-000000000001',
    '99000000-0000-4000-8000-000000000001',
    true
  ),
  (
    'a3000000-0000-4000-8000-000000000002',
    'a1000000-0000-4000-8000-000000000002',
    '91000000-0000-4000-8000-000000000002',
    '92000000-0000-4000-8000-000000000002',
    '93000000-0000-4000-8000-000000000002',
    '94000000-0000-4000-8000-000000000002',
    '95000000-0000-4000-8000-000000000002',
    '96000000-0000-4000-8000-000000000002',
    '97000000-0000-4000-8000-000000000002',
    '98000000-0000-4000-8000-000000000002',
    '99000000-0000-4000-8000-000000000002',
    false
  );

select results_eq(
  $$
    with recursive cascade_graph(parent_oid, child_oid) as (
      select 'api.dogs'::regclass::oid, conrelid
      from pg_constraint
      where contype = 'f'
        and confrelid = 'api.dogs'::regclass
        and confdeltype = 'c'
      union
      select graph.child_oid, constraint_row.conrelid
      from cascade_graph graph
      join pg_constraint constraint_row
        on constraint_row.confrelid = graph.child_oid
       and constraint_row.contype = 'f'
       and constraint_row.confdeltype = 'c'
    )
    select child_oid::regclass::text
    from cascade_graph
    order by 1
  $$,
  $$
    values
      ('api.collars'),
      ('api.config_reported'),
      ('api.config_resource_heads'),
      ('api.config_revisions'),
      ('api.daily_summaries'),
      ('api.dog_memberships'),
      ('api.recording_summaries'),
      ('api.recordings'),
      ('api.telemetry_points'),
      ('private.config_hlc_state'),
      ('private.device_claims'),
      ('private.device_credentials'),
      ('private.device_daily_summaries'),
      ('private.dirty_summary_days'),
      ('private.sync_requests'),
      ('private.telemetry_chunks'),
      ('private.telemetry_loss_markers')
  $$,
  'the explicit deletion fixture covers every table in the dog cascade graph'
);

select is(
  (
    select count(*)
    from pg_constraint constraint_row
    join pg_class relation on relation.oid = constraint_row.conrelid
    join pg_namespace namespace on namespace.oid = relation.relnamespace
    where constraint_row.contype = 'f'
      and namespace.nspname in ('api', 'private')
      and not exists (
        select 1
        from pg_index index_row
        where index_row.indrelid = constraint_row.conrelid
          and index_row.indisvalid
          and index_row.indisready
          and (
            index_row.indpred is null
            or (
              cardinality(constraint_row.conkey) = 1
              and pg_get_expr(index_row.indpred, index_row.indrelid) = (
                '(' || pg_catalog.quote_ident((
                  select attribute.attname
                  from pg_attribute attribute
                  where attribute.attrelid = constraint_row.conrelid
                    and attribute.attnum = constraint_row.conkey[1]
                )) || ' IS NOT NULL)'
              )
            )
          )
          and index_row.indnkeyatts >= cardinality(constraint_row.conkey)
          and not exists (
            select 1
            from unnest(constraint_row.conkey) with ordinality as key_column(attnum, position)
            where (index_row.indkey::smallint[])[position - 1] <> key_column.attnum
          )
      )
  ),
  0::bigint,
  'every project-owned foreign key has a usable leading-column index for joins and cascades'
);

insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
)
select
  '00000000-0000-0000-0000-000000000000', owner_id,
  'authenticated', 'authenticated',
  case when is_delete_target then 'delete-target@example.test' else 'delete-survivor@example.test' end,
  extensions.crypt('local-deletion-test-password', extensions.gen_salt('bf')),
  statement_timestamp(), '{"provider":"email","providers":["email"]}'::jsonb,
  '{}'::jsonb, statement_timestamp(), statement_timestamp(), '', '', '', ''
from deletion_fixture;

insert into api.dogs (id, name, timezone, created_by)
select
  dog_id,
  case when is_delete_target then 'Delete Target' else 'Survivor' end,
  'America/Bogota', owner_id
from deletion_fixture;

insert into api.dog_memberships (dog_id, user_id, role)
select dog_id, owner_id, 'owner'
from deletion_fixture;

insert into api.collars (id, device_public_id, dog_id, state, revoked_at)
select
  collar_id,
  ('a1000000-0000-4000-8000-' || right(replace(collar_id::text, '-', ''), 12))::uuid,
  dog_id,
  case when is_delete_target then 'revoked' else 'active' end,
  case when is_delete_target then statement_timestamp() else null end
from deletion_fixture;

insert into private.device_claims (
  id, dog_id, requested_by, code_digest, expires_at, state
)
select
  claim_id, dog_id, owner_id,
  extensions.digest(dog_id::text, 'sha256'),
  statement_timestamp() + interval '10 minutes',
  'cancelled'
from deletion_fixture;

insert into private.device_credentials (
  credential_id, collar_id, secret_digest, state, revoked_at
)
select
  credential_id, collar_id,
  extensions.digest(collar_id::text, 'sha256'),
  case when is_delete_target then 'revoked' else 'active' end,
  case when is_delete_target then statement_timestamp() else null end
from deletion_fixture;

insert into private.sync_requests (
  collar_id, request_id, request_sha256, protocol_version,
  committed_at, status, response_json
)
select
  collar_id, request_id, extensions.digest(request_id::text, 'sha256'), 1,
  statement_timestamp(), 'committed', '{}'::jsonb
from deletion_fixture;

insert into api.recordings (
  id, collar_id, boot_sequence, started_at, ended_at, timezone_at_start,
  state, first_point_sequence, last_point_sequence, point_count,
  min_lat_e7, max_lat_e7, min_lon_e7, max_lon_e7,
  clock_quality, telemetry_schema, firmware_version
)
select
  recording_id, collar_id, 1,
  '2026-08-17 12:00:00+00', '2026-08-17 12:00:05+00', 'America/Bogota',
  'closed', 1, 1, 1, 47110000, 47110000, -740721000, -740721000,
  'gnss_trusted', 3, 'deletion-fixture'
from deletion_fixture;

insert into private.telemetry_chunks (
  collar_id, boot_sequence, chunk_sequence, first_point_sequence,
  last_point_sequence, point_count, content_sha256, request_id, is_final
)
select
  collar_id, 1, 1, 1, 1, 1,
  extensions.digest(('chunk-' || collar_id)::text, 'sha256'), request_id, true
from deletion_fixture;

insert into api.telemetry_points (
  collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
  reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
  firmware_version, chunk_sequence
)
select
  collar_id, 1, 1, '2026-08-17 12:00:00+00', 47110000, -740721000,
  25, 8, 13, 'gnss_trusted', 3, 'deletion-fixture', 1
from deletion_fixture;

insert into private.telemetry_loss_markers (
  id, collar_id, request_id, boot_sequence, first_missing_point_sequence,
  last_missing_point_sequence, dropped_points, reason, recorded_utc_ms
)
select loss_id, collar_id, request_id, 1, 2, 2, 1, 'fixture_gap', 1786968005000
from deletion_fixture;

insert into private.device_daily_summaries (
  summary_id, collar_id, request_id, local_date, timezone, source_revision,
  window_start, window_end, observed_s, moving_s, inactive_s, distance_m,
  max_speed_cmps, valid_points, gap_count, dropped_points, time_quality
)
select
  summary_id, collar_id, request_id, '2026-08-17', 'America/Bogota', 1,
  '2026-08-17 05:00:00+00', '2026-08-18 05:00:00+00',
  5, 5, 0, 1, 25, 1, 1, 1, 'gnss_trusted'
from deletion_fixture;

insert into api.daily_summaries (
  dog_id, local_date, timezone, observed_s, moving_s, inactive_s, unknown_s,
  distance_m, valid_points, warning_points, gap_count, dropped_points,
  coverage_ratio, algorithm_version, source_revision
)
select
  dog_id, '2026-08-17', 'America/Bogota', 5, 5, 0, 86395,
  1, 1, 0, 1, 1, 0.000058, 1, 1
from deletion_fixture;

insert into api.recording_summaries (
  recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
  valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
  algorithm_version
)
select recording_id, 5, 5, 0, 0, 1, 1, 0, 1, 1, 1, 1
from deletion_fixture;

insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
select dog_id, '2026-08-17', 'America/Bogota', 'deletion_fixture'
from deletion_fixture;

insert into api.config_revisions (
  id, collar_id, resource_key, mutation_id, resource_schema,
  base_server_version, origin, actor_user_id,
  submitted_hlc_physical_ms, submitted_hlc_logical, submitted_actor_id,
  submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
  accepted_actor_id, ordering_mode, server_version, body, body_sha256,
  disposition
)
select
  revision_id, collar_id, 'brightness', mutation_id, 1, 0, 'web',
  'a1000000-0000-4000-8000-000000000001',
  1786968000000, 0, owner_id, 'sntp_synced',
  1786968000000, 0, owner_id, 'authored', 1,
  '{"brightness":64}'::jsonb,
  extensions.digest('{"brightness":64}'::jsonb::text, 'sha256'), 'winning'
from deletion_fixture;

insert into api.config_resource_heads (
  collar_id, resource_key, resource_schema, server_version, body,
  body_sha256, winning_revision_id, accepted_hlc_physical_ms,
  accepted_hlc_logical, accepted_actor_id
)
select
  collar_id, 'brightness', 1, 1, '{"brightness":64}'::jsonb,
  extensions.digest('{"brightness":64}'::jsonb::text, 'sha256'),
  revision_id, 1786968000000, 0, owner_id
from deletion_fixture;

insert into api.config_reported (
  collar_id, resource_key, reported_server_version, reported_body_sha256,
  status, firmware_version, config_schema, device_applied_at
)
select
  collar_id, 'brightness', 1,
  extensions.digest('{"brightness":64}'::jsonb::text, 'sha256'),
  'applied', 'deletion-fixture', 1, statement_timestamp()
from deletion_fixture;

insert into private.config_hlc_state (collar_id, physical_ms, logical)
select collar_id, 1786968000000, 0
from deletion_fixture;

create function pg_temp.dog_graph_counts(p_dog_id uuid)
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

create function pg_temp.account_delete_is_blocked(p_user_id uuid)
returns boolean
language plpgsql
set search_path = ''
as $$
begin
  delete from auth.users where id = p_user_id;
  return false;
exception when foreign_key_violation then
  return true;
end
$$;

create function pg_temp.uniform_dog_graph_counts(p_count bigint)
returns jsonb
language sql
immutable
set search_path = ''
as $$
  select jsonb_build_object(
    'dogs', p_count,
    'dog_memberships', p_count,
    'collars', p_count,
    'device_claims', p_count,
    'daily_summaries', p_count,
    'dirty_summary_days', p_count,
    'device_credentials', p_count,
    'sync_requests', p_count,
    'recordings', p_count,
    'telemetry_chunks', p_count,
    'telemetry_points', p_count,
    'telemetry_loss_markers', p_count,
    'device_daily_summaries', p_count,
    'config_revisions', p_count,
    'config_resource_heads', p_count,
    'config_reported', p_count,
    'config_hlc_state', p_count,
    'recording_summaries', p_count
  )
$$;

select ok(
  pg_temp.account_delete_is_blocked('a1000000-0000-4000-8000-000000000001'),
  'account deletion fails closed while the account still owns a dog'
);

select is(
  pg_temp.dog_graph_counts('a3000000-0000-4000-8000-000000000001'),
  pg_temp.uniform_dog_graph_counts(1),
  'the delete target contains one row in every current dog-scoped data class'
);

select is(
  pg_temp.dog_graph_counts('a3000000-0000-4000-8000-000000000002'),
  pg_temp.uniform_dog_graph_counts(1),
  'the survivor fixture has the same complete data-class topology'
);

select lives_ok(
  $$ delete from api.dogs where id = 'a3000000-0000-4000-8000-000000000001' $$,
  'deleting a dog completes even with a desired-config head referencing its winning revision'
);

select is(
  pg_temp.dog_graph_counts('a3000000-0000-4000-8000-000000000001'),
  pg_temp.uniform_dog_graph_counts(0),
  'dog deletion removes raw, derived, receipt, credential, loss, and configuration rows'
);

select is(
  pg_temp.dog_graph_counts('a3000000-0000-4000-8000-000000000002'),
  pg_temp.uniform_dog_graph_counts(1),
  'dog deletion leaves every data class for another dog intact'
);

insert into api.dog_memberships (dog_id, user_id, role)
values (
  'a3000000-0000-4000-8000-000000000002',
  'a1000000-0000-4000-8000-000000000001',
  'viewer'
);

select lives_ok(
  $$ delete from auth.users where id = 'a1000000-0000-4000-8000-000000000001' $$,
  'account deletion succeeds after its owned dogs have been deleted'
);

select is(
  (select count(*) from api.profiles where user_id = 'a1000000-0000-4000-8000-000000000001'),
  0::bigint,
  'account deletion cascades to the account profile'
);

select is(
  (
    select count(*)
    from api.dog_memberships
    where dog_id = 'a3000000-0000-4000-8000-000000000002'
      and user_id = 'a1000000-0000-4000-8000-000000000001'
  ),
  0::bigint,
  'account deletion removes memberships on dogs owned by someone else'
);

select is(
  (
    select actor_user_id
    from api.config_revisions
    where id = '98000000-0000-4000-8000-000000000002'
  ),
  null::uuid,
  'account deletion anonymizes retained configuration audit authorship'
);

select is(
  pg_temp.dog_graph_counts('a3000000-0000-4000-8000-000000000002'),
  pg_temp.uniform_dog_graph_counts(1),
  'account deletion does not damage a dog owned by another account'
);

select * from finish();
rollback;
