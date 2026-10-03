import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { authorizationPassword } from "./authorization-fixtures.mjs";

export const DOG_LIFECYCLE_CHECKPOINTS = Object.freeze([
  "owner-confirmation-and-password-request",
  "export-denied-immediately-after-request",
  "pending-status-survives-logout-login",
  "worker-purges-dog-and-status-is-durable",
  "deleted-dog-data-absent-and-other-dog-survives",
]);

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const DB_CONTAINER = "supabase_db_Dog-RGB-1";
const MAX_WORKER_BATCHES = 10;
const WORKER_BATCH_SIZE = 5000;

const DOG_DATA_COUNT_KEYS = Object.freeze([
  "dogs",
  "dog_memberships",
  "collars",
  "device_claims",
  "daily_summaries",
  "dirty_summary_days",
  "device_credentials",
  "sync_requests",
  "recordings",
  "telemetry_chunks",
  "telemetry_points",
  "telemetry_loss_markers",
  "device_daily_summaries",
  "config_revisions",
  "config_resource_heads",
  "config_reported",
  "config_hlc_state",
  "recording_summaries",
  "telemetry_retention_watermarks",
  "retention_jobs",
  "retention_receipts",
]);

const FAILURE_STAGES = new Set([
  "fixture-validation",
  "initial-login",
  "dog-data-page",
  "strong-confirmation-controls",
  "dog-deletion-request",
  "immediate-export-denial",
  "logout",
  "relogin-pending-status",
  "existing-deletion-worker",
  "completed-deletion-status",
  "dog-data-purge-verification",
  "other-dog-survival",
]);

function fixtureUuid(value) {
  assert.equal(typeof value, "string", "fixture identifier is missing");
  assert.match(value, UUID_PATTERN, "fixture identifier is invalid");
  return value;
}

async function requestJson(url, { method = "GET", publishableKey, accessToken, body } = {}) {
  const headers = { apikey: publishableKey, accept: "application/json" };
  if (accessToken) headers.authorization = `Bearer ${accessToken}`;
  if (body !== undefined) headers["content-type"] = "application/json";
  const response = await fetch(url, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(15_000),
  });
  const text = await response.text();
  let payload = null;
  if (text) {
    try { payload = JSON.parse(text); } catch { payload = text; }
  }
  return { status: response.status, payload };
}

async function passwordLogin(apiUrl, publishableKey, account, password) {
  const result = await requestJson(`${apiUrl}/auth/v1/token?grant_type=password`, {
    method: "POST",
    publishableKey,
    body: { email: account.email, password },
  });
  assert.equal(result.status, 200, "other owner login failed");
  assert.equal(typeof result.payload?.access_token, "string", "other owner login returned no session");
  return result.payload.access_token;
}

function databaseScalar(sql) {
  try {
    return execFileSync("docker", [
      "exec", "-i", DB_CONTAINER, "psql", "-qAt", "-v", "ON_ERROR_STOP=1",
      "-U", "supabase_admin", "-d", "postgres", "-c", sql,
    ], {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
      timeout: 30_000,
      maxBuffer: 1024 * 1024,
    }).trim();
  } catch {
    throw new Error("M5.6 dog lifecycle could not inspect the local deletion result.");
  }
}

function databaseJson(sql) {
  const output = databaseScalar(sql);
  assert.notEqual(output, "", "local deletion query returned no result");
  return JSON.parse(output);
}

function runExistingDeletionWorker(dogId) {
  let batches = 0;
  for (; batches < MAX_WORKER_BATCHES; batches += 1) {
    const nextJob = databaseJson(`
      select coalesce((
        select jsonb_build_object(
          'scope_id', tombstone.scope_id::text,
          'status', job.status
        )
        from private.deletion_jobs job
        join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
        where job.status in ('pending', 'processing', 'failed')
          and job.next_attempt_at <= statement_timestamp()
        order by job.requested_at, job.id
        limit 1
      ), 'null'::jsonb)::text
    `);
    assert.equal(nextJob?.scope_id, dogId, "the bounded worker would select a different deletion job");

    const result = databaseJson(`select private.process_dog_deletion_batch_v1(${WORKER_BATCH_SIZE})::text`);
    assert.equal(result?.scope_id, dogId, "the existing worker processed a different dog");
    assert.ok(["pending", "completed"].includes(result?.status), "the existing worker did not make bounded progress");
    if (result.status === "completed") return batches + 1;
  }
  throw new Error("M5.6 dog deletion worker exceeded its bounded batch count.");
}

function readDogDeletionState(dogId) {
  return databaseJson(`
    select jsonb_build_object(
      'completed_jobs', (
        select count(*) from private.deletion_jobs job
        join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
        where tombstone.scope = 'dog' and tombstone.scope_id = '${dogId}'::uuid
          and job.status = 'completed'
      ),
      'receipts', (
        select count(*) from private.deletion_jobs job
        join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
        join private.deletion_receipts receipt on receipt.job_id = job.id
        where tombstone.scope = 'dog' and tombstone.scope_id = '${dogId}'::uuid
      ),
      'dog_counts', private.dog_deletion_counts_v1('${dogId}'::uuid)
    )::text
  `);
}

function artifactFor(phase, checkpoints, counts, failureStage = null) {
  return {
    schemaVersion: 1,
    phase,
    checkpoints,
    failureStage,
    counts: {
      workerBatches: counts.workerBatches,
      pendingAfterRelogin: counts.pendingAfterRelogin,
      exportDenied: counts.exportDenied,
      completedJobs: counts.completedJobs,
      durableReceipts: counts.durableReceipts,
      dogDataRowsRemaining: counts.dogDataRowsRemaining,
      dogBRowsRemaining: counts.dogBRowsRemaining,
    },
  };
}

export async function runDogLifecycle({
  browserType,
  fixture,
  apiUrl,
  publishableKey,
  portalUrl,
  outputDirectory,
}) {
  let dogId;
  let dogBId;
  let account;
  let password;
  const checkpoints = [];
  const counts = {
    workerBatches: 0,
    pendingAfterRelogin: 0,
    exportDenied: 0,
    completedJobs: 0,
    durableReceipts: 0,
    dogDataRowsRemaining: 0,
    dogBRowsRemaining: 0,
  };
  let browser;
  let page;
  let phase = "failed";
  let failed = false;
  let failureStage = "fixture-validation";

  const stage = (name) => {
    assert.equal(FAILURE_STAGES.has(name), true, "failure stage is not registered");
    failureStage = name;
    console.log(`Dog lifecycle: running ${name}.`);
  };
  const checkpoint = (name) => {
    assert.equal(name, DOG_LIFECYCLE_CHECKPOINTS[checkpoints.length], "dog lifecycle checkpoint order changed");
    checkpoints.push(name);
    console.log(`Dog lifecycle: ${name} passed.`);
  };

  try {
    account = fixture?.accounts?.ownerA;
    assert.equal(typeof account?.email, "string", "owner account is missing");
    dogId = fixtureUuid(fixture?.dogA?.id);
    dogBId = fixtureUuid(fixture?.dogB?.id);
    assert.notEqual(dogId, dogBId, "the fixture dogs must be separate");
    password = authorizationPassword(fixture.cycle);

    browser = await browserType.launch({ headless: true });
    const context = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    page = await context.newPage();
    page.setDefaultTimeout(15_000);
    page.setDefaultNavigationTimeout(20_000);

    stage("initial-login");
    await page.goto("/login");
    await page.getByLabel("Correo").fill(account.email);
    await page.getByLabel("Contraseña").fill(password);
    await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await page.waitForURL((url) => url.pathname !== "/login");

    stage("dog-data-page");
    await page.goto(`/app/${dogId}/data`);
    await page.getByRole("heading", { name: "Eliminar perro y datos" }).waitFor();

    stage("strong-confirmation-controls");
    const confirmation = page.getByLabel("Escribe ELIMINAR para confirmar");
    const currentPassword = page.getByLabel("Contraseña actual");
    assert.equal(await confirmation.getAttribute("pattern"), "ELIMINAR", "confirmation must require the exact phrase");
    assert.equal(await confirmation.getAttribute("required"), "", "confirmation must be required");
    assert.equal(await currentPassword.getAttribute("type"), "password", "current password must be a password field");
    assert.equal(await currentPassword.getAttribute("required"), "", "current password must be required");
    assert.equal(await currentPassword.getAttribute("autocomplete"), "current-password", "password field must use current-password autocomplete");
    await confirmation.fill("eliminar");
    assert.equal(await confirmation.evaluate((input) => input.checkValidity()), false, "confirmation phrase must be case-sensitive");
    await confirmation.fill("ELIMINAR");
    await currentPassword.fill(password);

    stage("dog-deletion-request");
    await page.getByRole("button", { name: "Eliminar perro y datos" }).click();
    await page.waitForURL((url) => url.pathname === "/account");
    await page.getByRole("heading", { name: "Cuenta y datos" }).waitFor();
    const pendingJob = page.getByRole("listitem").filter({ hasText: dogId }).last();
    await pendingJob.getByText("Pendiente", { exact: true }).waitFor();
    checkpoint("owner-confirmation-and-password-request");

    stage("immediate-export-denial");
    const exportResponse = await context.request.get(
      new URL(`/app/${dogId}/data/export`, portalUrl).toString(),
      { maxRedirects: 0, timeout: 15_000 },
    );
    assert.equal(exportResponse.status(), 404, "export must be denied immediately after the deletion request");
    const exportHeaders = exportResponse.headers();
    assert.equal(/attachment/iu.test(exportHeaders["content-disposition"] ?? ""), false, "denied export must not attach a file");
    assert.match(exportHeaders["cache-control"] ?? "", /no-store/iu, "denied export must not be cached");
    counts.exportDenied = 1;
    checkpoint("export-denied-immediately-after-request");

    stage("logout");
    await page.getByRole("button", { name: "CERRAR SESIÓN" }).click();
    await page.waitForURL((url) => url.pathname === "/login");
    stage("relogin-pending-status");
    await page.getByLabel("Correo").fill(account.email);
    await page.getByLabel("Contraseña").fill(password);
    await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await page.waitForURL((url) => url.pathname !== "/login");
    await page.goto("/account");
    const recoveredPendingJob = page.getByRole("listitem").filter({ hasText: dogId }).last();
    await recoveredPendingJob.getByText("Pendiente", { exact: true }).waitFor();
    counts.pendingAfterRelogin = 1;
    checkpoint("pending-status-survives-logout-login");

    stage("existing-deletion-worker");
    counts.workerBatches = runExistingDeletionWorker(dogId);

    stage("completed-deletion-status");
    await page.goto("/account");
    let completedJob = page.getByRole("listitem").filter({ hasText: dogId }).last();
    await completedJob.getByText("Borrado completado", { exact: true }).waitFor();
    await page.reload();
    completedJob = page.getByRole("listitem").filter({ hasText: dogId }).last();
    await completedJob.getByText("Borrado completado", { exact: true }).waitFor();

    stage("dog-data-purge-verification");
    const deletionState = readDogDeletionState(dogId);
    assert.equal(deletionState.completed_jobs, 1, "completed job was not durable");
    assert.equal(deletionState.receipts, 1, "completed deletion receipt was not durable");
    assert.equal(typeof deletionState.dog_counts, "object", "purge counts are unavailable");
    for (const key of DOG_DATA_COUNT_KEYS) {
      const value = deletionState.dog_counts[key];
      assert.equal(Number.isSafeInteger(value), true, "purge count is not a safe integer");
      counts.dogDataRowsRemaining += value;
    }
    assert.equal(counts.dogDataRowsRemaining, 0, "dog data remains after the worker completed");
    counts.completedJobs = deletionState.completed_jobs;
    counts.durableReceipts = deletionState.receipts;
    checkpoint("worker-purges-dog-and-status-is-durable");

    stage("other-dog-survival");
    counts.dogBRowsRemaining = Number(databaseScalar(`select count(*) from api.dogs where id = '${dogBId}'::uuid`));
    assert.equal(counts.dogBRowsRemaining, 1, "the other owner's dog did not survive the purge");
    const ownerBToken = await passwordLogin(apiUrl, publishableKey, fixture.accounts.ownerB, password);
    const dogBRead = await requestJson(
      `${apiUrl}/rest/v1/dogs?id=eq.${encodeURIComponent(dogBId)}&select=id`,
      { publishableKey, accessToken: ownerBToken },
    );
    assert.equal(dogBRead.status, 200, "the other owner lost API access to their dog");
    assert.deepEqual(dogBRead.payload, [{ id: dogBId }], "the other owner could not read their surviving dog");
    checkpoint("deleted-dog-data-absent-and-other-dog-survives");
    phase = "passed";
  } catch {
    failed = true;
  } finally {
    await browser?.close().catch(() => undefined);
    await mkdir(outputDirectory, { recursive: true });
    await writeFile(join(outputDirectory, "dog-lifecycle.json"), `${JSON.stringify(
      artifactFor(phase, checkpoints, counts, failed ? failureStage : null), null, 2,
    )}\n`);
  }

  if (failed) {
    throw new Error(`M5.6 dog lifecycle failed at ${failureStage}.`);
  }
  return artifactFor(phase, checkpoints, counts, null);
}
