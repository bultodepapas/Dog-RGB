import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  createDogDataAccess,
  DogDataAccessError,
} from "./dogs-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "20000000-0000-4000-8000-000000000001";
const OTHER_DOG_ID = "20000000-0000-4000-8000-000000000002";
const COLLAR_ID = "30000000-0000-4000-8000-000000000001";
const OTHER_COLLAR_ID = "30000000-0000-4000-8000-000000000002";
const RECORDING_ID = "40000000-0000-4000-8000-000000000001";
const OTHER_RECORDING_ID = "40000000-0000-4000-8000-000000000002";
const NOW = new Date("2026-08-25T04:30:00.000Z");
const LOCAL_DATE = "2026-08-24";

const DOG = {
  id: DOG_ID,
  name: "Mora",
  timezone: "America/Bogota",
};
const MEMBERSHIP = { dog_id: DOG_ID, role: "viewer" };
const COLLAR = {
  id: COLLAR_ID,
  dog_id: DOG_ID,
  display_name: "Collar de Mora",
  state: "active",
  last_sync_at: "2026-08-24T04:30:00.000Z",
  linked_at: "2026-08-20T12:00:00.000Z",
};
const DAILY_SUMMARY = {
  dog_id: DOG_ID,
  local_date: LOCAL_DATE,
  timezone: "America/Bogota",
  coverage_ratio: 0.875,
  unknown_s: 10800,
  algorithm_version: 3,
  computed_at: "2026-08-25T04:20:00.000Z",
};
const RECORDING = {
  id: RECORDING_ID,
  collar_id: COLLAR_ID,
  started_at: "2026-08-25T03:00:00.000Z",
  ended_at: "2026-08-25T03:30:00.000Z",
  created_at: "2026-08-25T03:05:00.000Z",
  state: "closed",
  point_count: 42,
  clock_quality: "gnss_trusted",
};
const RECORDING_SUMMARY = {
  recording_id: RECORDING_ID,
  coverage_ratio: 0.75,
  algorithm_version: 2,
  computed_at: "2026-08-25T04:00:00.000Z",
};

function harness({
  now = NOW,
  userId = USER_ID,
  membership = MEMBERSHIP,
  dog = DOG,
  collar = COLLAR,
  dailySummary = DAILY_SUMMARY,
  recording = RECORDING,
  recordingSummary = RECORDING_SUMMARY,
} = {}) {
  const client = {};
  const events = [];
  const calls = {
    createClient: 0,
    getFreshUserId: 0,
    findMembership: 0,
    findDog: 0,
    findActiveCollar: 0,
    findDailySummary: 0,
    findLatestRecording: 0,
    findRecordingSummary: 0,
  };
  const verifyClient = (received) => assert.equal(received, client);

  const dal = createDogDataAccess({
    async createClient() {
      calls.createClient += 1;
      events.push("client");
      return client;
    },
    async getFreshUserId(receivedClient) {
      verifyClient(receivedClient);
      calls.getFreshUserId += 1;
      events.push("auth");
      return userId;
    },
    async findMembership(receivedClient, receivedUserId, receivedDogId) {
      verifyClient(receivedClient);
      assert.equal(receivedUserId, USER_ID);
      assert.equal(receivedDogId, DOG_ID);
      calls.findMembership += 1;
      events.push("membership");
      return membership;
    },
    async findDog(receivedClient, receivedDogId) {
      verifyClient(receivedClient);
      assert.equal(receivedDogId, DOG_ID);
      calls.findDog += 1;
      events.push("dog");
      return dog;
    },
    async findActiveCollar(receivedClient, receivedDogId) {
      verifyClient(receivedClient);
      assert.equal(receivedDogId, DOG_ID);
      calls.findActiveCollar += 1;
      events.push("collar");
      return collar;
    },
    async findDailySummary(receivedClient, receivedDogId, localDate) {
      verifyClient(receivedClient);
      assert.equal(receivedDogId, DOG_ID);
      assert.equal(localDate, dailySummary?.local_date ?? LOCAL_DATE);
      calls.findDailySummary += 1;
      events.push("daily");
      return dailySummary;
    },
    async findLatestRecording(receivedClient, receivedCollarId) {
      verifyClient(receivedClient);
      assert.equal(receivedCollarId, collar?.id);
      calls.findLatestRecording += 1;
      events.push("recording");
      return recording;
    },
    async findRecordingSummary(receivedClient, receivedRecordingId) {
      verifyClient(receivedClient);
      assert.equal(receivedRecordingId, recording?.id);
      calls.findRecordingSummary += 1;
      events.push("recording-summary");
      return recordingSummary;
    },
    async listMemberships() {
      return [];
    },
    async listDogs() {
      return [];
    },
    now() {
      events.push("now");
      return new Date(now);
    },
  });

  return { calls, dal, events };
}

async function expectCode(promise, code) {
  await assert.rejects(promise, (error) => {
    assert.ok(error instanceof DogDataAccessError);
    assert.equal(error.code, code);
    return true;
  });
}

test("Today uses one fresh authorized client and returns one deeply frozen minimal DTO", async () => {
  const { calls, dal, events } = harness();
  const snapshot = await dal.getTodaySnapshot(DOG_ID);

  assert.deepEqual(snapshot, {
    dog: {
      id: DOG_ID,
      name: "Mora",
      timezone: "America/Bogota",
      role: "viewer",
    },
    localDate: LOCAL_DATE,
    collar: {
      name: "Collar de Mora",
      lastSyncAt: "2026-08-24T04:30:00.000Z",
      freshness: "recent",
    },
    dailySummary: { coverageRatio: 0.875, unknownSeconds: 10800 },
    latestRecording: {
      startedAt: "2026-08-25T03:00:00.000Z",
      timeQuality: "trusted",
      state: "closed",
      pointCount: 42,
      coverageRatio: 0.75,
    },
  });
  assert.deepEqual(calls, {
    createClient: 1,
    getFreshUserId: 1,
    findMembership: 1,
    findDog: 1,
    findActiveCollar: 1,
    findDailySummary: 1,
    findLatestRecording: 1,
    findRecordingSummary: 1,
  });
  assert.deepEqual(events.slice(0, 5), [
    "client",
    "auth",
    "membership",
    "dog",
    "now",
  ]);
  assert.equal(Object.isFrozen(snapshot), true);
  assert.equal(Object.isFrozen(snapshot.dog), true);
  assert.equal(Object.isFrozen(snapshot.collar), true);
  assert.equal(Object.isFrozen(snapshot.dailySummary), true);
  assert.equal(Object.isFrozen(snapshot.latestRecording), true);
  assert.deepEqual(Object.keys(snapshot).sort(), [
    "collar",
    "dailySummary",
    "dog",
    "latestRecording",
    "localDate",
  ]);
  assert.deepEqual(Object.keys(snapshot.collar).sort(), [
    "freshness",
    "lastSyncAt",
    "name",
  ]);
});

test("no active collar remains honest and skips collar-scoped reads", async () => {
  const { calls, dal } = harness({
    collar: null,
    dailySummary: null,
    recording: null,
    recordingSummary: null,
  });
  const snapshot = await dal.getTodaySnapshot(DOG_ID);

  assert.equal(snapshot.collar, null);
  assert.equal(snapshot.dailySummary, null);
  assert.equal(snapshot.latestRecording, null);
  assert.equal(calls.findLatestRecording, 0);
  assert.equal(calls.findRecordingSummary, 0);
});

test("dog-scoped daily truth remains available without an active collar", async () => {
  const snapshot = await harness({
    collar: null,
    recording: null,
    recordingSummary: null,
  }).dal.getTodaySnapshot(DOG_ID);

  assert.equal(snapshot.collar, null);
  assert.deepEqual(snapshot.dailySummary, {
    coverageRatio: 0.875,
    unknownSeconds: 10800,
  });
  assert.equal(snapshot.latestRecording, null);
});

test("owner, editor, and viewer use the same read-only Today path", async () => {
  for (const role of ["owner", "editor", "viewer"]) {
    const snapshot = await harness({
      membership: { ...MEMBERSHIP, role },
      recording: null,
      recordingSummary: null,
    }).dal.getTodaySnapshot(DOG_ID);
    assert.equal(snapshot.dog.role, role);
  }
});

test("nullable collar names and freshness boundaries are exact", async () => {
  const never = await harness({
    collar: { ...COLLAR, display_name: null, last_sync_at: null },
    recording: null,
    recordingSummary: null,
  }).dal.getTodaySnapshot(DOG_ID);
  assert.deepEqual(never.collar, {
    name: "Collar sin nombre",
    lastSyncAt: null,
    freshness: "never",
  });

  const stale = await harness({
    collar: {
      ...COLLAR,
      last_sync_at: new Date(NOW.getTime() - 86_400_001).toISOString(),
    },
    recording: null,
    recordingSummary: null,
  }).dal.getTodaySnapshot(DOG_ID);
  assert.equal(stale.collar.freshness, "stale");

  await expectCode(
    harness({
      collar: {
        ...COLLAR,
        last_sync_at: new Date(NOW.getTime() + 1).toISOString(),
      },
    }).dal.getTodaySnapshot(DOG_ID),
    "data_unavailable",
  );
});

test("dog-local date uses the IANA zone across UTC and DST boundaries", async () => {
  const bogota = await harness({
    collar: null,
    dailySummary: null,
    recording: null,
    recordingSummary: null,
  }).dal.getTodaySnapshot(DOG_ID);
  assert.equal(bogota.localDate, "2026-08-24");

  const newYorkDog = { ...DOG, timezone: "America/New_York" };
  const newYorkSummary = {
    ...DAILY_SUMMARY,
    timezone: "America/New_York",
    local_date: "2026-03-08",
    computed_at: "2026-03-08T06:20:00.000Z",
  };
  const newYork = await harness({
    now: new Date("2026-03-08T06:30:00.000Z"),
    dog: newYorkDog,
    collar: null,
    dailySummary: newYorkSummary,
    recording: null,
    recordingSummary: null,
  }).dal.getTodaySnapshot(DOG_ID);
  assert.equal(newYork.localDate, "2026-03-08");

  const invalid = harness({ dog: { ...DOG, timezone: "Mars/Olympus" } });
  await expectCode(invalid.dal.getTodaySnapshot(DOG_ID), "data_unavailable");
  assert.equal(invalid.calls.findActiveCollar, 0);
  assert.equal(invalid.calls.findDailySummary, 0);
});

test("authentication and membership denial happen before every product read", async () => {
  const stale = harness({ userId: null });
  await expectCode(stale.dal.getTodaySnapshot(DOG_ID), "authentication_required");
  assert.equal(stale.calls.findMembership, 0);
  assert.equal(stale.calls.findActiveCollar, 0);

  const outsider = harness({ membership: null });
  await expectCode(outsider.dal.getTodaySnapshot(DOG_ID), "access_denied");
  assert.equal(outsider.calls.findDog, 0);
  assert.equal(outsider.calls.findDailySummary, 0);
});

test("daily summaries fail closed on cross-dog, timezone, numeric, or future data", async () => {
  const invalidRows = [
    { ...DAILY_SUMMARY, dog_id: OTHER_DOG_ID },
    { ...DAILY_SUMMARY, timezone: "UTC" },
    { ...DAILY_SUMMARY, coverage_ratio: Number.NaN },
    { ...DAILY_SUMMARY, coverage_ratio: 1.01 },
    { ...DAILY_SUMMARY, unknown_s: 1.5 },
    { ...DAILY_SUMMARY, algorithm_version: 0 },
    { ...DAILY_SUMMARY, computed_at: "2026-08-25T04:30:00.001Z" },
  ];

  for (const dailySummary of invalidRows) {
    await expectCode(
      harness({ dailySummary }).dal.getTodaySnapshot(DOG_ID),
      "data_unavailable",
    );
  }
});

test("selected collars fail closed on cross-dog, malformed, or future fields", async () => {
  const invalidRows = [
    { ...COLLAR, dog_id: OTHER_DOG_ID },
    { ...COLLAR, display_name: "   " },
    { ...COLLAR, state: "revoked" },
    { ...COLLAR, linked_at: "2026-02-30T12:00:00.000Z" },
    { ...COLLAR, linked_at: "2026-08-25T04:30:00.001Z" },
  ];

  for (const collar of invalidRows) {
    await expectCode(
      harness({ collar }).dal.getTodaySnapshot(DOG_ID),
      "data_unavailable",
    );
  }
});

test("recording time distinguishes trusted, approximate, and unavailable evidence", async () => {
  for (const clockQuality of ["approximate_persisted", "legacy_minute"]) {
    const snapshot = await harness({
      recording: { ...RECORDING, clock_quality: clockQuality },
    }).dal.getTodaySnapshot(DOG_ID);
    assert.equal(snapshot.latestRecording.timeQuality, "approximate");
  }

  const unknown = await harness({
    recording: {
      ...RECORDING,
      started_at: null,
      ended_at: null,
      clock_quality: "unknown",
    },
  }).dal.getTodaySnapshot(DOG_ID);
  assert.equal(unknown.latestRecording.startedAt, null);
  assert.equal(unknown.latestRecording.timeQuality, "unknown");

  const ingestionGap = await harness({
    recording: {
      ...RECORDING,
      started_at: null,
      ended_at: null,
      clock_quality: "gnss_trusted",
    },
  }).dal.getTodaySnapshot(DOG_ID);
  assert.equal(ingestionGap.latestRecording.startedAt, null);
  assert.equal(ingestionGap.latestRecording.timeQuality, "unknown");

  await expectCode(
    harness({
      recording: { ...RECORDING, clock_quality: "unknown" },
    }).dal.getTodaySnapshot(DOG_ID),
    "data_unavailable",
  );
});

test("recording and recording-summary identities and values fail closed", async () => {
  const cases = [
    { recording: { ...RECORDING, collar_id: OTHER_COLLAR_ID } },
    { recording: { ...RECORDING, state: "walk" } },
    { recording: { ...RECORDING, point_count: -1 } },
    { recording: { ...RECORDING, clock_quality: "device_guess" } },
    { recording: { ...RECORDING, created_at: "2026-08-25T04:30:00.001Z" } },
    { recording: { ...RECORDING, started_at: "2026-08-25T04:30:00.001Z" } },
    { recording: { ...RECORDING, ended_at: "2026-08-25T04:30:00.001Z" } },
    { recording: { ...RECORDING, ended_at: "2026-08-25T02:59:59.999Z" } },
    {
      recordingSummary: {
        ...RECORDING_SUMMARY,
        recording_id: OTHER_RECORDING_ID,
      },
    },
    { recordingSummary: { ...RECORDING_SUMMARY, coverage_ratio: -0.1 } },
    { recordingSummary: { ...RECORDING_SUMMARY, algorithm_version: 0 } },
    {
      recordingSummary: {
        ...RECORDING_SUMMARY,
        computed_at: "2026-08-25T04:30:00.001Z",
      },
    },
  ];

  for (const overrides of cases) {
    await expectCode(
      harness(overrides).dal.getTodaySnapshot(DOG_ID),
      "data_unavailable",
    );
  }
});

test("production Today adapter is explicit, deterministic, RLS-session scoped, and bounded", async () => {
  const source = await readFile(new URL("./dogs.ts", import.meta.url), "utf8");

  for (const columns of [
    "id, dog_id, display_name, state, last_sync_at, linked_at",
    "dog_id, local_date, timezone, coverage_ratio, unknown_s, algorithm_version, computed_at",
    "id, collar_id, started_at, ended_at, created_at, state, point_count, clock_quality",
    "recording_id, coverage_ratio, algorithm_version, computed_at",
  ]) {
    assert.ok(source.includes(`"${columns}"`), columns);
  }
  assert.match(source, /\.eq\("dog_id", dogId\)[\s\S]*?\.eq\("state", "active"\)/u);
  assert.match(source, /\.order\("last_sync_at", \{ ascending: false, nullsFirst: false \}\)/u);
  assert.match(source, /\.order\("linked_at", \{ ascending: false, nullsFirst: false \}\)/u);
  assert.match(source, /\.order\("id", \{ ascending: true \}\)/u);
  assert.match(source, /\.order\("started_at", \{ ascending: false, nullsFirst: false \}\)/u);
  assert.match(source, /\.order\("created_at", \{ ascending: false \}\)/u);
  assert.match(source, /\.order\("id", \{ ascending: false \}\)/u);
  assert.ok((source.match(/\.limit\(1\)/gu) ?? []).length >= 4);
  assert.doesNotMatch(source, /\.select\(\s*["'`]\*["'`]\s*\)/u);
  assert.doesNotMatch(
    source,
    /telemetry_points|device_public_id|capability_manifest|min_lat_e7|max_lat_e7|min_lon_e7|max_lon_e7|service_role|sb_secret_/u,
  );
});
