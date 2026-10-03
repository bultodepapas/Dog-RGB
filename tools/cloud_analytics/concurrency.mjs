import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";

const OWNER_ID = "10000000-0000-4000-8000-000000000001";
const DATABASE_FILTER = "label=com.supabase.cli.project=Dog-RGB-1";
const DATABASE_NAME_FILTER = "name=^/supabase_db_Dog-RGB-1$";
const POLL_MS = 20;
const SESSION_TIMEOUT_MS = 8000;

function databaseContainer() {
  const result = spawnSync("docker", [
    "ps", "--filter", DATABASE_FILTER, "--filter", DATABASE_NAME_FILTER,
    "--format", "{{.ID}}",
  ], { encoding: "utf8" });
  if (result.status !== 0) {
    throw new Error(`Unable to list the local Dog-RGB database: ${result.stderr.trim()}`);
  }
  const ids = result.stdout.trim().split(/\s+/u).filter(Boolean);
  if (ids.length !== 1) throw new Error("Expected this repository's local Supabase database.");
  return ids[0];
}

function sqlUuid(value) {
  if (!/^[0-9a-f-]{36}$/u.test(value)) throw new Error("Unsafe generated UUID.");
  return `'${value}'::uuid`;
}

function psql(container, sql) {
  const result = spawnSync("docker", [
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], { encoding: "utf8", input: sql, timeout: 20_000 });
  if (result.status !== 0) {
    throw new Error(`Local database command failed: ${(result.stderr || result.stdout).trim()}`);
  }
  return result.stdout.trim();
}

function psqlSession(container) {
  const child = spawn("docker", [
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], { stdio: ["pipe", "pipe", "pipe"] });
  const session = { child, stdout: "", stderr: "", closed: false, status: null };
  child.stdout.setEncoding("utf8");
  child.stderr.setEncoding("utf8");
  child.stdout.on("data", (chunk) => { session.stdout += chunk; });
  child.stderr.on("data", (chunk) => { session.stderr += chunk; });
  session.closedPromise = new Promise((resolve, reject) => {
    child.on("error", reject);
    child.on("close", (status) => {
      session.closed = true;
      session.status = status;
      resolve(status);
    });
  });
  return session;
}

async function waitForOutput(session, marker) {
  const deadline = Date.now() + SESSION_TIMEOUT_MS;
  while (!session.stdout.includes(marker)) {
    if (session.closed) {
      throw new Error(`Database session ended before ${marker}: ${session.stderr.trim()}`);
    }
    if (Date.now() >= deadline) throw new Error(`Timed out waiting for ${marker}.`);
    await new Promise((resolve) => setTimeout(resolve, POLL_MS));
  }
}

function countForDog(container, table, dogId) {
  const allowed = new Map([
    ["private.dirty_summary_days", "dog_id"],
    ["api.telemetry_points", "collar_id"],
    ["api.recording_summaries", "recording_id"],
  ]);
  if (!allowed.has(table)) throw new Error("Unsupported artifact count table.");
  const column = allowed.get(table);
  const where = column === "dog_id"
    ? `${column} = ${sqlUuid(dogId)}`
    : column === "collar_id"
      ? `${column} in (select id from api.collars where dog_id = ${sqlUuid(dogId)})`
      : `${column} in (select recording.id from api.recordings recording join api.collars collar on collar.id = recording.collar_id where collar.dog_id = ${sqlUuid(dogId)})`;
  const output = psql(container, `select count(*)::integer from ${table} where ${where};`);
  const count = Number(output);
  if (!Number.isSafeInteger(count) || count < 0) throw new Error("Invalid local artifact count.");
  return count;
}

function runWorker(container) {
  const output = psql(container, `
    begin;
    set local statement_timeout = '10s';
    set local role service_role;
    select private.recompute_dirty_summaries_v1(4);
    commit;
  `);
  const consumed = Number(output.split(/\s+/u).at(-1));
  if (!Number.isSafeInteger(consumed) || consumed < 0) {
    throw new Error("The bounded summary worker returned an invalid count.");
  }
  return consumed;
}

function seedFixture(container, ids) {
  psql(container, `
    begin;
    insert into api.dogs (id, name, timezone, created_by)
    values (${sqlUuid(ids.dog)}, 'Local summary race fixture', 'UTC', ${sqlUuid(OWNER_ID)});
    insert into api.dog_memberships (dog_id, user_id, role)
    values (${sqlUuid(ids.dog)}, ${sqlUuid(OWNER_ID)}, 'owner');
    insert into api.collars (id, device_public_id, dog_id, state)
    values (${sqlUuid(ids.collar)}, ${sqlUuid(ids.device)}, ${sqlUuid(ids.dog)}, 'active');
    insert into api.recordings (
      id, collar_id, boot_sequence, started_at, timezone_at_start, state,
      first_point_sequence, last_point_sequence, point_count,
      clock_quality, telemetry_schema, firmware_version
    ) values (
      ${sqlUuid(ids.recording)}, ${sqlUuid(ids.collar)}, 1,
      statement_timestamp() - interval '30 seconds', 'UTC', 'open',
      1, 1, 1, 'gnss_trusted', 3, 'm4-race-fixture'
    );
    insert into api.telemetry_points (
      collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
      reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
      firmware_version, chunk_sequence
    ) values (
      ${sqlUuid(ids.collar)}, 1, 1, statement_timestamp() - interval '10 seconds',
      400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'm4-race-fixture', 1
    );
    commit;
  `);
}

function finalArtifactState(container, ids) {
  return JSON.parse(psql(container, `
    select jsonb_build_object(
      'points', (select count(*) from api.telemetry_points where collar_id = ${sqlUuid(ids.collar)}),
      'dirty_days', (select count(*) from private.dirty_summary_days where dog_id = ${sqlUuid(ids.dog)}),
      'recording_summaries', (select count(*) from api.recording_summaries where recording_id = ${sqlUuid(ids.recording)} and algorithm_version = 1),
      'valid_points', (select valid_points from api.recording_summaries where recording_id = ${sqlUuid(ids.recording)} and algorithm_version = 1)
    )::text;
  `));
}

const container = databaseContainer();
const ids = {
  dog: randomUUID(),
  collar: randomUUID(),
  device: randomUUID(),
  recording: randomUUID(),
};
let ingestion = null;

try {
  seedFixture(container, ids);
  runWorker(container);
  assert.equal(countForDog(container, "private.dirty_summary_days", ids.dog), 0,
    "initial source must have a fresh summary before the race");
  assert.equal(countForDog(container, "api.recording_summaries", ids.dog), 1,
    "initial recording summary must exist before the race");

  // Requeue the already-summarized day without changing its source watermark.
  // This gives the two concurrent sessions one known row to contend over.
  psql(container, `
    insert into private.dirty_summary_days (dog_id, local_date, timezone, reason)
    values (
      ${sqlUuid(ids.dog)}, (statement_timestamp() at time zone 'UTC')::date,
      'UTC', 'm4_race_barrier'
    );
  `);

  ingestion = psqlSession(container);
  ingestion.child.stdin.write(`
    set application_name = 'm4_analytics_ingest_${ids.dog.replaceAll("-", "")}';
    begin;
    select 1 from private.dirty_summary_days
    where dog_id = ${sqlUuid(ids.dog)} for update;
    \\echo M4_INGESTION_BARRIER_READY
  `);
  await waitForOutput(ingestion, "M4_INGESTION_BARRIER_READY");
  assert.equal(countForDog(container, "private.dirty_summary_days", ids.dog), 1,
    "the ingestion transaction must hold the existing dirty mark");

  // The real worker runs while the ingestion transaction owns the dirty row.
  // Its FOR UPDATE SKIP LOCKED query must leave this work available.
  runWorker(container);
  assert.equal(countForDog(container, "private.dirty_summary_days", ids.dog), 1,
    "the worker must skip rather than consume an ingestion-locked dirty mark");

  ingestion.child.stdin.write(`
    insert into api.telemetry_points (
      collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
      reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
      firmware_version, chunk_sequence
    ) values (
      ${sqlUuid(ids.collar)}, 1, 2, statement_timestamp() - interval '5 seconds',
      400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'm4-race-fixture', 1
    );
    commit;
    \\echo M4_INGESTION_COMMITTED
  `);
  await waitForOutput(ingestion, "M4_INGESTION_COMMITTED");
  ingestion.child.stdin.write("\\q\n");
  const status = await ingestion.closedPromise;
  if (status !== 0) throw new Error(`Ingestion session failed: ${ingestion.stderr.trim()}`);
  ingestion = null;

  assert.equal(countForDog(container, "api.telemetry_points", ids.dog), 2,
    "the concurrent ingestion must commit both source points");
  assert.equal(countForDog(container, "private.dirty_summary_days", ids.dog), 1,
    "the dirty mark must remain after ingestion commits");

  for (let batch = 0; batch < 8; batch += 1) {
    runWorker(container);
    if (countForDog(container, "private.dirty_summary_days", ids.dog) === 0) break;
  }
  const state = finalArtifactState(container, ids);
  assert.deepEqual(state, {
    points: 2,
    dirty_days: 0,
    recording_summaries: 1,
    valid_points: 2,
  }, "the next worker must consume the retained mark and summarize both points");
  console.log(JSON.stringify({
    test: "M4 bounded worker ingestion race passed",
    concurrentSessions: 2,
    sourcePoints: state.points,
    dirtyDaysAfterDrain: state.dirty_days,
    recordingSummaries: state.recording_summaries,
    summarizedPoints: state.valid_points,
  }));
} finally {
  if (ingestion && !ingestion.closed) {
    ingestion.child.stdin.write("rollback;\\q\n");
    await Promise.race([
      ingestion.closedPromise,
      new Promise((resolve) => setTimeout(resolve, SESSION_TIMEOUT_MS)),
    ]);
    if (!ingestion.closed) ingestion.child.kill("SIGTERM");
  }
  psql(container, `delete from api.dogs where id = ${sqlUuid(ids.dog)};`);
}
