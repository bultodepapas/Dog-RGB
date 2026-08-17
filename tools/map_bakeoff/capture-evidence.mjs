import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { platform, release } from "node:os";
import { dirname, join } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "@playwright/test";
import { FIXTURE_VERSION, PROVIDERS, SCENARIOS, routeFacts, selectScenarios } from "./fixtures.mjs";

const root = dirname(fileURLToPath(import.meta.url));
const evidenceRoot = join(root, "evidence", "2026-08-13");
const manifestPath = join(evidenceRoot, "manifest.json");
const port = 4174;
const origin = `http://127.0.0.1:${port}`;
const mapLibreVersion = "5.23.0";

const standardProviders = ["stadia-dark", "stadia-light", "stadia-outdoor"];
const viewportProfiles = [
  { name: "desktop-1280x720", width: 1280, height: 720, dpr: 1 },
  { name: "desktop-1280x720", width: 1280, height: 720, dpr: 2 },
  { name: "mobile-428x844", width: 428, height: 844, dpr: 1 },
  { name: "mobile-428x844", width: 428, height: 844, dpr: 2 },
];

const matrixCells = standardProviders.flatMap((provider) => viewportProfiles.map((viewport) => ({
  id: `matrix-${provider}-${viewport.name}-dpr${viewport.dpr}`,
  kind: "matrix",
  provider,
  ...viewport,
  scenarioSet: "all",
  labelMode: "normal",
  cvdMode: "none",
  networkProfile: "fresh-context",
})));

const diagnosticCells = [
  {
    id: "diagnostic-label-deemphasis-stadia-dark-mobile-428-dpr1",
    kind: "diagnostic",
    provider: "stadia-dark",
    name: "mobile-428x844",
    width: 428,
    height: 844,
    dpr: 1,
    scenarioSet: "stress",
    labelMode: "deemphasized",
    cvdMode: "none",
    networkProfile: "fresh-context",
  },
  ...["deuteranopia", "protanopia"].map((cvdMode) => ({
    id: `diagnostic-cvd-${cvdMode}-stadia-dark-mobile-428-dpr1`,
    kind: "diagnostic",
    provider: "stadia-dark",
    name: "mobile-428x844",
    width: 428,
    height: 844,
    dpr: 1,
    scenarioSet: "stress",
    labelMode: "normal",
    cvdMode,
    networkProfile: "fresh-context",
  })),
  {
    id: "diagnostic-cold-cache-stadia-dark-mobile-428-dpr1",
    kind: "diagnostic",
    provider: "stadia-dark",
    name: "mobile-428x844",
    width: 428,
    height: 844,
    dpr: 1,
    scenarioSet: "stress",
    labelMode: "normal",
    cvdMode: "none",
    networkProfile: "browser-cache-disabled",
  },
  {
    id: "diagnostic-low-bandwidth-stadia-dark-mobile-428-dpr1",
    kind: "diagnostic",
    provider: "stadia-dark",
    name: "mobile-428x844",
    width: 428,
    height: 844,
    dpr: 1,
    scenarioSet: "stress",
    labelMode: "normal",
    cvdMode: "none",
    networkProfile: "cache-disabled-1.6mbps-300ms-rtt",
    timeoutMs: 120_000,
  },
];

const cells = [...matrixCells, ...diagnosticCells];

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}

function pngDimensions(bytes) {
  if (bytes.length < 24 || bytes.toString("ascii", 1, 4) !== "PNG") return null;
  return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
}

async function waitForServer(child) {
  let lastError;
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (child.exitCode !== null) throw new Error(`Bakeoff server exited early with ${child.exitCode}`);
    try {
      const response = await fetch(origin);
      if (response.ok) return;
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Bakeoff server did not become ready: ${lastError ?? "timeout"}`);
}

async function sourceDigests() {
  const names = [
    "app.js",
    "capture-evidence.mjs",
    "fixtures.mjs",
    "index.html",
    "server.mjs",
    "styles.css",
    "test-harness.mjs",
  ];
  return Object.fromEntries(
    await Promise.all(names.map(async (name) => [name, sha256(await readFile(join(root, name)))])),
  );
}

async function externalAssetDigests() {
  const urls = [
    "https://unpkg.com/maplibre-gl@5.23.0/dist/maplibre-gl.css",
    "https://unpkg.com/maplibre-gl@5.23.0/dist/maplibre-gl.js",
    ...standardProviders.map((providerId) => PROVIDERS[providerId].style),
  ];
  return Object.fromEntries(await Promise.all(urls.map(async (url) => {
    try {
      const response = await fetch(url, { signal: AbortSignal.timeout(30_000) });
      const body = Buffer.from(await response.arrayBuffer());
      return [url, response.ok
        ? { status: "captured", httpStatus: response.status, sha256: sha256(body), bytes: body.length }
        : { status: "failed_http", httpStatus: response.status, sha256: sha256(body), bytes: body.length }];
    } catch (error) {
      return [url, { status: "failed_transport", error: errorMessage(error) }];
    }
  })));
}

async function priorEvidence() {
  try {
    const prior = JSON.parse(await readFile(manifestPath, "utf8"));
    if (prior.evidenceSchema === "dog-rgb-map-bakeoff/1") {
      const artifacts = await Promise.all((prior.runs ?? []).map(async (run) => {
        const bytes = await readFile(join(evidenceRoot, run.screenshot));
        return {
          screenshot: run.screenshot,
          recordedSha256: run.screenshotSha256,
          currentSha256: sha256(bytes),
          hashStillMatches: run.screenshotSha256 === sha256(bytes),
          viewport: `${run.width}x${run.height}`,
          dpr: run.deviceScaleFactor,
        };
      }));
      return {
        status: "preserved_v1_390px_regression_evidence",
        evidenceSchema: prior.evidenceSchema,
        capturedAtUtc: prior.capturedAtUtc,
        artifacts,
      };
    }
    return prior.legacyEvidence ?? { status: "no_v1_manifest_available" };
  } catch (error) {
    return { status: "unavailable", error: errorMessage(error) };
  }
}

function coordinateNeedles() {
  return SCENARIOS.flatMap(({ coordinates }) => coordinates.flatMap(([longitude, latitude]) => [
    longitude.toFixed(6),
    latitude.toFixed(6),
  ]));
}

function aggregateOrigins(urls) {
  const counts = urls.reduce((result, url) => {
    const requestOrigin = new URL(url).origin;
    result[requestOrigin] = (result[requestOrigin] ?? 0) + 1;
    return result;
  }, {});
  return Object.entries(counts).map(([requestOrigin, count]) => ({ origin: requestOrigin, count }));
}

async function configureNetwork(context, page, profile) {
  if (profile === "fresh-context") {
    return {
      browserCache: "fresh isolated browser context; not explicitly disabled",
      serviceWorkers: "blocked",
      osAndUpstreamCaches: "uncontrolled",
    };
  }
  const cdp = await context.newCDPSession(page);
  await cdp.send("Network.enable");
  await cdp.send("Network.setCacheDisabled", { cacheDisabled: true });
  if (profile === "cache-disabled-1.6mbps-300ms-rtt") {
    await cdp.send("Network.emulateNetworkConditions", {
      offline: false,
      latency: 300,
      downloadThroughput: 1_600_000 / 8,
      uploadThroughput: 750_000 / 8,
      connectionType: "cellular3g",
    });
    return {
      browserCache: "disabled with Chromium DevTools protocol",
      serviceWorkers: "blocked",
      latencyMs: 300,
      downloadBitsPerSecond: 1_600_000,
      uploadBitsPerSecond: 750_000,
      osAndUpstreamCaches: "uncontrolled",
    };
  }
  return {
    browserCache: "disabled with Chromium DevTools protocol",
    serviceWorkers: "blocked",
    osAndUpstreamCaches: "uncontrolled",
  };
}

async function safePageFacts(page) {
  try {
    return await page.evaluate((needles) => {
      const maps = [...document.querySelectorAll(".map")];
      const canvases = [...document.querySelectorAll(".maplibregl-canvas")];
      const attribution = [...document.querySelectorAll(".maplibregl-ctrl-attrib")];
      const resources = performance.getEntriesByType("resource");
      const routeTable = document.querySelector(".data-ledger table");
      const evidence = window.__dogRgbEvidence ?? null;
      const bodyText = document.body.innerText;
      return {
        dataReady: document.documentElement.dataset.ready,
        declaredScenarioCount: Number(document.documentElement.dataset.scenarioCount ?? 0),
        browserUserAgent: navigator.userAgent,
        viewport: { width: innerWidth, height: innerHeight },
        devicePixelRatio: devicePixelRatio,
        reducedMotionActive: matchMedia("(prefers-reduced-motion: reduce)").matches,
        mapLibreRuntimeVersion: typeof maplibregl === "undefined" ? null : (maplibregl.version ?? null),
        canvasCount: canvases.length,
        focusableCanvasCount: canvases.filter((canvas) => canvas.tabIndex >= 0).length,
        mapRegionCount: maps.filter((map) => map.getAttribute("role") === "region").length,
        uniqueMapAccessibleNameCount: new Set(maps.map((map) => map.getAttribute("aria-label")).filter(Boolean)).size,
        scenarioHeadingCount: document.querySelectorAll(".scenario h2").length,
        routeTablePresent: Boolean(routeTable),
        routeTableRowCount: document.querySelectorAll("#route-data-body tr").length,
        tableColumnHeaderCount: document.querySelectorAll(".data-ledger thead th[scope='col']").length,
        mapBoxes: maps.map((map) => {
          const rect = map.getBoundingClientRect();
          return { width: rect.width, height: rect.height };
        }),
        attributionVisible: maps.length > 0 && attribution.length === maps.length && attribution.every((item) => {
          const rect = item.getBoundingClientRect();
          const style = getComputedStyle(item);
          return rect.width > 0 && rect.height > 0 && style.visibility !== "hidden" && style.display !== "none";
        }),
        horizontalOverflowPx: Math.max(0, document.documentElement.scrollWidth - document.documentElement.clientWidth),
        rawCoordinateRenderedInText: needles.some((needle) => bodyText.includes(needle)),
        evidence,
        fatalError: document.querySelector("#fatal-error")?.textContent ?? "",
        resourceTiming: {
          entries: resources.length,
          reportedTransferBytes: resources.reduce((sum, entry) => sum + (entry.transferSize || 0), 0),
          zeroTransferSizeEntries: resources.filter((entry) => !entry.transferSize).length,
        },
      };
    }, coordinateNeedles());
  } catch (error) {
    return { pageFactsError: errorMessage(error) };
  }
}

function hardGateFailures(cell, result) {
  const expectedScenarios = selectScenarios(cell.scenarioSet).length;
  const failures = [];
  const require = (condition, name, actual) => {
    if (!condition) failures.push({ check: name, actual });
  };
  require(!result.runnerError, "runner completed", result.runnerError);
  require(result.dataReady === "true", "page reached data-ready=true", result.dataReady);
  require(result.declaredScenarioCount === expectedScenarios, "declared scenario count", result.declaredScenarioCount);
  require(result.canvasCount === expectedScenarios, "one canvas per scenario", result.canvasCount);
  require(result.mapRegionCount === expectedScenarios, "one named region per scenario", result.mapRegionCount);
  require(result.uniqueMapAccessibleNameCount === expectedScenarios, "unique map accessible names", result.uniqueMapAccessibleNameCount);
  require(result.scenarioHeadingCount === expectedScenarios, "one heading per scenario", result.scenarioHeadingCount);
  require(result.focusableCanvasCount === expectedScenarios, "keyboard-focusable map canvases", result.focusableCanvasCount);
  require(result.routeTablePresent === true, "non-map route table exists", result.routeTablePresent);
  require(result.routeTableRowCount === expectedScenarios, "non-map table row per scenario", result.routeTableRowCount);
  require(result.tableColumnHeaderCount === 5, "route table exposes five column headers", result.tableColumnHeaderCount);
  require(result.attributionVisible === true, "provider attribution visible", result.attributionVisible);
  require(result.horizontalOverflowPx === 0, "no document-level horizontal overflow", result.horizontalOverflowPx);
  require(result.rawCoordinateRenderedInText === false, "raw route coordinates absent from DOM text", result.rawCoordinateRenderedInText);
  require(result.failedRequests.length === 0, "no failed browser requests", result.failedRequests);
  require(result.consoleErrors.length === 0, "no console errors", result.consoleErrors);
  require(result.pageErrors.length === 0, "no page errors", result.pageErrors);
  require(result.rawCoordinateLeaks.length === 0, "no raw route coordinate in provider URL", result.rawCoordinateLeaks);
  require(result.devicePixelRatio === cell.dpr, "requested device pixel ratio", result.devicePixelRatio);
  require(result.screenshotDimensions?.width === cell.width * cell.dpr, "physical screenshot width matches CSS width × DPR", result.screenshotDimensions);
  require(result.evidence?.fixtureVersion === FIXTURE_VERSION, "fixture version exposed", result.evidence?.fixtureVersion);
  require(result.evidence?.qualityGapUsesDashPattern === true, "quality gaps have a non-color dash cue", result.evidence?.qualityGapUsesDashPattern);
  require(result.evidence?.routeWidthAlsoVariesBySpeedBand === true, "speed band has a non-color width cue", result.evidence?.routeWidthAlsoVariesBySpeedBand);
  require(result.evidence?.labelMode === cell.labelMode, "label mode applied", result.evidence?.labelMode);
  require(result.evidence?.cvdMode === cell.cvdMode, "CVD diagnostic mode applied", result.evidence?.cvdMode);
  if (cell.labelMode === "deemphasized") {
    require(result.evidence?.labelLayersModified > 0, "provider label layers actually de-emphasized", result.evidence?.labelLayersModified);
  }
  return failures;
}

async function captureCell(browser, cell) {
  const context = await browser.newContext({
    viewport: { width: cell.width, height: cell.height },
    deviceScaleFactor: cell.dpr,
    colorScheme: PROVIDERS[cell.provider].variant === "light" ? "light" : "dark",
    reducedMotion: "reduce",
    serviceWorkers: "block",
  });
  const page = await context.newPage();
  const requestedUrls = [];
  const failedRequests = [];
  const consoleErrors = [];
  const pageErrors = [];
  page.on("request", (request) => requestedUrls.push(request.url()));
  page.on("requestfailed", (request) => failedRequests.push({
    url: request.url(),
    error: request.failure()?.errorText ?? "unknown",
  }));
  page.on("console", (message) => {
    if (message.type() === "error") consoleErrors.push(message.text());
  });
  page.on("pageerror", (error) => pageErrors.push(error.message));

  let networkConditions;
  let runnerError = null;
  let readyMs = null;
  const startedAt = Date.now();
  try {
    networkConditions = await configureNetwork(context, page, cell.networkProfile);
    const query = new URLSearchParams({
      provider: cell.provider,
      labels: cell.labelMode,
      cvd: cell.cvdMode,
      scenarios: cell.scenarioSet,
    });
    await page.goto(`${origin}/?${query}`, { waitUntil: "domcontentloaded", timeout: cell.timeoutMs ?? 60_000 });
    await page.waitForFunction(
      () => ["true", "error"].includes(document.documentElement.dataset.ready),
      undefined,
      { timeout: cell.timeoutMs ?? 60_000 },
    );
    const readyState = await page.locator("html").getAttribute("data-ready");
    if (readyState !== "true") throw new Error(`Page entered data-ready=${readyState}: ${await page.locator("#fatal-error").innerText()}`);
    await page.evaluate(() => document.fonts.ready);
    readyMs = Date.now() - startedAt;
  } catch (error) {
    runnerError = errorMessage(error);
  }

  const screenshotName = `${cell.id}.png`;
  const screenshotPath = join(evidenceRoot, screenshotName);
  let screenshotSha256 = null;
  let screenshotDimensions = null;
  try {
    await page.screenshot({ path: screenshotPath, fullPage: true, animations: "disabled" });
    const screenshotBytes = await readFile(screenshotPath);
    screenshotSha256 = sha256(screenshotBytes);
    screenshotDimensions = pngDimensions(screenshotBytes);
  } catch (error) {
    runnerError = [runnerError, `Screenshot failed: ${errorMessage(error)}`].filter(Boolean).join(" | ");
  }

  const pageFacts = await safePageFacts(page);
  const externalRequests = requestedUrls.filter((url) => {
    if (url.startsWith("blob:") || url.startsWith("data:")) return false;
    return new URL(url).origin !== origin;
  });
  const needles = coordinateNeedles();
  const rawCoordinateLeaks = externalRequests.filter((url) => {
    const decoded = decodeURIComponent(url);
    return needles.some((needle) => decoded.includes(needle));
  });
  const result = {
    ...cell,
    status: "pending_gate_evaluation",
    mapLibreVersion,
    networkConditions,
    readyMs,
    screenshot: screenshotName,
    screenshotSha256,
    screenshotDimensions,
    runnerError,
    externalRequestCount: externalRequests.length,
    requestsByOrigin: aggregateOrigins(externalRequests),
    failedRequests,
    consoleErrors,
    pageErrors,
    rawCoordinateLeaks,
    ...pageFacts,
  };
  result.hardGateFailures = hardGateFailures(cell, result);
  result.status = result.hardGateFailures.length === 0 ? "passed" : "failed";
  await context.close();
  return result;
}

async function captureMissingMapTilerCredentials(browser) {
  const results = [];
  for (const provider of ["maptiler-dark", "maptiler-light", "maptiler-outdoor"]) {
    const context = await browser.newContext({ viewport: { width: 428, height: 844 }, deviceScaleFactor: 1 });
    const page = await context.newPage();
    const requests = [];
    page.on("request", (request) => requests.push(request.url()));
    let runnerError = null;
    try {
      await page.goto(`${origin}/?provider=${provider}`, { waitUntil: "domcontentloaded", timeout: 30_000 });
      await page.locator("html[data-ready='error']").waitFor({ timeout: 10_000 });
    } catch (error) {
      runnerError = errorMessage(error);
    }
    const message = await page.locator("#fatal-error").innerText().catch(() => "");
    const screenshot = `${provider}-missing-credential-428-dpr1.png`;
    await page.screenshot({ path: join(evidenceRoot, screenshot), animations: "disabled" });
    const bytes = await readFile(join(evidenceRoot, screenshot));
    const maptilerRequests = requests.filter((url) => url.startsWith("https://api.maptiler.com/"));
    results.push({
      provider,
      status: !runnerError && /requires a temporary testing key/.test(message) && maptilerRequests.length === 0
        ? "blocked_missing_credential_as_expected"
        : "credential_gate_capture_failed",
      runnerError,
      message,
      screenshot,
      screenshotSha256: sha256(bytes),
      maptilerRequestCountBeforeGate: maptilerRequests.length,
    });
    await context.close();
  }
  return results;
}

function manualReviewChecklist() {
  return [
    {
      id: "outdoor-cartography-aesthetic",
      status: "pending_human_review",
      artifacts: [
        "matrix-stadia-outdoor-desktop-1280x720-dpr1.png",
        "matrix-stadia-outdoor-mobile-428x844-dpr2.png",
      ],
      acceptance: "Two reviewers agree terrain, trail hierarchy, attribution, and route salience remain legible without looking visually noisy.",
    },
    {
      id: "label-deemphasis-usefulness",
      status: "pending_human_review",
      artifacts: [
        "matrix-stadia-dark-mobile-428x844-dpr1.png",
        "diagnostic-label-deemphasis-stadia-dark-mobile-428-dpr1.png",
      ],
      acceptance: "Reduced labels lower distraction while retaining enough place and road context to orient the route.",
    },
    {
      id: "cvd-deuteranopia-route-cues",
      status: "pending_human_review",
      artifacts: ["diagnostic-cvd-deuteranopia-stadia-dark-mobile-428-dpr1.png"],
      acceptance: "Route remains traceable; gaps are recognizable by dashes and speed bands retain a useful non-color width cue. Simulation is diagnostic, not certification.",
    },
    {
      id: "cvd-protanopia-route-cues",
      status: "pending_human_review",
      artifacts: ["diagnostic-cvd-protanopia-stadia-dark-mobile-428-dpr1.png"],
      acceptance: "Route remains traceable; start/end and quality gaps do not rely on hue alone. Record any ambiguous pair as a design finding.",
    },
    {
      id: "mobile-428-touch-and-label-density",
      status: "pending_human_review",
      artifacts: [
        "matrix-stadia-dark-mobile-428x844-dpr1.png",
        "matrix-stadia-light-mobile-428x844-dpr2.png",
        "matrix-stadia-outdoor-mobile-428x844-dpr2.png",
      ],
      acceptance: "At 428 CSS px, route casing, endpoints, legend, attribution, and text alternative remain readable without page-level horizontal clipping.",
    },
  ];
}

await mkdir(evidenceRoot, { recursive: true });
const legacyEvidence = await priorEvidence();

let existingServer = false;
try {
  const response = await fetch(origin);
  existingServer = response.ok;
} catch {
  // Expected: the evidence runner owns this loopback port.
}
if (existingServer) throw new Error(`Refusing to reuse an existing server on ${origin}`);

const server = spawn(process.execPath, [join(root, "server.mjs"), "--port", String(port)], {
  stdio: ["ignore", "pipe", "pipe"],
});
let serverStdout = "";
let serverStderr = "";
server.stdout.on("data", (chunk) => { serverStdout += chunk.toString(); });
server.stderr.on("data", (chunk) => { serverStderr += chunk.toString(); });

let browser;
let fatalRunnerError = null;
const runs = [];
let mapTilerCredentialGates = [];
let browserVersion = null;
try {
  await waitForServer(server);
  browser = await chromium.launch({ headless: true });
  browserVersion = browser.version();
  for (const cell of cells) {
    process.stderr.write(`Capturing ${cell.id}...\n`);
    runs.push(await captureCell(browser, cell));
  }
  mapTilerCredentialGates = await captureMissingMapTilerCredentials(browser);
} catch (error) {
  fatalRunnerError = errorMessage(error);
} finally {
  if (browser) await browser.close();
  server.kill();
}

const externalAssets = await externalAssetDigests();
const passedRuns = runs.filter(({ status }) => status === "passed").length;
const manifest = {
  evidenceSchema: "dog-rgb-map-bakeoff/2",
  capturedAtUtc: new Date().toISOString(),
  captureIntent: "Phase 0 visual/technical provider bakeoff; synthetic fixtures only; no product or animal telemetry",
  environment: {
    platform: platform(),
    release: release(),
    browser: "Chromium",
    browserVersion,
    nodeVersion: process.version,
    mapLibreVersion,
  },
  fixtureVersion: FIXTURE_VERSION,
  sourceSha256: await sourceDigests(),
  externalAssets,
  matrixDefinition: {
    providers: standardProviders.map((id) => ({ id, variant: PROVIDERS[id].variant, credentialMode: PROVIDERS[id].credentialMode })),
    viewports: viewportProfiles,
    scenarios: SCENARIOS.map(routeFacts),
    standardCellCount: matrixCells.length,
    diagnosticCellCount: diagnosticCells.length,
    diagnostics: {
      labelMode: "provider symbol text opacity reduced to 0.24; actual modified-layer count asserted",
      cvdModes: "SVG color-matrix approximations for deuteranopia and protanopia; human review required",
      coldCache: "fresh context plus browser HTTP cache disabled through Chromium DevTools protocol",
      lowBandwidth: "browser cache disabled, 300 ms latency, 1.6 Mbps down, 0.75 Mbps up; diagnostic only",
    },
  },
  runSummary: {
    expectedRuns: cells.length,
    completedRuns: runs.length,
    passedRuns,
    failedRuns: runs.length - passedRuns,
    fatalRunnerError,
    overallTechnicalPass: !fatalRunnerError && runs.length === cells.length && passedRuns === runs.length,
  },
  runs,
  credentialGates: [
    ...mapTilerCredentialGates,
    {
      provider: "maptiler-all-visual-comparison",
      status: "blocked_missing_credential",
      reason: "No MapTiler testing key was provided; no MapTiler basemap request was attempted.",
    },
    {
      provider: "stadia-domain-authentication",
      status: "blocked_no_property_credential",
      reason: "The requested no-credential bakeoff cannot exercise a Stadia property key or unapproved-origin rejection. Keyless loopback development is the only Stadia mode captured.",
    },
  ],
  automatedAccessibilityEvidence: {
    status: runs.length > 0 && runs.every(({ hardGateFailures }) => !hardGateFailures.some(({ check }) => [
      "one named region per scenario",
      "unique map accessible names",
      "one heading per scenario",
      "keyboard-focusable map canvases",
      "non-map route table exists",
      "non-map table row per scenario",
      "route table exposes five column headers",
      "quality gaps have a non-color dash cue",
      "speed band has a non-color width cue",
    ].includes(check))) ? "passed_structural_checks" : "failed_structural_checks",
    note: "Structural assertions do not decide aesthetics, CVD distinguishability, screen-reader usability, or touch ergonomics.",
  },
  manualReviewChecklist: manualReviewChecklist(),
  legacyEvidence,
  serverProcess: {
    stdout: serverStdout.trim(),
    stderr: serverStderr.trim(),
    exitCodeAtManifestTime: server.exitCode,
  },
  limitations: [
    "All coordinates form synthetic test fixtures; they are not dog, owner, or production telemetry.",
    "Provider tile requests necessarily disclose viewed tile bounds, but the runner asserts that raw route coordinates are absent from provider URLs and DOM text.",
    "PerformanceResourceTiming may report zero transfer bytes when a cross-origin server omits Timing-Allow-Origin.",
    "Browser cache controls do not control OS, recursive DNS, provider edge, or upstream CDN caches; readyMs is not a product SLO.",
    "Network emulation controls browser throughput and latency, not provider-side variability.",
    "CVD matrices are review aids, not medical models or WCAG certification.",
    "MapTiler aesthetics and rejected-origin authentication cannot be evaluated without explicitly supplied restricted credentials.",
    "Any live CDN failure is retained as a failed run with its screenshot and diagnostics; it is never converted to a pass.",
  ],
};

await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
process.stdout.write(`${JSON.stringify(manifest.runSummary, null, 2)}\n`);
if (!manifest.runSummary.overallTechnicalPass) process.exitCode = 1;
