-- M1.12 keeps one bounded, latest-only diagnostic snapshot on the existing
-- RLS-protected collar row. It intentionally stores neither the raw request nor
-- the device's machine error code.
alter table api.collars
  add column diagnostics_observed_at timestamptz,
  add column outbox_chunks bigint,
  add column outbox_points bigint,
  add column outbox_used_bytes bigint,
  add column outbox_capacity_bytes bigint,
  add column oldest_unacknowledged_at timestamptz,
  add column dropped_points_total bigint,
  add column sync_error_present boolean,
  add constraint collars_diagnostics_uint32_check check (
    (outbox_chunks is null or outbox_chunks between 0 and 4294967295)
    and (outbox_points is null or outbox_points between 0 and 4294967295)
    and (outbox_used_bytes is null or outbox_used_bytes between 0 and 4294967295)
    and (outbox_capacity_bytes is null or outbox_capacity_bytes between 0 and 4294967295)
    and (dropped_points_total is null or dropped_points_total between 0 and 4294967295)
  ),
  add constraint collars_diagnostics_capacity_check check (
    outbox_used_bytes is null
    or outbox_capacity_bytes is null
    or outbox_used_bytes <= outbox_capacity_bytes
  ),
  add constraint collars_diagnostics_snapshot_check check (
    (
      diagnostics_observed_at is null
      and outbox_chunks is null
      and outbox_points is null
      and outbox_used_bytes is null
      and outbox_capacity_bytes is null
      and oldest_unacknowledged_at is null
      and dropped_points_total is null
      and sync_error_present is null
    )
    or (
      diagnostics_observed_at is not null
      and outbox_chunks is not null
      and outbox_points is not null
      and outbox_used_bytes is not null
      and outbox_capacity_bytes is not null
      and dropped_points_total is not null
      and sync_error_present is not null
      and diagnostics_observed_at <= last_sync_at
    )
  );

-- Every pre-M1.12 pairing crossed the device-v1 claim endpoint and stored a
-- validated manifest declaring protocol 1. Repair only those provable rows.
update api.collars
set protocol_version = 1,
    updated_at = statement_timestamp()
where protocol_version is null
  and capability_manifest is not null
  and coalesce(capability_manifest -> 'protocol_versions', '[]'::jsonb) @> '[1]'::jsonb;

-- The outer gateway owns capability negotiation and the safe diagnostic
-- snapshot. The inner transaction remains the one telemetry/configuration
-- authority. Exact receipt replay returns before either snapshot can advance.
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
  v_collar api.collars%rowtype;
  v_received_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
  v_original_markers jsonb := coalesce(p_request #> '{upload,loss_markers}', '[]'::jsonb);
  v_original_mutations jsonb := coalesce(p_request #> '{configuration,mutations}', '[]'::jsonb);
  v_normalized_markers jsonb;
  v_normalized_mutations jsonb;
  v_normalized_request jsonb;
  v_capabilities jsonb := p_request -> 'capabilities';
  v_capability_hash bytea;
  v_diagnostics jsonb := p_request -> 'diagnostics';
  v_outbox_chunks bigint;
  v_outbox_points bigint;
  v_outbox_used_bytes bigint;
  v_outbox_capacity_bytes bigint;
  v_oldest_unacknowledged_ms bigint;
  v_dropped_points_total bigint;
  v_response jsonb;
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

  select * into v_collar
  from api.collars
  where id = v_credential.collar_id
  for update;
  if not found then
    raise exception using errcode = '28000', message = 'device_identity_mismatch';
  end if;

  perform 1
  from private.sync_requests
  where collar_id = v_collar.id and request_id = p_request_id;
  if found then
    return api.device_sync_v1(
      p_credential_id,
      p_secret_digest,
      p_request_id,
      p_request_sha256,
      p_request
    );
  end if;

  begin
    v_capability_hash := private.base64url_decode(p_request #>> '{device,capability_hash}');
  exception when others then
    raise exception using errcode = '22023', message = 'invalid_capabilities';
  end;
  if octet_length(v_capability_hash) <> 32 then
    raise exception using errcode = '22023', message = 'invalid_capabilities';
  end if;

  if v_capabilities is null or v_capabilities = 'null'::jsonb then
    if v_collar.capability_hash is null
       or not private.secure_digest_equal(v_collar.capability_hash, v_capability_hash) then
      raise exception using errcode = '22023', message = 'capability_hash_mismatch';
    end if;
  else
    if jsonb_typeof(v_capabilities) <> 'object'
       or v_capabilities #>> '{hardware_revision}' is distinct from p_request #>> '{device,hardware_revision}'
       or not coalesce(v_capabilities -> 'protocol_versions', '[]'::jsonb) @> '[1]'::jsonb
       or not coalesce(v_capabilities #> '{telemetry,schemas}', '[]'::jsonb) @> '[3]'::jsonb
       or not coalesce(v_capabilities -> 'config_schemas', '[]'::jsonb) @> '[7]'::jsonb then
      raise exception using errcode = '22023', message = 'invalid_capabilities';
    end if;
    update api.collars
    set capability_manifest = v_capabilities,
        capability_hash = v_capability_hash,
        updated_at = statement_timestamp()
    where id = v_collar.id;
  end if;

  if jsonb_typeof(v_diagnostics) <> 'object'
     or jsonb_typeof(v_diagnostics -> 'outbox_chunks') <> 'number'
     or jsonb_typeof(v_diagnostics -> 'outbox_points') <> 'number'
     or jsonb_typeof(v_diagnostics -> 'outbox_used_bytes') <> 'number'
     or jsonb_typeof(v_diagnostics -> 'outbox_capacity_bytes') <> 'number'
     or jsonb_typeof(v_diagnostics -> 'dropped_points_total') <> 'number'
     or coalesce(v_diagnostics ->> 'outbox_chunks', '') !~ '^[0-9]+$'
     or coalesce(v_diagnostics ->> 'outbox_points', '') !~ '^[0-9]+$'
     or coalesce(v_diagnostics ->> 'outbox_used_bytes', '') !~ '^[0-9]+$'
     or coalesce(v_diagnostics ->> 'outbox_capacity_bytes', '') !~ '^[0-9]+$'
     or coalesce(v_diagnostics ->> 'dropped_points_total', '') !~ '^[0-9]+$'
     or not (v_diagnostics ? 'oldest_unacknowledged_utc_ms')
     or not (v_diagnostics ? 'last_error_code') then
    raise exception using errcode = '22023', message = 'invalid_diagnostics';
  end if;

  v_outbox_chunks := (v_diagnostics ->> 'outbox_chunks')::bigint;
  v_outbox_points := (v_diagnostics ->> 'outbox_points')::bigint;
  v_outbox_used_bytes := (v_diagnostics ->> 'outbox_used_bytes')::bigint;
  v_outbox_capacity_bytes := (v_diagnostics ->> 'outbox_capacity_bytes')::bigint;
  v_dropped_points_total := (v_diagnostics ->> 'dropped_points_total')::bigint;
  if v_outbox_chunks not between 0 and 4294967295
     or v_outbox_points not between 0 and 4294967295
     or v_outbox_used_bytes not between 0 and 4294967295
     or v_outbox_capacity_bytes not between 0 and 4294967295
     or v_dropped_points_total not between 0 and 4294967295
     or v_outbox_used_bytes > v_outbox_capacity_bytes then
    raise exception using errcode = '22023', message = 'invalid_diagnostics';
  end if;

  if v_diagnostics -> 'oldest_unacknowledged_utc_ms' <> 'null'::jsonb then
    if jsonb_typeof(v_diagnostics -> 'oldest_unacknowledged_utc_ms') <> 'number'
       or coalesce(v_diagnostics ->> 'oldest_unacknowledged_utc_ms', '') !~ '^[0-9]+$' then
      raise exception using errcode = '22023', message = 'invalid_diagnostics';
    end if;
    v_oldest_unacknowledged_ms := (v_diagnostics ->> 'oldest_unacknowledged_utc_ms')::bigint;
    if v_oldest_unacknowledged_ms not between 0 and 4102444800000 then
      raise exception using errcode = '22023', message = 'invalid_diagnostics';
    end if;
  end if;

  if v_diagnostics -> 'last_error_code' <> 'null'::jsonb
     and (
       jsonb_typeof(v_diagnostics -> 'last_error_code') <> 'string'
       or coalesce(v_diagnostics ->> 'last_error_code', '') !~ '^[a-z][a-z0-9_]{0,63}$'
     ) then
    raise exception using errcode = '22023', message = 'invalid_diagnostics';
  end if;

  select coalesce(jsonb_agg(
    (marker - 'marker_id' - 'lost_points') || jsonb_build_object(
      'loss_id', marker -> 'marker_id',
      'dropped_points', marker -> 'lost_points'
    ) order by ordinality
  ), '[]'::jsonb)
  into v_normalized_markers
  from jsonb_array_elements(v_original_markers) with ordinality as item(marker, ordinality);

  v_normalized_mutations := private.normalize_device_mutations_v1(
    v_original_mutations,
    v_received_ms
  );
  perform pg_catalog.set_config('dog_rgb.loss_markers', v_original_markers::text, true);
  v_normalized_request := jsonb_set(
    jsonb_set(p_request, '{upload,loss_markers}', v_normalized_markers, false),
    '{configuration,mutations}',
    v_normalized_mutations,
    false
  );

  v_response := api.device_sync_v1(
    p_credential_id,
    p_secret_digest,
    p_request_id,
    p_request_sha256,
    v_normalized_request
  );

  update api.collars
  set protocol_version = (p_request ->> 'protocol_version')::integer,
      diagnostics_observed_at = last_sync_at,
      outbox_chunks = v_outbox_chunks,
      outbox_points = v_outbox_points,
      outbox_used_bytes = v_outbox_used_bytes,
      outbox_capacity_bytes = v_outbox_capacity_bytes,
      oldest_unacknowledged_at = case
        when v_oldest_unacknowledged_ms is null then null
        else to_timestamp(v_oldest_unacknowledged_ms::double precision / 1000.0)
      end,
      dropped_points_total = v_dropped_points_total,
      sync_error_present = v_diagnostics -> 'last_error_code' <> 'null'::jsonb,
      updated_at = statement_timestamp()
  where id = v_collar.id;

  return v_response;
end
$$;

revoke execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated;
grant execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  to service_role;

-- Website revocation is an owner/admin action. Lock credentials before the
-- collar, matching device sync/revoke and eliminating the inverse lock race.
create or replace function api.revoke_collar_v1(p_collar_id uuid)
returns boolean
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_state text;
begin
  if v_user_id is null or not exists (
    select 1
    from api.collars c
    join api.dog_memberships dm on dm.dog_id = c.dog_id
    where c.id = p_collar_id
      and dm.user_id = v_user_id
      and dm.role = 'owner'
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  perform 1
  from private.device_credentials credential
  where credential.collar_id = p_collar_id
  order by credential.credential_id
  for update;

  select c.state into v_state
  from api.collars c
  where c.id = p_collar_id
  for update;
  if not found or not exists (
    select 1
    from api.dog_memberships dm
    join api.collars c on c.dog_id = dm.dog_id
    where c.id = p_collar_id
      and dm.user_id = v_user_id
      and dm.role = 'owner'
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;
  if v_state not in ('active', 'revoked') then
    return false;
  end if;

  update private.device_credentials
  set state = 'revoked',
      revoked_at = coalesce(revoked_at, statement_timestamp())
  where collar_id = p_collar_id and state <> 'revoked';

  update api.collars
  set state = 'revoked',
      revoked_at = coalesce(revoked_at, statement_timestamp()),
      updated_at = statement_timestamp()
  where id = p_collar_id and state in ('active', 'revoked');

  return found;
end
$$;

revoke execute on function api.revoke_collar_v1(uuid)
  from public, anon, service_role;
grant execute on function api.revoke_collar_v1(uuid)
  to authenticated;
