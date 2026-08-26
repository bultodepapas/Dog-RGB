import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  BRIGHTNESS_GENERIC_MESSAGE,
  BRIGHTNESS_STALE_MESSAGE,
  BRIGHTNESS_VALIDATION_MESSAGE,
  brightnessMutationHandler,
} from "./brightness-mutation.ts";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const COLLAR_ID = "89000000-0000-4000-8000-000000000001";
const MUTATION_ID = "89200000-0000-4000-8000-000000000001";
const HASH = "qHIb69FUgaOyv3H4RMfHX8vtlwreJSvnUEiF6YMbFvk";

function form(overrides = {}) {
  const values = {
    dogId: DOG_ID,
    collarId: COLLAR_ID,
    brightness: "96",
    mutationId: MUTATION_ID,
    baseServerVersion: "4",
    ...overrides,
  };
  const data = new FormData();
  for (const [key, value] of Object.entries(values)) {
    if (value !== undefined) data.set(key, value);
  }
  return data;
}

function response(overrides = {}) {
  return {
    mutation_id: MUTATION_ID,
    disposition: "winning",
    server_version: 5,
    body_sha256: HASH,
    ...overrides,
  };
}

function harness(result = { ok: true, data: response() }) {
  const calls = [];
  const mutate = brightnessMutationHandler({
    async mutate(input) {
      calls.push(input);
      if (result instanceof Error) throw result;
      return result;
    },
  });
  return { calls, mutate };
}

test("canonical form values produce one exact brightness RPC request", async () => {
  const { calls, mutate } = harness();
  assert.deepEqual(await mutate(form()), {
    status: "saved",
    message: "",
    attemptedBrightness: "96",
    brightness: 96,
    serverVersion: 5,
  });
  assert.deepEqual(calls, [{
    dogId: DOG_ID,
    collarId: COLLAR_ID,
    brightness: 96,
    mutationId: MUTATION_ID,
    baseServerVersion: 4,
    canonicalBody: '{"brightness":96}',
    bodySha256Hex: "\\xa8721bebd15481a3b2bf71f844c7c75fcbed970ade252be7504885e9831b16f9",
    bodySha256Base64Url: HASH,
  }]);
});

test("limits 1 and 255 are accepted with their exact canonical bodies", async () => {
  for (const [brightness, hash, hex] of [
    ["1", "c-rRdatLwSsa4AoS1FY9S_ka8-lt-ZeK_df4OztbllM", "\\x73ead175ab4bc12b1ae00a12d4563d4bf91af3e96df9978afdd7f83b3b5b9653"],
    ["255", "dI4F488PwIoo4QF5qvzQ7D5EI7zD2mShY0iMUpvK1fk", "\\x748e05e3cf0fc08a28e10179aafcd0ec3e4423bcc3da64a163488c529bcad5f9"],
  ]) {
    const numeric = Number(brightness);
    const { calls, mutate } = harness({
      ok: true,
      data: response({
        server_version: 1,
        body_sha256: hash,
      }),
    });
    const data = form({ brightness, baseServerVersion: "0" });
    assert.equal((await mutate(data)).status, "saved");
    assert.equal(calls[0].brightness, numeric);
    assert.equal(calls[0].canonicalBody, `{"brightness":${brightness}}`);
    assert.equal(calls[0].bodySha256Hex, hex);
  }
});

test("missing, duplicate, Blob, malformed, and noncanonical fields fail before mutation", async () => {
  const invalid = [
    form({ dogId: undefined }),
    form({ dogId: "not-a-uuid" }),
    form({ collarId: "not-a-uuid" }),
    form({ mutationId: "89200000-0000-1000-8000-000000000001" }),
    form({ brightness: "" }),
    form({ brightness: "0" }),
    form({ brightness: "256" }),
    form({ brightness: "1.5" }),
    form({ brightness: "01" }),
    form({ brightness: "+1" }),
    form({ brightness: " 1" }),
    form({ brightness: "1e2" }),
    form({ baseServerVersion: "01" }),
    form({ baseServerVersion: "-1" }),
    form({ baseServerVersion: "9007199254740992" }),
  ];
  const duplicate = form();
  duplicate.append("brightness", "97");
  invalid.push(duplicate);
  const blob = form();
  blob.set("brightness", new Blob(["96"]));
  invalid.push(blob);

  for (const data of invalid) {
    const { calls, mutate } = harness();
    const state = await mutate(data);
    assert.equal(state.status, "validation");
    assert.equal(state.message, BRIGHTNESS_VALIDATION_MESSAGE);
    assert.equal(calls.length, 0);
  }
});

test("an exact same-value response is a bounded unchanged result", async () => {
  const { mutate } = harness({
    ok: true,
    data: response({ disposition: "unchanged", server_version: 4 }),
  });
  assert.deepEqual(await mutate(form()), {
    status: "unchanged",
    message: "",
    attemptedBrightness: "96",
    brightness: 96,
    serverVersion: 4,
  });
});

test("stale, selection drift, and mutation conflicts require explicit refresh", async () => {
  for (const reason of ["stale", "selection_changed", "conflict"]) {
    const { mutate } = harness({ ok: false, reason });
    assert.deepEqual(await mutate(form()), {
      status: "stale",
      message: BRIGHTNESS_STALE_MESSAGE,
      attemptedBrightness: "96",
    });
  }
});

test("ambiguous failures preserve only one exact retry fingerprint", async () => {
  for (const result of [
    { ok: false, reason: "ambiguous" },
    new Error("database detail"),
    { ok: true, data: { internal: "detail" } },
  ]) {
    const { mutate } = harness(result);
    const state = await mutate(form());
    assert.deepEqual(state, {
      status: "ambiguous",
      message: BRIGHTNESS_GENERIC_MESSAGE,
      attemptedBrightness: "96",
      retry: {
        dogId: DOG_ID,
        collarId: COLLAR_ID,
        brightness: "96",
        mutationId: MUTATION_ID,
        baseServerVersion: "4",
      },
    });
    assert.equal(Object.isFrozen(state.retry), true);
  }
});

test("response validation rejects extra keys and identity, disposition, version, or hash drift", async () => {
  const invalid = [
    null,
    response({ extra: true }),
    response({ mutation_id: "89200000-0000-4000-8000-000000000002" }),
    response({ disposition: "applied" }),
    response({ server_version: 4 }),
    response({ server_version: Number.MAX_SAFE_INTEGER + 1 }),
    response({ body_sha256: "raw-database-hash" }),
    response({ disposition: "unchanged", server_version: 5 }),
  ];
  for (const data of invalid) {
    const { mutate } = harness({ ok: true, data });
    assert.equal((await mutate(form())).status, "ambiguous");
  }
});

test("the component locks ambiguous edits, focuses outcomes, and exposes no browser data path", async () => {
  const component = await readFile(
    new URL("../../app/components/brightness-form.tsx", import.meta.url),
    "utf8",
  );
  const display = await readFile(
    new URL("../../app/components/brightness-configuration.tsx", import.meta.url),
    "utf8",
  );
  const action = await readFile(
    new URL("../../app/app/[dogId]/configuration/actions.ts", import.meta.url),
    "utf8",
  );
  assert.match(action, /^"use server";/u);
  assert.match(action, /revalidatePath/u);
  assert.match(component, /brightness-state/u);
  assert.doesNotMatch(component, /brightness-mutation|node:crypto/u);
  assert.match(component, /useActionState|submitLocked|aria-busy/u);
  assert.match(component, /type="number"[\s\S]*?min=\{1\}[\s\S]*?max=\{255\}|max=\{255\}[\s\S]*?min=\{1\}/u);
  assert.match(component, /aria-describedby|aria-invalid|role="alert"/u);
  assert.match(component, /role="status"[\s\S]*?tabIndex=\{-1\}/u);
  assert.match(component, /state\.retry\.mutationId[\s\S]*?readOnly/u);
  assert.match(display, /snapshot\.canEdit && mutationId[\s\S]*?<BrightnessForm/u);
  assert.match(display, /SOLO LECTURA/u);
  const all = `${component}\n${display}\n${action}`;
  assert.doesNotMatch(all, /createBrowserClient|createServerSupabaseClient|\.from\(|\.rpc\(|setInterval|Realtime|crypto\.randomUUID/u);
  assert.doesNotMatch(display, /body_sha256|reported_body_sha256|error_code/u);
});
