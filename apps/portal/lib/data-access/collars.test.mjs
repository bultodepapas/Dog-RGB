import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  CollarDataValidationError,
  createCollarDataAccess,
  createCollarPageDto,
} from "./collars-core.ts";
import { DogDataAccessError } from "./dogs-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "30000000-0000-4000-8000-000000000003";
const COLLAR_ID = "89000000-0000-4000-8000-000000000001";
const OTHER_COLLAR_ID = "89000000-0000-4000-8000-000000000002";
const NOW = new Date("2026-08-25T12:00:00.000Z");
const UINT32_MAX = 4_294_967_295;
const CAPABILITIES = JSON.parse(await readFile(
  new URL("../../../../contracts/device-v1/fixtures/valid/capabilities.json", import.meta.url),
  "utf8",
));
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
  hardware_revision: "xiao-s3-r1",
  firmware_version: "2.0.0-cloud.1",
  protocol_version: 1,
  telemetry_schema: 3,
  config_schema: 7,
  capability_manifest: CAPABILITIES,
  linked_at: "2026-08-20T12:00:00.000Z",
  last_sync_at: "2026-08-25T11:55:00.000Z",
  revoked_at: null,
  diagnostics_observed_at: "2026-08-25T11:55:00.000Z",
  outbox_chunks: 1,
  outbox_points: 3,
  outbox_used_bytes: 96,
  outbox_capacity_bytes: 1_376_256,
  oldest_unacknowledged_at: "2026-08-25T11:54:00.000Z",
  dropped_points_total: 0,
  sync_error_present: false,
};

function page(overrides = {}) {
  return createCollarPageDto({
    dog: DOG,
    collar: COLLAR,
    capturedAt: NOW,
    ...overrides,
  });
}

function harness({
  role = "owner",
  active = COLLAR,
  exact = null,
  rpc = { ok: true, data: true },
  confirmed = {
    id: COLLAR_ID,
    dog_id: DOG_ID,
    state: "revoked",
    revoked_at: "2026-08-25T12:00:00.000Z",
  },
} = {}) {
  const client = {};
  const events = [];
  let identityCalls = 0;
  const dal = createCollarDataAccess({
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
      events.push("active");
      return active;
    },
    async findCollarIdentity(received, dogId, collarId) {
      assert.equal(received, client);
      assert.equal(dogId, DOG_ID);
      assert.equal(collarId, COLLAR_ID);
      events.push("identity");
      identityCalls += 1;
      return identityCalls === 1 && exact ? exact : confirmed;
    },
    async invokeRevoke(received, collarId) {
      assert.equal(received, client);
      assert.equal(collarId, COLLAR_ID);
      events.push("rpc");
      return rpc;
    },
    now() {
      events.push("now");
      return NOW;
    },
  });
  return { dal, events };
}

test("collar DTO returns frozen accepted compatibility and pre-ACK diagnostics", () => {
  const dto = page();
  assert.equal(dto.canIssueClaim, true);
  assert.equal(dto.canRevoke, true);
  assert.deepEqual(dto.collar.compatibility, {
    hardwareRevision: "xiao-s3-r1",
    firmwareVersion: "2.0.0-cloud.1",
    protocolVersion: 1,
    telemetrySchema: 3,
    configSchema: 7,
    brightnessBidirectional: true,
    configurationReporting: true,
    telemetryLossMarkers: true,
    legacyV2Upload: false,
    resourceCount: 6,
    effectCount: 12,
    paletteCount: 8,
    maxChunksPerSync: 8,
    maxPointsPerSync: 384,
  });
  assert.deepEqual(dto.collar.diagnostics, {
    observedAt: "2026-08-25T11:55:00.000Z",
    outboxChunks: 1,
    outboxPoints: 3,
    usedBytes: 96,
    capacityBytes: 1_376_256,
    oldestUnacknowledgedAt: "2026-08-25T11:54:00.000Z",
    droppedPointsTotal: 0,
    errorReported: false,
    state: "pending",
  });
  assert.equal(Object.isFrozen(dto), true);
  assert.equal(Object.isFrozen(dto.collar), true);
  assert.equal(Object.isFrozen(dto.collar.compatibility), true);
  assert.equal(Object.isFrozen(dto.collar.diagnostics), true);
});

test("missing diagnostics stay unavailable and exact zeros mean empty only", () => {
  const missing = page({
    collar: {
      ...COLLAR,
      diagnostics_observed_at: null,
      outbox_chunks: null,
      outbox_points: null,
      outbox_used_bytes: null,
      outbox_capacity_bytes: null,
      oldest_unacknowledged_at: null,
      dropped_points_total: null,
      sync_error_present: null,
    },
  });
  assert.equal(missing.collar.diagnostics, null);
  const empty = page({
    collar: {
      ...COLLAR,
      outbox_chunks: 0,
      outbox_points: 0,
      outbox_used_bytes: 0,
      oldest_unacknowledged_at: null,
    },
  });
  assert.equal(empty.collar.diagnostics.state, "empty");
  const building = page({
    collar: {
      ...COLLAR,
      outbox_chunks: 0,
      outbox_points: 1,
      outbox_used_bytes: 16,
    },
  });
  assert.equal(building.collar.diagnostics.state, "pending");
});

test("one read client selects only deterministic active truth", async () => {
  const active = harness();
  assert.equal((await active.dal.getCollarPage(DOG_ID)).collar.state, "active");
  assert.deepEqual(active.events, ["client", "auth", "membership", "dog", "active", "now"]);

  const none = harness({ active: null });
  assert.equal((await none.dal.getCollarPage(DOG_ID)).collar, null);
  assert.deepEqual(none.events, ["client", "auth", "membership", "dog", "active", "now"]);
});

test("owner/editor/viewer share read truth while only owner may revoke", async () => {
  assert.equal((await harness({ role: "owner" }).dal.getCollarPage(DOG_ID)).canRevoke, true);
  assert.equal((await harness({ role: "editor" }).dal.getCollarPage(DOG_ID)).canRevoke, false);
  assert.equal((await harness({ role: "viewer" }).dal.getCollarPage(DOG_ID)).canRevoke, false);
  for (const role of ["editor", "viewer"]) {
    const attempt = harness({ role });
    await assert.rejects(
      attempt.dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }),
      (error) => error instanceof DogDataAccessError && error.code === "access_denied",
    );
    assert.deepEqual(attempt.events, ["client", "auth", "membership"]);
  }
});

test("owner revoke reselects, invokes once, and confirms persisted revoked state", async () => {
  const { dal, events } = harness();
  assert.deepEqual(await dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }), {
    ok: true,
    previousState: "active",
  });
  assert.deepEqual(events, ["client", "auth", "membership", "active", "rpc", "identity"]);
});

test("an exact retry confirms an already-revoked collar but never follows a changed selection", async () => {
  const revokedIdentity = {
    id: COLLAR_ID,
    dog_id: DOG_ID,
    state: "revoked",
    revoked_at: "2026-08-25T11:59:00.000Z",
  };
  const replay = harness({ active: null, exact: revokedIdentity });
  assert.deepEqual(await replay.dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }), {
    ok: true,
    previousState: "revoked",
  });
  assert.deepEqual(replay.events, [
    "client", "auth", "membership", "active", "identity", "rpc", "identity",
  ]);

  const changed = harness({
    active: { ...COLLAR, id: OTHER_COLLAR_ID },
    exact: { id: COLLAR_ID, dog_id: DOG_ID, state: "active", revoked_at: null },
  });
  assert.deepEqual(await changed.dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }), {
    ok: false,
    reason: "selection_changed",
  });
  assert.equal(changed.events.includes("rpc"), false);
});

test("RPC and confirmation uncertainty never produce a success claim", async () => {
  assert.deepEqual(
    await harness({ rpc: { ok: false } }).dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }),
    { ok: false, reason: "ambiguous" },
  );
  assert.deepEqual(
    await harness({ confirmed: { id: COLLAR_ID, dog_id: DOG_ID, state: "active", revoked_at: null } })
      .dal.revokeCollar({ dogId: DOG_ID, collarId: COLLAR_ID }),
    { ok: false, reason: "ambiguous" },
  );
});

test("malformed identity, capability, timestamps, and diagnostic snapshots fail closed", () => {
  const invalid = [
    { collar: { ...COLLAR, dog_id: "39000000-0000-4000-8000-000000000003" } },
    { collar: { ...COLLAR, state: "retired" } },
    { collar: { ...COLLAR, protocol_version: null } },
    { collar: { ...COLLAR, firmware_version: "" } },
    { collar: { ...COLLAR, capability_manifest: { ...CAPABILITIES, protocol_versions: [2] } } },
    { collar: { ...COLLAR, capability_manifest: { ...CAPABILITIES, config_resources: [...CAPABILITIES.config_resources, CAPABILITIES.config_resources[0]] } } },
    { collar: { ...COLLAR, last_sync_at: "2026-08-25T12:00:00.001Z" } },
    { collar: { ...COLLAR, diagnostics_observed_at: null } },
    { collar: { ...COLLAR, diagnostics_observed_at: "2026-08-25T11:54:59.000Z" } },
    { collar: { ...COLLAR, outbox_used_bytes: 1_376_257 } },
    { collar: { ...COLLAR, dropped_points_total: UINT32_MAX + 1 } },
  ];
  for (const overrides of invalid) {
    assert.throws(() => page(overrides), CollarDataValidationError);
  }
});

test("production adapter uses explicit RLS projections, bounded ordering, and one existing RPC", async () => {
  const source = await readFile(new URL("./collars.ts", import.meta.url), "utf8");
  const pageSource = await readFile(
    new URL("../../app/app/[dogId]/collars/page.tsx", import.meta.url),
    "utf8",
  );
  assert.match(source, /id, dog_id, display_name, state, hardware_revision, firmware_version, protocol_version/u);
  assert.match(source, /diagnostics_observed_at, outbox_chunks, outbox_points, outbox_used_bytes/u);
  assert.match(source, /\.eq\("state", "active"\)[\s\S]*last_sync_at[\s\S]*linked_at[\s\S]*\.order\("id", \{ ascending: true \}\)/u);
  assert.match(source, /client\.rpc\("revoke_collar_v1"/u);
  assert.doesNotMatch(source, /select\(\s*["'`]\*["'`]\s*\)|service_role|device_public_id|capability_hash|sync_requests|device_credentials/u);
  assert.match(pageSource, /requireCollarsPage/u);
  assert.doesNotMatch(pageSource, /requireDogPage|createServerSupabaseClient|\.from\(|\.rpc\(/u);
});
