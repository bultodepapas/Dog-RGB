import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { authorizationPassword } from "./authorization-fixtures.mjs";
import { inspectAccessibility } from "./quality.mjs";

export const M56C_CHECKPOINTS = Object.freeze([
  "password-confirmed-account-request",
  "pending-account-survives-logout-login",
  "pending-reads-and-writes-are-denied",
  "existing-worker-purges-owned-dog",
  "finalization-action-contract-guards",
  "finalization-reauthentication-prepares-cookie",
  "lost-finalize-response-recovers-receipt",
  "wrong-requester-cannot-read-receipt",
  "receipt-recovery-survives-reload",
  "receipt-acknowledgement-clears-session",
  "deleted-account-rejects-old-session",
]);

const ACCOUNT_CONFIRMATION = "ELIMINAR CUENTA Y TODOS LOS PERROS";
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const RECEIPT_PATTERN = /^[A-Za-z0-9_-]{43}$/u;
const RECOVERY_COOKIE = "dog_rgb_account_deletion_request";
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

function actionForRequest(request) {
  try { return request.postDataJSON()?.action; } catch { return null; }
}

function isReceipt(value, requestId) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const keys = Object.keys(value).sort();
  return keys.join(",") === "completed_at,receipt_sha256,request_id,schema_version,status" &&
    value.schema_version === "account-deletion-receipt-v1" && value.status === "completed" &&
    value.request_id === requestId && typeof value.completed_at === "string" &&
    Number.isFinite(Date.parse(value.completed_at)) && typeof value.receipt_sha256 === "string" &&
    RECEIPT_PATTERN.test(value.receipt_sha256);
}

async function forwardRouteRequest(route) {
  const request = route.request();
  const headers = await request.allHeaders();
  for (const name of ["host", "content-length", "connection", "accept-encoding"]) delete headers[name];
  const response = await fetch(request.url(), {
    method: request.method(),
    headers,
    body: request.postData() ?? undefined,
    redirect: "manual",
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
      preparedRecoveryCookie: counts.preparedRecoveryCookie,
      lostFinalizeResponses: counts.lostFinalizeResponses,
      recoveredReceipts: counts.recoveredReceipts,
      reloadRecoveredReceipts: counts.reloadRecoveredReceipts,
      acknowledgedReceipts: counts.acknowledgedReceipts,
      recoveryCookiesCleared: counts.recoveryCookiesCleared,
      authCookiesCleared: counts.authCookiesCleared,
      wrongRequesterRejected: counts.wrongRequesterRejected,
      receiptA11yViewports: counts.receiptA11yViewports,
      receiptA11yViolations: counts.receiptA11yViolations,
      receiptA11yLayoutOverflows: counts.receiptA11yLayoutOverflows,
      receiptA11ySmallTargets: counts.receiptA11ySmallTargets,
      receiptA11yZoomOverflows: counts.receiptA11yZoomOverflows,
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
    preparedRecoveryCookie: 0,
    lostFinalizeResponses: 0,
    recoveredReceipts: 0,
    reloadRecoveredReceipts: 0,
    acknowledgedReceipts: 0,
    recoveryCookiesCleared: 0,
    authCookiesCleared: 0,
    wrongRequesterRejected: 0,
    receiptA11yViewports: 0,
    receiptA11yViolations: 0,
    receiptA11yLayoutOverflows: 0,
    receiptA11ySmallTargets: 0,
    receiptA11yZoomOverflows: 0,
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
    const requestId = fixtureUuid(ready.payload?.request_id, "ready account deletion request id");
    checkpoint("existing-worker-purges-owned-dog");

    stage("finalization-action-contract-guards");
    const finalizeUrl = new URL("/account/finalize", portalUrl).toString();
    const sameOrigin = new URL(portalUrl).origin;
    const jsonHeaders = { "content-type": "application/json", accept: "application/json" };
    const endpointRequest = page.context().request;
    const foreignOrigin = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: "https://attacker.invalid" },
      data: JSON.stringify({ action: "prepare", request_id: requestId, password }),
      timeout: 15_000,
    });
    assert.equal(foreignOrigin.status(), 403, "foreign-origin finalization was not rejected");
    const missingOrigin = await endpointRequest.post(finalizeUrl, {
      headers: jsonHeaders,
      data: JSON.stringify({ action: "prepare", request_id: requestId, password }),
      timeout: 15_000,
    });
    assert.equal(missingOrigin.status(), 403, "missing-origin finalization was not rejected");
    const wrongContentType = await endpointRequest.post(finalizeUrl, {
      headers: { origin: sameOrigin, "content-type": "text/plain", accept: "application/json" },
      data: JSON.stringify({ action: "prepare", request_id: requestId, password }),
      timeout: 15_000,
    });
    assert.equal(wrongContentType.status(), 400, "non-JSON finalization action was not rejected");
    const oversizedJson = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "prepare", request_id: requestId, password: "x".repeat(1_200) }),
      timeout: 15_000,
    });
    assert.equal(oversizedJson.status(), 400, "oversized finalization body was not rejected");
    const unknownAction = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "status", request_id: requestId }),
      timeout: 15_000,
    });
    assert.equal(unknownAction.status(), 400, "unknown finalization action was not rejected");
    const wrongPassword = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "prepare", request_id: requestId, password: "invalid-reauth-password" }),
      timeout: 15_000,
    });
    assert.equal(wrongPassword.status(), 403, "incorrect-password finalization was not rejected");
    const finalizeWithoutPrepare = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "finalize", request_id: requestId }),
      timeout: 15_000,
    });
    assert.equal(finalizeWithoutPrepare.status(), 409, "finalization without a prepared recovery cookie was not rejected");
    const receiptWithoutPrepare = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "receipt" }),
      timeout: 15_000,
    });
    assert.equal(receiptWithoutPrepare.status(), 401, "receipt without a recovery cookie was not rejected");
    const acknowledgeWithoutPrepare = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "acknowledge", request_id: requestId, receipt_sha256: "A".repeat(43) }),
      timeout: 15_000,
    });
    assert.equal(acknowledgeWithoutPrepare.status(), 409, "acknowledgement without a recovery cookie was not rejected");
    for (const response of [foreignOrigin, missingOrigin, wrongContentType, oversizedJson, unknownAction, wrongPassword,
      finalizeWithoutPrepare, receiptWithoutPrepare, acknowledgeWithoutPrepare]) {
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
    checkpoint("finalization-action-contract-guards");

    stage("reauthenticated-prepare-finalize");
    const recoveryEvidence = {
      forwardedFinalizeStatus: 0,
      forwardedFinalizeReceiptValid: false,
      forwardedInitialReceiptStatus: 0,
      forwardedInitialReceiptValid: false,
      forwardedAcknowledgeStatus: 0,
      forwardedReloadReceiptStatus: 0,
      forwardedReloadReceiptValid: false,
      forwardedReloadAcknowledgeStatus: 0,
    };
    let droppedFinalize = false;
    let committedReceipt = null;
    let droppedInitialReceipt = false;
    let droppedFirstAcknowledge = false;
    let resolveFinalizeForwarded;
    let resolveInitialReceiptForwarded;
    let resolveFirstAcknowledgeForwarded;
    const finalizeForwarded = new Promise(resolve => { resolveFinalizeForwarded = resolve; });
    const initialReceiptForwarded = new Promise(resolve => { resolveInitialReceiptForwarded = resolve; });
    const firstAcknowledgeForwarded = new Promise(resolve => { resolveFirstAcknowledgeForwarded = resolve; });
    await page.route("**/account/finalize", async (route) => {
      const action = actionForRequest(route.request());
      const dropResponse = async (key, receiptKey, resolver) => {
        try {
          const forwarded = await forwardRouteRequest(route);
          if (key === "forwardedFinalizeStatus") committedReceipt = forwarded.payload;
          recoveryEvidence[key] = forwarded.status;
          if (receiptKey) recoveryEvidence[receiptKey] = isReceipt(forwarded.payload, requestId);
        } catch {
          recoveryEvidence[key] = 0;
        } finally {
          resolver();
          await route.abort("failed").catch(() => undefined);
        }
      };
      if (action === "finalize" && !droppedFinalize) {
        droppedFinalize = true;
        await dropResponse("forwardedFinalizeStatus", "forwardedFinalizeReceiptValid", resolveFinalizeForwarded);
        return;
      }
      if (action === "receipt" && !droppedInitialReceipt) {
        droppedInitialReceipt = true;
        await dropResponse("forwardedInitialReceiptStatus", "forwardedInitialReceiptValid", resolveInitialReceiptForwarded);
        return;
      }
      if (action === "acknowledge" && !droppedFirstAcknowledge) {
        droppedFirstAcknowledge = true;
        await dropResponse("forwardedAcknowledgeStatus", null, resolveFirstAcknowledgeForwarded);
        return;
      }
      await route.continue();
    });

    await page.locator("#finalize-password").fill(password);
    const prepareResponse = page.waitForResponse((response) => new URL(response.url()).pathname === "/account/finalize" &&
      actionForRequest(response.request()) === "prepare", { timeout: 20_000 });
    await page.getByRole("button", { name: "Completar eliminación de cuenta" }).click();
    const prepared = await prepareResponse;
    assert.equal(prepared.status(), 200, "password reauthentication did not prepare finalization");
    const preparedPayload = await prepared.json();
    assert.deepEqual(preparedPayload, { status: "prepared", request_id: requestId }, "prepare returned an unexpected contract");
    const cookiesAfterPrepare = await page.context().cookies(new URL("/account", portalUrl).toString());
    const recoveryCookie = cookiesAfterPrepare.find(cookie => cookie.name === RECOVERY_COOKIE && cookie.path === "/account");
    assert.ok(recoveryCookie, "prepare did not issue the request-scoped recovery cookie");
    assert.equal(recoveryCookie.value, requestId, "recovery cookie does not match the deletion request");
    assert.equal(recoveryCookie.httpOnly, true, "recovery cookie must be HttpOnly");
    assert.equal(recoveryCookie.sameSite, "Strict", "recovery cookie must use SameSite=Strict");
    counts.preparedRecoveryCookie = 1;
    checkpoint("finalization-reauthentication-prepares-cookie");

    stage("lost-finalize-response");
    await page.getByRole("link", { name: "Recuperar comprobante" }).waitFor();
    await Promise.all([finalizeForwarded, initialReceiptForwarded]);
    assert.equal(recoveryEvidence.forwardedFinalizeStatus, 200, "finalization did not commit before its response was dropped");
    assert.equal(recoveryEvidence.forwardedFinalizeReceiptValid, true, "committed finalization did not return the minimal receipt");
    assert.equal(recoveryEvidence.forwardedInitialReceiptStatus, 200, "same-page receipt recovery did not reach the committed result");
    assert.equal(recoveryEvidence.forwardedInitialReceiptValid, true, "same-page receipt recovery returned an invalid receipt");
    assert.equal(await page.getByRole("heading", { name: "Cuenta eliminada" }).count(), 0, "a dropped receipt response was shown as completion");
    counts.lostFinalizeResponses = 1;
    checkpoint("lost-finalize-response-recovers-receipt");

    stage("wrong-requester-and-request-guards");
    const unrelatedToken = await passwordLogin(apiUrl, publishableKey, fixture.accounts.ownerA, password);
    const wrongRequester = await requestJson(`${apiUrl}/functions/v1/user-v1-account-deletion`, {
      method: "POST", publishableKey, accessToken: unrelatedToken,
      body: { action: "receipt", request_id: requestId },
    });
    assert.equal(wrongRequester.status, 409, "another authenticated user read this account deletion receipt");
    assert.equal(Object.hasOwn(wrongRequester.payload ?? {}, "receipt_sha256"), false, "denied requester received a receipt");
    counts.wrongRequesterRejected = 1;

    const wrongRequestId = randomUUID();
    const mismatchedFinalize = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "finalize", request_id: wrongRequestId }),
      timeout: 15_000,
    });
    assert.equal(mismatchedFinalize.status(), 409, "prepared recovery cookie authorized a different request id");
    const callerSelectedReceipt = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "receipt", request_id: requestId }),
      timeout: 15_000,
    });
    assert.equal(callerSelectedReceipt.status(), 400, "receipt action accepted a caller-selected request id");
    const mismatchedAcknowledge = await endpointRequest.post(finalizeUrl, {
      headers: { ...jsonHeaders, origin: sameOrigin },
      data: JSON.stringify({ action: "acknowledge", request_id: wrongRequestId, receipt_sha256: "A".repeat(43) }),
      timeout: 15_000,
    });
    assert.equal(mismatchedAcknowledge.status(), 409, "acknowledgement accepted a different request id");
    for (const response of [mismatchedFinalize, callerSelectedReceipt, mismatchedAcknowledge]) await response.dispose();
    const cookieBeforeRecoveryPage = (await page.context().cookies(new URL("/account", portalUrl).toString()))
      .find(cookie => cookie.name === RECOVERY_COOKIE);
    assert.equal(cookieBeforeRecoveryPage?.value, requestId, "rejected request id cleared the valid recovery cookie");
    checkpoint("wrong-requester-cannot-read-receipt");

    stage("receipt-recovery-page");
    const recoveryResponse = page.waitForResponse((response) => new URL(response.url()).pathname === "/account/finalize" &&
      actionForRequest(response.request()) === "receipt", { timeout: 20_000 });
    const firstAcknowledgeRequest = page.waitForRequest((request) => new URL(request.url()).pathname === "/account/finalize" &&
      actionForRequest(request) === "acknowledge", { timeout: 20_000 });
    await page.getByRole("link", { name: "Recuperar comprobante" }).click();
    const recoveredResponse = await recoveryResponse;
    assert.equal(recoveredResponse.status(), 200, "recovery page could not retrieve the completed receipt");
    await page.getByText("La eliminación quedó confirmada.", { exact: true }).waitFor();
    const recoveredPayload = await recoveredResponse.json();
    assert.equal(isReceipt(recoveredPayload, requestId), true, "recovery page returned an invalid receipt");
    assert.deepEqual(recoveredPayload, committedReceipt, "recovery must return the exact committed receipt");
    await firstAcknowledgeRequest;
    await firstAcknowledgeForwarded;
    assert.equal(recoveryEvidence.forwardedAcknowledgeStatus, 200, "recovery acknowledgement did not reach the server");
    counts.recoveredReceipts = 1;

    const receiptA11y = { a11y: [] };
    await inspectAccessibility(page, "deletion-receipt-completed", receiptA11y);
    counts.receiptA11yViewports = receiptA11y.a11y.length;
    counts.receiptA11yViolations = receiptA11y.a11y.reduce((sum, row) => sum + row.violations.length, 0);
    counts.receiptA11yLayoutOverflows = receiptA11y.a11y.filter(row => row.overflow).length;
    counts.receiptA11ySmallTargets = receiptA11y.a11y.reduce((sum, row) => sum + row.smallTargets, 0);
    counts.receiptA11yZoomOverflows = receiptA11y.a11y.filter(row => row.zoomOverflow === true).length;
    assert.equal(counts.receiptA11yViewports, 4, "completed receipt accessibility coverage missed a viewport");
    assert.equal(counts.receiptA11yViolations, 0, "completed receipt has automated accessibility violations");
    assert.equal(counts.receiptA11yLayoutOverflows, 0, "completed receipt layout overflows at a checked viewport");
    assert.equal(counts.receiptA11ySmallTargets, 0, "completed receipt has small pointer targets");
    assert.equal(counts.receiptA11yZoomOverflows, 0, "completed receipt overflows at 200% zoom");

    const cookiesBeforeReload = await page.context().cookies(new URL("/account", portalUrl).toString());
    assert.equal(cookiesBeforeReload.some(cookie => cookie.name === RECOVERY_COOKIE), true,
      "dropped acknowledgement unexpectedly cleared the recovery cookie");
    stage("reload-recovery-and-acknowledgement");
    const reloadReceiptResponse = page.waitForResponse((response) => new URL(response.url()).pathname === "/account/finalize" &&
      actionForRequest(response.request()) === "receipt", { timeout: 20_000 });
    const reloadAcknowledgeResponse = page.waitForResponse((response) => new URL(response.url()).pathname === "/account/finalize" &&
      actionForRequest(response.request()) === "acknowledge", { timeout: 20_000 });
    await page.reload();
    const reloadedReceipt = await reloadReceiptResponse;
    assert.equal(reloadedReceipt.status(), 200, "receipt could not be recovered after reloading the page");
    assert.deepEqual(await reloadedReceipt.json(), committedReceipt, "reload must return the exact committed receipt");
    await page.getByText("La eliminación quedó confirmada.", { exact: true }).waitFor();
    const acknowledged = await reloadAcknowledgeResponse;
    assert.equal(acknowledged.status(), 200, "receipt acknowledgement did not succeed after reload");
    assert.deepEqual(await acknowledged.json(), { status: "acknowledged" }, "acknowledgement returned an unexpected contract");
    recoveryEvidence.forwardedReloadReceiptStatus = reloadedReceipt.status();
    recoveryEvidence.forwardedReloadReceiptValid = true;
    recoveryEvidence.forwardedReloadAcknowledgeStatus = acknowledged.status();
    counts.reloadRecoveredReceipts = 1;
    counts.acknowledgedReceipts = 1;
    const cookiesAfterAcknowledgement = await page.context().cookies();
    counts.recoveryCookiesCleared = Number(!cookiesAfterAcknowledgement.some(cookie => cookie.name === RECOVERY_COOKIE));
    counts.authCookiesCleared = Number(!cookiesAfterAcknowledgement.some(cookie => /^sb-.+auth-token(?:\.\d+)?$/u.test(cookie.name)));
    assert.equal(counts.recoveryCookiesCleared, 1, "acknowledgement did not clear the recovery cookie");
    assert.equal(counts.authCookiesCleared, 1, "acknowledgement did not clear the local Supabase session");
    checkpoint("receipt-recovery-survives-reload");
    checkpoint("receipt-acknowledgement-clears-session");

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
      method: "POST", publishableKey, accessToken: oldAccessToken, body: { action: "finalize", request_id: requestId },
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
