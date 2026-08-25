import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  createDogDataAccess,
  DogDataAccessError,
} from "./dogs-core.ts";
import {
  createRecordingDetailContext,
  createRecordingPageDto,
  parseRecordingAfter,
  PLAIN_PREVIEW_TIME_GAP_SECONDS,
  RecordingDataValidationError,
  RECORDING_POINT_QUERY_LIMIT,
  TELEMETRY_FLAGS,
} from "./recording-detail-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "20000000-0000-4000-8000-000000000001";
const OTHER_DOG_ID = "20000000-0000-4000-8000-000000000002";
const COLLAR_ID = "30000000-0000-4000-8000-000000000001";
const OTHER_COLLAR_ID = "30000000-0000-4000-8000-000000000002";
const RECORDING_ID = "40000000-0000-4000-8000-000000000001";
const NOW = new Date("2026-08-25T12:00:00.000Z");
const DOG = Object.freeze({ id: DOG_ID, name: "Mora", timezone: "America/Bogota", role: "viewer" });
const RECORDING = Object.freeze({
  id: RECORDING_ID,
  collar_id: COLLAR_ID,
  joined_collar_id: COLLAR_ID,
  dog_id: DOG_ID,
  collar_display_name: "Collar de Mora",
  boot_sequence: 7,
  started_at: "2026-08-25T10:00:00.000Z",
  ended_at: "2026-08-25T10:10:00.000Z",
  timezone_at_start: "America/Bogota",
  state: "closed",
  first_point_sequence: 1,
  last_point_sequence: 500,
  point_count: 500,
  clock_quality: "gnss_trusted",
  telemetry_schema: 3,
  firmware_version: "m1.10-fixture",
});

function point(sequence, overrides = {}) {
  return {
    point_sequence: sequence,
    recorded_at: new Date(Date.parse("2026-08-25T10:00:00.000Z") + sequence * 5_000).toISOString(),
    lat_e7: 47_110_000 + sequence,
    lon_e7: -740_721_000 + sequence,
    reported_speed_cmps: 123,
    satellites: 9,
    flags: TELEMETRY_FLAGS.FIX_VALID | TELEMETRY_FLAGS.TIME_TRUSTED,
    time_quality: "gnss_trusted",
    ...overrides,
  };
}

function context(recording = RECORDING) {
  return createRecordingDetailContext({ dog: DOG, capturedAt: NOW, row: recording });
}

function page(rows, { after = null, recording = RECORDING } = {}) {
  return createRecordingPageDto({
    context: context(recording),
    capturedAt: NOW,
    after,
    rows,
  });
}

function harness({
  userId = USER_ID,
  membership = { dog_id: DOG_ID, role: "viewer" },
  dog = DOG,
  recording = RECORDING,
  points = [point(1)],
  recordingError = null,
  pointError = null,
} = {}) {
  const client = {};
  const events = [];
  const calls = { createClient: 0, auth: 0, membership: 0, dog: 0, recording: 0, points: 0 };
  const dal = createDogDataAccess({
    async createClient() { calls.createClient += 1; events.push("client"); return client; },
    async getFreshUserId(received) { assert.equal(received, client); calls.auth += 1; events.push("auth"); return userId; },
    async findMembership(received, receivedUser, receivedDog) {
      assert.equal(received, client); assert.equal(receivedUser, USER_ID); assert.equal(receivedDog, DOG_ID);
      calls.membership += 1; events.push("membership"); return membership;
    },
    async findDog(received, receivedDog) {
      assert.equal(received, client); assert.equal(receivedDog, DOG_ID);
      calls.dog += 1; events.push("dog"); return dog;
    },
    async findRecordingDetail(received, receivedDog, receivedRecording) {
      assert.equal(received, client); assert.equal(receivedDog, DOG_ID); assert.equal(receivedRecording, RECORDING_ID);
      calls.recording += 1; events.push("recording");
      if (recordingError) throw recordingError;
      return recording;
    },
    async listRecordingPoints(received, query, after) {
      assert.equal(received, client);
      assert.deepEqual(query, {
        collarId: COLLAR_ID,
        bootSequence: 7,
        firstPointSequence: RECORDING.first_point_sequence,
        lastPointSequence: RECORDING.last_point_sequence,
      });
      calls.points += 1; events.push(`points:${after ?? "first"}`);
      if (pointError) throw pointError;
      return points.filter((candidate) => after === null || candidate.point_sequence > after)
        .slice(0, RECORDING_POINT_QUERY_LIMIT);
    },
    async listMemberships() { return []; },
    async listDogs() { return []; },
    async findActiveCollar() { return null; },
    async findDailySummary() { return null; },
    async findLatestRecording() { return null; },
    async findRecordingSummary() { return null; },
    async listHistoryRecordings() { return []; },
    now() { events.push("now"); return new Date(NOW); },
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

test("recording detail uses one fresh authorized client and a deeply frozen bounded DTO", async () => {
  const points = Array.from({ length: 101 }, (_, index) => point(index + 1));
  const { calls, dal, events } = harness({ points });
  const result = await dal.getRecordingPage(DOG_ID, RECORDING_ID, undefined);

  assert.equal(result.status, "ready");
  assert.equal(result.points.length, 100);
  assert.equal(result.nextAfter, "100");
  assert.deepEqual(calls, { createClient: 1, auth: 1, membership: 1, dog: 1, recording: 1, points: 1 });
  assert.deepEqual(events, ["client", "auth", "membership", "dog", "now", "recording", "points:first"]);
  assert.equal(Object.isFrozen(result), true);
  assert.equal(Object.isFrozen(result.dog), true);
  assert.equal(Object.isFrozen(result.recording), true);
  assert.equal(Object.isFrozen(result.points), true);
  assert.equal(Object.isFrozen(result.points[0]), true);
  assert.equal(Object.isFrozen(result.points[0].flagLabels), true);
  assert.equal(Object.isFrozen(result.preview), true);
  assert.equal(Object.isFrozen(result.preview.segments), true);
});

test("route identifiers, authorization, and inaccessible recordings fail before point reads", async () => {
  const invalid = harness();
  await expectCode(invalid.dal.getRecordingPage("not-a-uuid", RECORDING_ID), "invalid_dog_id");
  await expectCode(invalid.dal.getRecordingPage(DOG_ID, "not-a-uuid"), "invalid_dog_id");
  assert.equal(invalid.calls.createClient, 0);

  const stale = harness({ userId: null });
  await expectCode(stale.dal.getRecordingPage(DOG_ID, RECORDING_ID), "authentication_required");
  assert.equal(stale.calls.membership, 0);

  const outsider = harness({ membership: null });
  await expectCode(outsider.dal.getRecordingPage(DOG_ID, RECORDING_ID), "access_denied");
  assert.equal(outsider.calls.recording, 0);

  for (const recording of [null, { ...RECORDING, dog_id: OTHER_DOG_ID }, { ...RECORDING, joined_collar_id: OTHER_COLLAR_ID }]) {
    const instance = harness({ recording });
    await expectCode(instance.dal.getRecordingPage(DOG_ID, RECORDING_ID), recording === null ? "access_denied" : "data_unavailable");
    assert.equal(instance.calls.points, 0);
  }
});

test("after accepts one canonical in-range uint32 and rejects every ambiguous form without a point query", async () => {
  const query = context().pointQuery;
  assert.deepEqual(parseRecordingAfter(undefined, query), { status: "valid", after: null });
  assert.deepEqual(parseRecordingAfter("1", query), { status: "valid", after: 1 });
  for (const value of ["", "01", "+1", " 1", "1 ", "1.0", "4294967296", ["1", "2"], 1, "500"]) {
    assert.deepEqual(parseRecordingAfter(value, query), { status: "invalid" });
    const instance = harness();
    const result = await instance.dal.getRecordingPage(DOG_ID, RECORDING_ID, value);
    assert.equal(result.status, "invalid_after");
    assert.equal(instance.calls.points, 0);
  }

  const retained = { ...RECORDING, first_point_sequence: null, last_point_sequence: null, point_count: 500 };
  const first = harness({ recording: retained, points: [] });
  const firstResult = await first.dal.getRecordingPage(DOG_ID, RECORDING_ID, undefined);
  assert.equal(firstResult.status, "ready");
  assert.equal(firstResult.points.length, 0);
  assert.equal(first.calls.points, 0);
  const after = harness({ recording: retained, points: [] });
  assert.equal((await after.dal.getRecordingPage(DOG_ID, RECORDING_ID, "1")).status, "invalid_after");
  assert.equal(after.calls.points, 0);
});

test("zero, fewer, exactly 100, and 101 rows have exact lookahead semantics", () => {
  for (const count of [0, 99, 100, 101]) {
    const result = page(Array.from({ length: count }, (_, index) => point(index + 1)));
    assert.equal(result.points.length, Math.min(count, 100));
    assert.equal(result.nextAfter, count === 101 ? "100" : null);
  }

  const firstRows = Array.from({ length: 101 }, (_, index) => point(index + 1));
  const secondRows = Array.from({ length: 101 }, (_, index) => point(index + 101));
  const first = page(firstRows);
  const second = page(secondRows, { after: Number(first.nextAfter) });
  assert.equal(first.points.at(-1).sequence, 100);
  assert.equal(second.points[0].sequence, 101);
  assert.equal(new Set([...first.points, ...second.points].map((item) => item.sequence)).size, 200);
});

test("segmentation connects through 60/65/equal/null evidence and breaks at every frozen discontinuity", () => {
  assert.equal(PLAIN_PREVIEW_TIME_GAP_SECONDS, 65);
  const at = (seconds) => new Date(Date.parse("2026-08-25T10:00:00.000Z") + seconds * 1_000).toISOString();
  const rows = [
    point(1, { recorded_at: at(0) }),
    point(2, { recorded_at: at(60) }),
    point(3, { recorded_at: at(125) }),
    point(4, { recorded_at: at(125) }),
    point(5, { recorded_at: at(191) }),
    point(6, { recorded_at: at(196), lat_e7: null, lon_e7: null, reported_speed_cmps: null, flags: TELEMETRY_FLAGS.TIME_TRUSTED | TELEMETRY_FLAGS.GAP }),
    point(7, { recorded_at: at(201) }),
    point(8, { recorded_at: at(206), lat_e7: null, lon_e7: null, reported_speed_cmps: null, flags: TELEMETRY_FLAGS.TIME_TRUSTED }),
    point(9, { recorded_at: at(211) }),
    point(11, { recorded_at: at(216) }),
    point(12, { recorded_at: null, time_quality: "unknown", flags: TELEMETRY_FLAGS.FIX_VALID }),
    point(13, { recorded_at: null, time_quality: "unknown", flags: TELEMETRY_FLAGS.FIX_VALID }),
    point(14, { recorded_at: at(221) }),
  ];
  const result = page(rows);
  assert.deepEqual(result.points.map(({ segmentNumber, continuity }) => [segmentNumber, continuity]), [
    [1, "page_boundary"], [1, "continues"], [1, "continues"], [1, "continues"],
    [2, "time_gap"], [null, "explicit_gap"], [3, "after_explicit_gap"],
    [null, "invalid_fix"], [4, "after_invalid_fix"], [5, "sequence_discontinuity"],
    [6, "time_discontinuity"], [6, "continues"], [7, "time_discontinuity"],
  ]);
  assert.equal(result.preview.segments.length, 7);
});

test("malformed, future, regressing, and flag-inconsistent point evidence fails closed", () => {
  const invalidRows = [
    [point(1, { recorded_at: "not-a-time" })],
    [point(1, { recorded_at: "2026-08-25T12:00:00.001Z" })],
    [point(1, { recorded_at: "2026-08-25T10:00:10.000Z" }), point(2, { recorded_at: "2026-08-25T10:00:09.000Z" })],
    [point(1, { flags: TELEMETRY_FLAGS.FIX_VALID })],
    [point(1, { time_quality: "unknown" })],
    [point(1, { flags: TELEMETRY_FLAGS.FIX_VALID | TELEMETRY_FLAGS.TIME_TRUSTED | TELEMETRY_FLAGS.MOVEMENT_EVIDENCE | TELEMETRY_FLAGS.STATIONARY_HEARTBEAT })],
    [point(1, { flags: TELEMETRY_FLAGS.FIX_VALID | TELEMETRY_FLAGS.TIME_TRUSTED | TELEMETRY_FLAGS.GAP })],
    [point(1, { lat_e7: null, lon_e7: null })],
    [point(1, { point_sequence: 0 })],
    [point(2), point(2)],
  ];
  for (const rows of invalidRows) {
    assert.throws(() => page(rows), RecordingDataValidationError);
  }
});

test("metadata accepts retained sparse truth and rejects malformed identities, pairs, times, and timezones", () => {
  const retained = context({ ...RECORDING, first_point_sequence: null, last_point_sequence: null, point_count: 500 });
  assert.equal(retained.pointQuery, null);
  assert.equal(retained.recording.pointCount, 500);
  for (const row of [
    { ...RECORDING, first_point_sequence: null },
    { ...RECORDING, first_point_sequence: 501, last_point_sequence: 500 },
    { ...RECORDING, timezone_at_start: "Mars/Olympus" },
    { ...RECORDING, started_at: "2026-02-30T10:00:00.000Z" },
    { ...RECORDING, ended_at: "2026-08-25T09:59:59.000Z" },
    { ...RECORDING, clock_quality: "unknown" },
    { ...RECORDING, state: "deleted" },
    { ...RECORDING, telemetry_schema: 0 },
    { ...RECORDING, firmware_version: "" },
  ]) assert.throws(() => context(row), RecordingDataValidationError);
});

test("preview handles one point, repeated positions, no positions, and ambiguous antimeridian evidence", () => {
  const single = page([point(1)]);
  assert.equal(single.preview.drawablePointCount, 1);
  assert.equal(single.preview.segments[0].points.length, 1);
  const repeated = page([point(1), point(2, { lat_e7: point(1).lat_e7, lon_e7: point(1).lon_e7 })]);
  assert.equal(repeated.preview.drawablePointCount, 2);
  const none = page([point(1, { lat_e7: null, lon_e7: null, reported_speed_cmps: null, flags: TELEMETRY_FLAGS.TIME_TRUSTED })]);
  assert.equal(none.preview, null);
  assert.equal(none.previewUnavailableReason, "no_drawable_points");
  const ambiguous = page([point(1, { lon_e7: -1_000_000_000 }), point(2, { lon_e7: 1_000_000_001 })]);
  assert.equal(ambiguous.preview, null);
  assert.equal(ambiguous.previewUnavailableReason, "antimeridian_ambiguous");
});

test("production adapter and server views freeze the exact M1.10 boundary", async () => {
  const [adapter, guard, route, view, history] = await Promise.all([
    readFile(new URL("./dogs.ts", import.meta.url), "utf8"),
    readFile(new URL("../auth/route-guard.ts", import.meta.url), "utf8"),
    readFile(new URL("../../app/app/[dogId]/recordings/[recordingId]/page.tsx", import.meta.url), "utf8"),
    readFile(new URL("../../app/components/recording-detail.tsx", import.meta.url), "utf8"),
    readFile(new URL("../../app/components/history-ledger.tsx", import.meta.url), "utf8"),
  ]);
  const pointQuery = adapter.slice(adapter.indexOf("async function listRecordingPoints"), adapter.indexOf("const dependencies"));
  assert.match(adapter, /collars!recordings_collar_id_fkey!inner/u);
  assert.match(adapter, /\.eq\("collar\.dog_id", dogId\)/u);
  assert.match(pointQuery, /point_sequence, recorded_at, lat_e7, lon_e7, reported_speed_cmps, satellites, flags, time_quality/u);
  assert.match(pointQuery, /\.gte\("point_sequence"/u);
  assert.match(pointQuery, /\.lte\("point_sequence"/u);
  assert.match(pointQuery, /\.gt\("point_sequence", after\)/u);
  assert.match(pointQuery, /\.limit\(RECORDING_POINT_QUERY_LIMIT\)/u);
  assert.doesNotMatch(pointQuery, /count|range\(|offset|select\([^)]*\*/iu);
  assert.match(guard, /getRecordingPage\(dogId, recordingId, after\)/u);
  assert.match(route, /await requireRecordingPage\(/u);
  assert.doesNotMatch(route, /requireDogPage|Array\.isArray/u);
  assert.match(view, /<caption>/u);
  assert.match(view, /role="region"/u);
  assert.match(view, /tabIndex=\{0\}/u);
  assert.match(view, /role="img"/u);
  assert.doesNotMatch(view, /"use client"|canvas|MapLibre|Realtime|setInterval|fetch\(|\.from\(/iu);
  assert.equal((history.match(/recordingAppPath\(/gu) ?? []).length, 1);
  assert.match(history, /prefetch=\{false\}/u);
});

test("recording and point read failures converge on the generic unavailable error", async () => {
  for (const options of [
    { recordingError: new DogDataAccessError("data_unavailable") },
    { pointError: new DogDataAccessError("data_unavailable") },
  ]) {
    const instance = harness(options);
    await expectCode(
      instance.dal.getRecordingPage(DOG_ID, RECORDING_ID),
      "data_unavailable",
    );
  }
});
