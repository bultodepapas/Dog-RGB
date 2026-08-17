-- Synthetic local-only identities. These addresses and coordinates are not
-- production data. Passwords are intentionally development-only.
insert into auth.users (
  instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
  raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
  confirmation_token, email_change, email_change_token_new, recovery_token
) values
  (
    '00000000-0000-0000-0000-000000000000',
    '10000000-0000-4000-8000-000000000001',
    'authenticated', 'authenticated', 'owner@example.test',
    extensions.crypt('local-owner-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}',
    '{"display_name":"Local Owner"}', statement_timestamp(), statement_timestamp(),
    '', '', '', ''
  ),
  (
    '00000000-0000-0000-0000-000000000000',
    '20000000-0000-4000-8000-000000000002',
    'authenticated', 'authenticated', 'other@example.test',
    extensions.crypt('local-other-password', extensions.gen_salt('bf')),
    statement_timestamp(), '{"provider":"email","providers":["email"]}',
    '{"display_name":"Other User"}', statement_timestamp(), statement_timestamp(),
    '', '', '', ''
  )
on conflict (id) do nothing;

insert into api.dogs (
  id, name, timezone, created_by
) values (
  '30000000-0000-4000-8000-000000000003',
  'Pixel',
  'America/Bogota',
  '10000000-0000-4000-8000-000000000001'
) on conflict (id) do nothing;

insert into api.dog_memberships (dog_id, user_id, role) values (
  '30000000-0000-4000-8000-000000000003',
  '10000000-0000-4000-8000-000000000001',
  'owner'
) on conflict (dog_id, user_id) do nothing;
