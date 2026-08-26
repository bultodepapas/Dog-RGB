import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  REVOKE_CONFIRMATION_VALUE,
  REVOKE_GENERIC_ERROR,
  REVOKE_SELECTION_ERROR,
  revokeCollarMutationHandler,
} from "./revoke.ts";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const COLLAR_ID = "89000000-0000-4000-8000-000000000001";
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;

function form(overrides = {}) {
  const data = new FormData();
  data.set("dogId", overrides.dogId ?? DOG_ID);
  data.set("collarId", overrides.collarId ?? COLLAR_ID);
  data.set("confirmation", overrides.confirmation ?? REVOKE_CONFIRMATION_VALUE);
  return data;
}

function handler(result = { ok: true, previousState: "active" }) {
  const calls = [];
  const mutation = revokeCollarMutationHandler({
    isCanonicalUuid: (value) => UUID_PATTERN.test(value),
    async revoke(input) {
      calls.push(input);
      return result;
    },
  });
  return { mutation, calls };
}

test("canonical confirmed input reaches one revoke and returns confirmed truth", async () => {
  const first = handler();
  assert.deepEqual(await first.mutation(form()), { status: "revoked", message: "" });
  assert.deepEqual(first.calls, [{ dogId: DOG_ID, collarId: COLLAR_ID }]);

  const replay = handler({ ok: true, previousState: "revoked" });
  assert.deepEqual(await replay.mutation(form()), {
    status: "already_revoked",
    message: "",
  });
});

test("missing, duplicate, Blob, malformed, and unchecked fields fail before mutation", async () => {
  const invalid = [
    (() => { const value = form(); value.delete("dogId"); return value; })(),
    (() => { const value = form(); value.append("collarId", COLLAR_ID); return value; })(),
    (() => { const value = form(); value.set("dogId", new Blob(["x"])); return value; })(),
    form({ collarId: "forged" }),
    form({ confirmation: "no" }),
  ];
  for (const value of invalid) {
    const attempt = handler();
    assert.deepEqual(await attempt.mutation(value), {
      status: "error",
      message: REVOKE_GENERIC_ERROR,
    });
    assert.equal(attempt.calls.length, 0);
  }
});

test("selection drift and ambiguous outcomes remain bounded", async () => {
  const changed = handler({ ok: false, reason: "selection_changed" });
  assert.deepEqual(await changed.mutation(form()), {
    status: "error",
    message: REVOKE_SELECTION_ERROR,
  });
  const ambiguous = handler({ ok: false, reason: "ambiguous" });
  assert.deepEqual(await ambiguous.mutation(form()), {
    status: "error",
    message: REVOKE_GENERIC_ERROR,
  });
});

test("thrown internals collapse to one generic message", async () => {
  const mutation = revokeCollarMutationHandler({
    isCanonicalUuid: (value) => UUID_PATTERN.test(value),
    async revoke() {
      throw new Error("credential digest SQL detail");
    },
  });
  assert.deepEqual(await mutation(form()), {
    status: "error",
    message: REVOKE_GENERIC_ERROR,
  });
});

test("client form has explicit confirmation, focus, duplicate lock, and no browser data client", async () => {
  const source = await readFile(
    new URL("../../app/components/collar-revoke-form.tsx", import.meta.url),
    "utf8",
  );
  assert.match(source, /aria-expanded=\{expanded\}/u);
  assert.match(source, /required[\s\S]*type="checkbox"/u);
  assert.match(source, /submitLocked/u);
  assert.match(source, /resultRef\.current\?\.focus/u);
  assert.match(source, /headingRef\.current\?\.focus/u);
  assert.match(source, /role="status"/u);
  assert.doesNotMatch(source, /createBrowserSupabaseClient|\.from\(|\.rpc\(|setInterval|WebSocket/u);
});
