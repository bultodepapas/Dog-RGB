-- Measured M1.9 index for the global History keyset order. The existing
-- collar-leading index remains useful for collar-scoped reads such as Today,
-- while this narrow index lets History stop after its 20+1 global rows.
create index recordings_history_started_id_idx
  on api.recordings (started_at desc nulls last, id desc);
