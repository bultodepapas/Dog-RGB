import test from "node:test";
import assert from "node:assert/strict";
import { coverageRatio } from "./index.js";

test("coverage is bounded and does not turn missing time into inactivity", () => {
  assert.equal(coverageRatio(30, 60), 0.5);
  assert.equal(coverageRatio(90, 60), 1);
  assert.equal(coverageRatio(0, 0), 0);
});
