import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  createDogDataAccess,
  DogDataAccessError,
} from "./dogs-core.ts";
import {
  createHistoryPageDto,
  HISTORY_CURSOR_MAX_LENGTH,
  HISTORY_PAGE_SIZE,
  HISTORY_QUERY_LIMIT,
  HistoryDataValidationError,
  parseHistoryCursor,
} from "./history-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "20000000-0000-4000-8000-000000000001";
const OTHER_DOG_ID = "20000000-0000-4000-8000-000000000002";
const ACTIVE_COLLAR_ID = "30000000-0000-4000-8000-000000000001";
const REVOKED_COLLAR_ID = "30000000-0000-4000-8000-000000000002";
const NOW = new Date("2026-08-25T12:00:00.000Z");
const DOG = {
  id: DOG_ID,
  name: "Mora",
  timezone: "America/Bogota",
  role: "viewer",
};

function uuid(index) {
  return `40000000-0000-4000-8000-${String(index).padStart(12, "0")}`;
}

function cursor(payload) {
  return Buffer.from(JSON.stringify(payload), "utf8").toString("base64url");
}

function row(index, overrides = {}) {
  return {
    id: uuid(index),
    collar_id: ACTIVE_COLLAR_ID,
    joined_collar_id: ACTIVE_COLLAR_ID,
    dog_id: DOG_ID,
    collar_display_name: "Collar de Mora",
    started_at: new Date(NOW.getTime() - index * 60_000).toISOString(),
    state: "closed",
    point_count: index,
    clock_quality: "gnss_trusted",
    ...overrides,
  };
}

function compareRows(left, right) {
  if (left.started_at === null && right.started_at !== null) return 1;
  if (left.started_at !== null && right.started_at === null) return -1;
  if (left.started_at !== right.started_at) {
    return left.started_at > right.started_at ? -1 : 1;
  }
  return left.id > right.id ? -1 : left.id < right.id ? 1 : 0;
}

function follows(cursorValue, candidate) {
  if (!cursorValue) return true;
  if (cursorValue.bucket === "unknown") {
    return candidate.started_at === null && candidate.id < cursorValue.id;
  }
  return (
    candidate.started_at === null ||
    candidate.started_at < cursorValue.startedAt ||
    (candidate.started_at === cursorValue.startedAt &&
      candidate.id < cursorValue.id)
  );
}

function harness({
  userId = USER_ID,
  membership = { dog_id: DOG_ID, role: "viewer" },
  dog = { id: DOG_ID, name: "Mora", timezone: "America/Bogota" },
  rows = [],
  returnUnbounded = false,
  historyError = null,
} = {}) {
  const client = {};
  const events = [];
  const calls = {
    createClient: 0,
    getFreshUserId: 0,
    findMembership: 0,
    findDog: 0,
    listHistoryRecordings: 0,
  };

  const dal = createDogDataAccess({
    async createClient() {
      calls.createClient += 1;
      events.push("client");
      return client;
    },
    async getFreshUserId(received) {
      assert.equal(received, client);
      calls.getFreshUserId += 1;
      events.push("auth");
      return userId;
    },
    async findMembership(received, receivedUserId, receivedDogId) {
      assert.equal(received, client);
      assert.equal(receivedUserId, USER_ID);
      assert.equal(receivedDogId, DOG_ID);
      calls.findMembership += 1;
      events.push("membership");
      return membership;
    },
    async findDog(received, receivedDogId) {
      assert.equal(received, client);
      assert.equal(receivedDogId, DOG_ID);
      calls.findDog += 1;
      events.push("dog");
      return dog;
    },
    async listHistoryRecordings(received, receivedDogId, cursorValue) {
      assert.equal(received, client);
      assert.equal(receivedDogId, DOG_ID);
      calls.listHistoryRecordings += 1;
      events.push("recordings");
      if (historyError) throw historyError;
      const result = rows
        .filter((candidate) => follows(cursorValue, candidate))
        .toSorted(compareRows);
      return returnUnbounded ? result : result.slice(0, HISTORY_QUERY_LIMIT);
    },
    async listMemberships() { return []; },
    async listDogs() { return []; },
    async findActiveCollar() { return null; },
    async findDailySummary() { return null; },
    async findLatestRecording() { return null; },
    async findRecordingSummary() { return null; },
    now() {
      events.push("now");
      return new Date(NOW);
    },
  });

  return { calls, dal, events, rows };
}

async function expectCode(promise, code) {
  await assert.rejects(promise, (error) => {
    assert.ok(error instanceof DogDataAccessError);
    assert.equal(error.code, code);
    return true;
  });
}

test("History uses one fresh authorized client and returns a frozen minimal ledger", async () => {
  const rows = [
    row(1),
    row(2, {
      collar_id: REVOKED_COLLAR_ID,
      joined_collar_id: REVOKED_COLLAR_ID,
      collar_display_name: null,
      clock_quality: "approximate_persisted",
    }),
    row(3, {
      started_at: null,
      clock_quality: "unknown",
      state: "incomplete",
    }),
  ];
  const { calls, dal, events } = harness({ rows });
  const result = await dal.getHistoryPage(DOG_ID, undefined);

  assert.deepEqual(result, {
    status: "ready",
    dog: DOG,
    position: "first",
    recordings: [
      {
        id: uuid(1),
        startedAt: rows[0].started_at,
        timeQuality: "trusted",
        state: "closed",
        pointCount: 1,
        collarName: "Collar de Mora",
      },
      {
        id: uuid(2),
        startedAt: rows[1].started_at,
        timeQuality: "approximate",
        state: "closed",
        pointCount: 2,
        collarName: "Collar sin nombre",
      },
      {
        id: uuid(3),
        startedAt: null,
        timeQuality: "unknown",
        state: "incomplete",
        pointCount: 3,
        collarName: "Collar de Mora",
      },
    ],
    nextCursor: null,
  });
  assert.deepEqual(calls, {
    createClient: 1,
    getFreshUserId: 1,
    findMembership: 1,
    findDog: 1,
    listHistoryRecordings: 1,
  });
  assert.deepEqual(events, [
    "client",
    "auth",
    "membership",
    "dog",
    "now",
    "recordings",
  ]);
  assert.equal(Object.isFrozen(result), true);
  assert.equal(Object.isFrozen(result.dog), true);
  assert.equal(Object.isFrozen(result.recordings), true);
  result.recordings.forEach((recording) => {
    assert.equal(Object.isFrozen(recording), true);
    assert.deepEqual(Object.keys(recording).sort(), [
      "collarName",
      "id",
      "pointCount",
      "startedAt",
      "state",
      "timeQuality",
    ]);
  });
});

test("0, fewer than 20, exactly 20, and 21 rows obey the 20+1 boundary", async () => {
  for (const count of [0, 1, HISTORY_PAGE_SIZE - 1, HISTORY_PAGE_SIZE]) {
    const result = await harness({
      rows: Array.from({ length: count }, (_, index) => row(index + 1)),
    }).dal.getHistoryPage(DOG_ID, undefined);
    assert.equal(result.recordings.length, count);
    assert.equal(result.nextCursor, null);
  }

  const twentyOne = await harness({
    rows: Array.from({ length: HISTORY_QUERY_LIMIT }, (_, index) => row(index + 1)),
  }).dal.getHistoryPage(DOG_ID, undefined);
  assert.equal(twentyOne.recordings.length, HISTORY_PAGE_SIZE);
  assert.ok(twentyOne.nextCursor);
  const parsed = parseHistoryCursor(twentyOne.nextCursor, NOW);
  assert.deepEqual(parsed, {
    status: "valid",
    cursor: {
      bucket: "known",
      startedAt: twentyOne.recordings[19].startedAt,
      id: twentyOne.recordings[19].id,
    },
  });
  assert.notEqual(parsed.cursor.id, uuid(21));

  const exhaustedCursor = cursor({
    v: 1,
    bucket: "known",
    startedAt: "2026-08-01T00:00:00.000Z",
    id: uuid(999),
  });
  const exhausted = await harness({ rows: [row(1)] }).dal.getHistoryPage(
    DOG_ID,
    exhaustedCursor,
  );
  assert.equal(exhausted.position, "after_cursor");
  assert.deepEqual(exhausted.recordings, []);
  assert.equal(exhausted.nextCursor, null);
});

test("equal timestamps, known-to-null transition, and all-null pages preserve total order", async () => {
  const tiedAt = "2026-08-24T10:00:00.000Z";
  const mixedRows = [
    ...Array.from({ length: 21 }, (_, index) =>
      row(100 - index, { started_at: tiedAt }),
    ),
    ...Array.from({ length: 22 }, (_, index) =>
      row(79 - index, { started_at: null, clock_quality: "unknown" }),
    ),
  ].toSorted(compareRows);
  const instance = harness({ rows: mixedRows });
  const seen = [];
  let cursorValue;
  do {
    const page = await instance.dal.getHistoryPage(DOG_ID, cursorValue);
    seen.push(...page.recordings.map(({ id }) => id));
    cursorValue = page.nextCursor ?? undefined;
  } while (cursorValue);

  assert.deepEqual(seen, mixedRows.map(({ id }) => id));
  assert.equal(new Set(seen).size, mixedRows.length);
  assert.equal(instance.calls.listHistoryRecordings, 3);

  const nullRows = Array.from({ length: 25 }, (_, index) =>
    row(200 - index, { started_at: null, clock_quality: "unknown" }),
  ).toSorted(compareRows);
  const nullInstance = harness({ rows: nullRows });
  const first = await nullInstance.dal.getHistoryPage(DOG_ID, undefined);
  const parsed = parseHistoryCursor(first.nextCursor, NOW);
  assert.equal(parsed.cursor.bucket, "unknown");
  const second = await nullInstance.dal.getHistoryPage(DOG_ID, first.nextCursor);
  assert.deepEqual(
    [...first.recordings, ...second.recordings].map(({ id }) => id),
    nullRows.map(({ id }) => id),
  );
});

test("a newer insertion between requests cannot duplicate or skip original rows", async () => {
  const original = Array.from({ length: 41 }, (_, index) => row(index + 2));
  const originalIds = original.toSorted(compareRows).map(({ id }) => id);
  const instance = harness({ rows: original });
  const first = await instance.dal.getHistoryPage(DOG_ID, undefined);
  instance.rows.push(row(1));

  const seen = [...first.recordings.map(({ id }) => id)];
  let cursorValue = first.nextCursor;
  while (cursorValue) {
    const page = await instance.dal.getHistoryPage(DOG_ID, cursorValue);
    seen.push(...page.recordings.map(({ id }) => id));
    cursorValue = page.nextCursor;
  }

  assert.deepEqual(seen, originalIds);
  assert.equal(new Set(seen).size, originalIds.length);
  assert.equal(seen.includes(uuid(1)), false);
});

test("strict cursor decoding rejects malformed syntax before the recording query", async () => {
  const validKnown = {
    v: 1,
    bucket: "known",
    startedAt: "2026-08-24T10:00:00.000Z",
    id: uuid(1),
  };
  const invalid = [
    "",
    "a".repeat(HISTORY_CURSOR_MAX_LENGTH + 1),
    "not+base64url",
    [cursor(validKnown), cursor(validKnown)],
    cursor({ ...validKnown, v: 2 }),
    cursor({
      ...validKnown,
      id: validKnown.id.replace(/^4/u, "a").toUpperCase(),
    }),
    cursor({ ...validKnown, startedAt: "2026-02-30T10:00:00.000Z" }),
    cursor({ ...validKnown, startedAt: "2026-08-26T10:00:00.000Z" }),
    cursor({ v: 1, bucket: "known", id: uuid(1) }),
    cursor({ v: 1, bucket: "unknown", id: uuid(1), startedAt: null }),
    cursor({ ...validKnown, extra: true }),
    Buffer.from(`{ "v":1,"bucket":"unknown","id":"${uuid(1)}" }`).toString("base64url"),
    Buffer.from(`{"v":1,"v":1,"bucket":"unknown","id":"${uuid(1)}"}`).toString("base64url"),
  ];

  for (const input of invalid) {
    const instance = harness({ rows: [row(1)] });
    const result = await instance.dal.getHistoryPage(DOG_ID, input);
    assert.equal(result.status, "invalid_cursor");
    assert.equal(instance.calls.listHistoryRecordings, 0);
    assert.equal(Object.isFrozen(result.dog), true);
  }
});

test("authorization and inaccessible-dog failures happen before history reads", async () => {
  const stale = harness({ userId: null, rows: [row(1)] });
  await expectCode(
    stale.dal.getHistoryPage(DOG_ID, undefined),
    "authentication_required",
  );
  assert.equal(stale.calls.findMembership, 0);
  assert.equal(stale.calls.listHistoryRecordings, 0);

  const outsider = harness({ membership: null, rows: [row(1)] });
  await expectCode(
    outsider.dal.getHistoryPage(DOG_ID, undefined),
    "access_denied",
  );
  assert.equal(outsider.calls.findDog, 0);
  assert.equal(outsider.calls.listHistoryRecordings, 0);

  const missing = harness({ dog: null, rows: [row(1)] });
  await expectCode(
    missing.dal.getHistoryPage(DOG_ID, undefined),
    "access_denied",
  );
  assert.equal(missing.calls.listHistoryRecordings, 0);
});

test("malformed, future, cross-dog, cross-collar, and unordered rows fail closed", async () => {
  const invalidRows = [
    { ...row(1), id: "not-a-uuid" },
    { ...row(1), dog_id: OTHER_DOG_ID },
    { ...row(1), joined_collar_id: REVOKED_COLLAR_ID },
    { ...row(1), collar_display_name: "   " },
    { ...row(1), started_at: "2026-02-30T10:00:00.000Z" },
    { ...row(1), started_at: "2026-08-26T10:00:00.000Z" },
    { ...row(1), started_at: row(1).started_at, clock_quality: "unknown" },
    { ...row(1), state: "walk" },
    { ...row(1), point_count: -1 },
  ];

  for (const invalidRow of invalidRows) {
    await expectCode(
      harness({ rows: [invalidRow] }).dal.getHistoryPage(DOG_ID, undefined),
      "data_unavailable",
    );
  }

  assert.throws(
    () => createHistoryPageDto({
      dog: DOG,
      capturedAt: NOW,
      cursor: null,
      rows: [row(2), row(1)],
    }),
    HistoryDataValidationError,
  );
  await expectCode(
    harness({
      rows: Array.from({ length: HISTORY_QUERY_LIMIT + 1 }, (_, index) => row(index + 1)),
      returnUnbounded: true,
    }).dal.getHistoryPage(DOG_ID, undefined),
    "data_unavailable",
  );

  const invalidLookahead = [
    ...Array.from({ length: HISTORY_PAGE_SIZE }, (_, index) => row(index + 1)),
    row(21, { started_at: null, clock_quality: "unknown", point_count: -1 }),
  ];
  await expectCode(
    harness({ rows: invalidLookahead }).dal.getHistoryPage(DOG_ID, undefined),
    "data_unavailable",
  );

  await expectCode(
    harness({
      rows: [row(1)],
      historyError: new DogDataAccessError("data_unavailable"),
    }).dal.getHistoryPage(DOG_ID, undefined),
    "data_unavailable",
  );
});

test("production query is one explicit RLS-scoped 21-row keyset request", async () => {
  const [adapter, page, view] = await Promise.all([
    readFile(new URL("./dogs.ts", import.meta.url), "utf8"),
    readFile(
      new URL("../../app/app/[dogId]/history/page.tsx", import.meta.url),
      "utf8",
    ),
    readFile(
      new URL("../../app/components/history-ledger.tsx", import.meta.url),
      "utf8",
    ),
  ]);

  assert.match(adapter, /^import "server-only";/u);
  assert.match(
    adapter,
    /id, collar_id, started_at, state, point_count, clock_quality, collar:collars!recordings_collar_id_fkey!inner\(id, dog_id, display_name\)/u,
  );
  assert.match(adapter, /\.eq\("collar\.dog_id", dogId\)/u);
  assert.match(adapter, /started_at\.lt\.\$\{cursor\.startedAt\}/u);
  assert.match(adapter, /started_at\.eq\.\$\{cursor\.startedAt\},id\.lt\.\$\{cursor\.id\}/u);
  assert.match(adapter, /started_at\.is\.null/u);
  assert.match(adapter, /\.is\("started_at", null\)\.lt\("id", cursor\.id\)/u);
  assert.match(adapter, /\.order\("started_at", \{ ascending: false, nullsFirst: false \}\)/u);
  assert.match(adapter, /\.order\("id", \{ ascending: false \}\)/u);
  assert.match(adapter, /\.limit\(HISTORY_QUERY_LIMIT\)/u);
  const historyQuery = adapter.slice(
    adapter.indexOf("async function listHistoryRecordings"),
    adapter.indexOf("const dependencies"),
  );
  assert.doesNotMatch(
    historyQuery,
    /\.range\(|\boffset\b|count\s*:\s*["']|telemetry_points|recording_summaries/iu,
  );

  assert.match(page, /await requireHistoryPage\(/u);
  assert.doesNotMatch(page, /requireDogPage|getDogSummary/u);
  assert.match(view, /<ol className="history-list" role="list">/u);
  assert.match(view, /<dl className="history-facts">/u);
  assert.match(view, /<time dateTime=/u);
  assert.match(view, /aria-label="Paginación del historial"/u);
  assert.match(view, /prefetch=\{false\}/u);
  assert.doesNotMatch(view, /"use client"|Realtime|\.from\(|recordingAppPath|paseo|duración|cobertura/iu);
});
