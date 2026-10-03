import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

import {
  authorizationInvokeRpc,
  authorizationPassword,
  authorizationPasswordLogin,
  authorizationRequestJson,
} from "./authorization-fixtures.mjs";
import { createPairOnlySimulator } from "../device-simulator/pair-only.mjs";

const WORKSPACE = fileURLToPath(new URL("../../", import.meta.url));
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const CLAIM_PATTERN = /^[0-9A-HJKMNP-TV-Z]{16}$/u;
const CHECKPOINTS = Object.freeze([
  "temporary-dog-created",
  "first-real-upload-and-exact-retry",
  "first-summary-recomputed-and-visible",
  "late-upload-invalidated-summary",
  "late-summary-recomputed-and-visible",
  "temporary-dog-cleaned",
]);
const FAILURE_STAGES = new Set([
  "fixture-validation", "local-boundary", "owner-login", "temporary-dog",
  "first-claim-and-upload", "first-summary-worker", "first-visible-summary",
  "late-upload", "late-pending-summary", "late-summary-worker",
  "late-visible-summary", "cleanup",
]);

function localHttpUrl(value, label) {
  let url;
  try { url = new URL(value); } catch { throw new Error(`M4.13 ${label} URL is invalid.`); }
  assert.equal(url.protocol, "http:", `M4.13 ${label} must use local HTTP.`);
  assert.ok(["127.0.0.1", "localhost", "[::1]", "::1"].includes(url.hostname),
    `M4.13 ${label} must be loopback.`);
  return url;
}

function requireUuid(value, label) {
  assert.equal(typeof value, "string", `M4.13 ${label} is missing.`);
  assert.match(value, UUID_PATTERN, `M4.13 ${label} is invalid.`);
  return value;
}

function databaseScalar(sql) {
  const containers = execFileSync("docker", [
    "ps", "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$", "--format", "{{.ID}}",
  ], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 15_000 })
    .trim().split(/\r?\n/u).filter(Boolean);
  assert.equal(containers.length, 1, "M4.13 could not select this repository's local database.");
  return execFileSync("docker", [
    "exec", "-i", containers[0], "psql", "-X", "-qAt", "-v", "ON_ERROR_STOP=1",
    "-U", "supabase_admin", "-d", "postgres", "-c", sql,
  ], {
    encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 30_000, maxBuffer: 4096,
  }).trim();
}

function dogPointCount(dogId) {
  const value = Number(databaseScalar(`
    select count(*)::text
    from api.telemetry_points point
    join api.collars collar on collar.id = point.collar_id
    where collar.dog_id = '${dogId}'::uuid
  `));
  assert.ok(Number.isSafeInteger(value) && value >= 0, "M4.13 point count is invalid.");
  return value;
}

function runBoundedSummaryProducer() {
  let output;
  try {
    output = execFileSync(process.execPath, [
      "tools/cloud_analytics/run.mjs", "--max-batches", "64",
    ], {
      cwd: WORKSPACE,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
      timeout: 180_000,
      maxBuffer: 64 * 1024,
      windowsHide: true,
    });
  } catch {
    throw new Error("M4.13 bounded summary producer failed or exceeded its runtime bound.");
  }
  const records = output.trim().split(/\r?\n/u).filter(Boolean).map((line) => {
    try { return JSON.parse(line); } catch { return null; }
  });
  assert.ok(records.length > 0 && records.every((item) =>
    Number.isSafeInteger(item?.batch) && Number.isSafeInteger(item?.remainingDirtyDays)),
  "M4.13 bounded summary producer returned an invalid result.");
  const remainingDirtyDays = records.at(-1).remainingDirtyDays;
  assert.equal(remainingDirtyDays, 0, "M4.13 bounded summary producer left dirty days queued.");
  return { batches: records.length, remainingDirtyDays };
}

function pointHash(points) {
  const bytes = Buffer.alloc(points.length * 16);
  points.forEach(([lat, lon, utc, speed, satellites, flags], index) => {
    const offset = index * 16;
    bytes.writeInt32LE(lat, offset);
    bytes.writeInt32LE(lon, offset + 4);
    bytes.writeUInt32LE(utc, offset + 8);
    bytes.writeUInt16LE(speed, offset + 12);
    bytes.writeUInt8(satellites, offset + 14);
    bytes.writeUInt8(flags, offset + 15);
  });
  return createHash("sha256").update(bytes).digest("base64url");
}

function bogotaLocalDate(date) {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: "America/Bogota", year: "numeric", month: "2-digit", day: "2-digit",
  }).formatToParts(date);
  const value = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${value.year}-${value.month}-${value.day}`;
}

function lateRequest(captured) {
  const body = JSON.parse(captured.body);
  const chunk = body.upload?.chunks?.[0];
  const summary = body.upload?.summaries?.[0];
  assert.ok(chunk && summary && Array.isArray(chunk.points) && chunk.points.length === 3,
    "M4.13 simulator did not provide the expected real upload.");
  const previousBoot = Number(chunk.boot_sequence);
  assert.ok(Number.isSafeInteger(previousBoot) && previousBoot < 4_294_967_295,
    "M4.13 simulator boot counter cannot advance safely.");
  const bootSequence = previousBoot + 1;
  const previousEnd = Number(chunk.points.at(-1)[2]);
  assert.ok(Number.isSafeInteger(previousEnd) && previousEnd >= 8,
    "M4.13 simulator event time is invalid.");
  const pointTimes = [previousEnd - 8, previousEnd - 5, previousEnd - 2];
  const timezoneDate = bogotaLocalDate(new Date(pointTimes.at(-1) * 1000));

  body.request_id = randomUUID();
  body.device.boot_sequence = bootSequence;
  body.clock.utc_ms = Date.now();
  chunk.boot_sequence = bootSequence;
  chunk.points.forEach((point, index) => { point[2] = pointTimes[index]; });
  chunk.content_sha256 = pointHash(chunk.points);
  body.diagnostics.oldest_unacknowledged_utc_ms = pointTimes[0] * 1000;
  summary.summary_id = randomUUID();
  summary.source_revision = Number(summary.source_revision) + 1;
  summary.local_date = timezoneDate;
  summary.window_start = new Date(pointTimes[0] * 1000).toISOString();
  summary.window_end = new Date(pointTimes.at(-1) * 1000).toISOString();
  summary.observed_s = pointTimes.at(-1) - pointTimes[0];
  summary.moving_s = pointTimes[1] - pointTimes[0];
  summary.inactive_s = summary.observed_s - summary.moving_s;
  summary.distance_m = 0;
  summary.max_speed_cmps = Math.max(...chunk.points.map((point) => point[3]));
  summary.valid_points = chunk.points.length;
  summary.gap_count = 0;
  summary.dropped_points = 0;
  return JSON.stringify(body);
}

function summaryElements(page) {
  return page.locator(".today-section").filter({
    has: page.getByRole("heading", { name: "Cobertura de hoy" }),
  }).locator(".computed-summary");
}

async function readVisibleSummary(page, allowedStates) {
  const summary = summaryElements(page);
  await summary.locator("strong").waitFor({ state: "visible" });
  const label = (await summary.locator("strong").innerText()).trim();
  assert.ok(allowedStates.includes(label), "M4.13 daily summary state was not visible.");
  const observed = summary.locator("dl.today-facts > div").filter({ hasText: "Tiempo observado" });
  await observed.locator("dd").waitFor({ state: "visible" });
  return (await observed.locator("dd").innerText()).trim();
}

function artifactFor(phase, checkpoints, failureStage, counts, flags) {
  return {
    schemaVersion: 1,
    phase,
    failureStage,
    checkpoints,
    counts: {
      baselinePoints: counts.baselinePoints,
      pointsAfterExactRetry: counts.pointsAfterExactRetry,
      pointsAfterLateUpload: counts.pointsAfterLateUpload,
      firstWorkerBatches: counts.firstWorkerBatches,
      secondWorkerBatches: counts.secondWorkerBatches,
      dirtyDaysAfterSecondWorker: counts.dirtyDaysAfterSecondWorker,
    },
    flags: {
      exactRetryDidNotDuplicate: flags.exactRetryDidNotDuplicate,
      firstSummaryVisible: flags.firstSummaryVisible,
      lateArrivalBecamePending: flags.lateArrivalBecamePending,
      recomputedMetricChangedVisibly: flags.recomputedMetricChangedVisibly,
      temporaryDogDeleted: flags.temporaryDogDeleted,
    },
  };
}

export async function runSummaryReplay({
  browserType,
  fixture,
  portalUrl,
  apiUrl,
  publishableKey,
  outputDirectory,
}) {
  let dogId;
  let ownerId;
  let browser;
  let page;
  let phase = "failed";
  let failureStage = "fixture-validation";
  const checkpoints = [];
  const counts = {
    baselinePoints: 0,
    pointsAfterExactRetry: 0,
    pointsAfterLateUpload: 0,
    firstWorkerBatches: 0,
    secondWorkerBatches: 0,
    dirtyDaysAfterSecondWorker: 0,
  };
  const flags = {
    exactRetryDidNotDuplicate: false,
    firstSummaryVisible: false,
    lateArrivalBecamePending: false,
    recomputedMetricChangedVisibly: false,
    temporaryDogDeleted: false,
  };
  const stage = (name) => {
    assert.equal(FAILURE_STAGES.has(name), true, "M4.13 failure stage is not registered.");
    failureStage = name;
    console.log(`Summary replay: running ${name}.`);
  };
  const checkpoint = (name) => {
    assert.equal(name, CHECKPOINTS[checkpoints.length], "M4.13 checkpoint order changed.");
    checkpoints.push(name);
    console.log(`Summary replay: ${name} passed.`);
  };

  try {
    assert.equal(typeof browserType?.launch, "function", "M4.13 browser type is missing.");
    assert.ok([1, 2].includes(fixture?.cycle), "M4.13 authorization cycle is invalid.");
    const account = fixture?.accounts?.ownerA;
    assert.equal(typeof account?.email, "string", "M4.13 owner account is missing.");
    assert.match(account.email, /^[a-z0-9.+_-]+@example\.test$/u,
      "M4.13 owner account is outside the local fixture boundary.");
    ownerId = requireUuid(account.id, "owner id");

    stage("local-boundary");
    localHttpUrl(apiUrl, "Supabase API");
    localHttpUrl(portalUrl, "portal");
    assert.equal(typeof publishableKey, "string", "M4.13 publishable key is missing.");
    assert.ok(publishableKey.length > 0, "M4.13 publishable key is empty.");

    stage("owner-login");
    const ownerToken = await authorizationPasswordLogin(
      apiUrl, publishableKey, account, authorizationPassword(fixture.cycle),
    );

    stage("temporary-dog");
    const created = await authorizationInvokeRpc(apiUrl, publishableKey, ownerToken, "create_dog_v1", {
      p_name: `M413 Replay ${fixture.cycle} ${randomUUID().slice(0, 8)}`,
      p_timezone: "America/Bogota",
    });
    assert.equal(created.status, 200, "M4.13 temporary dog could not be created.");
    dogId = requireUuid(created.payload, "temporary dog id");
    checkpoint("temporary-dog-created");

    browser = await browserType.launch({ headless: true });
    const context = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    page = await context.newPage();
    page.setDefaultTimeout(15_000);
    page.setDefaultNavigationTimeout(20_000);
    await page.goto("/login");
    await page.getByLabel("Correo").fill(account.email);
    await page.getByLabel("Contraseña").fill(authorizationPassword(fixture.cycle));
    await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await page.waitForURL((url) => url.pathname !== "/login");

    stage("first-claim-and-upload");
    const claim = await authorizationRequestJson(`${apiUrl}/functions/v1/user-v1-issue-claim`, {
      method: "POST",
      publishableKey,
      accessToken: ownerToken,
      body: { protocol_version: 1, request_id: randomUUID(), dog_id: dogId },
    });
    const claimCode = claim.payload?.claim?.code;
    assert.equal(claim.status, 200, "M4.13 local claim issuance failed.");
    assert.match(claimCode ?? "", CLAIM_PATTERN, "M4.13 local claim code is invalid.");

    let capturedSync = null;
    let exactReplaySucceeded = false;
    let pointsAfterFirstRequest = 0;
    const fetchImpl = async (input, init) => {
      if (new URL(input).pathname === "/functions/v1/device-v1-sync" && !capturedSync) {
        assert.equal(typeof init?.body, "string", "M4.13 simulator sync body is missing.");
        capturedSync = {
          url: String(input),
          body: init.body,
          headers: { ...init.headers },
        };
        const first = await fetch(input, init);
        assert.equal(first.status, 200, "M4.13 first real telemetry upload failed.");
        pointsAfterFirstRequest = dogPointCount(dogId);
        const firstBody = await first.clone().json();
        const retry = await fetch(input, {
          ...init,
          body: capturedSync.body,
          headers: { ...capturedSync.headers },
        });
        const retryBody = await retry.clone().json();
        assert.equal(retry.status, 200, "M4.13 exact telemetry retry failed.");
        assert.deepEqual(retryBody.telemetry, firstBody.telemetry,
          "M4.13 exact retry did not return its committed acknowledgements.");
        exactReplaySucceeded = true;
        return retry;
      }
      return fetch(input, init);
    };
    const simulator = await createPairOnlySimulator({
      apiUrl, claimCode, expectedDogId: dogId, fetchImpl,
    });
    const paired = await simulator.attempt();
    assert.equal(paired.ok, true, "M4.13 first simulator pairing failed.");
    const firstUpload = await simulator.uploadJourneyRecording();
    assert.equal(firstUpload.acceptedPointCount, 3, "M4.13 first upload acknowledgement changed.");
    assert.equal(exactReplaySucceeded, true, "M4.13 exact replay did not run.");
    counts.baselinePoints = pointsAfterFirstRequest;
    counts.pointsAfterExactRetry = dogPointCount(dogId);
    assert.equal(counts.baselinePoints, 3, "M4.13 first upload did not persist three points.");
    flags.exactRetryDidNotDuplicate = counts.pointsAfterExactRetry === counts.baselinePoints;
    assert.equal(flags.exactRetryDidNotDuplicate, true,
      "M4.13 exact retry duplicated persisted telemetry points.");
    checkpoint("first-real-upload-and-exact-retry");

    stage("first-summary-worker");
    const firstWorker = runBoundedSummaryProducer();
    counts.firstWorkerBatches = firstWorker.batches;
    stage("first-visible-summary");
    await page.goto(`/app/${dogId}/today`);
    await page.getByRole("heading", { name: "Resumen de hoy." }).waitFor({ state: "visible" });
    const firstObserved = await readVisibleSummary(page, ["Resumen calculado", "Resumen desactualizado"]);
    flags.firstSummaryVisible = true;
    checkpoint("first-summary-recomputed-and-visible");

    stage("late-upload");
    assert.ok(capturedSync, "M4.13 original sync request was not retained in memory.");
    const changedBody = lateRequest(capturedSync);
    const lateResponse = await fetch(capturedSync.url, {
      method: "POST",
      headers: {
        ...capturedSync.headers,
        "content-length": String(Buffer.byteLength(changedBody)),
      },
      body: changedBody,
      signal: AbortSignal.timeout(15_000),
    });
    const lateBody = await lateResponse.clone().json();
    assert.equal(lateResponse.status, 200, "M4.13 late telemetry upload was rejected.");
    assert.equal(lateBody.request_id, JSON.parse(changedBody).request_id,
      "M4.13 late telemetry request identity drifted.");
    assert.equal(lateBody.telemetry?.accepted_chunks?.length, 1,
      "M4.13 late telemetry chunk was not acknowledged.");
    assert.equal(lateBody.telemetry.accepted_chunks[0]?.accepted_point_count, 3,
      "M4.13 late telemetry acknowledgement changed the point count.");
    counts.pointsAfterLateUpload = dogPointCount(dogId);
    assert.equal(counts.pointsAfterLateUpload, 6,
      "M4.13 late upload did not add exactly three new points.");

    stage("late-pending-summary");
    await page.goto(`/app/${dogId}/today`);
    const pendingSummary = summaryElements(page);
    await pendingSummary.getByText("Pendiente de cálculo", { exact: true }).waitFor({ state: "visible" });
    flags.lateArrivalBecamePending = true;
    checkpoint("late-upload-invalidated-summary");

    stage("late-summary-worker");
    const lateWorker = runBoundedSummaryProducer();
    counts.secondWorkerBatches = lateWorker.batches;
    counts.dirtyDaysAfterSecondWorker = lateWorker.remainingDirtyDays;

    stage("late-visible-summary");
    await page.goto(`/app/${dogId}/today`);
    await page.getByRole("heading", { name: "Resumen de hoy." }).waitFor({ state: "visible" });
    const lateObserved = await readVisibleSummary(page, ["Resumen calculado", "Resumen desactualizado"]);
    flags.recomputedMetricChangedVisibly = lateObserved !== firstObserved;
    assert.equal(flags.recomputedMetricChangedVisibly, true,
      "M4.13 recomputed daily metric did not change visibly after the late upload.");
    checkpoint("late-summary-recomputed-and-visible");

    phase = "passed";
  } catch {
    phase = "failed";
  } finally {
    await browser?.close().catch(() => undefined);
    if (dogId) {
      const failedAt = failureStage;
      if (phase === "passed") stage("cleanup");
      else console.log("Summary replay: running cleanup.");
      try {
        const deleted = Number(databaseScalar(`
          with removed as (
            delete from api.dogs
            where id = '${dogId}'::uuid and created_by = '${ownerId}'::uuid
            returning id
          ) select count(*)::text from removed
        `));
        assert.equal(deleted, 1, "M4.13 temporary dog cleanup did not delete exactly one fixture dog.");
        flags.temporaryDogDeleted = true;
        if (phase === "passed" && checkpoints.length === CHECKPOINTS.length - 1) {
          checkpoint("temporary-dog-cleaned");
        }
      } catch {
        phase = "failed";
        failureStage = "cleanup";
      }
      if (phase === "failed" && failureStage !== "cleanup") failureStage = failedAt;
    }
    if (phase === "failed" && !FAILURE_STAGES.has(failureStage)) failureStage = "fixture-validation";
    await mkdir(outputDirectory, { recursive: true });
    await writeFile(join(outputDirectory, "summary-replay.json"), `${JSON.stringify(
      artifactFor(phase, checkpoints, phase === "passed" ? null : failureStage, counts, flags), null, 2,
    )}\n`);
  }

  if (phase === "failed") throw new Error(`M4.13 summary replay failed at ${failureStage}.`);
  return artifactFor(phase, checkpoints, null, counts, flags);
}
