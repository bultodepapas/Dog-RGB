import assert from "node:assert/strict";
import { readdir, readFile, stat, writeFile, mkdir } from "node:fs/promises";
import { join } from "node:path";

export const M116_CHECKPOINTS = Object.freeze([
  "browser-build-surfaces",
  "anonymous-html-rsc",
  "authenticated-private-cache",
  "recording-route-containment",
  "server-action-payload",
  "browser-state-and-observers",
  "server-edge-database-logs",
  "retained-artifacts",
]);

const ROUTE_PAYLOAD_MARKERS = Object.freeze([
  "468123456",
  "-740123456",
  "468123500",
  "-740123400",
  "46.8123456",
  "-74.0123456",
  "46.8123500",
  "-74.0123400",
]);
const INTERNAL_ERROR_PATTERN = /(?:SQLSTATE|PGRST\d{3}|PostgrestError|stack trace|node_modules[/\\].+?:\d+:\d+)/iu;
const SENSITIVE_TOKEN_PATTERN = /(?:\bsb_secret_[A-Za-z0-9_-]{16,}\b|\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\b)/u;
const RICH_ARTIFACT_PATTERN = /(?:\.har|\.zip|\.png|\.jpeg|\.jpg|\.webm|trace\.zip)$/iu;
const MAX_INSPECTED_BODY_BYTES = 2 * 1024 * 1024;

function exactKeys(value, keys, label) {
  assert(value && typeof value === "object" && !Array.isArray(value), `${label} must be an object`);
  assert.deepEqual(Object.keys(value).sort(), [...keys].sort(), `${label} fields changed`);
}

function cacheIsNoStore(headers) {
  const cacheControl = headers["cache-control"] ?? "";
  return /(?:^|,)\s*no-store(?:\s|,|$)/iu.test(cacheControl) &&
    !/(?:^|,)\s*(?:public|s-maxage\s*=)/iu.test(cacheControl);
}

function cacheIsPrivateNoStore(headers) {
  const cacheControl = headers["cache-control"] ?? "";
  return cacheIsNoStore(headers) && /(?:^|,)\s*private(?:\s|,|$)/iu.test(cacheControl);
}

function recordingDetailPath(pathname, fixture) {
  return pathname === `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`;
}

function privateRoute(pathname) {
  return pathname === "/onboarding" || pathname.startsWith("/app/");
}

function assertNoRoutePayload(text, label) {
  assert.equal(
    ROUTE_PAYLOAD_MARKERS.some((marker) => text.includes(marker)),
    false,
    `${label} exposed an exact route payload`,
  );
}

function assertNoUnauthorizedIdentity(text, fixture, label) {
  const prohibited = [
    fixture.accounts.ownerB.email,
    fixture.dogB.name,
  ];
  assert.equal(
    prohibited.some((value) => text.includes(value)),
    false,
    `${label} exposed owner B material to owner A or an anonymous surface`,
  );
}

function assertNoPrivateMaterial(text, containsPrivateMaterial, containsInfrastructureSecret, label) {
  assert.equal(containsPrivateMaterial(text), false, `${label} exposed fixture secret material`);
  assert.equal(containsInfrastructureSecret(text), false, `${label} exposed local infrastructure secret material`);
  assert.equal(SENSITIVE_TOKEN_PATTERN.test(text), false, `${label} exposed a secret key or JWT-shaped token`);
}

async function filesBelow(root) {
  const entries = await readdir(root, { recursive: true, withFileTypes: true });
  return entries
    .filter((entry) => entry.isFile())
    .map((entry) => join(entry.parentPath, entry.name));
}

async function inspectBuildSurfaces({ portalDirectory, containsPrivateMaterial, containsInfrastructureSecret }) {
  const roots = [
    join(portalDirectory, ".next", "static"),
    join(portalDirectory, ".next", "server", "app"),
  ];
  let inspected = 0;
  for (const root of roots) {
    for (const path of await filesBelow(root)) {
      if (!/\.(?:css|html|js|json|rsc|txt)$/iu.test(path)) continue;
      const metadata = await stat(path);
      assert(metadata.size <= 8 * 1024 * 1024, "M1.16 build surface exceeded the bounded scanner size");
      const body = await readFile(path, "utf8");
      assertNoPrivateMaterial(
        body,
        containsPrivateMaterial,
        containsInfrastructureSecret,
        "browser/static build surface",
      );
      inspected += 1;
    }
  }
  assert(inspected > 0, "M1.16 found no browser/static build surface to inspect");
  return inspected;
}

function artifactFor(cycle, phase, checkpoints) {
  return {
    phase,
    cycle,
    checkpoints,
    surfaces: M116_CHECKPOINTS,
    findings: {
      secretLeaks: 0,
      unauthorizedIdentityLeaks: 0,
      routePayloadLeaks: 0,
      cacheViolations: 0,
      externalRequests: 0,
      browserErrors: 0,
      unexpectedStorageEntries: 0,
      retainedRichArtifacts: 0,
    },
  };
}

export function validateM116Artifact(value, cycle, expectedPhase) {
  const artifact = JSON.parse(Buffer.isBuffer(value) ? value.toString("utf8") : String(value));
  exactKeys(artifact, ["phase", "cycle", "checkpoints", "surfaces", "findings"], "M1.16 artifact");
  exactKeys(artifact.findings, [
    "secretLeaks",
    "unauthorizedIdentityLeaks",
    "routePayloadLeaks",
    "cacheViolations",
    "externalRequests",
    "browserErrors",
    "unexpectedStorageEntries",
    "retainedRichArtifacts",
  ], "M1.16 findings");
  assert.equal(artifact.phase, expectedPhase);
  assert.equal(artifact.cycle, cycle);
  assert.deepEqual(artifact.surfaces, M116_CHECKPOINTS);
  const expectedCheckpoints = expectedPhase === "passed"
    ? M116_CHECKPOINTS
    : M116_CHECKPOINTS.slice(0, artifact.checkpoints.length);
  assert.deepEqual(artifact.checkpoints, expectedCheckpoints);
  assert.deepEqual(artifact.findings, artifactFor(cycle, expectedPhase, []).findings);
  return artifact;
}

export async function runM116PrivacyCacheGate({
  artifactDirectory,
  browserType,
  containsInfrastructureSecret,
  cycle,
  fixture: privacyFixture,
  portalDirectory,
  portalLogs,
  portalUrl,
  readServiceLogs,
}) {
  const artifactPath = join(artifactDirectory, `cycle-${cycle}.json`);
  const checkpoints = [];
  const checkpoint = (name) => {
    assert.equal(name, M116_CHECKPOINTS[checkpoints.length], "M1.16 checkpoint order changed");
    checkpoints.push(name);
    console.log(`M1.16: ${name} passed.`);
  };
  const fixture = privacyFixture.manifest;
  const containsPrivateMaterial = privacyFixture.artifactContainsPrivateMaterial;
  let browser;

  await mkdir(artifactDirectory, { recursive: true });
  try {
    await inspectBuildSurfaces({
      portalDirectory,
      containsPrivateMaterial,
      containsInfrastructureSecret,
    });
    checkpoint("browser-build-surfaces");

    browser = await browserType.launch({ headless: true });
    const context = await browser.newContext({
      baseURL: portalUrl,
      serviceWorkers: "block",
    });
    const page = await context.newPage();
    page.setDefaultTimeout(15_000);
    page.setDefaultNavigationTimeout(20_000);
    const pendingInspections = new Set();
    const browserErrors = [];
    const externalRequests = [];
    const webSockets = [];
    const workers = [];
    const serviceWorkers = [];
    let actionResponses = 0;
    let protectedResponses = 0;
    let detailResponses = 0;
    let observerError = null;
    const inspectRuntimeText = (body, label, { allowRoute = false } = {}) => {
      assert(Buffer.byteLength(body, "utf8") <= MAX_INSPECTED_BODY_BYTES, `${label} exceeded the bounded body scanner size`);
      assertNoPrivateMaterial(body, containsPrivateMaterial, containsInfrastructureSecret, label);
      assert.equal(INTERNAL_ERROR_PATTERN.test(body), false, `${label} exposed an internal error`);
      assertNoUnauthorizedIdentity(body, fixture, label);
      if (!allowRoute) assertNoRoutePayload(body, label);
    };
    const inspectCurrentPage = async (label, options) => {
      inspectRuntimeText(await page.content(), label, options);
    };
    const inspectRsc = async (path, { allowRoute = false } = {}) => {
      const response = await context.request.get(path, {
        headers: { RSC: "1" },
        maxRedirects: 0,
      });
      const headers = response.headers();
      if (privateRoute(path)) {
        assert(cacheIsPrivateNoStore(headers), `M1.16 RSC response was cacheable: ${path}`);
      }
      inspectRuntimeText(await response.text(), "explicit RSC response", { allowRoute });
    };

    const observe = (promise) => {
      const guarded = promise.catch((error) => {
        observerError ??= error;
      });
      pendingInspections.add(guarded);
      guarded.then(() => pendingInspections.delete(guarded));
    };
    const drainObservers = async () => {
      while (pendingInspections.size > 0) {
        let timeout;
        await Promise.race([
          Promise.all([...pendingInspections]),
          new Promise((_, reject) => {
            timeout = setTimeout(
              () => reject(new Error("M1.16 browser observer did not settle within 15 seconds.")),
              15_000,
            );
          }),
        ]).finally(() => clearTimeout(timeout));
      }
      if (observerError) throw observerError;
    };
    context.on("serviceworker", (worker) => serviceWorkers.push(worker.url()));
    page.on("worker", (worker) => workers.push(worker.url()));
    page.on("websocket", (socket) => webSockets.push(socket.url()));
    page.on("console", (message) => {
      try {
        const text = message.text();
        assertNoPrivateMaterial(
          text,
          containsPrivateMaterial,
          containsInfrastructureSecret,
          "browser console",
        );
        if (message.type() === "error" || message.type() === "warning") browserErrors.push(text);
      } catch (error) {
        observerError ??= error;
      }
    });
    page.on("pageerror", (error) => browserErrors.push(String(error)));
    page.on("request", (request) => {
      try {
        const url = new URL(request.url());
        if (url.origin !== portalUrl) externalRequests.push(url.origin);
        const urlText = request.url();
        assertNoRoutePayload(urlText, "browser request URL");
        assertNoPrivateMaterial(
          urlText,
          containsPrivateMaterial,
          containsInfrastructureSecret,
          "browser request URL",
        );
        const body = request.postData() ?? "";
        const allowLoginCredential = url.pathname === "/login" && request.method() === "POST";
        const inspectedBody = allowLoginCredential
          ? body.replaceAll(`M114-local-authorization-${cycle}-24!`, "<allowed-login-password>")
          : body;
        assertNoPrivateMaterial(
          inspectedBody,
          containsPrivateMaterial,
          containsInfrastructureSecret,
          "browser action/request body",
        );
        assertNoRoutePayload(body, "browser action/request body");
      } catch (error) {
        observerError ??= error;
      }
    });
    page.on("response", (response) => observe((async () => {
      const request = response.request();
      const url = new URL(response.url());
      const headers = await response.allHeaders();
      const isAction = request.method() === "POST";
      const isProtected = privateRoute(url.pathname) &&
        (request.resourceType() === "document" || isAction || request.headers()["rsc"] === "1");
      if (isAction) actionResponses += 1;
      if (isProtected) {
        protectedResponses += 1;
        const cacheSafe = isAction ? cacheIsNoStore(headers) : cacheIsPrivateNoStore(headers);
        assert(cacheSafe, `M1.16 protected response was cacheable: ${url.pathname}`);
      }
      if (headers["set-cookie"]) {
        assert(cacheIsNoStore(headers), `M1.16 Set-Cookie response was cacheable: ${url.pathname}`);
      }
      if (recordingDetailPath(url.pathname, fixture)) {
        detailResponses += 1;
        assert(isProtected && cacheIsPrivateNoStore(headers), "recording detail was not private/no-store");
      }
    })()));

    await page.goto("/");
    await inspectCurrentPage("anonymous home HTML");
    await page.goto("/login");
    await inspectCurrentPage("anonymous login HTML");
    await inspectRsc("/login");
    await page.goto(`/app/${fixture.dogB.id}/today`);
    assert.equal(page.url().startsWith(`${portalUrl}/login`), true, "anonymous protected route did not redirect to login");
    await inspectCurrentPage("anonymous protected redirect HTML");
    await drainObservers();
    checkpoint("anonymous-html-rsc");

    await page.goto("/login");
    await page.getByLabel("Correo").fill(fixture.accounts.ownerA.email);
    await page.getByLabel("Contraseña").fill(`M114-local-authorization-${cycle}-24!`);
    await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
    await page.waitForURL(/\/onboarding|\/app\//u);
    if (new URL(page.url()).pathname === "/onboarding") {
      await page.goto(`/app/${fixture.dogA.id}/today`);
    }
    await page.waitForURL(new RegExp(`/app/${fixture.dogA.id}/today$`, "u"));
    await inspectCurrentPage("owner Today HTML");
    await inspectRsc(`/app/${fixture.dogA.id}/today`);
    await page.getByRole("link", { name: "Historial" }).click();
    await page.waitForURL(new RegExp(`/app/${fixture.dogA.id}/history$`, "u"));
    await inspectCurrentPage("owner History HTML");
    await page.getByRole("link", { name: /Ver detalle de la grabación/u }).first().click();
    await page.waitForURL(new RegExp(`/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}$`, "u"));
    await inspectCurrentPage("owner recording-detail HTML", { allowRoute: true });
    await inspectRsc(
      `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`,
      { allowRoute: true },
    );
    await drainObservers();
    assert(protectedResponses >= 3, "M1.16 observed too few authenticated protected responses");
    checkpoint("authenticated-private-cache");

    assert(detailResponses >= 1, "M1.16 did not observe the authorized recording-detail response");
    assert((await page.locator("body").innerText()).includes("46.8123456"), "recording detail omitted its bounded route table");
    const errorsBeforeExpectedDenial = browserErrors.length;
    await page.goto(`/app/${fixture.dogB.id}/recordings/${fixture.dogB.recordingId}`);
    assert.equal((await page.locator("body").innerText()).includes(fixture.dogB.name), false);
    await inspectCurrentPage("cross-owner recording response HTML");
    const denialErrors = browserErrors.splice(errorsBeforeExpectedDenial);
    assert(
      denialErrors.length <= 1 && denialErrors.every((message) =>
        message === "Failed to load resource: the server responded with a status of 404 (Not Found)"),
      "cross-owner denial emitted an unexpected browser error",
    );
    await page.goto(`/app/${fixture.dogA.id}/configuration`);
    await inspectCurrentPage("owner configuration HTML");
    checkpoint("recording-route-containment");

    await page.getByLabel("Brillo deseado").fill(String(fixture.brightness + 1));
    const actionPath = `/app/${fixture.dogA.id}/configuration`;
    let actionBody = null;
    await page.route(`**${actionPath}`, async (route) => {
      if (route.request().method() !== "POST") {
        await route.continue();
        return;
      }
      const response = await route.fetch({ maxRetries: 0 });
      const body = await response.body();
      assert(cacheIsNoStore(response.headers()), "brightness Server Action response was cacheable");
      actionBody = body.toString("utf8");
      await route.fulfill({ response, body });
    });
    await page.getByRole("button", { name: "GUARDAR BRILLO" }).click();
    await page.locator(".configuration-result").waitFor({ state: "visible" });
    await page.unroute(`**${actionPath}`);
    assert.notEqual(actionBody, null, "M1.16 did not capture the brightness Server Action response");
    inspectRuntimeText(actionBody, "brightness Server Action response");
    await inspectCurrentPage("post-action configuration HTML");
    await drainObservers();
    assert(actionResponses >= 2, "M1.16 did not inspect login plus product Server Action responses");
    checkpoint("server-action-payload");

    const storage = await page.evaluate(async () => ({
      local: Object.keys(localStorage),
      session: Object.keys(sessionStorage),
      indexedDb: typeof indexedDB.databases === "function"
        ? (await indexedDB.databases()).map((database) => database.name ?? "")
        : [],
      caches: "caches" in globalThis ? await caches.keys() : [],
    }));
    assert.deepEqual(storage, { local: [], session: [], indexedDb: [], caches: [] });
    const cookies = await context.cookies();
    assert(cookies.length > 0, "M1.16 expected a server-managed Supabase session cookie");
    for (const cookie of cookies) {
      assert(/^sb-.+-auth-token(?:\.\d+)?$/u.test(cookie.name), `unexpected browser cookie ${cookie.name}`);
      assertNoRoutePayload(cookie.value, "browser cookie");
      assertNoUnauthorizedIdentity(cookie.value, fixture, "browser cookie");
      assert.equal(containsPrivateMaterial(cookie.value), false, "browser cookie exposed fixture device/claim material");
      assert.equal(containsInfrastructureSecret(cookie.value), false, "browser cookie exposed infrastructure secret");
    }
    assert.deepEqual(externalRequests, []);
    assert.deepEqual(browserErrors, []);
    assert.deepEqual(webSockets, []);
    assert.deepEqual(workers, []);
    assert.deepEqual(serviceWorkers, []);
    await drainObservers();
    checkpoint("browser-state-and-observers");

    await context.close();
    await browser.close();
    browser = null;

    const runtimeLogs = `${portalLogs()}\n${await readServiceLogs()}`;
    assertNoPrivateMaterial(
      runtimeLogs,
      containsPrivateMaterial,
      containsInfrastructureSecret,
      "server/Edge/database logs",
    );
    assert.equal(INTERNAL_ERROR_PATTERN.test(runtimeLogs), false, "runtime logs exposed an internal error");
    checkpoint("server-edge-database-logs");

    const retained = await filesBelow(artifactDirectory);
    assert.equal(retained.some((path) => RICH_ARTIFACT_PATTERN.test(path)), false);
    checkpoint("retained-artifacts");

    const artifact = artifactFor(cycle, "passed", checkpoints);
    await writeFile(artifactPath, `${JSON.stringify(artifact, null, 2)}\n`, "utf8");
    return Object.freeze({ artifactPath });
  } catch (error) {
    await writeFile(
      artifactPath,
      `${JSON.stringify(artifactFor(cycle, "failed", checkpoints), null, 2)}\n`,
      "utf8",
    );
    throw error;
  } finally {
    await browser?.close().catch(() => undefined);
  }
}
