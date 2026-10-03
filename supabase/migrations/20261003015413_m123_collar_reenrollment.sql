-- M1.23 keeps the hardware identity and telemetry ledger stable across a
-- same-owner re-enrollment. Resolve any duplicate active collars with the
-- owner before applying the unique index; this preflight never mutates rows.
do $$
declare
  v_duplicates text;
begin
  select string_agg(
    pg_catalog.format('dog_id=%s active_collar_ids=%s', duplicate.dog_id, duplicate.collar_ids),
    E'\n' order by duplicate.dog_id
  )
  into v_duplicates
  from (
    select collar.dog_id,
           pg_catalog.array_agg(collar.id order by collar.id) as collar_ids
    from api.collars as collar
    where collar.state = 'active'
    group by collar.dog_id
    having count(*) > 1
  ) as duplicate;

  if v_duplicates is not null then
    raise exception using
      errcode = 'P0001',
      message = 'active_collar_preflight_failed',
      detail = v_duplicates,
      hint = 'Review each dog with its owner, revoke or retire the unintended active link, preserve its history, then retry this additive migration.';
  end if;
end
$$;

create unique index collars_one_active_per_dog_idx
  on api.collars (dog_id)
  where state = 'active';

-- Claims serialize on the dog row. Existing UUIDs can only reactivate their
-- revoked collar for its existing dog, and only a current owner can do so.
-- Credential rows are locked before the collar row to match device sync,
-- device revoke and website revoke. Telemetry, request receipts and retention
-- state stay attached to the stable collar_id.
create or replace function api.consume_device_claim_v1(
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
  v_collar api.collars%rowtype;
  v_collar_id uuid;
  v_dog_id uuid;
  v_issuer_role text;
  v_existing_collar boolean := false;
  v_now timestamptz := statement_timestamp();
  v_response jsonb;
begin
  if p_code_digest is null or octet_length(p_code_digest) <> 32
     or p_request_id is null or p_request_sha256 is null or octet_length(p_request_sha256) <> 32
     or p_device_public_id is null or p_credential_id is null
     or p_secret_digest is null or octet_length(p_secret_digest) <> 32
     or jsonb_typeof(p_device) <> 'object' or jsonb_typeof(p_capabilities) <> 'object'
     or pg_column_size(p_capabilities) > 32768 then
    raise exception using errcode = '22023', message = 'invalid_claim_request';
  end if;

  select * into v_claim
  from private.device_claims
  where code_digest = p_code_digest
  for update;
  if not found then
    raise exception using errcode = '28000', message = 'claim_not_available';
  end if;
  if v_claim.state = 'consumed' then
    if v_claim.consumed_by_device_id = p_device_public_id
       and v_claim.request_id = p_request_id then
      if v_claim.request_sha256 <> p_request_sha256 then
        raise exception using errcode = '23505', message = 'request_id_conflict';
      end if;
      return v_claim.response_json;
    end if;
    raise exception using errcode = '28000', message = 'claim_not_available';
  end if;
  if v_claim.state <> 'issued' or v_claim.expires_at <= statement_timestamp()
     or v_claim.attempt_count >= v_claim.max_attempts then
    if v_claim.state = 'issued' and v_claim.expires_at <= statement_timestamp() then
      update private.device_claims set state = 'expired' where id = v_claim.id;
    end if;
    raise exception using errcode = '28000', message = 'claim_not_available';
  end if;

  -- Deletion takes this row before credentials and collars. Holding it also
  -- serializes claims for different collars targeting the same dog.
  select dog.id into v_dog_id
  from api.dogs as dog
  where dog.id = v_claim.dog_id
    and dog.deleted_at is null
  for update;
  if not found then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  -- Claim issuance is a point-in-time authorization. Recheck both the live
  -- account and its current dog role when the physical device consumes it.
  select membership.role into v_issuer_role
  from api.dog_memberships as membership
  join auth.users as issuer on issuer.id = membership.user_id
  where membership.dog_id = v_claim.dog_id
    and membership.user_id = v_claim.requested_by
    and membership.role in ('owner', 'editor')
    and issuer.deleted_at is null;
  if v_issuer_role is null then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  select * into v_collar
  from api.collars as collar
  where collar.device_public_id = p_device_public_id;
  v_existing_collar := found;

  if v_existing_collar then
    if v_collar.dog_id is distinct from v_claim.dog_id then
      raise exception using errcode = '23505', message = 'device_already_linked';
    end if;

    if v_issuer_role <> 'owner' then
      raise exception using errcode = '42501', message = 'not_authorized';
    end if;

    -- Do not let an active or retired identity be claimed again.
    if v_collar.state <> 'revoked' then
      raise exception using errcode = '23505', message = 'device_already_linked';
    end if;

    perform 1
    from private.device_credentials as credential
    where credential.collar_id = v_collar.id
    order by credential.credential_id
    for update;

    select * into v_collar
    from api.collars as collar
    where collar.id = v_collar.id
    for update;
    if not found or v_collar.state <> 'revoked' then
      raise exception using errcode = '23505', message = 'device_already_linked';
    end if;

    if exists (
      select 1
      from api.collars as active_collar
      where active_collar.dog_id = v_claim.dog_id
        and active_collar.state = 'active'
        and active_collar.id <> v_collar.id
    ) then
      raise exception using errcode = 'P0001', message = 'active_collar_exists';
    end if;

    -- Expired and rotating credentials are also tombstoned before the new
    -- credential is inserted. Already-revoked credentials and their receipts
    -- remain unchanged, so exact old revoke replays stay bounded and stable.
    update private.device_credentials as credential
    set state = 'revoked',
        revoked_at = coalesce(credential.revoked_at, v_now)
    where credential.collar_id = v_collar.id
      and credential.state <> 'revoked';

    update api.collars as collar
    set state = 'active',
        hardware_revision = left(p_device ->> 'hardware_revision', 64),
        firmware_version = left(p_device ->> 'firmware_version', 64),
        protocol_version = (p_device ->> 'protocol_version')::integer,
        telemetry_schema = (p_device ->> 'telemetry_schema')::integer,
        config_schema = (p_device ->> 'config_schema')::integer,
        capability_manifest = p_capabilities,
        capability_hash = private.base64url_decode(p_device ->> 'capability_hash'),
        linked_at = v_now,
        revoked_at = null,
        updated_at = v_now
    where collar.id = v_collar.id
    returning collar.id into v_collar_id;
  else
    if exists (
      select 1
      from api.collars as active_collar
      where active_collar.dog_id = v_claim.dog_id
        and active_collar.state = 'active'
    ) then
      raise exception using errcode = 'P0001', message = 'active_collar_exists';
    end if;

    insert into api.collars (
      device_public_id, dog_id, state, hardware_revision, firmware_version,
      protocol_version, telemetry_schema, config_schema, capability_manifest,
      capability_hash, linked_at
    ) values (
      p_device_public_id,
      v_claim.dog_id,
      'active',
      left(p_device ->> 'hardware_revision', 64),
      left(p_device ->> 'firmware_version', 64),
      (p_device ->> 'protocol_version')::integer,
      (p_device ->> 'telemetry_schema')::integer,
      (p_device ->> 'config_schema')::integer,
      p_capabilities,
      private.base64url_decode(p_device ->> 'capability_hash'),
      v_now
    ) returning id into v_collar_id;
  end if;

  -- The existing global unique digest and credential_id constraints also
  -- reject attempts to reuse any revoked credential material.
  insert into private.device_credentials (
    credential_id, collar_id, secret_digest
  ) values (
    p_credential_id, v_collar_id, p_secret_digest
  );

  v_response := jsonb_build_object(
    'collar_id', v_collar_id,
    'dog_id', v_claim.dog_id,
    'device_id', p_device_public_id,
    'disposition', 'claimed',
    'accepted_capability_hash', p_device ->> 'capability_hash',
    'server_time', to_char(v_now at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
  );

  update private.device_claims
  set state = 'consumed', consumed_by_device_id = p_device_public_id,
      consumed_at = v_now, request_id = p_request_id,
      request_sha256 = p_request_sha256, response_json = v_response
  where id = v_claim.id;

  return v_response;
end
$$;

-- A revoked credential may still submit its exact stored revoke receipt, or
-- one bounded tombstone receipt, but it never mutates a later enrollment.
create or replace function api.device_revoke_v1(
  p_credential_id uuid,
  p_secret_digest bytea,
  p_request_id uuid,
  p_request_sha256 bytea,
  p_device_id uuid,
  p_reason text
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
  v_response jsonb;
  v_disposition text;
  v_revoked_at timestamptz;
begin
  if p_secret_digest is null or octet_length(p_secret_digest) <> 32
     or p_request_id is null or p_request_sha256 is null or octet_length(p_request_sha256) <> 32
     or p_device_id is null or char_length(p_reason) not between 1 and 64 then
    raise exception using errcode = '22023', message = 'invalid_revoke_request';
  end if;

  select * into v_credential
  from private.device_credentials as credential
  where credential.credential_id = p_credential_id
  for update;
  if not found or not private.secure_digest_equal(v_credential.secret_digest, p_secret_digest) then
    raise exception using errcode = '28000', message = 'invalid_device_credential';
  end if;

  -- Keep the established credential -> collar lock order used by sync and
  -- website revoke; the collar is never taken before waiting on credentials.
  select * into v_collar
  from api.collars as collar
  where collar.id = v_credential.collar_id
    and collar.device_public_id = p_device_id
  for update;
  if not found then
    raise exception using errcode = '28000', message = 'device_identity_mismatch';
  end if;

  select * into v_receipt
  from private.sync_requests as receipt
  where receipt.collar_id = v_credential.collar_id
    and receipt.request_id = p_request_id;
  if found then
    if v_receipt.request_sha256 <> p_request_sha256 then
      raise exception using errcode = '23505', message = 'request_id_conflict';
    end if;
    return v_receipt.response_json;
  end if;

  if v_credential.state = 'revoked' then
    v_disposition := 'already_revoked';
    v_revoked_at := v_credential.revoked_at;
    -- This is a revoke-only tombstone receipt. In particular, do not update
    -- api.collars: its active state may belong to a newer credential.
  else
    v_disposition := 'newly_revoked';
    v_revoked_at := coalesce(v_credential.revoked_at, statement_timestamp());
    update private.device_credentials as credential
    set state = 'revoked', revoked_at = v_revoked_at
    where credential.credential_id = p_credential_id;
    update api.collars as collar
    set state = 'revoked',
        revoked_at = coalesce(collar.revoked_at, v_revoked_at),
        updated_at = statement_timestamp()
    where collar.id = v_collar.id;
  end if;

  v_response := jsonb_build_object(
    'protocol_version', 1,
    'request_id', p_request_id,
    'device_id', v_collar.device_public_id,
    'credential_id', p_credential_id,
    'state', 'revoked',
    'disposition', v_disposition,
    'revoked_at', v_revoked_at
  );
  insert into private.sync_requests (
    collar_id, request_id, request_sha256, protocol_version, status,
    response_json, committed_at
  ) values (
    v_credential.collar_id, p_request_id, p_request_sha256, 1, 'committed',
    v_response, statement_timestamp()
  );
  return v_response;
end
$$;

revoke execute on function api.consume_device_claim_v1(bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb)
  from public, anon, authenticated, service_role;
revoke execute on function api.device_revoke_v1(uuid, bytea, uuid, bytea, uuid, text)
  from public, anon, authenticated;
grant execute on function api.device_revoke_v1(uuid, bytea, uuid, bytea, uuid, text)
  to service_role;
