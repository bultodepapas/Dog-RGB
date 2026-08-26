import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  brightnessSha256,
  canonicalBrightnessBody,
  ConfigurationDataValidationError,
  createBrightnessConfigurationDto,
  createConfigurationDataAccess,
} from "./configuration-core.ts";
import { DogDataAccessError } from "./dogs-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "30000000-0000-4000-8000-000000000003";
const COLLAR_ID = "89000000-0000-4000-8000-000000000001";
const OTHER_COLLAR_ID = "89000000-0000-4000-8000-000000000002";
const MUTATION_ID = "89200000-0000-4000-8000-000000000001";
const NOW = new Date("2026-08-25T12:00:00.000Z");
const DOG = Object.freeze({
  id: DOG_ID,
  name: "Pixel",
  timezone: "America/Bogota",
  role: "owner",
});
const COLLAR = {
  id: COLLAR_ID,
  dog_id: DOG_ID,
  display_name: "Collar de Pixel",
  state: "active",
  last_sync_at: "2026-08-24T12:00:00.000Z",
  linked_at: "2026-08-20T12:00:00.000Z",
};
const HASH = brightnessSha256(96);
const HEAD = {
  collar_id: COLLAR_ID,
  resource_key: "brightness",
  resource_schema: 1,
  server_version: 4,
  body: { brightness: 96 },
  body_sha256: HASH.hex,
  updated_at: "2026-08-25T11:55:00.000Z",
};
const REPORT = {
  collar_id: COLLAR_ID,
  resource_key: "brightness",
  reported_server_version: 4,
  reported_body_sha256: HASH.hex,
  status: "applied",
  error_code: null,
  firmware_version: "simulator-1",
  config_schema: 1,
  device_applied_at: "2026-08-25T11:56:00.000Z",
  cloud_received_at: "2026-08-25T11:57:00.000Z",
};

function snapshot(overrides = {}) {
  return createBrightnessConfigurationDto({
    dog: DOG,
    capturedAt: NOW,
    collar: COLLAR,
    head: HEAD,
    report: REPORT,
    ...overrides,
  });
}

function harness({
  role = "owner",
  collar = COLLAR,
  head = HEAD,
  report = REPORT,
  rpc = { ok: true, data: { bounded: true } },
} = {}) {
  const client = {};
  const events = [];
  const dal = createConfigurationDataAccess({
    async createClient() {
      events.push("client");
      return client;
    },
    async getFreshUserId(received) {
      assert.equal(received, client);
      events.push("auth");
      return USER_ID;
    },
    async findMembership(received, userId, dogId) {
      assert.equal(received, client);
      assert.equal(userId, USER_ID);
      assert.equal(dogId, DOG_ID);
      events.push("membership");
      return { dog_id: DOG_ID, role };
    },
    async findDog(received, dogId) {
      assert.equal(received, client);
      assert.equal(dogId, DOG_ID);
      events.push("dog");
      return { id: DOG_ID, name: "Pixel", timezone: "America/Bogota" };
    },
    async findActiveCollar(received, dogId) {
      assert.equal(received, client);
      assert.equal(dogId, DOG_ID);
      events.push("collar");
      return collar;
    },
    async findBrightnessHead(received, collarId) {
      assert.equal(received, client);
      assert.equal(collarId, COLLAR_ID);
      events.push("head");
      return head;
    },
    async findBrightnessReport(received, collarId) {
      assert.equal(received, client);
      assert.equal(collarId, COLLAR_ID);
      events.push("report");
      return report;
    },
    async invokeBrightnessMutation(received, input) {
      assert.equal(received, client);
      events.push(["rpc", input]);
      return rpc;
    },
    now() {
      events.push("now");
      return NOW;
    },
  });
  return { dal, events };
}

function mutationInput(overrides = {}) {
  return {
    dogId: DOG_ID,
    collarId: COLLAR_ID,
    brightness: 96,
    mutationId: MUTATION_ID,
    baseServerVersion: 4,
    canonicalBody: canonicalBrightnessBody(96),
    bodySha256Hex: HASH.hex,
    bodySha256Base64Url: HASH.base64url,
    ...overrides,
  };
}

test("brightness canonicalization has one stable UTF-8 SHA-256 contract", () => {
  assert.equal(canonicalBrightnessBody(96), '{"brightness":96}');
  assert.deepEqual(HASH, {
    hex: "\\xa8721bebd15481a3b2bf71f844c7c75fcbed970ade252be7504885e9831b16f9",
    base64url: "qHIb69FUgaOyv3H4RMfHX8vtlwreJSvnUEiF6YMbFvk",
  });
  assert.equal(Object.isFrozen(HASH), true);
});

test("one fresh read client authorizes, selects the collar, and returns a deeply frozen exact DTO", async () => {
  const { dal, events } = harness();
  const dto = await dal.getBrightnessConfiguration(DOG_ID);

  assert.equal(dto.truth, "applied");
  assert.equal(dto.canEdit, true);
  assert.deepEqual(dto.desired, {
    brightness: 96,
    serverVersion: 4,
    updatedAt: "2026-08-25T11:55:00.000Z",
  });
  assert.equal(dto.collar.freshness, "recent");
  assert.equal(Object.isFrozen(dto), true);
  assert.equal(Object.isFrozen(dto.dog), true);
  assert.equal(Object.isFrozen(dto.collar), true);
  assert.equal(Object.isFrozen(dto.desired), true);
  assert.equal(Object.isFrozen(dto.report), true);
  assert.deepEqual(events, [
    "client", "auth", "membership", "dog", "collar", "head", "report", "now",
  ]);
});

test("no active collar performs no configuration query and invents no value", async () => {
  const { dal, events } = harness({ collar: null, head: null, report: null });
  const dto = await dal.getBrightnessConfiguration(DOG_ID);
  assert.equal(dto.collar, null);
  assert.equal(dto.desired, null);
  assert.equal(dto.report, null);
  assert.equal(dto.truth, "unknown");
  assert.deepEqual(events, ["client", "auth", "membership", "dog", "collar", "now"]);
});

test("desired/reported truth requires exact version and hash", () => {
  assert.equal(snapshot({ report: null }).truth, "pending");
  assert.equal(snapshot({ head: null, report: null }).truth, "unknown");
  assert.equal(snapshot().truth, "applied");
  for (const status of [
    "rejected_unsupported",
    "rejected_invalid",
    "storage_failed",
  ]) {
    assert.equal(snapshot({
      report: { ...REPORT, status, error_code: "bounded_device_error" },
    }).truth, status);
  }
  assert.equal(snapshot({
    report: { ...REPORT, reported_server_version: 3 },
  }).truth, "pending");
  assert.equal(snapshot({
    report: { ...REPORT, reported_body_sha256: brightnessSha256(95).hex },
  }).truth, "pending");
});

test("freshness uses the captured server instant and the exact 24-hour boundary", () => {
  assert.equal(snapshot().collar.freshness, "recent");
  assert.equal(snapshot({
    collar: { ...COLLAR, last_sync_at: "2026-08-24T11:59:59.999Z" },
  }).collar.freshness, "stale");
  assert.equal(snapshot({
    collar: { ...COLLAR, last_sync_at: null },
  }).collar.freshness, "never");
});

test("malformed identity, body, hashes, versions, statuses, and future timestamps fail closed", () => {
  const invalid = [
    { collar: { ...COLLAR, dog_id: "39000000-0000-4000-8000-000000000003" } },
    { collar: { ...COLLAR, state: "retired" } },
    { head: { ...HEAD, resource_key: "visual_mode" } },
    { head: { ...HEAD, resource_schema: 2 } },
    { head: { ...HEAD, body: { brightness: 96, extra: true } } },
    { head: { ...HEAD, body: { brightness: 96.5 } } },
    { head: { ...HEAD, body_sha256: brightnessSha256(95).hex } },
    { head: { ...HEAD, server_version: Number.MAX_SAFE_INTEGER + 1 } },
    { head: { ...HEAD, updated_at: "2026-08-25T12:00:00.001Z" } },
    { report: { ...REPORT, status: "database_detail" } },
    { report: { ...REPORT, status: "applied", error_code: "unexpected" } },
    { report: { ...REPORT, status: "storage_failed", error_code: null } },
    { report: { ...REPORT, reported_body_sha256: "raw-hash" } },
    { report: { ...REPORT, cloud_received_at: "tomorrow" } },
    { report: { ...REPORT, device_applied_at: "2026-08-25T12:00:00.001Z" } },
  ];
  for (const overrides of invalid) {
    assert.throws(() => snapshot(overrides), ConfigurationDataValidationError);
  }
});

test("viewer reads the same truth but write authorization fails before collar and RPC", async () => {
  const read = harness({ role: "viewer" });
  assert.equal((await read.dal.getBrightnessConfiguration(DOG_ID)).canEdit, false);

  const write = harness({ role: "viewer" });
  await assert.rejects(
    write.dal.mutateBrightness(mutationInput()),
    (error) => error instanceof DogDataAccessError && error.code === "access_denied",
  );
  assert.deepEqual(write.events, ["client", "auth", "membership"]);
});

test("one fresh write client reselects the deterministic active collar before one RPC", async () => {
  const { dal, events } = harness();
  assert.deepEqual(await dal.mutateBrightness(mutationInput()), {
    ok: true,
    data: { bounded: true },
  });
  assert.deepEqual(events.slice(0, 4), ["client", "auth", "membership", "collar"]);
  assert.equal(events.filter((event) => Array.isArray(event) && event[0] === "rpc").length, 1);
  assert.deepEqual(events.at(-1)[1], mutationInput());

  const changed = harness({ collar: { ...COLLAR, id: OTHER_COLLAR_ID } });
  assert.deepEqual(await changed.dal.mutateBrightness(mutationInput()), {
    ok: false,
    reason: "selection_changed",
  });
  assert.equal(changed.events.some((event) => Array.isArray(event)), false);
});

test("production adapter keeps exact RLS columns, ordering, and one existing RPC", async () => {
  const source = await readFile(new URL("./configuration.ts", import.meta.url), "utf8");
  const page = await readFile(
    new URL("../../app/app/[dogId]/configuration/page.tsx", import.meta.url),
    "utf8",
  );
  for (const columns of [
    "id, dog_id, display_name, state, last_sync_at, linked_at",
    "collar_id, resource_key, resource_schema, server_version, body, body_sha256, updated_at",
    "collar_id, resource_key, reported_server_version, reported_body_sha256, status, error_code, firmware_version, config_schema, device_applied_at, cloud_received_at",
  ]) assert.ok(source.includes(`"${columns}"`));
  assert.match(source, /\.eq\("state", "active"\)[\s\S]*?last_sync_at[\s\S]*?linked_at[\s\S]*?\.order\("id", \{ ascending: true \}\)/u);
  assert.match(source, /client\.rpc\("mutate_config_resource_v1"/u);
  assert.doesNotMatch(source, /select\(\s*["'`]\*["'`]\s*\)|service_role|sb_secret_/u);
  assert.match(page, /requireConfigurationPage/u);
  assert.doesNotMatch(page, /requireDogPage|createServerSupabaseClient|\.from\(|\.rpc\(/u);
});
