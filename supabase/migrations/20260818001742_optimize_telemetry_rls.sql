-- The original telemetry policy resolved collar -> dog -> membership for every
-- candidate point. At one million points this made a 500k-row owner aggregate
-- execute the membership helper 500k times and exceed 60 seconds. Build the
-- caller-visible collar set once instead; api.collars retains the canonical
-- membership policy, so authorization semantics do not change.
drop policy telemetry_points_select_member on api.telemetry_points;

create policy telemetry_points_select_member on api.telemetry_points
  for select to authenticated
  using (
    collar_id in (
      select visible_collar.id
      from api.collars as visible_collar
    )
  );
