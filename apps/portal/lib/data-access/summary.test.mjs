import test from "node:test";
import assert from "node:assert/strict";
import { summaryDto } from "./summary-core.ts";

const row = { summary_status: "available", observed_s: 10, moving_s: 4, inactive_s: 6, unknown_s: 10,
  distance_m: 12, average_moving_cmps: 100, filtered_max_speed_cmps: 200, coverage_ratio: .5,
  gap_count: 1, dropped_points: 2, algorithm_version: 1, computed_at: "2026-10-02T12:01:00Z",
  source_received_at: "2026-10-02T12:00:30Z", window_start: "2026-10-02T12:00:00Z", window_end: "2026-10-02T12:00:20Z",
  source_schema_min: 3, source_schema_max: 3 };
const state = { ...row, status: "available", pending: false };
test("verified summary retains metrics, units and provenance", () => {
  const result = summaryDto(row, state);
  assert.equal(result.status, "available"); assert.equal(result.metrics.coverageRatio, .5);
  assert.equal(result.metrics.unknownSeconds, 10); assert.equal(result.algorithmVersion, 1);
});
test("pending, racing computations and legacy rows cannot expose stale metrics as current", () => {
  assert.equal(summaryDto(null, { status: "pending", pending: true }).status, "pending");
  assert.equal(summaryDto(row, { ...state, pending: true }).metrics, null);
  assert.equal(summaryDto(row, { ...state, computed_at: "2026-10-02T13:00:00Z" }).status, "stale");
  assert.equal(summaryDto({ ...row, source_received_at: null }, state).metrics, null);
  assert.equal(summaryDto({ ...row, moving_s: 999 }, state).metrics, null);
  assert.equal(summaryDto(row, { ...state, window_end: "2026-10-02T12:00:40Z" }).metrics, null);
});
test("missing or insufficient retained data is unavailable, never zero activity", () => {
  assert.equal(summaryDto(null, { status: "unavailable" }).metrics, null);
  assert.equal(summaryDto({ summary_status: "insufficient_retained_data" }, { status: "insufficient_retained_data" }).status, "unavailable");
});

test("newest unsupported algorithm or telemetry schema is unavailable without v1 fallback", () => {
  const futureAlgorithm = summaryDto({ ...row, algorithm_version: 2 }, { ...state, algorithm_version: 2 });
  assert.equal(futureAlgorithm.status, "unavailable");
  assert.equal(futureAlgorithm.reason, "unsupported_algorithm_version");
  assert.equal(futureAlgorithm.algorithmVersion, 2);
  assert.equal(futureAlgorithm.metrics, null);
  assert.equal(summaryDto({ ...row, algorithm_version: 2 }, { ...state, algorithm_version: 2, pending: true }).status, "unavailable");

  const futureSchema = summaryDto({ ...row, source_schema_max: 4 }, { ...state, source_schema_max: 4 });
  assert.equal(futureSchema.status, "unavailable");
  assert.equal(futureSchema.reason, "unsupported_source_schema");
  assert.equal(futureSchema.metrics, null);

  const olderUnsupportedSchema = summaryDto({ ...row, source_schema_min: 2, source_schema_max: 2 },
    { ...state, source_schema_min: 2, source_schema_max: 2 });
  assert.equal(olderUnsupportedSchema.status, "unavailable");
  assert.equal(olderUnsupportedSchema.reason, "unsupported_source_schema");
  assert.equal(olderUnsupportedSchema.metrics, null);

  const unknownSchema = summaryDto({ ...row, source_schema_min: null, source_schema_max: null },
    { ...state, source_schema_min: null, source_schema_max: null });
  assert.equal(unknownSchema.status, "unavailable");
  assert.equal(unknownSchema.metrics, null);
});

test("clock-stale summary retains only metrics for its explicitly matched cutoff", () => {
  const result = summaryDto(row, { ...state, status: "stale" });
  assert.equal(result.status, "stale"); assert.ok(result.metrics);
  assert.equal(summaryDto(row, { ...state, status: "stale", source_received_at: "2026-10-02T13:00:00Z" }).metrics, null);
});
