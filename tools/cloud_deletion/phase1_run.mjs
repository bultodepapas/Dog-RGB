import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const EVIDENCE_PATH = resolve(ROOT, "test-results", "deletion", "phase1-local.json");
const DOG_ID = "30000000-0000-4000-8000-000000000003";
const BATCH_SIZE = 2;
const MAX_BATCHES = 64;
const apiUrl = process.env.SUPABASE_URL ?? "http://127.0.0.1:56321";
const publishableKey = process.env.SUPABASE_PUBLISHABLE_KEY;

if (!publishableKey) {
  throw new Error("SUPABASE_PUBLISHABLE_KEY is required (use `supabase status -o env`).");
}
if (process.argv.length > 2) {
  throw new Error(`Unknown arguments: ${process.argv.slice(2).join(", ")}`);
}

async function api(path, { method = "POST", token, body } = {}) {
  const raw = body === undefined ? undefined : JSON.stringify(body);
  const headers = {
    apikey: publishableKey,
    accept: "application/json",
  };
  if (token) headers.authorization = `Bearer ${token}`;
  if (raw !== undefined) {
    headers["content-type"] = "application/json";
    headers["content-length"] = String(Buffer.byteLength(raw));
  }
  const response = await fetch(`${apiUrl}${path}`, {
    method,
    headers,
    body: raw,
    signal: AbortSignal.timeout(15_000),
  });
  const text = await response.text();
  let parsed;
  try {
    parsed = text === "" ? null : JSON.parse(text);
  } catch {
    throw new Error(`HTTP ${response.status} returned non-JSON data.`);
  }
  return { response, body: parsed };
}

async function login(email, password) {
  const result = await api("/auth/v1/token?grant_type=password", {
    body: { email, password },
  });
  assert.equal(result.response.ok, true, `Authentication failed with HTTP ${result.response.status}.`);
  assert.equal(typeof result.body?.access_token, "string");
  return result.body.access_token;
}

function docker(args, options = {}) {
  const result = spawnSync(process.platform === "win32" ? "docker.exe" : "docker", args, {
    cwd: ROOT,
    encoding: "utf8",
    maxBuffer: 4 * 1024 * 1024,
    ...options,
  });
  if (result.status !== 0) {
    if (result.stdout) process.stdout.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    throw new Error(`docker ${args.join(" ")} failed with status ${result.status ?? result.error?.code ?? "unknown"}.`);
  }
  return result.stdout.trim();
}

function locateDatabaseContainer() {
  const containers = docker([
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ]).split(/\r?\n/).filter(Boolean);
  if (containers.length !== 1) {
    throw new Error(`Expected one local Dog-RGB database container; found ${containers.length}.`);
  }
  return containers[0];
}

function runWorkerBatch(container) {
  const sql = `
    begin;
    set local role service_role;
    select private.process_dog_deletion_batch_v1(${BATCH_SIZE})::text;
    commit;
  `;
  const output = docker([
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], { input: sql });
  try {
    return JSON.parse(output);
  } catch {
    throw new Error("Deletion worker did not return one valid JSON object.");
  }
}

const evidence = {
  status: "failed",
  request_transport: "authenticated_postgrest_rpc",
  exact_requests_sent_concurrently: 2,
  worker_batch_size: BATCH_SIZE,
  coordinate_payload_persisted: false,
  scheduler_enabled: false,
};
const startedAt = Date.now();

try {
  const [ownerToken, outsiderToken] = await Promise.all([
    login("owner@example.test", "local-owner-password"),
    login("other@example.test", "local-other-password"),
  ]);
  const requestId = randomUUID();
  const requestBody = {
    p_dog_id: DOG_ID,
    p_request_id: requestId,
    p_confirmation_version: "dog-delete-v1",
  };
  const sendDeletion = () => api("/rest/v1/rpc/request_dog_deletion_v1", {
    token: ownerToken,
    body: requestBody,
  });

  const [first, replay] = await Promise.all([sendDeletion(), sendDeletion()]);
  assert.equal(first.response.ok, true, `First deletion request failed with HTTP ${first.response.status}.`);
  assert.equal(replay.response.ok, true, `Concurrent replay failed with HTTP ${replay.response.status}.`);
  assert.equal(first.body.job_id, replay.body.job_id, "Concurrent replay returned another job.");
  assert.equal(first.body.tombstone_sha256, replay.body.tombstone_sha256);
  assert.equal(first.body.status, "pending");

  const statusPath = "/rest/v1/rpc/get_deletion_job_v1";
  const outsider = await api(statusPath, {
    token: outsiderToken,
    body: { p_job_id: first.body.job_id },
  });
  assert.equal(outsider.response.status, 403, "Another account must not inspect deletion status.");
  assert.equal(outsider.body?.message, "not_authorized");

  const [hiddenDog, hiddenRoute] = await Promise.all([
    api(`/rest/v1/dogs?select=id&id=eq.${DOG_ID}`, { method: "GET", token: ownerToken }),
    api("/rest/v1/telemetry_points?select=collar_id&limit=1", { method: "GET", token: ownerToken }),
  ]);
  assert.equal(hiddenDog.response.ok, true);
  assert.deepEqual(hiddenDog.body, [], "A deleting dog remains visible through RLS.");
  assert.equal(hiddenRoute.response.ok, true);
  assert.deepEqual(hiddenRoute.body, [], "Deleting route data remains visible through RLS.");

  const container = locateDatabaseContainer();
  let workerResult;
  let batches = 0;
  for (; batches < MAX_BATCHES; batches += 1) {
    workerResult = runWorkerBatch(container);
    assert.notEqual(workerResult.disposition, "idle", "Worker became idle before completion.");
    if (workerResult.status === "completed") break;
    assert.equal(workerResult.status, "pending");
  }
  assert.equal(workerResult?.status, "completed", `Deletion exceeded ${MAX_BATCHES} batches.`);

  const finalReplay = await sendDeletion();
  assert.equal(finalReplay.response.ok, true);
  assert.equal(finalReplay.body.job_id, first.body.job_id);
  assert.equal(finalReplay.body.status, "completed");
  assert.equal(typeof finalReplay.body.receipt_sha256, "string");

  evidence.status = "passed";
  evidence.worker_batches = batches + 1;
  evidence.telemetry_points_deleted = finalReplay.body.telemetry_points_deleted;
  evidence.owner_visible_after_request = { dogs: 0, telemetry_points: 0 };
  evidence.outsider_status_http = outsider.response.status;
  evidence.tombstone_hash_present = typeof finalReplay.body.tombstone_sha256 === "string";
  evidence.receipt_hash_present = true;
  console.log(
    `Concurrent owner deletion replay converged; ${evidence.worker_batches} bounded batches completed.`,
  );
} catch (error) {
  evidence.error = error instanceof Error ? error.message : String(error);
  throw error;
} finally {
  evidence.total_duration_ms = Date.now() - startedAt;
  mkdirSync(dirname(EVIDENCE_PATH), { recursive: true });
  writeFileSync(EVIDENCE_PATH, `${JSON.stringify(evidence, null, 2)}\n`, "utf8");
  console.log(`Coordinate-free deletion evidence written to ${EVIDENCE_PATH}.`);
}
