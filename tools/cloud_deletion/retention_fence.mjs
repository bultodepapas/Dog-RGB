import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";

const DATABASE_FILTER = "label=com.supabase.cli.project=Dog-RGB-1";
const DATABASE_NAME_FILTER = "name=^/supabase_db_Dog-RGB-1$";
const API_URL = process.env.SUPABASE_URL ?? "http://127.0.0.1:56321";
const PUBLISHABLE_KEY = process.env.SUPABASE_PUBLISHABLE_KEY;
const OWNER_EMAIL = "owner@example.test";
const OWNER_PASSWORD = "local-owner-password";
const SESSION_TIMEOUT_MS = 12_000;
const POLL_MS = 25;
const RETENTION_HOLD_SECONDS = 4;
const MAX_DELETION_BATCHES = 8;

if (!PUBLISHABLE_KEY) throw new Error("SUPABASE_PUBLISHABLE_KEY is required.");
if (process.argv.length > 2) throw new Error("Unknown arguments.");

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;

function sqlUuid(value) {
  if (!UUID_PATTERN.test(value)) throw new Error("Unsafe generated UUID.");
  return `'${value}'::uuid`;
}

function databaseContainer() {
  const result = spawnSync("docker", [
    "ps", "--filter", DATABASE_FILTER, "--filter", DATABASE_NAME_FILTER, "--format", "{{.ID}}",
  ], { encoding: "utf8" });
  if (result.status !== 0) throw new Error("Unable to locate the local Dog-RGB database.");
  const ids = result.stdout.trim().split(/\s+/u).filter(Boolean);
  if (ids.length !== 1) throw new Error("Expected this repository's local Supabase database.");
  return ids[0];
}

function psql(container, sql) {
  const result = spawnSync("docker", [
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], { encoding: "utf8", input: sql, timeout: 20_000 });
  if (result.status !== 0) throw new Error("Local deletion retention-fence SQL failed.");
  return result.stdout.trim();
}

function psqlSession(container) {
  const child = spawn("docker", [
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], { stdio: ["pipe", "pipe", "pipe"] });
  const session = { child, stdout: "", closed: false, status: null };
  child.stdout.setEncoding("utf8");
  child.stderr.resume();
  child.stdout.on("data", (chunk) => { session.stdout += chunk; });
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
    if (session.closed) throw new Error("The retention session ended unexpectedly.");
    if (Date.now() >= deadline) throw new Error("Timed out waiting for the retention transaction.");
    await new Promise((resolve) => setTimeout(resolve, POLL_MS));
  }
}

async function api(path, { method = "POST", token, body } = {}) {
  const headers = { apikey: PUBLISHABLE_KEY, accept: "application/json" };
  if (token) headers.authorization = `Bearer ${token}`;
  if (body !== undefined) headers["content-type"] = "application/json";
  const response = await fetch(`${API_URL}${path}`, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(15_000),
  });
  const raw = await response.text();
  let payload = null;
  if (raw) {
    try { payload = JSON.parse(raw); } catch { throw new Error("The local API returned a non-JSON response."); }
  }
  return { status: response.status, payload };
}

async function ownerAccessToken() {
  const result = await api("/auth/v1/token?grant_type=password", {
    body: { email: OWNER_EMAIL, password: OWNER_PASSWORD },
  });
  if (result.status !== 200 || typeof result.payload?.access_token !== "string") {
    throw new Error("The local deletion owner could not sign in.");
  }
  return result.payload.access_token;
}

function scalar(container, sql) {
  return psql(container, sql);
}

function json(container, sql) {
  const result = scalar(container, sql);
  if (!result) throw new Error("Local deletion status query returned no row.");
  try { return JSON.parse(result); } catch { throw new Error("Local deletion status query returned invalid JSON."); }
}

function advisoryKeySql(collarId) {
  return `pg_catalog.hashtextextended('dog-rgb:telemetry:' || ${sqlUuid(collarId)}::text, 0)`;
}

async function waitForDeletionToWait(container, collarId, hasSettled) {
  const key = advisoryKeySql(collarId);
  const deadline = Date.now() + SESSION_TIMEOUT_MS;
  while (Date.now() < deadline) {
    if (hasSettled()) return false;
    const waiting = scalar(container, `
      select exists (
        select 1 from pg_locks lock_row
        where lock_row.locktype = 'advisory'
          and lock_row.pid <> pg_backend_pid()
          and not lock_row.granted
          and lock_row.classid = ((${key} >> 32) & 4294967295)::oid
          and lock_row.objid = (${key} & 4294967295)::oid
          and lock_row.objsubid = 1
      );
    `);
    if (waiting === "t") return true;
    await new Promise((resolve) => setTimeout(resolve, POLL_MS));
  }
  return false;
}

function seedRaceFixture(container, ids, ownerId) {
  psql(container, `
    begin;
    do $$ begin
      if exists (
        select 1 from private.retention_jobs
        where status in ('pending', 'processing', 'failed')
          and next_attempt_at <= statement_timestamp()
      ) then
        raise exception 'retention_fence_fixture_requires_idle_queue';
      end if;
    end $$;
    insert into api.dogs (id, name, timezone, created_by)
    values (${sqlUuid(ids.dog)}, 'Retention fence race fixture', 'UTC', ${sqlUuid(ownerId)});
    insert into api.dog_memberships (dog_id, user_id, role)
    values (${sqlUuid(ids.dog)}, ${sqlUuid(ownerId)}, 'owner');
    insert into api.collars (id, device_public_id, dog_id, state, revoked_at)
    values (${sqlUuid(ids.collar)}, ${sqlUuid(ids.device)}, ${sqlUuid(ids.dog)}, 'revoked', statement_timestamp());
    insert into api.telemetry_points (
      collar_id, boot_sequence, point_sequence, recorded_at, lat_e7, lon_e7,
      reported_speed_cmps, satellites, flags, time_quality, telemetry_schema,
      firmware_version, chunk_sequence
    ) values (
      ${sqlUuid(ids.collar)}, 1, 1, statement_timestamp() - interval '400 days',
      400000000, -740000000, 0, 8, 13, 'gnss_trusted', 3, 'm56d-retention-fence', 0
    );
    insert into private.retention_jobs (
      data_class, collar_id, cutoff, requested_at, next_attempt_at
    ) values (
      'raw_telemetry_v1', ${sqlUuid(ids.collar)},
      pg_catalog.date_trunc('day', statement_timestamp(), 'UTC') - interval '1 year',
      statement_timestamp(), statement_timestamp()
    );
    commit;
  `);
}

function runDeletionBatch(container, batchSize) {
  return json(container, `
    begin;
    set local role service_role;
    select private.process_dog_deletion_batch_v1(${batchSize})::text;
    commit;
  `);
}

function cleanup(container, dogId) {
  if (!dogId) return;
  const safeId = sqlUuid(dogId);
  psql(container, `
    begin;
    delete from private.deletion_receipts receipt
    using private.deletion_jobs job, private.deletion_tombstones tombstone
    where receipt.job_id = job.id and job.tombstone_id = tombstone.id
      and tombstone.scope = 'dog' and tombstone.scope_id = ${safeId};
    delete from private.deletion_jobs job
    using private.deletion_tombstones tombstone
    where job.tombstone_id = tombstone.id
      and tombstone.scope = 'dog' and tombstone.scope_id = ${safeId};
    delete from private.deletion_tombstones
    where scope = 'dog' and scope_id = ${safeId};
    delete from api.dogs where id = ${safeId};
    commit;
  `);
}

const container = databaseContainer();
const ids = { dog: randomUUID(), collar: randomUUID(), device: randomUUID() };
let dogSeeded = false;
let retention = null;
let deletionSettled = false;
let deletionPromise = null;
const counts = { retentionPointsDeleted: 0, pointsAtDeletionSnapshot: 0, deletionBatches: 0, remainingDogRows: 0 };

try {
  const ownerToken = await ownerAccessToken();
  const ownerId = scalar(container, `
    select id::text from auth.users where email = '${OWNER_EMAIL}' and deleted_at is null;
  `);
  if (!UUID_PATTERN.test(ownerId)) throw new Error("The local deletion owner identity is unavailable.");

  seedRaceFixture(container, ids, ownerId);
  dogSeeded = true;

  retention = psqlSession(container);
  retention.child.stdin.write(`
    set application_name = 'm56d_retention_fence_${ids.dog.replaceAll("-", "")}';
    begin;
    set local role service_role;
    select private.process_raw_telemetry_retention_batch_v1(100)::text;
    \\echo M56D_RETENTION_BATCH_FINISHED
    select pg_sleep(${RETENTION_HOLD_SECONDS});
    commit;
    \\echo M56D_RETENTION_COMMITTED
    \\q
  `);
  await waitForOutput(retention, "M56D_RETENTION_BATCH_FINISHED");
  const retentionResultLine = retention.stdout.split(/\r?\n/u).find((line) => line.startsWith("{"));
  if (!retentionResultLine) throw new Error("The retention worker returned no batch result.");
  const retentionResult = JSON.parse(retentionResultLine);
  assert.equal(retentionResult.collar_id, ids.collar, "the retention worker selected another collar");
  assert.equal(retentionResult.status, "pending", "the bounded retention batch did not leave the next stage pending");
  counts.retentionPointsDeleted = retentionResult.batch_points_deleted;
  assert.equal(counts.retentionPointsDeleted, 1, "retention did not delete the fixture point while uncommitted");

  const requestBody = {
    p_dog_id: ids.dog,
    p_request_id: randomUUID(),
    p_confirmation_version: "dog-delete-v1",
  };

  deletionPromise = api("/rest/v1/rpc/request_dog_deletion_v1", {
    token: ownerToken,
    body: requestBody,
  }).then((result) => {
    deletionSettled = true;
    return result;
  }).catch((error) => {
    deletionSettled = true;
    return null;
  });

  const waitedOnFence = await waitForDeletionToWait(container, ids.collar, () => deletionSettled);
  assert.equal(waitedOnFence, true, "the deletion request never waited for the retention transaction");
  assert.equal(deletionSettled, false, "the deletion request completed before the retention transaction committed");
  assert.equal(Number(scalar(container, `
    select count(*) from private.deletion_tombstones
    where scope = 'dog' and scope_id = ${sqlUuid(ids.dog)};
  `)), 0, "the deletion request installed a tombstone before the retention fence opened");

  await waitForOutput(retention, "M56D_RETENTION_COMMITTED");
  const retentionStatus = await retention.closedPromise;
  if (retentionStatus !== 0) throw new Error("The held retention transaction failed.");
  retention = null;

  const result = await deletionPromise;
  if (!result || result.status !== 200 || !result.payload?.job_id) {
    throw new Error("The owner deletion request failed after the retention commit.");
  }
  const jobState = json(container, `
    select jsonb_build_object(
      'initial_points', job.initial_counts -> 'telemetry_points',
      'retention_receipts', job.initial_counts -> 'retention_receipts',
      'retention_jobs', job.initial_counts -> 'retention_jobs'
    )::text
    from private.deletion_jobs job
    join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
    where tombstone.scope_id = ${sqlUuid(ids.dog)};
  `);
  counts.pointsAtDeletionSnapshot = Number(jobState.initial_points);
  assert.equal(counts.pointsAtDeletionSnapshot, 0, "the deletion inventory included the point retention just committed");
  assert.equal(Number(jobState.retention_receipts), 0, "the deletion inventory included a receipt the bounded retention job has not created");
  assert.equal(Number(jobState.retention_jobs), 1, "the deletion inventory omitted the committed retention job");

  let deletionState = null;
  for (let batch = 0; batch < MAX_DELETION_BATCHES; batch += 1) {
    deletionState = runDeletionBatch(container, 100);
    counts.deletionBatches += 1;
    if (deletionState.status === "completed") break;
    assert.equal(deletionState.status, "pending", "the deletion worker entered a retryable mismatch state");
  }
  assert.equal(deletionState?.status, "completed", "the bounded deletion worker did not complete");
  counts.remainingDogRows = Number(scalar(container, `
    select coalesce(sum(value::bigint), 0)
    from jsonb_each_text(private.dog_deletion_counts_v1(${sqlUuid(ids.dog)}));
  `));
  assert.equal(counts.remainingDogRows, 0, "dog-scoped data remains after the fenced deletion");

  console.log(JSON.stringify({
    test: "M56d two-session retention/deletion fence passed",
    concurrentSessions: 2,
    retentionPointsDeleted: counts.retentionPointsDeleted,
    pointsAtDeletionSnapshot: counts.pointsAtDeletionSnapshot,
    deletionBatches: counts.deletionBatches,
    remainingDogRows: counts.remainingDogRows,
  }));
} finally {
  if (retention && !retention.closed) {
    retention.child.stdin.write("rollback;\n\\q\n");
    await Promise.race([
      retention.closedPromise,
      new Promise((resolve) => setTimeout(resolve, SESSION_TIMEOUT_MS)),
    ]);
    if (!retention.closed) retention.child.kill("SIGTERM");
  }
  if (deletionPromise && !deletionSettled) await Promise.race([
    deletionPromise.catch(() => undefined),
    new Promise((resolve) => setTimeout(resolve, SESSION_TIMEOUT_MS)),
  ]);
  if (dogSeeded) cleanup(container, ids.dog);
}
