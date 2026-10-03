import assert from "node:assert/strict";
import test from "node:test";

import { resolveDogEntry } from "./entry.ts";

test("dog entry creates a profile only when there are no memberships", () => {
  assert.deepEqual(resolveDogEntry([]), { kind: "create" });
});

test("dog entry opens Today for the single authorized membership", () => {
  assert.deepEqual(
    resolveDogEntry([{ id: "20000000-0000-4000-8000-000000000001" }]),
    { kind: "open", dogId: "20000000-0000-4000-8000-000000000001" },
  );
});

test("dog entry selects when there are multiple authorized memberships", () => {
  assert.deepEqual(
    resolveDogEntry([
      { id: "20000000-0000-4000-8000-000000000001" },
      { id: "20000000-0000-4000-8000-000000000002" },
    ]),
    { kind: "select" },
  );
});
