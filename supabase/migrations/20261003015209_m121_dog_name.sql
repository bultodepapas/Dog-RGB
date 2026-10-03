-- Owner-only display-name correction; timezone and ownership are not writable.
create or replace function api.rename_dog_v1(p_dog_id uuid, p_name text)
returns uuid language plpgsql security definer set search_path = '' as $$
declare
  v_user uuid := auth.uid();
  v_name text := pg_catalog.btrim(p_name, E' \t\n\u000b\f\r\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000\ufeff');
  v_id uuid;
begin
  if v_user is null or not exists (select 1 from auth.users where id = v_user and deleted_at is null) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  if v_name is null or char_length(v_name) not between 1 and 80 then
    raise exception using errcode = '22023', message = 'invalid_dog_profile';
  end if;
  update api.dogs d set name = v_name, updated_at = statement_timestamp()
  where d.id = p_dog_id and d.deleted_at is null and exists (
    select 1 from api.dog_memberships m where m.dog_id = d.id and m.user_id = v_user and m.role = 'owner'
  ) returning d.id into v_id;
  if v_id is null then raise exception using errcode = '42501', message = 'not_authorized'; end if;
  return v_id;
end $$;
revoke all on function api.rename_dog_v1(uuid, text) from public, anon, authenticated;
grant execute on function api.rename_dog_v1(uuid, text) to authenticated;
