-- Dog RGB optional cloud foundation.
-- Extension versions are deliberately not pinned: hosted Supabase installs
-- the current project default. private.extension_inventory records what was
-- actually installed for each reset/deployment.

create schema if not exists api;
create schema if not exists private;

revoke all on schema api from public, anon, authenticated;
revoke all on schema private from public, anon, authenticated;
grant usage on schema api to authenticated, service_role;
grant usage on schema private to service_role;

create extension if not exists pgcrypto with schema extensions;
create extension if not exists postgis with schema extensions;

create table private.extension_inventory (
  extension_name text primary key,
  installed_version text not null,
  recorded_at timestamptz not null default statement_timestamp()
);

insert into private.extension_inventory (extension_name, installed_version)
select extname, extversion
from pg_extension
where extname in ('pgcrypto', 'postgis')
on conflict (extension_name) do update
set installed_version = excluded.installed_version,
    recorded_at = statement_timestamp();

create table api.profiles (
  user_id uuid primary key references auth.users(id) on delete cascade,
  display_name text check (display_name is null or char_length(display_name) between 1 and 80),
  default_timezone text not null default 'America/Bogota'
    check (char_length(default_timezone) between 1 and 64),
  units text not null default 'metric' check (units in ('metric', 'imperial')),
  created_at timestamptz not null default statement_timestamp(),
  updated_at timestamptz not null default statement_timestamp()
);

create table api.dogs (
  id uuid primary key default extensions.gen_random_uuid(),
  name text not null check (char_length(name) between 1 and 80),
  timezone text not null default 'America/Bogota' check (char_length(timezone) between 1 and 64),
  timezone_effective_at timestamptz not null default statement_timestamp(),
  breed text check (breed is null or char_length(breed) between 1 and 80),
  birth_date date,
  weight_kg numeric(6,3) check (weight_kg is null or weight_kg > 0),
  created_by uuid not null references auth.users(id) on delete restrict,
  created_at timestamptz not null default statement_timestamp(),
  updated_at timestamptz not null default statement_timestamp(),
  deleted_at timestamptz
);

create table api.dog_memberships (
  dog_id uuid not null references api.dogs(id) on delete cascade,
  user_id uuid not null references auth.users(id) on delete cascade,
  role text not null check (role in ('owner', 'editor', 'viewer')),
  created_at timestamptz not null default statement_timestamp(),
  primary key (dog_id, user_id)
);

create table api.collars (
  id uuid primary key default extensions.gen_random_uuid(),
  device_public_id uuid not null unique,
  dog_id uuid not null references api.dogs(id) on delete cascade,
  display_name text check (display_name is null or char_length(display_name) between 1 and 80),
  state text not null default 'pending' check (state in ('pending', 'active', 'revoked', 'retired')),
  hardware_revision text check (hardware_revision is null or char_length(hardware_revision) <= 64),
  firmware_version text check (firmware_version is null or char_length(firmware_version) <= 64),
  protocol_version integer check (protocol_version is null or protocol_version > 0),
  telemetry_schema integer check (telemetry_schema is null or telemetry_schema > 0),
  config_schema integer check (config_schema is null or config_schema > 0),
  capability_manifest jsonb check (
    capability_manifest is null or jsonb_typeof(capability_manifest) = 'object'
  ),
  capability_hash bytea check (capability_hash is null or octet_length(capability_hash) = 32),
  linked_at timestamptz,
  last_sync_at timestamptz,
  revoked_at timestamptz,
  created_at timestamptz not null default statement_timestamp(),
  updated_at timestamptz not null default statement_timestamp()
);

create table private.device_claims (
  id uuid primary key default extensions.gen_random_uuid(),
  dog_id uuid not null references api.dogs(id) on delete cascade,
  requested_by uuid not null references auth.users(id) on delete cascade,
  code_digest bytea not null unique check (octet_length(code_digest) = 32),
  expires_at timestamptz not null,
  attempt_count integer not null default 0 check (attempt_count >= 0),
  max_attempts integer not null default 5 check (max_attempts between 1 and 10),
  state text not null default 'issued' check (state in ('issued', 'consumed', 'expired', 'cancelled')),
  consumed_by_device_id uuid,
  request_id uuid,
  request_sha256 bytea check (request_sha256 is null or octet_length(request_sha256) = 32),
  response_json jsonb check (response_json is null or jsonb_typeof(response_json) = 'object'),
  created_at timestamptz not null default statement_timestamp(),
  consumed_at timestamptz,
  check (attempt_count <= max_attempts),
  check ((state = 'consumed') = (consumed_at is not null)),
  check ((state = 'consumed') = (
    consumed_by_device_id is not null and request_id is not null
    and request_sha256 is not null and response_json is not null
  )),
  unique (consumed_by_device_id, request_id)
);

create table private.device_credentials (
  credential_id uuid primary key,
  collar_id uuid not null references api.collars(id) on delete cascade,
  secret_digest bytea not null unique check (octet_length(secret_digest) = 32),
  credential_version integer not null default 1 check (credential_version > 0),
  state text not null default 'active' check (state in ('active', 'rotating', 'revoked', 'expired')),
  valid_from timestamptz not null default statement_timestamp(),
  valid_until timestamptz,
  last_used_at timestamptz,
  revoked_at timestamptz,
  created_at timestamptz not null default statement_timestamp(),
  check (valid_until is null or valid_until > valid_from),
  check ((state = 'revoked') = (revoked_at is not null))
);

create table private.sync_requests (
  collar_id uuid not null references api.collars(id) on delete cascade,
  request_id uuid not null,
  request_sha256 bytea not null check (octet_length(request_sha256) = 32),
  protocol_version integer not null check (protocol_version > 0),
  received_at timestamptz not null default statement_timestamp(),
  committed_at timestamptz,
  status text not null check (status in ('processing', 'committed', 'rejected')),
  response_json jsonb check (response_json is null or jsonb_typeof(response_json) = 'object'),
  primary key (collar_id, request_id),
  check ((status = 'committed') = (committed_at is not null))
);

create table api.recordings (
  id uuid primary key default extensions.gen_random_uuid(),
  collar_id uuid not null references api.collars(id) on delete cascade,
  boot_sequence bigint not null check (boot_sequence between 0 and 4294967295),
  started_at timestamptz,
  ended_at timestamptz,
  timezone_at_start text not null check (char_length(timezone_at_start) between 1 and 64),
  state text not null default 'open' check (state in ('open', 'closed', 'legacy', 'incomplete')),
  first_point_sequence bigint check (first_point_sequence between 0 and 4294967295),
  last_point_sequence bigint check (last_point_sequence between 0 and 4294967295),
  point_count integer not null default 0 check (point_count >= 0),
  min_lat_e7 integer check (min_lat_e7 between -900000000 and 900000000),
  max_lat_e7 integer check (max_lat_e7 between -900000000 and 900000000),
  min_lon_e7 integer check (min_lon_e7 between -1800000000 and 1800000000),
  max_lon_e7 integer check (max_lon_e7 between -1800000000 and 1800000000),
  clock_quality text not null check (clock_quality in (
    'unknown', 'approximate_persisted', 'server_anchored', 'sntp_synced', 'gnss_trusted', 'legacy_minute'
  )),
  telemetry_schema integer not null check (telemetry_schema > 0),
  firmware_version text not null check (char_length(firmware_version) between 1 and 64),
  created_at timestamptz not null default statement_timestamp(),
  updated_at timestamptz not null default statement_timestamp(),
  unique (collar_id, boot_sequence),
  check (ended_at is null or started_at is null or ended_at >= started_at),
  check ((first_point_sequence is null) = (last_point_sequence is null)),
  check (first_point_sequence is null or first_point_sequence <= last_point_sequence),
  check ((min_lat_e7 is null) = (max_lat_e7 is null)),
  check ((min_lon_e7 is null) = (max_lon_e7 is null))
);

create table private.telemetry_chunks (
  collar_id uuid not null references api.collars(id) on delete cascade,
  boot_sequence bigint not null check (boot_sequence between 0 and 4294967295),
  chunk_sequence bigint not null check (chunk_sequence between 0 and 4294967295),
  first_point_sequence bigint not null check (first_point_sequence between 0 and 4294967295),
  last_point_sequence bigint not null check (last_point_sequence between 0 and 4294967295),
  point_count integer not null check (point_count between 1 and 96),
  content_sha256 bytea not null check (octet_length(content_sha256) = 32),
  received_at timestamptz not null default statement_timestamp(),
  request_id uuid not null,
  is_final boolean not null default false,
  primary key (collar_id, boot_sequence, chunk_sequence),
  unique (collar_id, boot_sequence, first_point_sequence, last_point_sequence),
  check (last_point_sequence = first_point_sequence + point_count - 1)
);

create table api.telemetry_points (
  collar_id uuid not null references api.collars(id) on delete cascade,
  boot_sequence bigint not null check (boot_sequence between 0 and 4294967295),
  point_sequence bigint not null check (point_sequence between 0 and 4294967295),
  recorded_at timestamptz,
  received_at timestamptz not null default statement_timestamp(),
  lat_e7 integer,
  lon_e7 integer,
  position extensions.geography(point, 4326) generated always as (
    case when lat_e7 is null then null else
      extensions.st_setsrid(
        extensions.st_makepoint(lon_e7::double precision / 10000000.0, lat_e7::double precision / 10000000.0),
        4326
      )::extensions.geography
    end
  ) stored,
  reported_speed_cmps integer check (reported_speed_cmps between 0 and 65534),
  satellites smallint check (satellites between 0 and 255),
  flags integer not null check (flags between 0 and 127),
  time_quality text not null check (time_quality in (
    'unknown', 'approximate_persisted', 'server_anchored', 'sntp_synced', 'gnss_trusted', 'legacy_minute'
  )),
  telemetry_schema integer not null check (telemetry_schema > 0),
  firmware_version text not null check (char_length(firmware_version) between 1 and 64),
  chunk_sequence bigint not null check (chunk_sequence between 0 and 4294967295),
  primary key (collar_id, boot_sequence, point_sequence),
  check ((lat_e7 is null) = (lon_e7 is null)),
  check (lat_e7 is null or lat_e7 between -900000000 and 900000000),
  check (lon_e7 is null or lon_e7 between -1800000000 and 1800000000),
  check ((time_quality = 'unknown') = (recorded_at is null))
);

create table private.telemetry_loss_markers (
  id uuid primary key,
  collar_id uuid not null references api.collars(id) on delete cascade,
  request_id uuid not null,
  first_missing_point_sequence bigint not null check (first_missing_point_sequence between 0 and 4294967295),
  last_missing_point_sequence bigint not null check (last_missing_point_sequence between 0 and 4294967295),
  dropped_points integer not null check (dropped_points > 0),
  reason text not null check (char_length(reason) between 1 and 64),
  recorded_at timestamptz not null default statement_timestamp(),
  unique (collar_id, id),
  check (first_missing_point_sequence <= last_missing_point_sequence)
);

create table private.device_daily_summaries (
  summary_id uuid primary key,
  collar_id uuid not null references api.collars(id) on delete cascade,
  request_id uuid not null,
  local_date date not null,
  timezone text not null check (char_length(timezone) between 1 and 64),
  source_revision bigint not null check (source_revision >= 0),
  window_start timestamptz not null,
  window_end timestamptz not null,
  observed_s bigint not null check (observed_s >= 0),
  moving_s bigint not null check (moving_s >= 0),
  inactive_s bigint not null check (inactive_s >= 0),
  distance_m bigint not null check (distance_m >= 0),
  max_speed_cmps integer check (max_speed_cmps is null or max_speed_cmps >= 0),
  valid_points integer not null check (valid_points >= 0),
  gap_count integer not null check (gap_count >= 0),
  dropped_points integer not null check (dropped_points >= 0),
  time_quality text not null check (time_quality in (
    'unknown', 'approximate_persisted', 'server_anchored', 'sntp_synced', 'gnss_trusted', 'legacy_minute'
  )),
  received_at timestamptz not null default statement_timestamp(),
  unique (collar_id, local_date, source_revision),
  check (window_end >= window_start),
  check (moving_s + inactive_s <= observed_s)
);

create table api.daily_summaries (
  dog_id uuid not null references api.dogs(id) on delete cascade,
  local_date date not null,
  timezone text not null check (char_length(timezone) between 1 and 64),
  observed_s bigint not null check (observed_s >= 0),
  moving_s bigint not null check (moving_s >= 0),
  inactive_s bigint not null check (inactive_s >= 0),
  unknown_s bigint not null check (unknown_s >= 0),
  distance_m bigint not null check (distance_m >= 0),
  average_observed_cmps integer check (average_observed_cmps >= 0),
  average_moving_cmps integer check (average_moving_cmps >= 0),
  filtered_max_speed_cmps integer check (filtered_max_speed_cmps >= 0),
  valid_points integer not null check (valid_points >= 0),
  warning_points integer not null check (warning_points >= 0),
  gap_count integer not null check (gap_count >= 0),
  dropped_points integer not null check (dropped_points >= 0),
  coverage_ratio numeric(7,6) not null check (coverage_ratio between 0 and 1),
  algorithm_version integer not null check (algorithm_version > 0),
  source_revision bigint not null check (source_revision >= 0),
  computed_at timestamptz not null default statement_timestamp(),
  primary key (dog_id, local_date, algorithm_version),
  check (moving_s + inactive_s <= observed_s)
);

create table api.recording_summaries (
  recording_id uuid not null references api.recordings(id) on delete cascade,
  observed_s bigint not null check (observed_s >= 0),
  moving_s bigint not null check (moving_s >= 0),
  inactive_s bigint not null check (inactive_s >= 0),
  unknown_s bigint not null check (unknown_s >= 0),
  distance_m bigint not null check (distance_m >= 0),
  average_observed_cmps integer check (average_observed_cmps >= 0),
  average_moving_cmps integer check (average_moving_cmps >= 0),
  filtered_max_speed_cmps integer check (filtered_max_speed_cmps >= 0),
  valid_points integer not null check (valid_points >= 0),
  warning_points integer not null check (warning_points >= 0),
  gap_count integer not null check (gap_count >= 0),
  dropped_points integer not null check (dropped_points >= 0),
  coverage_ratio numeric(7,6) not null check (coverage_ratio between 0 and 1),
  phase_durations jsonb,
  algorithm_version integer not null check (algorithm_version > 0),
  computed_at timestamptz not null default statement_timestamp(),
  primary key (recording_id, algorithm_version),
  check (moving_s + inactive_s <= observed_s),
  check (phase_durations is null or jsonb_typeof(phase_durations) = 'object')
);

create table private.dirty_summary_days (
  dog_id uuid not null references api.dogs(id) on delete cascade,
  local_date date not null,
  timezone text not null check (char_length(timezone) between 1 and 64),
  reason text not null check (char_length(reason) between 1 and 64),
  first_marked_at timestamptz not null default statement_timestamp(),
  last_marked_at timestamptz not null default statement_timestamp(),
  attempts integer not null default 0 check (attempts >= 0),
  locked_at timestamptz,
  last_error_code text check (last_error_code is null or char_length(last_error_code) <= 64),
  primary key (dog_id, local_date, timezone)
);

create table api.config_revisions (
  id uuid primary key default extensions.gen_random_uuid(),
  collar_id uuid not null references api.collars(id) on delete cascade,
  resource_key text not null check (resource_key in (
    'brightness', 'visual_mode', 'simple_effect', 'speed_profile', 'gps_quality', 'geofence_policy'
  )),
  mutation_id uuid not null,
  resource_schema integer not null check (resource_schema > 0),
  base_server_version bigint check (base_server_version is null or base_server_version >= 0),
  origin text not null check (origin in ('ap', 'web', 'migration', 'system')),
  actor_user_id uuid references auth.users(id) on delete set null,
  actor_device_id uuid,
  submitted_hlc_physical_ms bigint not null check (submitted_hlc_physical_ms between 0 and 4102444800000),
  submitted_hlc_logical bigint not null check (submitted_hlc_logical between 0 and 4294967295),
  submitted_actor_id uuid not null,
  submitted_time_quality text not null check (submitted_time_quality in (
    'unknown', 'approximate_persisted', 'server_anchored', 'sntp_synced', 'gnss_trusted'
  )),
  accepted_hlc_physical_ms bigint not null check (accepted_hlc_physical_ms between 0 and 4102444800000),
  accepted_hlc_logical bigint not null check (accepted_hlc_logical between 0 and 4294967295),
  accepted_actor_id uuid not null,
  ordering_mode text not null check (ordering_mode in ('authored', 'fallback_received')),
  server_version bigint check (server_version is null or server_version > 0),
  body jsonb not null check (jsonb_typeof(body) = 'object'),
  body_sha256 bytea not null check (octet_length(body_sha256) = 32),
  disposition text not null check (disposition in ('winning', 'superseded', 'rejected')),
  rejection_code text check (rejection_code is null or char_length(rejection_code) <= 64),
  received_at timestamptz not null default statement_timestamp(),
  unique (collar_id, mutation_id),
  check ((disposition = 'rejected') = (rejection_code is not null))
);

create table api.config_resource_heads (
  collar_id uuid not null references api.collars(id) on delete cascade,
  resource_key text not null,
  resource_schema integer not null check (resource_schema > 0),
  server_version bigint not null check (server_version > 0),
  body jsonb not null check (jsonb_typeof(body) = 'object'),
  body_sha256 bytea not null check (octet_length(body_sha256) = 32),
  winning_revision_id uuid not null references api.config_revisions(id) on delete restrict,
  accepted_hlc_physical_ms bigint not null check (accepted_hlc_physical_ms between 0 and 4102444800000),
  accepted_hlc_logical bigint not null check (accepted_hlc_logical between 0 and 4294967295),
  accepted_actor_id uuid not null,
  updated_at timestamptz not null default statement_timestamp(),
  primary key (collar_id, resource_key)
);

create table api.config_reported (
  collar_id uuid not null references api.collars(id) on delete cascade,
  resource_key text not null,
  reported_server_version bigint not null check (reported_server_version > 0),
  reported_body_sha256 bytea not null check (octet_length(reported_body_sha256) = 32),
  status text not null check (status in ('applied', 'rejected_unsupported', 'rejected_invalid', 'storage_failed')),
  error_code text check (error_code is null or char_length(error_code) <= 64),
  firmware_version text not null check (char_length(firmware_version) between 1 and 64),
  config_schema integer not null check (config_schema > 0),
  device_applied_at timestamptz,
  cloud_received_at timestamptz not null default statement_timestamp(),
  primary key (collar_id, resource_key),
  check ((status = 'applied') = (error_code is null))
);

-- Foreign keys are not automatically indexed by PostgreSQL.
create index dogs_created_by_idx on api.dogs (created_by);
create index dog_memberships_user_dog_idx on api.dog_memberships (user_id, dog_id, role);
create index collars_dog_id_idx on api.collars (dog_id);
create index device_claims_dog_id_idx on private.device_claims (dog_id);
create index device_claims_requested_by_idx on private.device_claims (requested_by);
create index device_claims_active_expiry_idx on private.device_claims (expires_at)
  where state = 'issued';
create index device_credentials_collar_state_idx on private.device_credentials (collar_id, state);
create index sync_requests_committed_retention_idx on private.sync_requests (committed_at)
  where status = 'committed';
create index recordings_collar_started_idx on api.recordings (collar_id, started_at desc);
create index telemetry_points_collar_time_idx
  on api.telemetry_points (collar_id, recorded_at, point_sequence);
create index telemetry_points_chunk_idx
  on api.telemetry_points (collar_id, boot_sequence, chunk_sequence);
create index telemetry_loss_markers_collar_idx on private.telemetry_loss_markers (collar_id);
create index device_daily_summaries_collar_date_idx
  on private.device_daily_summaries (collar_id, local_date desc);
create index config_revisions_collar_resource_received_idx
  on api.config_revisions (collar_id, resource_key, received_at desc);
create index config_revisions_actor_user_idx on api.config_revisions (actor_user_id)
  where actor_user_id is not null;
create index config_resource_heads_revision_idx on api.config_resource_heads (winning_revision_id);

alter table api.profiles enable row level security;
alter table api.dogs enable row level security;
alter table api.dog_memberships enable row level security;
alter table api.collars enable row level security;
alter table api.recordings enable row level security;
alter table api.telemetry_points enable row level security;
alter table api.daily_summaries enable row level security;
alter table api.recording_summaries enable row level security;
alter table api.config_revisions enable row level security;
alter table api.config_resource_heads enable row level security;
alter table api.config_reported enable row level security;

revoke all on all tables in schema api from public, anon, authenticated;
revoke all on all tables in schema private from public, anon, authenticated;
grant select, update (display_name, default_timezone, units, updated_at)
  on api.profiles to authenticated;
grant select on api.dogs, api.dog_memberships, api.collars, api.recordings,
  api.telemetry_points, api.daily_summaries, api.recording_summaries,
  api.config_revisions, api.config_resource_heads, api.config_reported
  to authenticated;

alter default privileges in schema api revoke all on tables from public, anon, authenticated;
alter default privileges in schema private revoke all on tables from public, anon, authenticated;
alter default privileges in schema api revoke execute on functions from public, anon, authenticated;
alter default privileges in schema private revoke execute on functions from public, anon, authenticated;
