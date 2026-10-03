import test from "node:test";
import assert from "node:assert/strict";
import { parseHistoryRange, historyRangeExpression } from "./history-range.ts";

test("history range rejects invalid, duplicate, incomplete and unbounded dates", () => {
  assert.equal(parseHistoryRange(undefined, undefined), null);
  for (const [a,b] of [["9999-12-31", "9999-12-31"], ["2026-02-30", "2026-03-01"], ["2026-03-02", "2026-03-01"], [["2026-03-01"], "2026-03-02"], ["2026-01-01", undefined], ["2025-01-01", "2026-01-02"]]) {
    assert.equal(parseHistoryRange(a,b), "invalid");
  }
});

test("history filter retains unknown-time records and resolves each local midnight in the dog timezone", () => {
  const range = parseHistoryRange("2026-11-01", "2026-11-01");
  assert.deepEqual(range, { from: "2026-11-01", to: "2026-11-01", until: "2026-11-02" });
  const expression = historyRangeExpression(range, "America/New_York");
  assert.match(expression, /started_at.is.null/u);
  assert.match(expression, /2026-11-01 00:00:00 America\/New_York/u);
  assert.match(expression, /2026-11-02 00:00:00 America\/New_York/u);
});
