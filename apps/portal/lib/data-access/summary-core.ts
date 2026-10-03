export type SummaryDto = Readonly<{
  status: "available" | "pending" | "stale" | "unavailable";
  reason: string | null;
  computedAt: string | null; sourceReceivedAt: string | null;
  windowStart: string | null; windowEnd: string | null; algorithmVersion: number | null;
  sourceSchemaMin: number | null; sourceSchemaMax: number | null;
  metrics: Readonly<{ observedSeconds: number; movingSeconds: number; inactiveSeconds: number;
    unknownSeconds: number; distanceMeters: number | null; averageMovingCmps: number | null;
    filteredMaxCmps: number | null; coverageRatio: number; gapCount: number; droppedPoints: number }> | null;
}>;

function record(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : null;
}
function date(value: unknown): string | null {
  return typeof value === "string" && Number.isFinite(Date.parse(value)) ? new Date(value).toISOString() : null;
}
function nonnegative(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= 0;
}
function positiveInteger(value: unknown): number | null {
  return Number.isSafeInteger(value) && Number(value) > 0 ? Number(value) : null;
}

export function summaryDto(raw: unknown, freshness: unknown): SummaryDto {
  const row = record(raw), state = record(freshness);
  const base = { computedAt: date(row?.computed_at), sourceReceivedAt: date(state?.source_received_at),
    windowStart: date(row?.window_start), windowEnd: date(row?.window_end),
    algorithmVersion: positiveInteger(row?.algorithm_version),
    sourceSchemaMin: positiveInteger(row?.source_schema_min),
    sourceSchemaMax: positiveInteger(row?.source_schema_max) };
  const reason = typeof state?.status === "string" ? state.status : null;
  const empty = { ...base, metrics: null, reason };
  const pending = reason === "pending" || state?.pending === true;
  if (!row) return { ...empty, status: pending ? "pending" : "unavailable" };
  if (row.summary_status !== "available") return { ...empty, status: pending ? "pending" : "unavailable" };
  if (base.algorithmVersion !== 1) return { ...empty, status: "unavailable", reason: "unsupported_algorithm_version" };
  if (base.sourceSchemaMin !== 3 || base.sourceSchemaMax !== 3) {
    return { ...empty, status: "unavailable", reason: "unsupported_source_schema" };
  }
  if (pending) return { ...empty, status: "pending" };
  if (reason !== "available" && reason !== "stale") return { ...empty, status: "unavailable" };
  if (!base.computedAt || !base.sourceReceivedAt || !base.windowStart || !base.windowEnd || !base.algorithmVersion ||
      date(state?.computed_at) !== base.computedAt || state?.algorithm_version !== base.algorithmVersion ||
      state?.source_schema_min !== base.sourceSchemaMin || state?.source_schema_max !== base.sourceSchemaMax ||
      date(state?.window_start) !== base.windowStart || date(state?.window_end) !== base.windowEnd ||
      date(state?.source_received_at) !== date(row.source_received_at)) return { ...empty, status: "stale" };
  const keys = ["observed_s", "moving_s", "inactive_s", "unknown_s", "coverage_ratio", "gap_count", "dropped_points"] as const;
  if (keys.some(key => !nonnegative(row[key])) || Number(row.coverage_ratio) > 1 ||
      Number(row.moving_s) + Number(row.inactive_s) !== row.observed_s ||
      Date.parse(base.windowEnd) <= Date.parse(base.windowStart)) return { ...empty, status: "unavailable" };
  const optional = (key: string) => nonnegative(row[key]) ? Number(row[key]) : null;
  return { ...base, status: reason === "stale" ? "stale" : "available", reason: null, metrics: {
    observedSeconds: Number(row.observed_s), movingSeconds: Number(row.moving_s), inactiveSeconds: Number(row.inactive_s),
    unknownSeconds: Number(row.unknown_s), distanceMeters: optional("distance_m"), averageMovingCmps: optional("average_moving_cmps"),
    filteredMaxCmps: optional("filtered_max_speed_cmps"), coverageRatio: Number(row.coverage_ratio), gapCount: Number(row.gap_count), droppedPoints: Number(row.dropped_points),
  } };
}
