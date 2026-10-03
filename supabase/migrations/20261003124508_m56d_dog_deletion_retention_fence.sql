-- A retention batch and dog-deletion request must agree on the point inventory
-- captured in the deletion job. Retention serializes per collar with this lock.
-- Keep the existing bounded count query as the snapshot implementation, and
-- acquire every dog collar's fence in deterministic order before evaluating it.
alter function private.dog_deletion_counts_v1(uuid)
  rename to dog_deletion_counts_snapshot_v1;

create function private.dog_deletion_counts_v1(p_dog_id uuid)
returns jsonb
language plpgsql
volatile
set search_path = ''
as $$
declare
  v_collar_id uuid;
  v_counts jsonb;
begin
  for v_collar_id in
    select collar.id
    from api.collars collar
    where collar.dog_id = p_dog_id
    order by collar.id
  loop
    perform private.lock_telemetry_collar_v1(v_collar_id);
  end loop;

  -- This nested query runs after any advisory-lock wait. VOLATILE gives it a
  -- fresh READ COMMITTED snapshot, so it sees a retention batch that just
  -- committed while this function was waiting.
  select private.dog_deletion_counts_snapshot_v1(p_dog_id)
    into v_counts;
  return v_counts;
end
$$;

revoke all on function private.dog_deletion_counts_v1(uuid)
  from public, anon, authenticated, service_role;

comment on function private.dog_deletion_counts_v1(uuid) is
  'Acquire every per-collar telemetry retention fence in UUID order, then return the exact deletion inventory from a fresh snapshot.';
