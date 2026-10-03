import test from "node:test";
import assert from "node:assert/strict";
import { accountDeletion, accountDeletionReceipt, accountPreview, ACCOUNT_CONFIRMATION } from "./account.ts";
const id = "30000000-0000-4000-8000-000000000003";
const hash = "a".repeat(43);
test("account completion requires a durable receipt and all linked purges completed", () => {
  const state = { schema_version: "account-deletion-v1", request_id: id, status: "completed", receipt_sha256: hash, dog_jobs: [{ status: "completed" }] };
  assert.equal(accountDeletion(state).status, "completed");
  for (const patch of [{ receipt_sha256: null }, { dog_jobs: [{status:"failed"}] }, {dog_jobs: null}, {request_id: "other"}, {status: "complete"}]) assert.throws(() => accountDeletion({...state,...patch}));
  assert.equal(accountDeletion({ ...state, status: "pending", receipt_sha256: null, dog_jobs: [{status:"failed"}] }).failedJobs, 1);
  assert.equal(accountDeletion({schema_version: "account-deletion-v1",status:"none"}).requestId,null);
});
test("account receipt accepts only a completed minimal receipt for one request", () => {
  const receipt = {
    schema_version: "account-deletion-receipt-v1", request_id: id, status: "completed",
    completed_at: "2026-10-03T18:00:00.000Z", receipt_sha256: hash,
  };
  assert.deepEqual(accountDeletionReceipt(receipt), {
    requestId: id, completedAt: receipt.completed_at, receipt: hash,
  });
  for (const patch of [
    { request_id: "40000000-0000-4000-8000-000000000004" },
    { status: "ready" },
    { completed_at: "invalid" },
    { dog_ids: [id] },
    { profile: { email: "person@example.invalid" } },
  ]) assert.throws(() => accountDeletionReceipt({ ...receipt, ...patch }));
});
test("account preview validates the destructive scope and strips unexpected fields", () => {
  const preview = { schema_version: "account-deletion-preview-v1", request_id: id, scope_sha256: hash, confirmation_version: "account-delete-v1", confirmation_phrase: ACCOUNT_CONFIRMATION,
    owned_dogs: [{dog_id:id,name:"Dog",other_member_count:2}], memberships_to_detach: [], blockers: [], internal_secret:"hidden" };
  assert.equal(accountPreview(preview).ownedDogs[0].otherMembers,2);
  assert.equal(Object.hasOwn(accountPreview(preview),"internal_secret"),false);
  assert.throws(() => accountPreview({...preview,confirmation_phrase:"ELIMINAR"}));
  assert.throws(() => accountPreview({...preview,owned_dogs:[{dog_id:id,name:"Dog",other_member_count:null}]}));
});
