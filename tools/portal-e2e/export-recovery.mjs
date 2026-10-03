import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { authorizationPassword } from "./authorization-fixtures.mjs";
import { createLocalExpiredSessionPair } from "./expired-session.mjs";

const CHECKPOINTS = Object.freeze([
  "signed-session-json-and-geojson-controls",
  "transport-failure-observed",
  "json-export-retry-succeeded",
  "expired-session-denied-on-json-and-geojson",
]);
const AUTH_COOKIE_PATTERN = /^(sb-.+-auth-token)(?:\.(0|[1-9][0-9]*))?$/u;
const COOKIE_CHUNK_SIZE = 3180;
const MAX_EXPORT_BYTES = 16 * 1024 * 1024;

function artifactFor(phase, checkpoints, counts, failureStage = null) {
  return {
    schemaVersion: 1,
    phase,
    checkpoints,
    failureStage,
    counts: {
      signedSessionControl: counts.signedSessionControl,
      transportFailures: counts.transportFailures,
      successfulRetries: counts.successfulRetries,
      expiredSessionRejections: counts.expiredSessionRejections,
      completeExports: counts.completeExports,
      completeGeoJsonExports: counts.completeGeoJsonExports,
      privateNoStoreResponses: counts.privateNoStoreResponses,
    },
  };
}

function cookieGroups(cookies) {
  const groups = new Map();
  for (const cookie of cookies) {
    const match = cookie.name.match(AUTH_COOKIE_PATTERN);
    if (!match) continue;
    const group = groups.get(match[1]) ?? new Map();
    group.set(cookie.name, cookie);
    groups.set(match[1], group);
  }
  return groups;
}

function readCookieValue(key, group) {
  const root = group.get(key);
  if (root) return root.value;
  let value = "";
  for (let index = 0; group.has(`${key}.${index}`); index += 1) {
    value += group.get(`${key}.${index}`).value;
  }
  return value || null;
}

function decodeSessionCookie(value) {
  assert.equal(typeof value, "string");
  assert.equal(value.startsWith("base64-"), true);
  const session = JSON.parse(Buffer.from(value.slice("base64-".length), "base64url").toString("utf8"));
  assert.equal(typeof session?.access_token, "string");
  assert.equal(typeof session?.refresh_token, "string");
  return session;
}

function tokenExpiry(token) {
  const payload = JSON.parse(Buffer.from(token.split(".")[1], "base64url").toString("utf8"));
  assert.equal(Number.isSafeInteger(payload.exp), true);
  return payload.exp;
}

function tokenClaims(token) {
  return JSON.parse(Buffer.from(token.split(".")[1], "base64url").toString("utf8"));
}

function encodeSessionCookie(session) {
  return `base64-${Buffer.from(JSON.stringify(session), "utf8").toString("base64url")}`;
}

function chunksForCookie(key, value) {
  const chunks = [];
  for (let offset = 0; offset < value.length; offset += COOKIE_CHUNK_SIZE) {
    chunks.push({
      name: value.length <= COOKIE_CHUNK_SIZE ? key : `${key}.${chunks.length}`,
      value: value.length <= COOKIE_CHUNK_SIZE ? value : value.slice(offset, offset + COOKIE_CHUNK_SIZE),
    });
  }
  return chunks;
}

function playwrightCookie(cookie, name, value, expires = cookie.expires) {
  const result = {
    name,
    value,
    domain: cookie.domain,
    path: cookie.path,
    secure: cookie.secure,
    httpOnly: cookie.httpOnly,
    sameSite: cookie.sameSite,
  };
  if (expires >= 0) result.expires = expires;
  return result;
}

async function replaceAuthSession(context, key, group, session) {
  const encoded = encodeSessionCookie(session);
  const chunks = chunksForCookie(key, encoded);
  const template = group.values().next().value;
  assert.ok(template);
  const replacements = chunks.map(({ name, value }) => playwrightCookie(template, name, value));
  const replacementNames = new Set(chunks.map(({ name }) => name));
  for (const cookie of group.values()) {
    if (!replacementNames.has(cookie.name)) {
      replacements.push(playwrightCookie(cookie, cookie.name, "", 1));
    }
  }
  await context.addCookies(replacements);
}

function sessionCookieGroup(cookies) {
  for (const [key, group] of cookieGroups(cookies)) {
    const value = readCookieValue(key, group);
    if (!value?.startsWith("base64-")) continue;
    try {
      const session = decodeSessionCookie(value);
      return { key, group, session };
    } catch {
      // Ignore non-session auth cookies such as PKCE state.
    }
  }
  throw new Error("No Supabase SSR auth session cookie was found.");
}

function isCompleteExport(document, dogId) {
  return document && typeof document === "object" && !Array.isArray(document) &&
    document.schema_version === 1 && document.complete === true &&
    document.export_type === "dog_data" && document.dog?.id === dogId &&
    typeof document.snapshot_at === "string" && Number.isFinite(Date.parse(document.snapshot_at)) &&
    typeof document.timezone === "string" && document.dog?.timezone === document.timezone &&
    Array.isArray(document.collars) && Array.isArray(document.recordings) &&
    Array.isArray(document.daily_summaries) && Array.isArray(document.recording_summaries) &&
    Array.isArray(document.telemetry_points) && Array.isArray(document.loss_markers) &&
    document.configuration && Array.isArray(document.configuration.resource_heads) &&
    Array.isArray(document.configuration.device_reported) && Array.isArray(document.configuration.revisions);
}

function isCompleteGeoJson(document, recordingId) {
  return document && typeof document === "object" && !Array.isArray(document) &&
    document.type === "FeatureCollection" && document.complete === true &&
    document.metadata?.schema_version === 1 && document.metadata?.recording?.id === recordingId &&
    typeof document.metadata?.timezone === "string" && Array.isArray(document.features);
}

async function readDownloadJson(download) {
  assert.equal(await download.failure(), null);
  const stream = await download.createReadStream();
  assert.ok(stream);
  const chunks = [];
  let byteLength = 0;
  for await (const chunk of stream) {
    byteLength += chunk.byteLength;
    assert.ok(byteLength <= MAX_EXPORT_BYTES);
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks, byteLength).toString("utf8"));
}

function privateDownload(response, { filename, contentType }) {
  const headers = response.headers();
  return response.status() === 200 &&
    filename.test(headers["content-disposition"] ?? "") &&
    (headers["content-type"] ?? "").startsWith(contentType) &&
    (headers["cache-control"] ?? "").includes("private") &&
    (headers["cache-control"] ?? "").includes("no-store");
}

export async function runExportRecovery({
  browserType,
  fixture,
  portalUrl,
  apiUrl,
  publishableKey,
  outputDirectory,
}) {
  void publishableKey;
  let dogId;
  let recordingId;
  let account;
  let password;
  let exportUrl;
  let geoJsonUrl;
  const checkpoints = [];
  const counts = {
    signedSessionControl: 0,
    transportFailures: 0,
    successfulRetries: 0,
    expiredSessionRejections: 0,
    completeExports: 0,
    completeGeoJsonExports: 0,
    privateNoStoreResponses: 0,
  };
  let browser;
  let phase = "failed";
  let failureStage = "fixture-login";
  let failed = false;

  const checkpoint = (name) => {
    assert.equal(name, CHECKPOINTS[checkpoints.length]);
    checkpoints.push(name);
    console.log(`M5.6a: ${name} passed.`);
  };

  try {
    dogId = fixture?.dogA?.id;
    recordingId = fixture?.dogA?.recordingId;
    account = fixture?.accounts?.ownerA;
    assert.ok(account?.email);
    assert.match(dogId ?? "", /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu);
    assert.match(recordingId ?? "", /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu);
    password = authorizationPassword(fixture?.cycle);
    exportUrl = new URL(`/app/${dogId}/data/export`, portalUrl).toString();
    geoJsonUrl = new URL(`/app/${dogId}/recordings/${recordingId}/geojson`, portalUrl).toString();
    browser = await browserType.launch({ headless: true });
    const loginContext = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    const loginPage = await loginContext.newPage();
    loginPage.setDefaultTimeout(15_000);
    loginPage.setDefaultNavigationTimeout(20_000);

    await loginPage.goto("/login");
    await loginPage.getByLabel("Correo").fill(account.email);
    await loginPage.getByLabel("Contraseña").fill(password);
    await loginPage.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await loginPage.waitForURL((url) => url.pathname !== "/login");

    failureStage = "read-ssr-session-cookie";
    const captured = sessionCookieGroup(await loginContext.cookies(portalUrl));
    const pair = createLocalExpiredSessionPair({ apiUrl, accessToken: captured.session.access_token });
    const unusableRefreshToken = "x".repeat(captured.session.refresh_token.length);
    assert.notEqual(unusableRefreshToken, captured.session.refresh_token);
    const validSession = {
      ...captured.session,
      access_token: pair.validAccessToken,
      refresh_token: unusableRefreshToken,
      expires_at: tokenExpiry(pair.validAccessToken),
    };
    const expiredSession = {
      ...captured.session,
      access_token: pair.expiredAccessToken,
      refresh_token: unusableRefreshToken,
      expires_at: tokenExpiry(pair.expiredAccessToken),
    };
    const validClaims = tokenClaims(pair.validAccessToken);
    const expiredClaims = tokenClaims(pair.expiredAccessToken);
    delete validClaims.exp;
    delete expiredClaims.exp;
    assert.deepEqual(validClaims, expiredClaims);

    failureStage = "signed-session-control";
    const context = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block" });
    await replaceAuthSession(context, captured.key, captured.group, validSession);
    const page = await context.newPage();
    page.setDefaultTimeout(15_000);
    page.setDefaultNavigationTimeout(20_000);
    await page.goto(`/app/${dogId}/data`);
    await page.getByRole("link", { name: "Descargar datos JSON" }).waitFor();

    const control = await context.request.get(exportUrl, { maxRedirects: 0, timeout: 15_000 });
    assert.equal(privateDownload(control, {
      filename: /^attachment;\s*filename="dog-rgb-[0-9a-f-]+\.json"$/iu,
      contentType: "application/json",
    }), true);
    const geoJsonControl = await context.request.get(geoJsonUrl, { maxRedirects: 0, timeout: 15_000 });
    assert.equal(privateDownload(geoJsonControl, {
      filename: /^attachment;\s*filename="recording-[0-9a-f-]+\.geojson"$/iu,
      contentType: "application/geo+json",
    }), true);
    assert.equal(isCompleteGeoJson(await geoJsonControl.json(), recordingId), true);
    counts.signedSessionControl = 2;
    counts.privateNoStoreResponses += 2;
    counts.completeGeoJsonExports = 1;
    checkpoint("signed-session-json-and-geojson-controls");

    failureStage = "transport-failure-retry";
    let intercepted = false;
    await page.route(exportUrl, async (route) => {
      if (!intercepted) {
        intercepted = true;
        await route.abort("failed");
      } else {
        await route.continue();
      }
    });
    const failedFetch = await page.evaluate(async (url) => {
      try {
        const response = await fetch(url, { credentials: "same-origin" });
        return { transportFailed: false, status: response.status };
      } catch {
        return { transportFailed: true };
      }
    }, exportUrl);
    assert.equal(failedFetch.transportFailed, true);
    assert.equal(intercepted, true);
    counts.transportFailures = 1;
    checkpoint("transport-failure-observed");

    const retryResponseEvent = page.waitForResponse((response) =>
      response.url() === exportUrl && response.request().method() === "GET", { timeout: 15_000 });
    const downloadEvent = page.waitForEvent("download", { timeout: 15_000 });
    await page.getByRole("link", { name: "Descargar datos JSON" }).click();
    const [retryResponse, download] = await Promise.all([retryResponseEvent, downloadEvent]);
    assert.equal(privateDownload(retryResponse, {
      filename: /^attachment;\s*filename="dog-rgb-[0-9a-f-]+\.json"$/iu,
      contentType: "application/json",
    }), true);
    counts.privateNoStoreResponses += 1;
    const document = await readDownloadJson(download);
    assert.equal(isCompleteExport(document, dogId), true);
    counts.completeExports = 1;
    counts.successfulRetries = 1;
    checkpoint("json-export-retry-succeeded");
    await page.unroute(exportUrl);

    failureStage = "expired-signed-session-denial";
    await replaceAuthSession(context, captured.key, captured.group, expiredSession);
    const denied = await context.request.get(exportUrl, { maxRedirects: 0, timeout: 15_000 });
    assert.equal(denied.status(), 401);
    assert.equal(denied.headers()["content-disposition"] ?? "", "");
    await replaceAuthSession(context, captured.key, captured.group, expiredSession);
    const geoJsonDenied = await context.request.get(geoJsonUrl, { maxRedirects: 0, timeout: 15_000 });
    assert.equal(geoJsonDenied.status(), 401);
    assert.equal(geoJsonDenied.headers()["content-disposition"] ?? "", "");
    counts.expiredSessionRejections = 2;
    checkpoint("expired-session-denied-on-json-and-geojson");
    phase = "passed";
  } catch {
    failed = true;
  } finally {
    await browser?.close().catch(() => undefined);
    try {
      await mkdir(outputDirectory, { recursive: true });
      await writeFile(join(outputDirectory, "export-recovery.json"), `${JSON.stringify(
        artifactFor(phase, checkpoints, counts, failed ? failureStage : null), null, 2,
      )}\n`);
    } catch {
      failed = true;
      failureStage = "artifact-write";
    }
  }

  if (failed) {
    throw new Error(`M5.6a export recovery failed at ${failureStage}.`);
  }
  return artifactFor(phase, checkpoints, counts, null);
}
