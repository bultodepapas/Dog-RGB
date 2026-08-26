-- M1.11 database prerequisite: serialize authenticated web mutations on the
-- stable collar row before reading an optional resource head. A SELECT ... FOR
-- UPDATE against a missing head locks nothing, so two first writes could both
-- calculate server_version = 1. Locking the active collar closes that gap while
-- preserving the published RPC signature and response contract.
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
  v_authorized_collar_id uuid;
  v_head api.config_resource_heads%rowtype;
  v_head_found boolean := false;
  v_existing api.config_revisions%rowtype;
  v_revision_id uuid := extensions.gen_random_uuid();
  v_version bigint;
  v_now_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
  v_clock jsonb;
  v_accepted_ms bigint;
  v_accepted_logical bigint;
  v_canonical_body text;
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if p_mutation_id is null
     or p_base_server_version is null or p_base_server_version < 0
     or p_body is null or p_body_sha256 is null
     or octet_length(p_body_sha256) <> 32 or jsonb_typeof(p_body) <> 'object'
     or p_resource_key is null or p_resource_schema is null
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
    or (p_body ->> 'brightness') !~ '^(?:[1-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$'
  ) then
    raise exception using errcode = '22023', message = 'invalid_brightness';
  end if;
  if p_resource_key = 'brightness' then
    v_canonical_body := pg_catalog.format(
      '{"brightness":%s}',
      (p_body ->> 'brightness')::integer
    );
    if p_body_sha256 <> extensions.digest(
      pg_catalog.convert_to(v_canonical_body, 'UTF8'),
      'sha256'
    ) then
      raise exception using errcode = '22023', message = 'invalid_body_sha256';
    end if;
  end if;

  select c.id into v_authorized_collar_id
  from api.collars c
  join api.dog_memberships dm on dm.dog_id = c.dog_id
  where c.id = p_collar_id and dm.user_id = v_user_id
    and dm.role in ('owner', 'editor') and c.state = 'active'
  for update of c;
  if v_authorized_collar_id is null then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;

  select * into v_existing from api.config_revisions
  where collar_id = p_collar_id and mutation_id = p_mutation_id;
  if found then
    if v_existing.body_sha256 <> p_body_sha256
       or v_existing.body <> p_body
       or v_existing.resource_key <> p_resource_key
       or v_existing.resource_schema <> p_resource_schema
       or v_existing.base_server_version is distinct from p_base_server_version
       or v_existing.origin <> 'web'
       or v_existing.actor_user_id is distinct from v_user_id then
      raise exception using errcode = '23505', message = 'mutation_id_conflict';
    end if;
    return jsonb_build_object(
      'mutation_id', p_mutation_id,
      'disposition', case
        when v_existing.disposition = 'superseded'
          and v_existing.server_version is not null then 'unchanged'
        else v_existing.disposition
      end,
      'server_version', v_existing.server_version,
      'body_sha256', private.base64url_encode(v_existing.body_sha256)
    );
  end if;

  select * into v_head from api.config_resource_heads
  where collar_id = p_collar_id and resource_key = p_resource_key
  for update;
  v_head_found := found;
  if v_head_found and p_base_server_version is distinct from v_head.server_version then
    -- PT409 is PostgREST's explicit HTTP-conflict code. SQLSTATE 40001 is
    -- retryable infrastructure state and can be retried below this RPC even
    -- though stale product intent must never be retried automatically.
    raise exception using errcode = 'PT409', message = 'stale_base_server_version';
  end if;
  if not v_head_found and coalesce(p_base_server_version, 0) <> 0 then
    raise exception using errcode = 'PT409', message = 'stale_base_server_version';
  end if;

  -- Preserve a durable idempotency fingerprint for a same-value submission
  -- without manufacturing a new winner, HLC value, head version, or head
  -- timestamp. The existing superseded revision shape is the receipt store.
  if v_head_found
     and v_head.body = p_body
     and v_head.body_sha256 = p_body_sha256 then
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
      v_now_ms, 0, v_user_id, 'server_anchored',
      v_head.accepted_hlc_physical_ms, v_head.accepted_hlc_logical,
      v_head.accepted_actor_id, 'authored', v_head.server_version,
      p_body, p_body_sha256, 'superseded'
    );

    return jsonb_build_object(
      'mutation_id', p_mutation_id,
      'disposition', 'unchanged',
      'server_version', v_head.server_version,
      'body_sha256', private.base64url_encode(p_body_sha256)
    );
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
