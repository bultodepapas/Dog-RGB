import assert from "node:assert/strict";
import test from "node:test";
import { renameDogHandler } from "./rename.ts";
const id = "00000000-0000-4000-8000-000000000001";
function form(name = " Luna ") { const f = new FormData(); f.set("dogId", id); f.set("name", name); return f; }
test("rename sends only validated dog identity and trimmed name", async () => {
  let call;
  const result = await renameDogHandler(async (...args) => { call = args; })(form());
  assert.deepEqual(call, [id, "Luna"]); assert.equal(result.status, "saved");
});
test("rename rejects duplicate fields and oversized Unicode before mutation", async () => {
  const save = renameDogHandler(async () => { assert.fail("invalid input reached writer"); });
  const duplicate = form(); duplicate.append("dogId", id);
  assert.equal((await save(duplicate)).status, "error");
  assert.equal((await save(form("🐕".repeat(81)))).status, "error");
});
test("rename hides authorization and database errors", async () => {
  const result = await renameDogHandler(async () => { throw new Error("private database detail"); })(form());
  assert.equal(result.status, "error"); assert.ok(!result.message.includes("private"));
});
