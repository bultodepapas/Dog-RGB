-- Expected claim failures must commit their counters. Raising an exception
-- would roll the counter update back with the RPC transaction, so the narrow
-- gateway returns an internal problem marker that Edge converts to RFC 9457.
alter table private.cloud_limits
  add column claim_failures_per_key_15m integer not null default 5
    check (claim_failures_per_key_15m between 1 and 100),
  add column claim_cooldown_seconds integer not null default 900
    check (claim_cooldown_seconds between 30 and 86400);

create table private.claim_attempt_windows (
  key_kind text not null check (key_kind in ('source', 'device')),
  attempt_key bytea not null check (octet_length(attempt_key) = 32),
  window_started_at timestamptz not null default statement_timestamp(),
  failure_count integer not null default 0 check (failure_count >= 0),
  blocked_until timestamptz,
  updated_at timestamptz not null default statement_timestamp(),
  primary key (key_kind, attempt_key),
  check (blocked_until is null or blocked_until >= window_started_at)
);

create index claim_attempt_windows_updated_idx
  on private.claim_attempt_windows (updated_at);
revoke all on private.claim_attempt_windows from public, anon, authenticated, service_role;

create or replace function private.claim_attempt_retry_after_v1(
  p_key_kind text,
  p_attempt_key bytea
)
returns integer
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_retry_after integer;
begin
  if p_key_kind is null or p_key_kind not in ('source', 'device')
     or p_attempt_key is null or octet_length(p_attempt_key) <> 32 then
    raise exception using errcode = '22023', message = 'invalid_attempt_key';
  end if;
  select greatest(
    1,
    ceil(extract(epoch from (blocked_until - statement_timestamp())))::integer
  ) into v_retry_after
  from private.claim_attempt_windows
  where key_kind = p_key_kind
    and attempt_key = p_attempt_key
    and blocked_until > statement_timestamp();
  return coalesce(v_retry_after, 0);
end
$$;

create or replace function private.record_claim_failure_v1(
  p_key_kind text,
  p_attempt_key bytea,
  p_failure_limit integer,
  p_cooldown_seconds integer
)
returns integer
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_retry_after integer;
begin
  if p_key_kind is null or p_key_kind not in ('source', 'device')
     or p_attempt_key is null or octet_length(p_attempt_key) <> 32
     or p_failure_limit is null or p_failure_limit not between 1 and 100
     or p_cooldown_seconds is null or p_cooldown_seconds not between 30 and 86400 then
    raise exception using errcode = '22023', message = 'invalid_attempt_limit';
  end if;

  -- Amortized bounded cleanup prevents randomized device IDs from becoming a
  -- permanent storage-amplification vector. Active windows are never touched.
  delete from private.claim_attempt_windows
  where ctid in (
    select ctid from private.claim_attempt_windows
    where updated_at < statement_timestamp() - interval '2 days'
    order by updated_at
    limit 32
  );

  insert into private.claim_attempt_windows (
    key_kind, attempt_key, window_started_at, failure_count, blocked_until, updated_at
  ) values (
    p_key_kind,
    p_attempt_key,
    statement_timestamp(),
    1,
    case when p_failure_limit = 1
      then statement_timestamp() + pg_catalog.make_interval(secs => p_cooldown_seconds)
      else null
    end,
    statement_timestamp()
  )
  on conflict (key_kind, attempt_key) do update
  set window_started_at = case
        when private.claim_attempt_windows.window_started_at <= statement_timestamp() - interval '15 minutes'
          then statement_timestamp()
        else private.claim_attempt_windows.window_started_at
      end,
      failure_count = case
        when private.claim_attempt_windows.window_started_at <= statement_timestamp() - interval '15 minutes'
          then 1
        else private.claim_attempt_windows.failure_count + 1
      end,
      blocked_until = case
        when (
          case
            when private.claim_attempt_windows.window_started_at <= statement_timestamp() - interval '15 minutes'
              then 1
            else private.claim_attempt_windows.failure_count + 1
          end
        ) >= p_failure_limit
          then statement_timestamp() + pg_catalog.make_interval(secs => p_cooldown_seconds)
        else null
      end,
      updated_at = statement_timestamp()
  returning case
    when blocked_until > statement_timestamp() then greatest(
      1,
      ceil(extract(epoch from (blocked_until - statement_timestamp())))::integer
    )
    else 0
  end into v_retry_after;
  return v_retry_after;
end
$$;

revoke execute on function private.claim_attempt_retry_after_v1(text, bytea)
  from public, anon, authenticated, service_role;
revoke execute on function private.record_claim_failure_v1(text, bytea, integer, integer)
  from public, anon, authenticated, service_role;

create or replace function api.consume_device_claim_gateway_v1(
  p_source_attempt_key bytea,
  p_device_attempt_key bytea,
  p_code_digest bytea,
  p_request_id uuid,
  p_request_sha256 bytea,
  p_device_public_id uuid,
  p_credential_id uuid,
  p_secret_digest bytea,
  p_device jsonb,
  p_capabilities jsonb
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_claim private.device_claims%rowtype;
  v_limits private.cloud_limits%rowtype;
  v_response jsonb;
  v_retry_after integer;
  v_other_retry integer;
  v_claim_found boolean;
begin
  if p_source_attempt_key is null or octet_length(p_source_attempt_key) <> 32
     or p_device_attempt_key is null or octet_length(p_device_attempt_key) <> 32
     or p_code_digest is null or octet_length(p_code_digest) <> 32
     or p_request_sha256 is null or octet_length(p_request_sha256) <> 32
     or p_secret_digest is null or octet_length(p_secret_digest) <> 32 then
    raise exception using errcode = '22023', message = 'invalid_claim_request';
  end if;

  select * into strict v_limits from private.cloud_limits where singleton;
  select * into v_claim
  from private.device_claims
  where code_digest = p_code_digest
  for update;
  v_claim_found := found;

  -- A committed exact replay is correctness work, not a new attempt. It must
  -- remain available even if the source entered cooldown after the commit.
  if v_claim_found and v_claim.state = 'consumed'
     and v_claim.consumed_by_device_id = p_device_public_id
     and v_claim.request_id = p_request_id then
    if v_claim.request_sha256 <> p_request_sha256 then
      raise exception using errcode = '23505', message = 'request_id_conflict';
    end if;
    return v_claim.response_json;
  end if;

  if v_limits.enabled then
    v_retry_after := private.claim_attempt_retry_after_v1('source', p_source_attempt_key);
    v_other_retry := private.claim_attempt_retry_after_v1('device', p_device_attempt_key);
    v_retry_after := greatest(v_retry_after, v_other_retry);
    if v_retry_after > 0 then
      return jsonb_build_object(
        '_problem', 'rate_limited',
        'retry_after_seconds', v_retry_after
      );
    end if;
  end if;

  if not v_claim_found or v_claim.state <> 'issued'
     or v_claim.expires_at <= statement_timestamp()
     or v_claim.attempt_count >= v_claim.max_attempts then
    if v_claim_found and v_claim.state = 'issued' and v_claim.expires_at <= statement_timestamp() then
      update private.device_claims set state = 'expired' where id = v_claim.id;
    elsif v_claim_found and v_claim.state = 'issued' and v_claim.attempt_count < v_claim.max_attempts then
      update private.device_claims set attempt_count = attempt_count + 1 where id = v_claim.id;
    end if;
    if v_limits.enabled then
      v_retry_after := private.record_claim_failure_v1(
        'source', p_source_attempt_key,
        v_limits.claim_failures_per_key_15m, v_limits.claim_cooldown_seconds
      );
      v_other_retry := private.record_claim_failure_v1(
        'device', p_device_attempt_key,
        v_limits.claim_failures_per_key_15m, v_limits.claim_cooldown_seconds
      );
      v_retry_after := greatest(v_retry_after, v_other_retry);
      if v_retry_after > 0 then
        return jsonb_build_object(
          '_problem', 'rate_limited',
          'retry_after_seconds', v_retry_after
        );
      end if;
    end if;
    return jsonb_build_object('_problem', 'claim_unavailable');
  end if;

  begin
    v_response := api.consume_device_claim_v1(
      p_code_digest,
      p_request_id,
      p_request_sha256,
      p_device_public_id,
      p_credential_id,
      p_secret_digest,
      p_device,
      p_capabilities
    );
  exception when unique_violation then
    update private.device_claims
    set attempt_count = attempt_count + 1
    where id = v_claim.id and attempt_count < max_attempts;
    if v_limits.enabled then
      v_retry_after := private.record_claim_failure_v1(
        'source', p_source_attempt_key,
        v_limits.claim_failures_per_key_15m, v_limits.claim_cooldown_seconds
      );
      v_other_retry := private.record_claim_failure_v1(
        'device', p_device_attempt_key,
        v_limits.claim_failures_per_key_15m, v_limits.claim_cooldown_seconds
      );
      v_retry_after := greatest(v_retry_after, v_other_retry);
      if v_retry_after > 0 then
        return jsonb_build_object(
          '_problem', 'rate_limited',
          'retry_after_seconds', v_retry_after
        );
      end if;
    end if;
    return jsonb_build_object('_problem', 'device_identity_conflict');
  end;

  update private.device_claims
  set attempt_count = attempt_count + 1
  where id = v_claim.id and attempt_count < max_attempts;
  delete from private.claim_attempt_windows
  where (key_kind = 'source' and attempt_key = p_source_attempt_key)
     or (key_kind = 'device' and attempt_key = p_device_attempt_key);
  return v_response;
end
$$;

revoke execute on function api.consume_device_claim_v1(bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb)
  from public, anon, authenticated, service_role;
revoke execute on function api.consume_device_claim_gateway_v1(bytea, bytea, bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb)
  from public, anon, authenticated;
grant execute on function api.consume_device_claim_gateway_v1(bytea, bytea, bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb)
  to service_role;
