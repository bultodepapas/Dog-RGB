-- A collar-wide HLC is required for deterministic ordering across independent
-- resources. Resource heads remain LWW registers; this state only mints and
-- merges server time without making unrelated resources conflict.
create table private.config_hlc_state (
  collar_id uuid primary key references api.collars(id) on delete cascade,
  physical_ms bigint not null check (physical_ms between 0 and 4102444800000),
  logical bigint not null check (logical between 0 and 4294967295),
  updated_at timestamptz not null default statement_timestamp()
);

insert into private.config_hlc_state (collar_id, physical_ms, logical)
select distinct on (r.collar_id)
  r.collar_id, r.accepted_hlc_physical_ms, r.accepted_hlc_logical
from api.config_revisions r
order by r.collar_id, r.accepted_hlc_physical_ms desc, r.accepted_hlc_logical desc;

revoke all on table private.config_hlc_state from public, anon, authenticated, service_role;

create or replace function private.advance_config_hlc_v1(
  p_collar_id uuid,
  p_now_ms bigint,
  p_received_physical_ms bigint default null,
  p_received_logical bigint default null
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_state private.config_hlc_state%rowtype;
  v_physical bigint;
  v_logical bigint;
begin
  if p_now_ms not between 0 and 4102444800000
     or (p_received_physical_ms is null) <> (p_received_logical is null)
     or (p_received_physical_ms is not null and (
       p_received_physical_ms not between 0 and 4102444800000
       or p_received_logical not between 0 and 4294967295
     )) then
    raise exception using errcode = '22023', message = 'invalid_hlc_input';
  end if;

  insert into private.config_hlc_state (collar_id, physical_ms, logical)
  values (p_collar_id, 0, 0)
  on conflict (collar_id) do nothing;

  select * into v_state
  from private.config_hlc_state
  where collar_id = p_collar_id
  for update;

  if p_received_physical_ms is null then
    v_physical := greatest(v_state.physical_ms, p_now_ms);
    v_logical := case
      when v_physical > v_state.physical_ms then 0
      else v_state.logical + 1
    end;
  else
    v_physical := greatest(v_state.physical_ms, p_received_physical_ms, p_now_ms);
    v_logical := case
      when v_physical = v_state.physical_ms and v_physical = p_received_physical_ms
        then greatest(v_state.logical, p_received_logical) + 1
      when v_physical = v_state.physical_ms then v_state.logical + 1
      when v_physical = p_received_physical_ms then p_received_logical + 1
      else 0
    end;
  end if;

  if v_logical > 4294967295 then
    raise exception using errcode = '22003', message = 'hlc_logical_overflow';
  end if;

  update private.config_hlc_state
  set physical_ms = v_physical,
      logical = v_logical,
      updated_at = statement_timestamp()
  where collar_id = p_collar_id;

  return jsonb_build_object('physical_ms', v_physical, 'logical', v_logical);
end
$$;

create or replace function private.normalize_device_mutations_v1(
  p_mutations jsonb,
  p_received_ms bigint
)
returns jsonb
language sql
immutable
strict
set search_path = ''
as $$
  select coalesce(jsonb_agg(
    mutation || jsonb_build_object(
      'submitted_time_quality', mutation ->> 'time_quality',
      'time_quality', case
        when mutation ->> 'time_quality' in ('server_anchored', 'sntp_synced', 'gnss_trusted')
         and abs((mutation #>> '{authored_hlc,physical_ms}')::bigint - p_received_ms) <= 600000
          then mutation ->> 'time_quality'
        else 'unknown'
      end
    ) order by ordinality
  ), '[]'::jsonb)
  from jsonb_array_elements(p_mutations) with ordinality as item(mutation, ordinality)
$$;

revoke execute on function private.advance_config_hlc_v1(uuid, bigint, bigint, bigint)
  from public, anon, authenticated, service_role;
revoke execute on function private.normalize_device_mutations_v1(jsonb, bigint)
  from public, anon, authenticated, service_role;

create or replace function private.apply_device_mutation_v1(
  p_collar_id uuid,
  p_device_id uuid,
  p_mutation jsonb,
  p_received_ms bigint
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_mutation_id uuid := (p_mutation ->> 'mutation_id')::uuid;
  v_resource_key text := p_mutation ->> 'resource_key';
  v_resource_schema integer := (p_mutation ->> 'resource_schema')::integer;
  v_body jsonb := p_mutation -> 'body';
  v_hash bytea := private.base64url_decode(p_mutation ->> 'body_sha256');
  v_origin text := p_mutation ->> 'origin';
  v_submitted_quality text := coalesce(
    p_mutation ->> 'submitted_time_quality',
    p_mutation ->> 'time_quality'
  );
  v_order_quality text := p_mutation ->> 'time_quality';
  v_submitted_ms bigint := (p_mutation #>> '{authored_hlc,physical_ms}')::bigint;
  v_submitted_logical bigint := (p_mutation #>> '{authored_hlc,logical}')::bigint;
  v_submitted_actor uuid := (p_mutation #>> '{authored_hlc,actor_id}')::uuid;
  v_accepted_ms bigint;
  v_accepted_logical bigint;
  v_accepted_actor uuid;
  v_clock jsonb;
  v_ordering text;
  v_head api.config_resource_heads%rowtype;
  v_head_found boolean := false;
  v_existing api.config_revisions%rowtype;
  v_revision_id uuid := extensions.gen_random_uuid();
  v_disposition text;
  v_rejection text;
  v_version bigint;
  v_wins boolean := false;
  v_server_actor constant uuid := '00000000-0000-4000-8000-0000000000ff'::uuid;
begin
  select * into v_existing
  from api.config_revisions
  where collar_id = p_collar_id and mutation_id = v_mutation_id;
  if found then
    if v_existing.body_sha256 <> v_hash
       or v_existing.resource_key <> v_resource_key
       or v_existing.resource_schema <> v_resource_schema
       or v_existing.actor_device_id is distinct from p_device_id then
      raise exception using errcode = '23505', message = 'mutation_id_conflict';
    end if;
    return jsonb_build_object(
      'mutation_id', v_existing.mutation_id,
      'resource_key', v_existing.resource_key,
      'disposition', v_existing.disposition,
      'replayed', true,
      'server_version', v_existing.server_version,
      'ordering', case when v_existing.disposition = 'rejected' then null else v_existing.ordering_mode end,
      'accepted_hlc', case when v_existing.disposition = 'rejected' then null else jsonb_build_object(
        'physical_ms', v_existing.accepted_hlc_physical_ms,
        'logical', v_existing.accepted_hlc_logical,
        'actor_id', v_existing.accepted_actor_id
      ) end,
      'error_code', v_existing.rejection_code
    );
  end if;

  if v_resource_key not in (
    'brightness', 'visual_mode', 'simple_effect', 'speed_profile', 'gps_quality', 'geofence_policy'
  ) or v_resource_schema <> 1 or v_origin <> 'ap'
     or v_submitted_quality not in (
       'unknown', 'approximate_persisted', 'server_anchored', 'sntp_synced', 'gnss_trusted'
     )
     or v_submitted_ms not between 0 and 4102444800000
     or v_submitted_logical not between 0 and 4294967295
     or v_submitted_actor <> p_device_id
     or jsonb_typeof(v_body) <> 'object'
     or octet_length(v_hash) <> 32 or pg_column_size(v_body) > 4096 then
    v_rejection := 'invalid_resource';
  elsif v_resource_key = 'brightness' and (
    (select count(*) from jsonb_object_keys(v_body)) <> 1
    or not (v_body ? 'brightness')
    or jsonb_typeof(v_body -> 'brightness') <> 'number'
    or (v_body ->> 'brightness')::integer not between 1 and 255
  ) then
    v_rejection := 'invalid_brightness';
  end if;

  if v_rejection is null then
    select * into v_head
    from api.config_resource_heads
    where collar_id = p_collar_id and resource_key = v_resource_key
    for update;
    v_head_found := found;

    if v_order_quality in ('server_anchored', 'sntp_synced', 'gnss_trusted') then
      v_accepted_ms := v_submitted_ms;
      v_accepted_logical := v_submitted_logical;
      v_accepted_actor := v_submitted_actor;
      v_ordering := 'authored';
      perform private.advance_config_hlc_v1(
        p_collar_id, p_received_ms, v_submitted_ms, v_submitted_logical
      );
    else
      v_clock := private.advance_config_hlc_v1(p_collar_id, p_received_ms);
      v_accepted_ms := (v_clock ->> 'physical_ms')::bigint;
      v_accepted_logical := (v_clock ->> 'logical')::bigint;
      v_accepted_actor := v_server_actor;
      v_ordering := 'fallback_received';
    end if;

    v_wins := not v_head_found or row(v_accepted_ms, v_accepted_logical, v_accepted_actor)
      > row(v_head.accepted_hlc_physical_ms, v_head.accepted_hlc_logical, v_head.accepted_actor_id);
  else
    -- Rejected rows retain bounded audit values, but do not advance the shared
    -- clock and expose no accepted_hlc in their outcome.
    v_accepted_ms := greatest(0, least(p_received_ms, 4102444800000));
    v_accepted_logical := 0;
    v_accepted_actor := v_server_actor;
    v_ordering := 'fallback_received';
  end if;

  v_disposition := case
    when v_rejection is not null then 'rejected'
    when v_wins then 'winning'
    else 'superseded'
  end;
  v_version := case when v_wins then coalesce(v_head.server_version, 0) + 1 else null end;

  insert into api.config_revisions (
    id, collar_id, resource_key, mutation_id, resource_schema,
    base_server_version, origin, actor_device_id,
    submitted_hlc_physical_ms, submitted_hlc_logical, submitted_actor_id,
    submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
    accepted_actor_id, ordering_mode, server_version, body, body_sha256,
    disposition, rejection_code
  ) values (
    v_revision_id, p_collar_id, v_resource_key, v_mutation_id, v_resource_schema,
    nullif(p_mutation ->> 'base_server_version', '')::bigint, v_origin, p_device_id,
    v_submitted_ms, v_submitted_logical, v_submitted_actor, v_submitted_quality,
    v_accepted_ms, v_accepted_logical, v_accepted_actor, v_ordering,
    v_version, v_body, v_hash, v_disposition, v_rejection
  );

  if v_wins then
    insert into api.config_resource_heads (
      collar_id, resource_key, resource_schema, server_version, body,
      body_sha256, winning_revision_id, accepted_hlc_physical_ms,
      accepted_hlc_logical, accepted_actor_id
    ) values (
      p_collar_id, v_resource_key, v_resource_schema, v_version, v_body,
      v_hash, v_revision_id, v_accepted_ms, v_accepted_logical, v_accepted_actor
    ) on conflict (collar_id, resource_key) do update
      set resource_schema = excluded.resource_schema,
          server_version = excluded.server_version,
          body = excluded.body,
          body_sha256 = excluded.body_sha256,
          winning_revision_id = excluded.winning_revision_id,
          accepted_hlc_physical_ms = excluded.accepted_hlc_physical_ms,
          accepted_hlc_logical = excluded.accepted_hlc_logical,
          accepted_actor_id = excluded.accepted_actor_id,
          updated_at = statement_timestamp();
  end if;

  return jsonb_build_object(
    'mutation_id', v_mutation_id,
    'resource_key', v_resource_key,
    'disposition', v_disposition,
    'replayed', false,
    'server_version', v_version,
    'ordering', case when v_rejection is not null then null else v_ordering end,
    'accepted_hlc', case when v_rejection is not null then null else jsonb_build_object(
      'physical_ms', v_accepted_ms,
      'logical', v_accepted_logical,
      'actor_id', v_accepted_actor
    ) end,
    'error_code', v_rejection
  );
end
$$;

revoke execute on function private.apply_device_mutation_v1(uuid, uuid, jsonb, bigint)
  from public, anon, authenticated, service_role;

create or replace function api.mutate_config_resource_v1(
  p_collar_id uuid,
  p_resource_key text,
  p_resource_schema integer,
  p_mutation_id uuid,
  p_base_server_version bigint,
  p_body jsonb,
  p_body_sha256 bytea
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_head api.config_resource_heads%rowtype;
  v_head_found boolean := false;
  v_existing api.config_revisions%rowtype;
  v_revision_id uuid := extensions.gen_random_uuid();
  v_version bigint;
  v_now_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
  v_clock jsonb;
  v_accepted_ms bigint;
  v_accepted_logical bigint;
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if octet_length(p_body_sha256) <> 32 or jsonb_typeof(p_body) <> 'object'
     or p_resource_schema <> 1 or pg_column_size(p_body) > 4096
     or p_resource_key not in (
       'brightness', 'visual_mode', 'simple_effect', 'speed_profile', 'gps_quality', 'geofence_policy'
     ) then
    raise exception using errcode = '22023', message = 'invalid_config_resource';
  end if;
  if p_resource_key = 'brightness' and (
    (select count(*) from jsonb_object_keys(p_body)) <> 1
    or not (p_body ? 'brightness')
    or jsonb_typeof(p_body -> 'brightness') <> 'number'
    or (p_body ->> 'brightness')::integer not between 1 and 255
  ) then
    raise exception using errcode = '22023', message = 'invalid_brightness';
  end if;
  if not exists (
    select 1 from api.collars c
    join api.dog_memberships dm on dm.dog_id = c.dog_id
    where c.id = p_collar_id and dm.user_id = v_user_id
      and dm.role in ('owner', 'editor') and c.state = 'active'
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  select * into v_existing from api.config_revisions
  where collar_id = p_collar_id and mutation_id = p_mutation_id;
  if found then
    if v_existing.body_sha256 <> p_body_sha256
       or v_existing.resource_key <> p_resource_key
       or v_existing.resource_schema <> p_resource_schema
       or v_existing.origin <> 'web'
       or v_existing.actor_user_id is distinct from v_user_id then
      raise exception using errcode = '23505', message = 'mutation_id_conflict';
    end if;
    return jsonb_build_object(
      'mutation_id', p_mutation_id,
      'disposition', v_existing.disposition,
      'server_version', v_existing.server_version,
      'body_sha256', private.base64url_encode(v_existing.body_sha256)
    );
  end if;

  select * into v_head from api.config_resource_heads
  where collar_id = p_collar_id and resource_key = p_resource_key
  for update;
  v_head_found := found;
  if v_head_found and p_base_server_version is distinct from v_head.server_version then
    raise exception using errcode = '40001', message = 'stale_base_server_version';
  end if;
  if not v_head_found and coalesce(p_base_server_version, 0) <> 0 then
    raise exception using errcode = '40001', message = 'stale_base_server_version';
  end if;

  v_clock := private.advance_config_hlc_v1(p_collar_id, v_now_ms);
  v_accepted_ms := (v_clock ->> 'physical_ms')::bigint;
  v_accepted_logical := (v_clock ->> 'logical')::bigint;
  v_version := coalesce(v_head.server_version, 0) + 1;

  insert into api.config_revisions (
    id, collar_id, resource_key, mutation_id, resource_schema,
    base_server_version, origin, actor_user_id,
    submitted_hlc_physical_ms, submitted_hlc_logical, submitted_actor_id,
    submitted_time_quality, accepted_hlc_physical_ms, accepted_hlc_logical,
    accepted_actor_id, ordering_mode, server_version, body, body_sha256,
    disposition
  ) values (
    v_revision_id, p_collar_id, p_resource_key, p_mutation_id, p_resource_schema,
    p_base_server_version, 'web', v_user_id,
    v_accepted_ms, v_accepted_logical, v_user_id, 'server_anchored',
    v_accepted_ms, v_accepted_logical, v_user_id,
    'authored', v_version, p_body, p_body_sha256, 'winning'
  );

  insert into api.config_resource_heads (
    collar_id, resource_key, resource_schema, server_version, body,
    body_sha256, winning_revision_id, accepted_hlc_physical_ms,
    accepted_hlc_logical, accepted_actor_id
  ) values (
    p_collar_id, p_resource_key, p_resource_schema, v_version, p_body,
    p_body_sha256, v_revision_id, v_accepted_ms, v_accepted_logical, v_user_id
  ) on conflict (collar_id, resource_key) do update
    set resource_schema = excluded.resource_schema,
        server_version = excluded.server_version,
        body = excluded.body,
        body_sha256 = excluded.body_sha256,
        winning_revision_id = excluded.winning_revision_id,
        accepted_hlc_physical_ms = excluded.accepted_hlc_physical_ms,
        accepted_hlc_logical = excluded.accepted_hlc_logical,
        accepted_actor_id = excluded.accepted_actor_id,
        updated_at = statement_timestamp();

  return jsonb_build_object(
    'mutation_id', p_mutation_id,
    'disposition', 'winning',
    'server_version', v_version,
    'body_sha256', private.base64url_encode(p_body_sha256)
  );
end
$$;

revoke execute on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  from public, anon;
grant execute on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  to authenticated;

-- Preserve the public gateway while making the database's trust-window decision
-- explicit in the payload used by the legacy transaction's ordering loop.
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
  v_received_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
  v_original_markers jsonb := coalesce(p_request #> '{upload,loss_markers}', '[]'::jsonb);
  v_original_mutations jsonb := coalesce(p_request #> '{configuration,mutations}', '[]'::jsonb);
  v_normalized_markers jsonb;
  v_normalized_mutations jsonb;
  v_normalized_request jsonb;
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

  return api.device_sync_v1(
    p_credential_id,
    p_secret_digest,
    p_request_id,
    p_request_sha256,
    v_normalized_request
  );
end
$$;

revoke execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  from public, anon, authenticated;
grant execute on function api.device_sync_gateway_v1(uuid, bytea, uuid, bytea, jsonb)
  to service_role;
