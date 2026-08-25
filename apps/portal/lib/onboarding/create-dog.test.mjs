import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  CREATE_DOG_GENERIC_ERROR,
  createDogMutationHandler,
  DOG_NAME_FIELD_ERROR,
  DOG_TIMEZONE,
  parseDogNameForm,
} from "./create-dog.ts";

const DOG_ID = "20000000-0000-4000-8000-000000000001";
const USER_ID = "10000000-0000-4000-8000-000000000001";

function form(value) {
  const data = new FormData();
  if (value !== undefined) {
    data.set("name", value);
  }
  return data;
}

function harness({ userId = USER_ID, rpcResult = DOG_ID, rpcError } = {}) {
  const calls = [];
  const client = Object.freeze({ request: "scoped" });
  const mutate = createDogMutationHandler({
    async createClient() {
      calls.push(["createClient"]);
      return client;
    },
    async getFreshUserId(receivedClient) {
      calls.push(["getFreshUserId", receivedClient]);
      return userId;
    },
    isCanonicalDogId(value) {
      calls.push(["isCanonicalDogId", value]);
      return value === DOG_ID;
    },
    async callCreateDogRpc(receivedClient, input) {
      calls.push(["callCreateDogRpc", receivedClient, input]);
      if (rpcError) {
        throw rpcError;
      }
      return rpcResult;
    },
  });

  return { calls, client, mutate };
}

test("dog names are trimmed once and reject missing or non-string values", () => {
  assert.deepEqual(parseDogNameForm(form("  Mora  ")), {
    ok: true,
    name: "Mora",
  });

  for (const invalid of [form(), form(""), form(" \t\n "), form(new Blob())]) {
    assert.deepEqual(parseDogNameForm(invalid), {
      ok: false,
      state: {
        status: "error",
        message: "",
        fieldErrors: { name: DOG_NAME_FIELD_ERROR },
      },
    });
  }
});

test("the 80-character bound counts Unicode code points", () => {
  assert.equal(parseDogNameForm(form("🐕".repeat(80))).ok, true);
  assert.equal(parseDogNameForm(form("🐕".repeat(81))).ok, false);
  assert.equal(parseDogNameForm(form("a".repeat(80))).ok, true);
  assert.equal(parseDogNameForm(form("a".repeat(81))).ok, false);
});

test("invalid input fails before client, Auth, or RPC access", async () => {
  const { calls, mutate } = harness();

  const result = await mutate(form("\n\t"));

  assert.equal(result.ok, false);
  assert.deepEqual(calls, []);
});

test("one request-scoped client performs fresh Auth before the exact RPC", async () => {
  const { calls, client, mutate } = harness();

  assert.deepEqual(await mutate(form("  Mora 🐕  ")), {
    ok: true,
    dogId: DOG_ID,
  });
  assert.deepEqual(calls, [
    ["createClient"],
    ["getFreshUserId", client],
    [
      "callCreateDogRpc",
      client,
      { name: "Mora 🐕", timezone: DOG_TIMEZONE },
    ],
    ["isCanonicalDogId", DOG_ID],
  ]);
});

test("missing fresh identity fails closed before the RPC", async () => {
  const { calls, mutate } = harness({ userId: null });

  assert.deepEqual(await mutate(form("Mora")), {
    ok: false,
    state: { status: "error", message: CREATE_DOG_GENERIC_ERROR },
  });
  assert.deepEqual(calls.map(([name]) => name), [
    "createClient",
    "getFreshUserId",
  ]);
});

test("RPC errors and malformed results share one bounded failure", async () => {
  for (const options of [
    { rpcResult: null },
    { rpcResult: "not-a-uuid" },
    { rpcError: new Error("database detail must not escape") },
  ]) {
    const { mutate } = harness(options);
    assert.deepEqual(await mutate(form("Mora")), {
      ok: false,
      state: { status: "error", message: CREATE_DOG_GENERIC_ERROR },
    });
  }
});

test("the production action exposes only fresh Auth, RPC, UUID-gated redirect", async () => {
  const action = await readFile(
    new URL("../../app/onboarding/actions.ts", import.meta.url),
    "utf8",
  );
  const core = await readFile(new URL("./create-dog.ts", import.meta.url), "utf8");
  const authIndex = action.indexOf("client.auth.getUser()");
  const rpcIndex = action.indexOf('client.rpc("create_dog_v1"');

  assert.ok(authIndex >= 0);
  assert.ok(rpcIndex > authIndex);
  assert.match(action, /p_name: input\.name/u);
  assert.match(action, /p_timezone: input\.timezone/u);
  assert.match(action, /redirect\(dogAppPath\(result\.dogId, "today"\)\)/u);
  assert.match(action, /isCanonicalDogId: isCanonicalUuid/u);
  assert.match(core, /dependencies\.isCanonicalDogId\(dogId\)/u);
  assert.doesNotMatch(action, /\.from\(|service_role|sb_secret_|user_metadata/u);
  assert.doesNotMatch(`${action}\n${core}`, /error\.message|error\.details/u);
});

test("the onboarding page keeps its guard and the form exposes accessible states", async () => {
  const page = await readFile(
    new URL("../../app/onboarding/page.tsx", import.meta.url),
    "utf8",
  );
  const component = await readFile(
    new URL("../../app/onboarding/create-dog-form.tsx", import.meta.url),
    "utf8",
  );

  assert.match(page, /await requireFreshPageIdentity\("\/onboarding"\)/u);
  assert.match(page, /<CreateDogForm \/>/u);
  assert.match(component, /useActionState/u);
  assert.match(component, /aria-describedby=\{describedBy\}/u);
  assert.match(component, /aria-invalid=\{nameError \? true : undefined\}/u);
  assert.match(component, /aria-busy=\{pending\}/u);
  assert.match(component, /disabled=\{pending\}/u);
  assert.match(component, /submitLocked\.current/u);
  assert.match(component, /role="alert"/u);
  assert.doesNotMatch(component, /useOptimistic|maxLength/u);
});
