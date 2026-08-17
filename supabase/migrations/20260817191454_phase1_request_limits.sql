-- Phase 1 request limits are database-owned so every Edge instance observes
-- the same counters. The switch keeps this optional for DIY/local use while
-- conservative defaults protect a hosted deployment.
create table private.cloud_limits (
  singleton boolean primary key default true check (singleton),
  enabled boolean not null default true,
  claim_issues_per_user_hour integer not null default 5
    check (claim_issues_per_user_hour between 1 and 100),
  sync_burst_per_collar_minute integer not null default 6
    check (sync_burst_per_collar_minute between 1 and 120),
  sync_sustained_per_collar_hour integer not null default 45
    check (sync_sustained_per_collar_hour between 1 and 3600),
  points_per_collar_utc_day integer not null default 120000
    check (points_per_collar_utc_day between 384 and 10000000),
  updated_at timestamptz not null default statement_timestamp()
);

insert into private.cloud_limits (singleton) values (true);
revoke all on private.cloud_limits from public, anon, authenticated, service_role;

create index device_claims_requested_created_idx
  on private.device_claims (requested_by, created_at desc);
create unique index device_claims_one_issued_per_dog_idx
  on private.device_claims (dog_id) where state = 'issued';
create index sync_requests_collar_received_idx
  on private.sync_requests (collar_id, received_at desc) where status = 'committed';
create index telemetry_chunks_collar_received_idx
  on private.telemetry_chunks (collar_id, received_at desc);

create or replace function api.issue_device_claim_v1(
  p_dog_id uuid,
  p_requested_by uuid,
  p_code_digest bytea,
  p_expires_at timestamptz,
  p_max_attempts integer default 5
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_claim_id uuid;
  v_limits private.cloud_limits%rowtype;
begin
  if octet_length(p_code_digest) <> 32
     or p_expires_at <= statement_timestamp()
     or p_expires_at > statement_timestamp() + interval '15 minutes'
     or p_max_attempts not between 1 and 5 then
    raise exception using errcode = '22023', message = 'invalid_claim';
  end if;
  if not exists (
    select 1 from api.dog_memberships dm
    where dm.dog_id = p_dog_id
      and dm.user_id = p_requested_by
      and dm.role in ('owner', 'editor')
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  -- Serialize issue attempts for one user so the hourly cap and active-claim
  -- invariant remain exact under concurrent browser requests.
  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(p_requested_by::text, 1788446301)
  );
  update private.device_claims
  set state = 'expired'
  where dog_id = p_dog_id
    and state = 'issued'
    and expires_at <= statement_timestamp();

  if exists (
    select 1 from private.device_claims
    where dog_id = p_dog_id and state = 'issued'
  ) then
    raise exception using errcode = '23505', message = 'active_claim_exists';
  end if;

  select * into strict v_limits from private.cloud_limits where singleton;
  if v_limits.enabled and (
    select count(*) from private.device_claims
    where requested_by = p_requested_by
      and created_at >= statement_timestamp() - interval '1 hour'
  ) >= v_limits.claim_issues_per_user_hour then
    raise exception using errcode = 'P0001', message = 'rate_limited_claim_issue';
  end if;

  insert into private.device_claims (
    dog_id, requested_by, code_digest, expires_at, max_attempts
  ) values (
    p_dog_id, p_requested_by, p_code_digest, p_expires_at, p_max_attempts
  ) returning id into v_claim_id;
  return v_claim_id;
end
$$;

revoke execute on function api.issue_device_claim_v1(uuid, uuid, bytea, timestamptz, integer)
  from public, anon, authenticated;
grant execute on function api.issue_device_claim_v1(uuid, uuid, bytea, timestamptz, integer)
  to service_role;

create or replace function private.enforce_sync_rate_limit_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_limits private.cloud_limits%rowtype;
begin
  -- Device revoke receipts are inserted already committed; sync requests are
  -- inserted as processing. Exact sync replays never reach this trigger.
  if new.status <> 'processing' then
    return new;
  end if;
  select * into strict v_limits from private.cloud_limits where singleton;
  if not v_limits.enabled then
    return new;
  end if;
  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(new.collar_id::text, 1937337691)
  );
  if (
    select count(*) from private.sync_requests
    where collar_id = new.collar_id and status = 'committed'
      and received_at >= new.received_at - interval '1 minute'
  ) >= v_limits.sync_burst_per_collar_minute then
    raise exception using errcode = 'P0001', message = 'rate_limited_sync_burst';
  end if;
  if (
    select count(*) from private.sync_requests
    where collar_id = new.collar_id and status = 'committed'
      and received_at >= new.received_at - interval '1 hour'
  ) >= v_limits.sync_sustained_per_collar_hour then
    raise exception using errcode = 'P0001', message = 'rate_limited_sync_sustained';
  end if;
  return new;
end
$$;

revoke execute on function private.enforce_sync_rate_limit_v1()
  from public, anon, authenticated, service_role;
create trigger dog_rgb_sync_rate_limit
before insert on private.sync_requests
for each row execute function private.enforce_sync_rate_limit_v1();

create or replace function private.enforce_telemetry_quota_v1()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_limits private.cloud_limits%rowtype;
  v_day_start timestamptz;
  v_points bigint;
begin
  select * into strict v_limits from private.cloud_limits where singleton;
  if not v_limits.enabled then
    return new;
  end if;
  if exists (
    select 1 from private.telemetry_chunks
    where collar_id = new.collar_id
      and boot_sequence = new.boot_sequence
      and chunk_sequence = new.chunk_sequence
  ) then
    return new;
  end if;
  perform pg_catalog.pg_advisory_xact_lock(
    pg_catalog.hashtextextended(new.collar_id::text, 2086298399)
  );
  v_day_start := pg_catalog.date_trunc('day', new.received_at at time zone 'UTC') at time zone 'UTC';
  select coalesce(sum(point_count), 0) into v_points
  from private.telemetry_chunks
  where collar_id = new.collar_id and received_at >= v_day_start
    and received_at < v_day_start + interval '1 day';
  if v_points + new.point_count > v_limits.points_per_collar_utc_day then
    raise exception using errcode = 'P0001', message = 'quota_exceeded';
  end if;
  return new;
end
$$;

revoke execute on function private.enforce_telemetry_quota_v1()
  from public, anon, authenticated, service_role;
create trigger dog_rgb_telemetry_quota
before insert on private.telemetry_chunks
for each row execute function private.enforce_telemetry_quota_v1();

-- Keep credential-state classification and the row lock in the same database
-- transaction as sync. The original transaction function becomes internal;
-- Edge receives only this narrower gateway wrapper.
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
  return api.device_sync_v1(
    p_credential_id,
    p_secret_digest,
    p_request_id,
    p_request_sha256,
    p_request
  );
end
$$;

revoke execute on function api.device_sync_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated, service_role;
revoke execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated;
grant execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  to service_role;
