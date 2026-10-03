import test from "node:test";
import assert from "node:assert/strict";
import { parseDeletionForm, parseDeletionJobs } from "./deletion.ts";

function form() {
  const data = new FormData();
  data.set("dogId", "30000000-0000-4000-8000-000000000003");
  data.set("requestId", "40000000-0000-4000-8000-000000000004");
  data.set("password", "a-current-password");
  data.set("confirmation", "ELIMINAR");
  return data;
}

test("deletion requires exact confirmation, current password and single canonical identifiers", () => {
  assert.ok(parseDeletionForm(form()));
  for (const key of ["dogId", "requestId", "password", "confirmation"]) {
    const input = form(); input.append(key, input.get(key));
    assert.equal(parseDeletionForm(input), null);
  }
  for (const [key, value] of [["confirmation", "eliminar"], ["password", ""], ["dogId", "other-dog"]]) {
    const input = form(); input.set(key, value);
    assert.equal(parseDeletionForm(input), null);
  }
});

test("deletion status never treats an unknown or partial result as completed", () => {
  assert.deepEqual(parseDeletionJobs([]), []);
  assert.throws(() => parseDeletionJobs(null));
  assert.throws(() => parseDeletionJobs([{ status: "completed" }]));
  const base = { job_id: "job", scope_id: "dog", requested_at: "2026-10-02T00:00:00Z" };
  assert.throws(() => parseDeletionJobs([{ ...base, status: "complete" }]));
  assert.equal(parseDeletionJobs([{ ...base, status: "pending", internal_secret: "never-copy" }])[0].status, "pending");
  assert.equal(Object.hasOwn(parseDeletionJobs([{ ...base, status: "pending" }])[0], "internal_secret"), false);
});
