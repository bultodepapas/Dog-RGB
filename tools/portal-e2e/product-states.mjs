import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { expect } from "@playwright/test";

import { authorizationPassword } from "./authorization-fixtures.mjs";
import { closePsqlSession, spawnPsqlSession, waitForOutput } from "./m115-fault-matrix.mjs";

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const DATABASE_CONTAINER = "supabase_db_Dog-RGB-1";
const CHECKPOINT_NAMES = Object.freeze([
  "claimExpired",
  "collarStaleToday",
  "collarStaleConfiguration",
  "brightnessUnsupported",
  "brightnessFailureShowsSameValueRetry",
  "brightnessSameValueRetrySucceeds",
  "brightnessReadOnlyForViewer",
]);
const FAILURE_STAGES = new Set([
  "fixture-validation",
  "owner-login",
  "claim-expiry",
  "claim-clock-restore",
  "stale-freshness",
  "unsupported-brightness",
  "brightness-mutation-failure",
  "brightness-mutation-retry",
  "viewer-read-only",
  "claim-cleanup",
  "browser-cleanup",
  "artifact-write",
]);

function fixtureUuid(value, label) {
  assert.equal(typeof value, "string", `${label} is missing`);
  assert.match(value, UUID_PATTERN, `${label} is invalid`);
  return value;
}

function sqlString(value, label) {
  assert.equal(typeof value, "string", `${label} is missing`);
  assert.ok(value.length > 0 && value.length <= 128, `${label} has an invalid length`);
  return `'${value.replaceAll("'", "''")}'`;
}

function sqlTimestamp(value, label) {
  assert.equal(typeof value, "string", `${label} is missing`);
  assert.ok(Number.isFinite(Date.parse(value)), `${label} is not a timestamp`);
  return `${sqlString(value, label)}::timestamptz`;
}

function psql(sql) {
  try {
    return execFileSync("docker", [
      "exec", "-i", DATABASE_CONTAINER,
      "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
      "-U", "supabase_admin", "-d", "postgres",
    ], {
      input: sql,
      encoding: "utf8",
      stdio: ["pipe", "pipe", "pipe"],
      timeout: 15_000,
      maxBuffer: 1024 * 1024,
      windowsHide: true,
    }).trim();
  } catch {
    throw new Error("M1.21 scoped product-state fixture operation failed.");
  }
}

function queryRows(sql) {
  const result = psql(`
    select coalesce(jsonb_agg(to_jsonb(q)), '[]'::jsonb)::text
    from (${sql}) q;
  `);
  try {
    return JSON.parse(result);
  } catch {
    throw new Error("M1.21 scoped product-state fixture returned invalid evidence.");
  }
}

function exactRow(rows, label) {
  assert.equal(rows.length, 1, `${label} fixture row was not unique`);
  return rows[0];
}

function scopedCollarQuery(dogId, collarId, selectSql) {
  return `select ${selectSql} from api.collars
    where id = '${collarId}'::uuid and dog_id = '${dogId}'::uuid and state = 'active'`;
}

function captureFreshness(dogId, collarId) {
  const row = exactRow(queryRows(scopedCollarQuery(
    dogId,
    collarId,
    "last_sync_at::text as last_sync_at, diagnostics_observed_at::text as diagnostics_observed_at, oldest_unacknowledged_at::text as oldest_unacknowledged_at",
  )), "collar freshness");
  assert.equal(typeof row.last_sync_at, "string", "fixture collar has no last sync");
  assert.ok(Number.isFinite(Date.parse(row.last_sync_at)), "fixture collar last sync is invalid");
  return row;
}

function setStaleFreshness(dogId, collarId) {
  const changed = psql(`
    with updated as (
      update api.collars
      set last_sync_at = last_sync_at - interval '25 hours',
          diagnostics_observed_at = diagnostics_observed_at - interval '25 hours',
          oldest_unacknowledged_at = oldest_unacknowledged_at - interval '25 hours'
      where id = '${collarId}'::uuid and dog_id = '${dogId}'::uuid and state = 'active'
      returning id
    ) select count(*) from updated;
  `);
  assert.equal(changed, "1", "scoped collar freshness update did not affect one row");
}

function restoreFreshness(dogId, collarId, saved) {
  const diagnosticsAt = saved.diagnostics_observed_at === null ? "null"
    : sqlTimestamp(saved.diagnostics_observed_at, "saved diagnostics time");
  const oldestAt = saved.oldest_unacknowledged_at === null ? "null"
    : sqlTimestamp(saved.oldest_unacknowledged_at, "saved outbox time");
  const changed = psql(`
    with updated as (
      update api.collars
      set last_sync_at = ${sqlTimestamp(saved.last_sync_at, "saved collar freshness")},
          diagnostics_observed_at = ${diagnosticsAt}, oldest_unacknowledged_at = ${oldestAt}
      where id = '${collarId}'::uuid and dog_id = '${dogId}'::uuid and state = 'active'
      returning id
    ) select count(*) from updated;
  `);
  assert.equal(changed, "1", "scoped collar freshness restore did not affect one row");
}

function captureBrightnessReport(collarId) {
  const row = exactRow(queryRows(`
    select status, error_code, device_applied_at::text as device_applied_at,
      cloud_received_at::text as cloud_received_at,
      reported_server_version, encode(reported_body_sha256, 'hex') as body_sha256
    from api.config_reported
    where collar_id = '${collarId}'::uuid and resource_key = 'brightness'
  `), "brightness report");
  assert.equal(row.status, "applied", "fixture brightness was not applied before the test");
  assert.equal(row.error_code, null, "fixture brightness has an unexpected error");
  assert.ok(Number.isSafeInteger(row.reported_server_version), "fixture report version is invalid");
  assert.match(row.body_sha256, /^[0-9a-f]{64}$/u, "fixture report hash is invalid");
  assert.ok(Number.isFinite(Date.parse(row.cloud_received_at)), "fixture report timestamp is invalid");
  return row;
}

function setUnsupportedBrightnessReport(collarId) {
  const changed = psql(`
    with updated as (
      update api.config_reported
      set status = 'rejected_unsupported', error_code = 'unsupported_config',
          device_applied_at = null, cloud_received_at = statement_timestamp()
      where collar_id = '${collarId}'::uuid and resource_key = 'brightness'
      returning collar_id
    ) select count(*) from updated;
  `);
  assert.equal(changed, "1", "scoped unsupported report update did not affect one row");
}

function restoreBrightnessReport(collarId, saved) {
  const errorCode = saved.error_code === null ? "null" : sqlString(saved.error_code, "saved report error code");
  const appliedAt = saved.device_applied_at === null
    ? "null"
    : sqlTimestamp(saved.device_applied_at, "saved device applied time");
  const changed = psql(`
    with updated as (
      update api.config_reported
      set status = ${sqlString(saved.status, "saved report status")},
          error_code = ${errorCode}, device_applied_at = ${appliedAt},
          cloud_received_at = ${sqlTimestamp(saved.cloud_received_at, "saved cloud report time")}
      where collar_id = '${collarId}'::uuid and resource_key = 'brightness'
        and reported_server_version = ${saved.reported_server_version}
        and encode(reported_body_sha256, 'hex') = ${sqlString(saved.body_sha256, "saved report hash")}
      returning collar_id
    ) select count(*) from updated;
  `);
  assert.equal(changed, "1", "scoped brightness report restore did not affect the original row");
}

async function signIn(page, account, password) {
  await page.goto("/login");
  await page.getByLabel("Correo").fill(account.email);
  await page.getByLabel("Contraseña").fill(password);
  await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
  await page.waitForURL((url) => url.pathname !== "/login");
}

export async function runProductStates({
  browserType,
  fixture,
  portalUrl,
  apiUrl,
  publishableKey,
  outputDirectory,
}) {
  assert.ok(browserType && typeof browserType.launch === "function", "browserType is required");
  assert.ok(typeof portalUrl === "string" && typeof apiUrl === "string", "portal and API URLs are required");
  assert.ok(typeof publishableKey === "string" && publishableKey.length > 0, "publishable key is required");
  assert.ok(typeof outputDirectory === "string" && outputDirectory.length > 0, "output directory is required");
  assert.ok(fixture && (fixture.cycle === 1 || fixture.cycle === 2), "authorization fixture is required");

  const counts = Object.fromEntries(CHECKPOINT_NAMES.map((name) => [name, 0]));
  const checkpoint = (name) => {
    assert.equal(name, CHECKPOINT_NAMES[Object.values(counts).reduce((sum, count) => sum + count, 0)],
      "M1.21 checkpoint order changed");
    counts[name] = 1;
    console.log(`M1.21 product state passed: ${name}.`);
  };

  let browser;
  let ownerContext;
  let ownerPage;
  let viewerContext;
  let fixtureClaimStartedAt = null;
  let dogId;
  let collarId;
  let owner;
  let viewer;
  let failureStage = "fixture-validation";
  let failed = false;
  const stage = (name) => {
    assert.equal(FAILURE_STAGES.has(name), true, "M1.21 failure stage is not registered");
    failureStage = name;
  };
  try {
    dogId = fixtureUuid(fixture.dogA?.id, "owner A dog id");
    collarId = fixtureUuid(fixture.dogA?.collarId, "owner A collar id");
    owner = fixture.accounts?.ownerA;
    viewer = fixture.accounts?.viewer;
    fixtureUuid(owner?.id, "owner A user id");
    fixtureUuid(viewer?.id, "viewer user id");

    stage("owner-login");
    browser = await browserType.launch({ headless: true });
    ownerContext = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    ownerPage = await ownerContext.newPage();
    ownerPage.setDefaultTimeout(15_000);
    ownerPage.setDefaultNavigationTimeout(20_000);
    await signIn(ownerPage, owner, authorizationPassword(fixture.cycle));

    const issuedBefore = queryRows(`
      select id from private.device_claims
      where dog_id = '${dogId}'::uuid and requested_by = '${owner.id}'::uuid and state = 'issued'
    `);
    assert.equal(issuedBefore.length, 0, "claim expiry test requires no pre-existing issued claim");
    fixtureClaimStartedAt = new Date().toISOString();
    stage("claim-expiry");
    await ownerPage.clock.install({ time: new Date() });
    await ownerPage.goto(`/app/${dogId}/collars`);
    await ownerPage.getByRole("checkbox").check();
    await ownerPage.getByRole("button", { name: "Generar código" }).click();
    await expect(ownerPage.locator("#claim-code-label")).toHaveText("CÓDIGO TEMPORAL");
    await expect(ownerPage.locator(".claim-code")).toBeVisible();
    const expiresAt = await ownerPage.locator(".claim-result time[datetime]").getAttribute("datetime");
    assert.ok(typeof expiresAt === "string" && Number.isFinite(Date.parse(expiresAt)), "claim expiry was not rendered");
    const browserNow = await ownerPage.evaluate(() => Date.now());
    await ownerPage.clock.fastForward(Math.max(0, Date.parse(expiresAt) - browserNow + 1_000));
    await expect(ownerPage.locator("#claim-code-label")).toHaveText("CÓDIGO CADUCADO");
    await expect(ownerPage.locator(".claim-code")).toHaveCount(0);
    await expect(ownerPage.getByText("Este código ya no puede vincular un collar.")).toBeVisible();
    checkpoint("claimExpired");
    stage("claim-clock-restore");
    await ownerPage.clock.setSystemTime(new Date());

    stage("stale-freshness");
    const priorLastSyncAt = captureFreshness(dogId, collarId);
    let freshnessChanged = false;
    try {
      setStaleFreshness(dogId, collarId);
      freshnessChanged = true;
      await ownerPage.goto(`/app/${dogId}/today`);
      await expect(ownerPage.getByText("SIN CONEXIÓN RECIENTE", { exact: true })).toBeVisible();
      checkpoint("collarStaleToday");
      await ownerPage.goto(`/app/${dogId}/configuration`);
      await expect(ownerPage.getByRole("note").filter({ hasText: "COLLAR SIN SINCRONIZACIÓN RECIENTE" })).toBeVisible();
      checkpoint("collarStaleConfiguration");
    } finally {
      if (freshnessChanged) {
        restoreFreshness(dogId, collarId, priorLastSyncAt);
      }
    }

    stage("unsupported-brightness");
    const priorBrightnessReport = captureBrightnessReport(collarId);
    let unsupportedChanged = false;
    try {
      setUnsupportedBrightnessReport(collarId);
      unsupportedChanged = true;
      await ownerPage.goto(`/app/${dogId}/configuration`);
      const truth = ownerPage.locator(".configuration-truth--rejected_unsupported");
      await expect(truth.getByText("RECHAZADO POR EL COLLAR", { exact: true })).toBeVisible();
      await expect(truth).toContainText("El collar reportó que no admite esta configuración.");
      checkpoint("brightnessUnsupported");
    } finally {
      if (unsupportedChanged) {
        restoreBrightnessReport(collarId, priorBrightnessReport);
      }
    }

    stage("brightness-mutation-failure");
    await ownerPage.goto(`/app/${dogId}/configuration`);
    await expect(ownerPage.locator(".configuration-truth--applied")).toBeVisible();
    await ownerPage.getByLabel("Brillo deseado").fill(String(fixture.brightness));
    // Hold only this collar row. Reads remain available, but the real RPC
    // reaches its existing statement deadline and exposes the retry state.
    const lock = spawnPsqlSession(DATABASE_CONTAINER);
    try {
      lock.write(`begin;
select id from api.collars where id = '${collarId}'::uuid for update;
select 'M121_LOCK_READY';
`);
      await waitForOutput(lock, "M121_LOCK_READY");
      await ownerPage.getByRole("button", { name: "GUARDAR BRILLO" }).click();
      await expect(ownerPage.getByText("NO PUDIMOS CONFIRMAR EL RESULTADO", { exact: true })).toBeVisible({ timeout: 20_000 });
      const retryInput = ownerPage.locator("#brightness-retry");
      await expect(retryInput).toHaveValue(String(fixture.brightness));
      await expect(retryInput).toHaveAttribute("readonly", "");
      await expect(ownerPage.getByRole("button", { name: "REINTENTAR EL MISMO VALOR" })).toBeVisible();
      checkpoint("brightnessFailureShowsSameValueRetry");
    } finally {
      await closePsqlSession(lock);
    }
    stage("brightness-mutation-retry");
    await ownerPage.getByRole("button", { name: "REINTENTAR EL MISMO VALOR" }).click();
    await expect(ownerPage.getByText("SIN CAMBIOS EN LA NUBE", { exact: true })).toBeVisible();
    await expect(ownerPage.locator(".configuration-result--unchanged")).toContainText(
      `El brillo ${fixture.brightness} ya era el valor deseado`,
    );
    checkpoint("brightnessSameValueRetrySucceeds");

    stage("viewer-read-only");
    viewerContext = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    const viewerPage = await viewerContext.newPage();
    viewerPage.setDefaultTimeout(15_000);
    viewerPage.setDefaultNavigationTimeout(20_000);
    await signIn(viewerPage, viewer, authorizationPassword(fixture.cycle));
    await viewerPage.goto(`/app/${dogId}/configuration`);
    const editSection = viewerPage.locator("#edit-title").locator("..");
    await expect(editSection.getByText("SOLO LECTURA", { exact: true })).toBeVisible();
    await expect(editSection.getByLabel("Brillo deseado")).toHaveCount(0);
    await expect(editSection.getByRole("button", { name: "GUARDAR BRILLO" })).toHaveCount(0);
    checkpoint("brightnessReadOnlyForViewer");
  } catch {
    failed = true;
  } finally {
    if (fixtureClaimStartedAt !== null && dogId && owner) {
      try {
        const removed = psql(`
          with removed as (
            delete from private.device_claims
            where dog_id = '${dogId}'::uuid and requested_by = '${owner.id}'::uuid
              and state = 'issued'
              and created_at >= ${sqlTimestamp(fixtureClaimStartedAt, "claim test start")} - interval '1 minute'
            returning id
          ) select count(*) from removed;
        `);
        assert.ok(removed === "0" || removed === "1", "claim expiry cleanup exceeded the scoped fixture");
        if (counts.claimExpired === 1) assert.equal(removed, "1", "issued test claim was not restored");
      } catch {
        if (!failed) failureStage = "claim-cleanup";
        failed = true;
      }
    }
    for (const close of [
      () => viewerContext?.close(),
      () => ownerContext?.close(),
      () => browser?.close(),
    ]) {
      try {
        await close();
      } catch {
        if (!failed) failureStage = "browser-cleanup";
        failed = true;
      }
    }
    try {
      await mkdir(outputDirectory, { recursive: true });
      await writeFile(join(outputDirectory, "product-states.json"), `${JSON.stringify({
        schemaVersion: 1,
        phase: failed ? "failed" : "passed",
        failureStage: failed ? failureStage : null,
        counts,
      }, null, 2)}\n`, { encoding: "utf8", flag: "w" });
    } catch {
      failureStage = "artifact-write";
      failed = true;
    }
  }
  if (failed) throw new Error(`M1.21 product-state flow failed at ${failureStage}.`);
  return Object.freeze({ ...counts });
}
