import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { authorizationPassword } from "./authorization-fixtures.mjs";

export const M56C_CHECKPOINTS = Object.freeze([
  "password-confirmed-account-request",
  "pending-account-survives-logout-login",
  "pending-reads-and-writes-are-denied",
  "existing-worker-purges-owned-dog",
  "finalization-endpoint-guards",
  "reauthenticated-finalization-persists-receipt",
  "deleted-account-rejects-old-session",
]);

const ACCOUNT_CONFIRMATION = "ELIMINAR CUENTA Y TODOS LOS PERROS";
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const RECEIPT_PATTERN = /^[A-Za-z0-9_-]{43}$/u;
const PENDING_ACCOUNT_STATUS = /^Solicitud [0-9a-f-]{36}: purga pendiente\. Puedes cerrar sesión y volver aquí para continuar\.$/u;

function fixtureUuid(value, label) {
  assert.equal(typeof value, "string", `${label} is missing`);
  assert.match(value, UUID_PATTERN, `${label} is not a UUID`);
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
  assert.equal(result.status, 200, "fixture password login failed");
  assert.equal(typeof result.payload?.access_token, "string", "fixture login returned no access token");
  return result.payload.access_token;
}

async function rpc(apiUrl, publishableKey, accessToken, name, body = {}) {
  return requestJson(`${apiUrl}/rest/v1/rpc/${name}`, {
    method: "POST", publishableKey, accessToken, body,
  });
}

function databaseScalar(sql) {
  try {
    return execFileSync("docker", [
      "exec", "supabase_db_Dog-RGB-1", "psql", "-qAt", "-v", "ON_ERROR_STOP=1",
      "-U", "supabase_admin", "-d", "postgres", "-c", sql,
    ], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 30_000 }).trim();
  } catch {
    throw new Error("M5.6c could not inspect the local deletion result.");
  }
}

function runExistingDeletionWorker() {
  try {
    execFileSync("docker", [
      "exec", "supabase_db_Dog-RGB-1", "psql", "-v", "ON_ERROR_STOP=1",
      "-U", "supabase_admin", "-d", "postgres", "-c",
      "select private.process_dog_deletion_batch_v1(5000)",
    ], { encoding: "utf8", stdio: ["ignore", "ignore", "pipe"], timeout: 30_000 });
  } catch {
    throw new Error("M5.6c existing dog deletion worker failed.");
  }
}

function artifactFor(phase, checkpoints, counts, failureStage = null) {
  return {
    schemaVersion: 1,
    phase,
    checkpoints,
    failureStage,
    counts: {
      workerBatches: counts.workerBatches,
      completedDogJobs: counts.completedDogJobs,
      ownedDogsRemaining: counts.ownedDogsRemaining,
      authUsersRemaining: counts.authUsersRemaining,
      oldSessionRejected: counts.oldSessionRejected,
    },
  };
}

export async function runAccountLifecycle({
  browserType,
  fixture,
  apiUrl,
  publishableKey,
  portalUrl,
  outputDirectory,
}) {
  const account = fixture.accounts.ownerB;
  const dogId = fixtureUuid(fixture.dogB.id, "owner B dog id");
  fixtureUuid(account.id, "owner B account id");
  const password = authorizationPassword(fixture.cycle);
  const checkpoints = [];
  const counts = {
    workerBatches: 0,
    completedDogJobs: 0,
    ownedDogsRemaining: 0,
    authUsersRemaining: 0,
    oldSessionRejected: 0,
  };
  let browser;
  let phase = "failed";
  let failure = null;
  let failureStage = "fixture-login";
  let page;

  const checkpoint = (name) => {
    assert.equal(name, M56C_CHECKPOINTS[checkpoints.length], "M5.6c checkpoint order changed");
    checkpoints.push(name);
    console.log(`M5.6c: ${name} passed.`);
  };
  const stage = (name) => {
    failureStage = name;
    console.log(`M5.6c: running ${name}.`);
  };

  try {
    // Keep a pre-deletion bearer token in memory so the final check exercises
    // a genuinely old session after the Auth row has been removed.
    const oldAccessToken = await passwordLogin(apiUrl, publishableKey, account, password);

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

    stage("account-preview");
    await page.goto("/account");
    await page.getByRole("heading", { name: "Eliminar cuenta" }).waitFor();
    await page.getByRole("listitem").filter({ hasText: fixture.dogB.name }).last().waitFor();
    await page.locator("#account-password").fill(password);
    await page.locator("#account-confirmation").fill(ACCOUNT_CONFIRMATION);
    stage("account-request-submit");
    await page.getByRole("button", { name: "Solicitar eliminación de cuenta y perros" }).click();
    await page.getByText(PENDING_ACCOUNT_STATUS).waitFor();
    checkpoint("password-confirmed-account-request");

    stage("logout-after-request");
    await page.getByRole("button", { name: "CERRAR SESIÓN" }).click();
    stage("login-page-after-logout");
    await page.waitForURL((url) => url.pathname === "/login");
    stage("pending-account-relogin");
    await page.getByLabel("Correo").fill(account.email);
    await page.getByLabel("Contraseña").fill(password);
    await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await page.waitForURL((url) => url.pathname !== "/login");
    stage("pending-status-after-relogin");
    await page.goto("/account");
    await page.getByText(PENDING_ACCOUNT_STATUS).waitFor();

    stage("authenticated-pending-retry");
    const recoveryToken = await passwordLogin(apiUrl, publishableKey, account, password);
    const retry = await rpc(apiUrl, publishableKey, recoveryToken, "retry_my_account_deletion_v1");
    assert.equal(retry.status, 200, "authenticated pending retry was not available after re-login");
    assert.equal(retry.payload?.status, "pending", "pending retry changed account status unexpectedly");
    checkpoint("pending-account-survives-logout-login");

    stage("pending-read-write-denial");
    const dogRead = await requestJson(
      `${apiUrl}/rest/v1/dogs?id=eq.${encodeURIComponent(dogId)}&select=id`,
      { publishableKey, accessToken: recoveryToken },
    );
    assert.equal(dogRead.status, 200, "pending dog read did not return a bounded response");
    assert.deepEqual(dogRead.payload, [], "pending account still reads its owned dog");

    const createDog = await rpc(apiUrl, publishableKey, recoveryToken, "create_dog_v1", {
      p_name: "Pending account dog",
      p_timezone: "America/Bogota",
    });
    assert.notEqual(createDog.status, 200, "pending account created another dog");

    const claim = await requestJson(`${apiUrl}/functions/v1/user-v1-issue-claim`, {
      method: "POST", publishableKey, accessToken: recoveryToken,
      body: { protocol_version: 1, request_id: randomUUID(), dog_id: dogId },
    });
    assert.notEqual(claim.status, 200, "pending account issued a device claim");

    const body = { brightness: fixture.brightness };
    const digest = createHash("sha256").update(JSON.stringify(body)).digest("hex");
    const config = await rpc(apiUrl, publishableKey, recoveryToken, "mutate_config_resource_v1", {
      p_collar_id: fixture.dogB.collarId,
      p_resource_key: "brightness",
      p_resource_schema: 1,
      p_mutation_id: randomUUID(),
      p_base_server_version: 1,
      p_body: body,
      p_body_sha256: `\\x${digest}`,
    });
    assert.notEqual(config.status, 200, "pending account mutated device configuration");
    checkpoint("pending-reads-and-writes-are-denied");

    stage("existing-deletion-worker");
    runExistingDeletionWorker();
    counts.workerBatches = 1;
    const ready = await rpc(apiUrl, publishableKey, recoveryToken, "get_my_account_deletion_v1");
    assert.equal(ready.status, 200, "authenticated account status failed after worker completion");
    assert.equal(ready.payload?.status, "ready", "account did not become ready after every linked purge job");
    checkpoint("existing-worker-purges-owned-dog");

    stage("finalization-endpoint-guards");
    const finalizeUrl = new URL("/account/finalize", portalUrl).toString();
    const sameOrigin = new URL(portalUrl).origin;
    const jsonHeaders = { "content-type": "application/json", accept: "application/json" };
    const endpointRequest = page.context().request;
    const foreignOrigin = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: "https://attacker.invalid" },
      data: JSON.stringify({ password }),
      timeout: 15_000,
    });
    assert.equal(foreignOrigin.status(), 403, "foreign-origin finalization was not rejected");
    const missingOrigin = await endpointRequest.post(finalizeUrl, {
      headers: jsonHeaders,
      data: JSON.stringify({ password }),
      timeout: 15_000,
    });
    assert.equal(missingOrigin.status(), 403, "missing-origin finalization was not rejected");
    const wrongContentType = await endpointRequest.post(finalizeUrl, {
      headers: { origin: sameOrigin, "content-type": "text/plain", accept: "application/json" },
      data: JSON.stringify({ password }),
      timeout: 15_000,
    });
    assert.equal(wrongContentType.status(), 415, "non-JSON finalization was not rejected");
    const oversizedJson = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ password: "x".repeat(1_200) }),
      timeout: 15_000,
    });
    assert.equal(oversizedJson.status(), 413, "oversized finalization body was not rejected");
    const wrongPassword = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ password: "invalid-reauth-password" }),
      timeout: 15_000,
    });
    assert.equal(wrongPassword.status(), 403, "incorrect-password finalization was not rejected");
    for (const response of [foreignOrigin, missingOrigin, wrongContentType, oversizedJson, wrongPassword]) {
      await response.dispose();
    }

    const stillReady = await rpc(apiUrl, publishableKey, recoveryToken, "get_my_account_deletion_v1");
    assert.equal(stillReady.status, 200, "account status stopped working after rejected finalization");
    assert.equal(stillReady.payload?.status, "ready", "rejected finalization changed the ready request");
    const liveUser = await requestJson(`${apiUrl}/auth/v1/user`, { publishableKey, accessToken: recoveryToken });
    assert.equal(liveUser.status, 200, "rejected finalization removed or disabled the live Auth user");
    assert.equal(liveUser.payload?.id, account.id, "rejected finalization changed the current identity");
    await page.goto("/account");
    await page.getByRole("button", { name: "Completar eliminación de cuenta" }).waitFor();
    assert.equal(await page.getByRole("heading", { name: "Cuenta eliminada" }).count(), 0, "rejected finalization displayed a completion receipt");
    checkpoint("finalization-endpoint-guards");

    stage("reauthenticated-finalize");
    await page.locator("#finalize-password").fill(password);
    const finalizeResponse = page.waitForResponse((response) => new URL(response.url()).pathname === "/account/finalize", { timeout: 20_000 });
    await page.getByRole("button", { name: "Completar eliminación de cuenta" }).click();
    const finalizeResult = await finalizeResponse;
    assert.equal(finalizeResult.status(), 200, "valid finalization route did not return success");
    await page.getByRole("heading", { name: "Cuenta eliminada" }).waitFor();
    const receipt = await page.locator("code").textContent();
    assert.match(receipt ?? "", RECEIPT_PATTERN, "finalization did not show the persisted completion receipt");
    checkpoint("reauthenticated-finalization-persists-receipt");

    stage("persisted-completion-checks");
    const quotedDog = `'${dogId}'::uuid`;
    counts.completedDogJobs = Number(databaseScalar(
      `select count(*) from private.deletion_jobs job join private.account_deletion_job_links link on link.job_id=job.id where link.dog_id=${quotedDog} and job.status='completed'`,
    ));
    counts.ownedDogsRemaining = Number(databaseScalar(`select count(*) from api.dogs where id=${quotedDog}`));
    counts.authUsersRemaining = Number(databaseScalar(
      `select count(*) from auth.users where id='${fixtureUuid(account.id, "owner B account id")}'::uuid`,
    ));
    assert.equal(counts.completedDogJobs, 1, "the existing worker did not complete exactly one linked dog job");
    assert.equal(counts.ownedDogsRemaining, 0, "the owned dog remains after finalization");
    assert.equal(counts.authUsersRemaining, 0, "the Auth identity remains after finalization");

    stage("old-session-denial");
    const oldSession = await requestJson(`${apiUrl}/functions/v1/user-v1-account-deletion`, {
      method: "POST", publishableKey, accessToken: oldAccessToken, body: { action: "finalize" },
    });
    assert.equal(oldSession.status, 401, "deleted identity's old session was not rejected by the live Auth check");
    counts.oldSessionRejected = 1;
    checkpoint("deleted-account-rejects-old-session");
    phase = "passed";
  } catch {
    failure = true;
  } finally {
    await browser?.close().catch(() => undefined);
    await mkdir(outputDirectory, { recursive: true });
    await writeFile(join(outputDirectory, "account-lifecycle.json"), `${JSON.stringify(
      artifactFor(phase, checkpoints, counts, failure ? failureStage : null), null, 2,
    )}\n`);
  }

  if (failure) {
    throw new Error(`M5.6c browser lifecycle failed after ${checkpoints.at(-1) ?? "startup"}.`);
  }
  return artifactFor(phase, checkpoints, counts, null);
}
