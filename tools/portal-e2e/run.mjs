import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import { existsSync } from "node:fs";
import { mkdir, readFile, rm, unlink, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { chromium } from "@playwright/test";

import { prepareAuthorizationFixture } from "./authorization-fixtures.mjs";
import { clearMailbox } from "./mailpit.mjs";
import {
  M115_CHECKPOINTS,
  M115_FAULTS,
  runM115FaultMatrix,
} from "./m115-fault-matrix.mjs";
import {
  runM116PrivacyCacheGate,
  validateM116Artifact,
} from "./m116-privacy-cache.mjs";

const workspace = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const portalDirectory = join(workspace, "apps", "portal");
const nextCli = join(workspace, "node_modules", "next", "dist", "bin", "next");
const playwrightCli = join(workspace, "node_modules", "@playwright", "test", "cli.js");
const playwrightConfig = join(workspace, "playwright.portal.config.ts");
const localSecretsPath = join(workspace, "supabase", "functions", ".env");
const ownerArtifactDirectory = join(workspace, "output", "playwright", "m113");
const authorizationArtifactDirectory = join(workspace, "output", "playwright", "m114");
const faultArtifactDirectory = join(workspace, "output", "playwright", "m115");
const privacyArtifactDirectory = join(workspace, "output", "playwright", "m116");
const expectedNode = "24.18.0";
const expectedPlaywright = "1.62.1";
const portalUrl = "http://127.0.0.1:3000";
const m116Only = process.argv.includes("--m116-only");
const M115_EXPECTED_COUNTS = Object.freeze({
  receipts: 6,
  chunks: 4,
  points: 7,
  recordings: 3,
  revisions: 3,
  heads: 2,
  reported: 1,
  raceSchedules: 2,
});
const M115_ZERO_COUNTS = Object.freeze(Object.fromEntries(
  Object.keys(M115_EXPECTED_COUNTS).map((key) => [key, 0]),
));

function executable(command) {
  return process.platform === "win32" && command === "supabase"
    ? "supabase.exe"
    : command;
}

function run(command, args, options = {}) {
  const result = spawnSync(executable(command), args, {
    cwd: workspace,
    stdio: "inherit",
    timeout: 180_000,
    ...options,
  });
  if (result.status !== 0) {
    throw new Error(`Portal E2E command failed: ${command} ${args[0] ?? ""}.`);
  }
}

function runQuiet(command, args, { allowFailure = false } = {}) {
  const result = spawnSync(executable(command), args, {
    cwd: workspace,
    encoding: "utf8",
    maxBuffer: 8 * 1024 * 1024,
    timeout: 180_000,
  });
  if (!allowFailure && result.status !== 0) {
    throw new Error(`Portal E2E command failed: ${command} ${args[0] ?? ""}.`);
  }
  return result.stdout.trim();
}

function runCaptured(command, args, { allowFailure = false } = {}) {
  const result = spawnSync(executable(command), args, {
    cwd: workspace,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
    timeout: 180_000,
  });
  if (!allowFailure && result.status !== 0) {
    throw new Error(`Portal E2E command failed: ${command} ${args[0] ?? ""}.`);
  }
  return `${result.stdout ?? ""}\n${result.stderr ?? ""}`;
}

function parseLocalEnvironment() {
  const output = runQuiet("supabase", ["status", "-o", "env"]);
  const values = Object.fromEntries(output.split(/\r?\n/u).flatMap((line) => {
    const match = line.match(/^([^=]+)="?(.*?)"?$/u);
    return match ? [[match[1], match[2].replace(/"$/u, "")]] : [];
  }));
  for (const key of ["API_URL", "MAILPIT_URL", "PUBLISHABLE_KEY"]) {
    if (typeof values[key] !== "string" || values[key].length < 1) {
      throw new Error("Portal E2E local Supabase environment was incomplete.");
    }
  }
  for (const key of ["API_URL", "MAILPIT_URL"]) {
    const url = new URL(values[key]);
    if (url.protocol !== "http:" || url.hostname !== "127.0.0.1") {
      throw new Error("Portal E2E refused a non-loopback local service.");
    }
  }
  return Object.freeze(values);
}

function validateAuthorizationArtifact(value, cycle, expectedPhase) {
  let artifact;
  try {
    artifact = JSON.parse(Buffer.isBuffer(value) ? value.toString("utf8") : String(value));
  } catch {
    throw new Error("M1.14 authorization artifact is not valid JSON.");
  }
  const exactKeys = (object, expected, label) => {
    if (object === null || typeof object !== "object" || Array.isArray(object)) {
      throw new Error(`M1.14 ${label} must be an object.`);
    }
    const actual = Object.keys(object).sort();
    if (JSON.stringify(actual) !== JSON.stringify([...expected].sort())) {
      throw new Error(`M1.14 ${label} fields changed outside the artifact allowlist.`);
    }
  };

  exactKeys(
    artifact,
    ["schemaVersion", "phase", "cycle", "surface", "graphCounts", "checkpoints"],
    "artifact",
  );
  exactKeys(artifact.surface, ["tables", "rpcs"], "artifact surface");
  exactKeys(artifact.graphCounts, [
    "profiles", "dogs", "memberships", "collars", "recordings", "telemetry_points",
    "daily_summaries", "recording_summaries", "config_revisions", "config_heads",
    "config_reported", "claims", "credentials", "sync_requests", "chunks",
    "deletion_tombstones", "deletion_jobs", "deletion_receipts",
  ], "artifact graph counts");
  if (
    artifact.schemaVersion !== 1 ||
    artifact.phase !== expectedPhase ||
    artifact.cycle !== cycle ||
    artifact.surface.tables !== 11 ||
    artifact.surface.rpcs !== 5 ||
    !Object.values(artifact.graphCounts).every(Number.isSafeInteger) ||
    !Object.values(artifact.graphCounts).every((count) => count >= 0) ||
    !Array.isArray(artifact.checkpoints) ||
    artifact.checkpoints.some((checkpoint) =>
      typeof checkpoint !== "string" || !/^[a-z0-9-]+$/u.test(checkpoint)
    ) ||
    new Set(artifact.checkpoints).size !== artifact.checkpoints.length
  ) {
    throw new Error("M1.14 authorization artifact failed its bounded schema contract.");
  }
  return artifact;
}

function validateFaultArtifact(value, cycle, expectedPhase) {
  let artifact;
  try {
    artifact = JSON.parse(Buffer.isBuffer(value) ? value.toString("utf8") : String(value));
  } catch {
    throw new Error("M1.15 fault artifact is not valid JSON.");
  }
  const exactKeys = (object, expected, label) => {
    if (object === null || typeof object !== "object" || Array.isArray(object)) {
      throw new Error(`M1.15 ${label} must be an object.`);
    }
    if (JSON.stringify(Object.keys(object).sort()) !== JSON.stringify([...expected].sort())) {
      throw new Error(`M1.15 ${label} fields changed outside the artifact allowlist.`);
    }
  };
  exactKeys(
    artifact,
    ["schemaVersion", "phase", "cycle", "faults", "checkpoints", "counts"],
    "artifact",
  );
  exactKeys(artifact.counts, [
    "receipts", "chunks", "points", "recordings", "revisions", "heads",
    "reported", "raceSchedules",
  ], "artifact counts");
  const checkpointList = Array.isArray(artifact.checkpoints) ? artifact.checkpoints : [];
  const expectedCheckpoints = expectedPhase === "passed"
    ? M115_CHECKPOINTS
    : M115_CHECKPOINTS.slice(0, checkpointList.length);
  const expectedCounts = expectedPhase === "passed"
    ? M115_EXPECTED_COUNTS
    : M115_ZERO_COUNTS;
  if (
    artifact.schemaVersion !== 1 ||
    artifact.phase !== expectedPhase ||
    artifact.cycle !== cycle ||
    JSON.stringify(artifact.faults) !== JSON.stringify(M115_FAULTS) ||
    !Array.isArray(artifact.checkpoints) ||
    JSON.stringify(checkpointList) !== JSON.stringify(expectedCheckpoints) ||
    !Object.values(artifact.counts).every(Number.isSafeInteger) ||
    !Object.values(artifact.counts).every((count) => count >= 0) ||
    JSON.stringify(artifact.counts) !== JSON.stringify(expectedCounts)
  ) {
    throw new Error("M1.15 fault artifact failed its bounded schema contract.");
  }
  return artifact;
}

async function portIsFree() {
  const server = createServer();
  server.unref();
  return new Promise((resolveFree) => {
    server.once("error", () => resolveFree(false));
    server.listen(3000, "127.0.0.1", () => {
      server.close(() => resolveFree(true));
    });
  });
}

async function waitForCondition(check, label, timeoutMs = 30_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await check()) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 250));
  }
  throw new Error(`Portal E2E ${label} did not become ready.`);
}

async function verifyServices(environment) {
  await waitForCondition(async () => {
    try {
      const response = await fetch(`${environment.API_URL}/auth/v1/health`, {
        signal: AbortSignal.timeout(1_000),
      });
      return response.status === 200;
    } catch {
      return false;
    }
  }, "Auth service");
  await waitForCondition(async () => {
    try {
      const response = await fetch(`${environment.API_URL}/rest/v1/`, {
        headers: { apikey: environment.PUBLISHABLE_KEY },
        signal: AbortSignal.timeout(1_000),
      });
      return response.status === 200;
    } catch {
      return false;
    }
  }, "Data API");
  await waitForCondition(async () => {
    try {
      const response = await fetch(`${environment.API_URL}/functions/v1/device-v1-sync`, {
        signal: AbortSignal.timeout(1_000),
      });
      return response.status === 405 &&
        response.headers.get("content-type")?.includes("application/problem+json");
    } catch {
      return false;
    }
  }, "Edge gateway");
  await waitForCondition(async () => {
    try {
      const response = await fetch(`${environment.MAILPIT_URL}/api/v1/info`, {
        signal: AbortSignal.timeout(1_000),
      });
      return response.status === 200;
    } catch {
      return false;
    }
  }, "Mailpit");
}

function portalEnvironment(environment) {
  const allowed = [
    "APPDATA", "CI", "ComSpec", "HOME", "LOCALAPPDATA", "PATH", "Path",
    "PATHEXT", "SystemRoot", "SYSTEMROOT", "TEMP", "TMP", "TMPDIR",
    "USERPROFILE", "WINDIR",
  ];
  return {
    ...Object.fromEntries(allowed.flatMap((name) => process.env[name] === undefined
      ? []
      : [[name, process.env[name]]])),
    NEXT_PUBLIC_SUPABASE_URL: environment.API_URL,
    NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY: environment.PUBLISHABLE_KEY,
    NEXT_TELEMETRY_DISABLED: "1",
  };
}

async function startPortal(environment) {
  if (!await portIsFree()) {
    throw new Error("Portal E2E requires exclusive ownership of 127.0.0.1:3000.");
  }
  const child = spawn(process.execPath, [
    nextCli, "start", "--hostname", "127.0.0.1", "--port", "3000",
  ], {
    cwd: portalDirectory,
    env: portalEnvironment(environment),
    detached: process.platform !== "win32",
    stdio: ["ignore", "pipe", "pipe"],
  });
  const logChunks = [];
  let logBytes = 0;
  let logOverflow = false;
  const retainLog = (chunk) => {
    if (logOverflow) return;
    logBytes += chunk.byteLength;
    if (logBytes > 2 * 1024 * 1024) {
      logOverflow = true;
      logChunks.length = 0;
      return;
    }
    logChunks.push(Buffer.from(chunk));
  };
  child.stdout?.on("data", retainLog);
  child.stderr?.on("data", retainLog);
  child.portalLogs = () => {
    if (logOverflow) throw new Error("Portal E2E server logs exceeded the bounded M1.16 scanner size.");
    return Buffer.concat(logChunks).toString("utf8");
  };
  await waitForCondition(async () => {
    if (child.exitCode !== null || child.signalCode !== null) {
      throw new Error("Portal E2E owned portal exited during startup.");
    }
    try {
      const response = await fetch(`${portalUrl}/login`, {
        signal: AbortSignal.timeout(1_000),
      });
      return response.status === 200;
    } catch {
      return false;
    }
  }, "portal", 45_000);
  return child;
}

function infrastructureSecretDetector(environment) {
  const privateValues = Object.entries(environment)
    .filter(([key, value]) =>
      /(?:SECRET|SERVICE_ROLE|JWT|POSTGRES_PASSWORD)/u.test(key) &&
      key !== "PUBLISHABLE_KEY" &&
      typeof value === "string" &&
      value.length >= 16)
    .map(([, value]) => value);
  return (value) => {
    const text = Buffer.isBuffer(value) ? value.toString("utf8") : String(value ?? "");
    return privateValues.some((privateValue) => text.includes(privateValue));
  };
}

function localServiceLogs(since) {
  const names = runQuiet("docker", ["ps", "--format", "{{.Names}}"]).split(/\r?\n/u)
    .filter((name) => name.endsWith("_Dog-RGB-1"))
    .sort();
  if (names.length < 2 || !names.some((name) => name.startsWith("supabase_db_")) ||
      !names.some((name) => name.startsWith("supabase_edge_runtime_"))) {
    throw new Error("M1.16 could not enumerate the local database and Edge log surfaces.");
  }
  return names.map((name) => runCaptured("docker", ["logs", "--since", since, name])).join("\n");
}

async function stopPortal(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  const closed = once(child, "close").then(() => true);
  if (process.platform === "win32") {
    spawnSync("taskkill", ["/PID", String(child.pid), "/T", "/F"], {
      stdio: "ignore",
    });
  } else {
    try {
      process.kill(-child.pid, "SIGTERM");
    } catch (error) {
      if (error?.code !== "ESRCH") throw error;
    }
  }
  const stopped = await Promise.race([
    closed,
    new Promise((resolveWait) => setTimeout(() => resolveWait(false), 5_000)),
  ]);
  if (!stopped) throw new Error("Portal E2E owned portal did not stop.");
}

async function preflight() {
  if (!process.argv.includes("--clean")) {
    throw new Error(
      "Portal E2E refuses to reset the local database implicitly. " +
      "Run `node tools/portal-e2e/run.mjs --clean`.",
    );
  }
  if (process.versions.node !== expectedNode) {
    throw new Error(`Portal E2E requires the isolated Node ${expectedNode} runtime.`);
  }
  const config = await readFile(join(workspace, "supabase", "config.toml"), "utf8");
  if (!/^project_id\s*=\s*"Dog-RGB-1"\s*$/mu.test(config)) {
    throw new Error("Portal E2E refused an unexpected Supabase project.");
  }
  const pinnedSupabase = (await readFile(join(workspace, ".supabase-version"), "utf8")).trim();
  if (runQuiet("supabase", ["--version"]) !== pinnedSupabase) {
    throw new Error("Portal E2E requires the repository-pinned Supabase CLI.");
  }
  const playwrightPackage = JSON.parse(await readFile(
    join(workspace, "node_modules", "@playwright", "test", "package.json"),
    "utf8",
  ));
  if (playwrightPackage.version !== expectedPlaywright) {
    throw new Error("Portal E2E requires the repository-pinned Playwright version.");
  }
  if (!existsSync(chromium.executablePath())) {
    throw new Error("Portal E2E requires the pinned Playwright Chromium installation.");
  }
  if (!await portIsFree()) {
    throw new Error("Portal E2E requires exclusive ownership of 127.0.0.1:3000.");
  }
  for (const artifactDirectory of [
    ownerArtifactDirectory,
    authorizationArtifactDirectory,
    faultArtifactDirectory,
    privacyArtifactDirectory,
  ]) {
    const resolvedArtifacts = resolve(artifactDirectory);
    if (!resolvedArtifacts.startsWith(`${workspace}\\`) && !resolvedArtifacts.startsWith(`${workspace}/`)) {
      throw new Error("Portal E2E artifact boundary is invalid.");
    }
  }
}

await preflight();
for (const artifactDirectory of [
  ownerArtifactDirectory,
  authorizationArtifactDirectory,
  faultArtifactDirectory,
  privacyArtifactDirectory,
]) {
  await rm(artifactDirectory, { recursive: true, force: true });
  await mkdir(artifactDirectory, { recursive: true });
}

let portal = null;
let environment = null;
let completed = false;
try {
  await writeFile(localSecretsPath, [
    `CLAIM_HMAC_PEPPER=${randomBytes(32).toString("base64url")}`,
    `DEVICE_CREDENTIAL_PEPPER=${randomBytes(32).toString("base64url")}`,
    "",
  ].join("\n"), { encoding: "utf8", mode: 0o600 });

  console.log("Portal E2E: replacing this repository's disposable local Supabase stack...");
  runQuiet("supabase", ["stop", "--no-backup"], { allowFailure: true });
  runQuiet("supabase", ["start"]);
  environment = parseLocalEnvironment();
  await verifyServices(environment);

  console.log("Portal E2E: building the portal once with the reviewed local environment...");
  run(process.execPath, [nextCli, "build"], {
    cwd: portalDirectory,
    env: portalEnvironment(environment),
  });

  for (const cycle of [1, 2]) {
    if (!m116Only) {
    console.log(`M1.13: clean owner journey ${cycle}/2...`);
    run("supabase", ["db", "reset"]);
    await verifyServices(environment);
    process.env.M113_MAILPIT_URL = environment.MAILPIT_URL;
    await clearMailbox();
    portal = await startPortal(environment);
    try {
      run(process.execPath, [
        playwrightCli,
        "test",
        "--config", playwrightConfig,
        "--project", "portal-owner-chromium",
      ], {
        env: {
          ...process.env,
          M113_CYCLE: String(cycle),
          M113_MAILPIT_URL: environment.MAILPIT_URL,
          M113_SUPABASE_URL: environment.API_URL,
        },
      });
    } finally {
      await stopPortal(portal);
      portal = null;
      await rm(join(ownerArtifactDirectory, "test-results"), {
        recursive: true,
        force: true,
      });
      await clearMailbox().catch(() => undefined);
    }

    console.log(`M1.14: clean authorization matrix ${cycle}/2...`);
    run("supabase", ["db", "reset"]);
    await verifyServices(environment);
    const authorizationFixture = await prepareAuthorizationFixture({
      apiUrl: environment.API_URL,
      publishableKey: environment.PUBLISHABLE_KEY,
      cycle,
    });
    portal = await startPortal(environment);
    let authorizationRunPassed = false;
    let authorizationRunError = null;
    try {
      try {
        run(process.execPath, [
          playwrightCli,
          "test",
          "--config", playwrightConfig,
          "--project", "portal-authorization-chromium",
        ], {
          env: {
            ...process.env,
            M114_CYCLE: String(cycle),
            M114_SUPABASE_URL: environment.API_URL,
            M114_PUBLISHABLE_KEY: environment.PUBLISHABLE_KEY,
            M114_MANIFEST: JSON.stringify(authorizationFixture.manifest),
          },
        });
        authorizationRunPassed = true;
      } catch (error) {
        authorizationRunError = error;
      }

      const artifactPath = join(authorizationArtifactDirectory, `cycle-${cycle}.json`);
      if (!existsSync(artifactPath)) {
        throw new Error("M1.14 authorization test produced no sanitized cycle artifact.");
      }
      const artifact = await readFile(artifactPath);
      if (authorizationFixture.artifactContainsPrivateMaterial(artifact)) {
        throw new Error("M1.14 retained private fixture material in its artifact.");
      }
      validateAuthorizationArtifact(
        artifact,
        cycle,
        authorizationRunPassed ? "passed" : "failed",
      );
      if (authorizationRunError) throw authorizationRunError;
    } finally {
      await stopPortal(portal);
      portal = null;
      await rm(join(authorizationArtifactDirectory, "test-results"), {
        recursive: true,
        force: true,
      });
    }

    console.log(`M1.15: clean deterministic fault matrix ${cycle}/2...`);
    run("supabase", ["db", "reset"]);
    await verifyServices(environment);
    const artifactPath = join(faultArtifactDirectory, `cycle-${cycle}.json`);
    let faultFixture = null;
    let faultRunError = null;
    try {
      faultFixture = await runM115FaultMatrix({
        apiUrl: environment.API_URL,
        publishableKey: environment.PUBLISHABLE_KEY,
        cycle,
        artifactPath,
        restartLocalStack: async () => {
          try {
            runQuiet("supabase", ["stop"]);
            runQuiet("supabase", ["start"]);
            const restarted = parseLocalEnvironment();
            await verifyServices(restarted);
            environment = restarted;
            return Object.freeze({
              apiUrl: restarted.API_URL,
              publishableKey: restarted.PUBLISHABLE_KEY,
            });
          } catch (restartError) {
            try {
              runQuiet("supabase", ["start"]);
              const recovered = parseLocalEnvironment();
              await verifyServices(recovered);
              environment = recovered;
            } catch {
              // Preserve the original restart failure; outer cleanup reports the failed artifact.
            }
            throw restartError;
          }
        },
      });
    } catch (error) {
      faultRunError = error;
    }
    if (!existsSync(artifactPath)) {
      throw new Error("M1.15 fault matrix produced no sanitized cycle artifact.");
    }
    const faultArtifact = await readFile(artifactPath);
    if (faultFixture?.artifactContainsPrivateMaterial(faultArtifact)) {
      throw new Error("M1.15 retained private fixture material in its artifact.");
    }
    validateFaultArtifact(faultArtifact, cycle, faultRunError ? "failed" : "passed");
    if (faultRunError) throw faultRunError;
    }

    console.log(`M1.16: clean privacy/cache gate ${cycle}/2...`);
    run("supabase", ["db", "reset"]);
    await verifyServices(environment);
    const privacyStartedAt = new Date().toISOString();
    const privacyFixture = await prepareAuthorizationFixture({
      apiUrl: environment.API_URL,
      publishableKey: environment.PUBLISHABLE_KEY,
      cycle,
    });
    const containsInfrastructureSecret = infrastructureSecretDetector(environment);
    const privacyArtifactPath = join(privacyArtifactDirectory, `cycle-${cycle}.json`);
    let privacyRunError = null;
    portal = await startPortal(environment);
    try {
      try {
        await runM116PrivacyCacheGate({
          artifactDirectory: privacyArtifactDirectory,
          browserType: chromium,
          containsInfrastructureSecret,
          cycle,
          fixture: privacyFixture,
          portalDirectory,
          portalLogs: () => portal.portalLogs(),
          portalUrl,
          readServiceLogs: async () => localServiceLogs(privacyStartedAt),
        });
      } catch (error) {
        privacyRunError = error;
      }
    } finally {
      await stopPortal(portal);
      portal = null;
      await rm(join(privacyArtifactDirectory, "test-results"), {
        recursive: true,
        force: true,
      });
    }
    if (!existsSync(privacyArtifactPath)) {
      throw new Error("M1.16 privacy/cache gate produced no sanitized cycle artifact.");
    }
    const privacyArtifact = await readFile(privacyArtifactPath);
    if (
      privacyFixture.artifactContainsPrivateMaterial(privacyArtifact) ||
      containsInfrastructureSecret(privacyArtifact)
    ) {
      throw new Error("M1.16 retained private fixture or infrastructure material in its artifact.");
    }
    validateM116Artifact(
      privacyArtifact,
      cycle,
      privacyRunError ? "failed" : "passed",
    );
    if (privacyRunError) throw privacyRunError;
  }
  completed = true;
  console.log(m116Only
    ? "M1.16: privacy/cache gate passed from two clean resets."
    : "M1.13/M1.14/M1.15/M1.16: owner, authorization, fault, and privacy/cache gates " +
      "passed from two clean resets each.");
} finally {
  await stopPortal(portal).catch(() => undefined);
  let cleanupError = null;
  if (environment) {
    process.env.M113_MAILPIT_URL = environment.MAILPIT_URL;
    await clearMailbox().catch(() => undefined);
    if (!completed) {
      runQuiet("supabase", ["db", "reset"], { allowFailure: true });
    } else {
      try {
        runQuiet("supabase", ["db", "reset"]);
      } catch {
        console.warn("Portal E2E final reset failed once; recovering services and retrying once.");
        try {
          runQuiet("supabase", ["start"], { allowFailure: true });
          const recovered = parseLocalEnvironment();
          await verifyServices(recovered);
          environment = recovered;
          runQuiet("supabase", ["db", "reset"]);
        } catch (error) {
          cleanupError = error;
        }
      }
    }
  }
  await unlink(localSecretsPath).catch((error) => {
    if (error?.code !== "ENOENT") throw error;
  });
  if (cleanupError) throw cleanupError;
  if (!completed) {
    console.error("Portal E2E failed; see the sanitized cycle artifact for its last completed checkpoint.");
  }
}
