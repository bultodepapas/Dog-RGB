create or replace function private.member_role(p_dog_id uuid)
returns text
language sql
stable
security definer
set search_path = ''
as $$
  select dm.role
  from api.dog_memberships as dm
  where dm.dog_id = p_dog_id
    and dm.user_id = (select auth.uid())
$$;

create or replace function private.can_read_dog(p_dog_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select coalesce((select private.member_role(p_dog_id) in ('owner', 'editor', 'viewer')), false)
$$;

create or replace function private.can_write_dog(p_dog_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select coalesce((select private.member_role(p_dog_id) in ('owner', 'editor')), false)
$$;

create or replace function private.can_admin_dog(p_dog_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select coalesce((select private.member_role(p_dog_id) = 'owner'), false)
$$;

revoke execute on function private.member_role(uuid) from public, anon;
revoke execute on function private.can_read_dog(uuid) from public, anon;
revoke execute on function private.can_write_dog(uuid) from public, anon;
revoke execute on function private.can_admin_dog(uuid) from public, anon;
grant execute on function private.member_role(uuid) to authenticated;
grant execute on function private.can_read_dog(uuid) to authenticated;
grant execute on function private.can_write_dog(uuid) to authenticated;
grant execute on function private.can_admin_dog(uuid) to authenticated;

create policy profiles_select_self on api.profiles
  for select to authenticated
  using (user_id = (select auth.uid()));
create policy profiles_update_self on api.profiles
  for update to authenticated
  using (user_id = (select auth.uid()))
  with check (user_id = (select auth.uid()));

create policy dogs_select_member on api.dogs
  for select to authenticated
  using ((select private.can_read_dog(id)));
create policy dogs_update_owner on api.dogs
  for update to authenticated
  using ((select private.can_admin_dog(id)))
  with check ((select private.can_admin_dog(id)));
create policy dogs_delete_owner on api.dogs
  for delete to authenticated
  using ((select private.can_admin_dog(id)));

create policy memberships_select_member on api.dog_memberships
  for select to authenticated
  using ((select private.can_read_dog(dog_id)));

create policy collars_select_member on api.collars
  for select to authenticated
  using ((select private.can_read_dog(dog_id)));
create policy recordings_select_member on api.recordings
  for select to authenticated
  using ((select private.can_read_dog((select c.dog_id from api.collars c where c.id = collar_id))));
create policy telemetry_points_select_member on api.telemetry_points
  for select to authenticated
  using ((select private.can_read_dog((select c.dog_id from api.collars c where c.id = collar_id))));
create policy daily_summaries_select_member on api.daily_summaries
  for select to authenticated
  using ((select private.can_read_dog(dog_id)));
create policy recording_summaries_select_member on api.recording_summaries
  for select to authenticated
  using ((select private.can_read_dog((
    select c.dog_id
    from api.recordings r
    join api.collars c on c.id = r.collar_id
    where r.id = recording_id
  ))));
create policy config_revisions_select_member on api.config_revisions
  for select to authenticated
  using ((select private.can_read_dog((select c.dog_id from api.collars c where c.id = collar_id))));
create policy config_heads_select_member on api.config_resource_heads
  for select to authenticated
  using ((select private.can_read_dog((select c.dog_id from api.collars c where c.id = collar_id))));
create policy config_reported_select_member on api.config_reported
  for select to authenticated
  using ((select private.can_read_dog((select c.dog_id from api.collars c where c.id = collar_id))));

create or replace function private.handle_new_auth_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into api.profiles (user_id, display_name)
  values (
    new.id,
    nullif(left(coalesce(new.raw_user_meta_data ->> 'display_name', ''), 80), '')
  )
  on conflict (user_id) do nothing;
  return new;
end
$$;

revoke execute on function private.handle_new_auth_user() from public, anon, authenticated, service_role;
create trigger dog_rgb_auth_user_profile
after insert on auth.users
for each row execute function private.handle_new_auth_user();

create or replace function private.base64url_decode(p_value text)
returns bytea
language sql
immutable
strict
set search_path = ''
as $$
  select decode(
    translate(p_value, '-_', '+/') || repeat('=', (4 - length(p_value) % 4) % 4),
    'base64'
  )
$$;
revoke execute on function private.base64url_decode(text) from public, anon, authenticated;

create or replace function private.base64url_encode(p_value bytea)
returns text
language sql
immutable
strict
set search_path = ''
as $$
  select rtrim(translate(encode(p_value, 'base64'), '+/', '-_'), '=')
$$;
revoke execute on function private.base64url_encode(bytea) from public, anon, authenticated;

-- PostgreSQL bytea equality is not documented as constant-time. Device
-- credential digests are fixed at 32 bytes, so compare all 32 octets before
-- deciding. Length rejection is safe because both persisted and accepted
-- credential digests have a public, fixed length.
create or replace function private.secure_digest_equal(p_left bytea, p_right bytea)
returns boolean
language plpgsql
immutable
strict
set search_path = ''
as $$
declare
  v_difference integer := 0;
begin
  if octet_length(p_left) <> 32 or octet_length(p_right) <> 32 then
    return false;
  end if;
  for v_index in 0..31 loop
    v_difference := v_difference | (get_byte(p_left, v_index) # get_byte(p_right, v_index));
  end loop;
  return v_difference = 0;
end
$$;
revoke execute on function private.secure_digest_equal(bytea, bytea)
  from public, anon, authenticated;

create or replace function api.create_dog_v1(
  p_name text,
  p_timezone text default 'America/Bogota'
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_user_id uuid := auth.uid();
  v_dog_id uuid;
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if char_length(btrim(p_name)) not between 1 and 80
     or not exists (select 1 from pg_catalog.pg_timezone_names where name = p_timezone) then
    raise exception using errcode = '22023', message = 'invalid_dog_profile';
  end if;
  insert into api.dogs (name, timezone, created_by)
  values (btrim(p_name), p_timezone, v_user_id)
  returning id into v_dog_id;
  insert into api.dog_memberships (dog_id, user_id, role)
  values (v_dog_id, v_user_id, 'owner');
  return v_dog_id;
end
$$;
revoke execute on function api.create_dog_v1(text, text) from public, anon;
grant execute on function api.create_dog_v1(text, text) to authenticated;

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
declare v_claim_id uuid;
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
  update private.device_claims
  set state = 'cancelled'
  where dog_id = p_dog_id and state = 'issued';
  insert into private.device_claims (
    dog_id, requested_by, code_digest, expires_at, max_attempts
  ) values (
    p_dog_id, p_requested_by, p_code_digest, p_expires_at, p_max_attempts
  ) returning id into v_claim_id;
  return v_claim_id;
end
$$;

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
  v_collar_id uuid;
  v_response jsonb;
begin
  if octet_length(p_code_digest) <> 32 or octet_length(p_request_sha256) <> 32
     or octet_length(p_secret_digest) <> 32
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

  select id into v_collar_id from api.collars
  where device_public_id = p_device_public_id
  for update;
  if found then
    raise exception using errcode = '23505', message = 'device_already_linked';
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
    statement_timestamp()
  ) returning id into v_collar_id;

  insert into private.device_credentials (
    credential_id, collar_id, secret_digest
  ) values (p_credential_id, v_collar_id, p_secret_digest);

  v_response := jsonb_build_object(
    'collar_id', v_collar_id,
    'dog_id', v_claim.dog_id,
    'device_id', p_device_public_id,
    'disposition', 'claimed',
    'accepted_capability_hash', p_device ->> 'capability_hash',
    'server_time', to_char(statement_timestamp() at time zone 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
  );

  update private.device_claims
  set state = 'consumed', consumed_by_device_id = p_device_public_id,
      consumed_at = statement_timestamp(), request_id = p_request_id,
      request_sha256 = p_request_sha256, response_json = v_response
  where id = v_claim.id;

  return v_response;
end
$$;

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
  v_existing api.config_revisions%rowtype;
  v_revision_id uuid := extensions.gen_random_uuid();
  v_version bigint;
  v_now_ms bigint := floor(extract(epoch from clock_timestamp()) * 1000);
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if octet_length(p_body_sha256) <> 32 or jsonb_typeof(p_body) <> 'object'
     or p_resource_schema <= 0 or pg_column_size(p_body) > 4096 then
    raise exception using errcode = '22023', message = 'invalid_config_resource';
  end if;
  if p_resource_key = 'brightness' and (
    (select count(*) from jsonb_object_keys(p_body)) <> 1
    or not (p_body ? 'brightness')
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
    if v_existing.body_sha256 <> p_body_sha256 then
      raise exception using errcode = '23505', message = 'mutation_id_conflict';
    end if;
    return jsonb_build_object(
      'mutation_id', p_mutation_id,
      'disposition', v_existing.disposition,
      'server_version', v_existing.server_version
    );
  end if;

  select * into v_head from api.config_resource_heads
  where collar_id = p_collar_id and resource_key = p_resource_key
  for update;
  if found and p_base_server_version is distinct from v_head.server_version then
    raise exception using errcode = '40001', message = 'stale_base_server_version';
  end if;
  if not found and coalesce(p_base_server_version, 0) <> 0 then
    raise exception using errcode = '40001', message = 'stale_base_server_version';
  end if;
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
    v_now_ms, 0, v_user_id, 'server_anchored', v_now_ms, 0, v_user_id,
    'authored', v_version, p_body, p_body_sha256, 'winning'
  );

  insert into api.config_resource_heads (
    collar_id, resource_key, resource_schema, server_version, body,
    body_sha256, winning_revision_id, accepted_hlc_physical_ms,
    accepted_hlc_logical, accepted_actor_id
  ) values (
    p_collar_id, p_resource_key, p_resource_schema, v_version, p_body,
    p_body_sha256, v_revision_id, v_now_ms, 0, v_user_id
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

create or replace function api.revoke_collar_v1(p_collar_id uuid)
returns boolean
language plpgsql
security definer
set search_path = ''
as $$
declare v_user_id uuid := auth.uid();
begin
  if v_user_id is null or not exists (
    select 1 from api.collars c
    join api.dog_memberships dm on dm.dog_id = c.dog_id
    where c.id = p_collar_id and dm.user_id = v_user_id
      and dm.role in ('owner', 'editor')
  ) then
    raise exception using errcode = '42501', message = 'not_authorized';
  end if;
  perform 1 from api.collars where id = p_collar_id for update;
  update private.device_credentials
  set state = 'revoked', revoked_at = statement_timestamp()
  where collar_id = p_collar_id and state in ('active', 'rotating');
  update api.collars
  set state = 'revoked', revoked_at = statement_timestamp(), updated_at = statement_timestamp()
  where id = p_collar_id and state <> 'revoked';
  return found;
end
$$;

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
  v_receipt private.sync_requests%rowtype;
  v_response jsonb;
  v_disposition text;
begin
  if octet_length(p_secret_digest) <> 32 or octet_length(p_request_sha256) <> 32
     or char_length(p_reason) not between 1 and 64 then
    raise exception using errcode = '22023', message = 'invalid_revoke_request';
  end if;
  select * into v_credential from private.device_credentials
  where credential_id = p_credential_id
  for update;
  if not found or not private.secure_digest_equal(v_credential.secret_digest, p_secret_digest) then
    raise exception using errcode = '28000', message = 'invalid_device_credential';
  end if;
  if not exists (
    select 1 from api.collars c
    where c.id = v_credential.collar_id and c.device_public_id = p_device_id
  ) then
    raise exception using errcode = '28000', message = 'device_identity_mismatch';
  end if;
  select * into v_receipt from private.sync_requests
  where collar_id = v_credential.collar_id and request_id = p_request_id;
  if found then
    if v_receipt.request_sha256 <> p_request_sha256 then
      raise exception using errcode = '23505', message = 'request_id_conflict';
    end if;
    return v_receipt.response_json;
  end if;
  v_disposition := case when v_credential.state = 'revoked' then 'already_revoked' else 'newly_revoked' end;
  update private.device_credentials set state = 'revoked', revoked_at = coalesce(revoked_at, statement_timestamp())
  where credential_id = p_credential_id;
  update api.collars set state = 'revoked', revoked_at = coalesce(revoked_at, statement_timestamp()),
    updated_at = statement_timestamp()
  where id = v_credential.collar_id;
  v_response := jsonb_build_object(
    'protocol_version', 1,
    'request_id', p_request_id,
    'device_id', (select device_public_id from api.collars where id = v_credential.collar_id),
    'credential_id', p_credential_id,
    'state', 'revoked',
    'disposition', v_disposition,
    'revoked_at', (select c.revoked_at from api.collars c where c.id = v_credential.collar_id)
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

create or replace function private.recompute_dirty_summaries_v1(p_limit integer default 16)
returns integer
language plpgsql
security definer
set search_path = ''
as $$
declare v_count integer;
begin
  if p_limit not between 1 and 100 then
    raise exception using errcode = '22023', message = 'invalid_batch_limit';
  end if;
  with claimed as (
    select dog_id, local_date, timezone
    from private.dirty_summary_days
    order by first_marked_at, dog_id
    for update skip locked
    limit p_limit
  ), deleted as (
    delete from private.dirty_summary_days d
    using claimed c
    where (d.dog_id, d.local_date, d.timezone) = (c.dog_id, c.local_date, c.timezone)
    returning d.dog_id
  )
  select count(*) into v_count from deleted;
  return v_count;
end
$$;

revoke execute on function api.issue_device_claim_v1(uuid, uuid, bytea, timestamptz, integer) from public, anon, authenticated;
revoke execute on function api.consume_device_claim_v1(bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb) from public, anon, authenticated;
revoke execute on function api.device_revoke_v1(uuid, bytea, uuid, bytea, uuid, text) from public, anon, authenticated;
revoke execute on function private.recompute_dirty_summaries_v1(integer) from public, anon, authenticated;
grant execute on function api.issue_device_claim_v1(uuid, uuid, bytea, timestamptz, integer) to service_role;
grant execute on function api.consume_device_claim_v1(bytea, uuid, bytea, uuid, uuid, bytea, jsonb, jsonb) to service_role;
grant execute on function api.device_revoke_v1(uuid, bytea, uuid, bytea, uuid, text) to service_role;
grant execute on function private.recompute_dirty_summaries_v1(integer) to service_role;

revoke execute on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  from public, anon;
grant execute on function api.mutate_config_resource_v1(uuid, text, integer, uuid, bigint, jsonb, bytea)
  to authenticated;
revoke execute on function api.revoke_collar_v1(uuid) from public, anon;
grant execute on function api.revoke_collar_v1(uuid) to authenticated;
