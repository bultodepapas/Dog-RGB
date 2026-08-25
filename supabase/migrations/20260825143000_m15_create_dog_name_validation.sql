-- M1.5 keeps dog creation behind one transactional RPC. Align its name
-- normalization with JavaScript String.trim() so direct authenticated calls
-- cannot create a profile whose name contains only Unicode whitespace.
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
  v_name text := pg_catalog.btrim(
    p_name,
    E' \t\n\u000b\f\r\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000\ufeff'
  );
  v_dog_id uuid;
begin
  if v_user_id is null then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;

  if v_name is null
     or pg_catalog.char_length(v_name) not between 1 and 80
     or not exists (
       select 1
       from pg_catalog.pg_timezone_names
       where name = p_timezone
     ) then
    raise exception using errcode = '22023', message = 'invalid_dog_profile';
  end if;

  insert into api.dogs (name, timezone, created_by)
  values (v_name, p_timezone, v_user_id)
  returning id into v_dog_id;

  insert into api.dog_memberships (dog_id, user_id, role)
  values (v_dog_id, v_user_id, 'owner');

  return v_dog_id;
end
$$;

revoke execute on function api.create_dog_v1(text, text) from public, anon;
grant execute on function api.create_dog_v1(text, text) to authenticated;
