import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import { existsSync } from "node:fs";
import { mkdir, readFile, rm, unlink, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { chromium } from "@playwright/test";

import { clearMailbox } from "./mailpit.mjs";

const workspace = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const portalDirectory = join(workspace, "apps", "portal");
const nextCli = join(workspace, "node_modules", "next", "dist", "bin", "next");
const playwrightCli = join(workspace, "node_modules", "@playwright", "test", "cli.js");
const playwrightConfig = join(workspace, "playwright.portal.config.ts");
const localSecretsPath = join(workspace, "supabase", "functions", ".env");
const artifactDirectory = join(workspace, "output", "playwright", "m113");
const expectedNode = "24.18.0";
const expectedPlaywright = "1.62.1";
const portalUrl = "http://127.0.0.1:3000";

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
    throw new Error(`M1.13 command failed: ${command} ${args[0] ?? ""}.`);
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
    throw new Error(`M1.13 command failed: ${command} ${args[0] ?? ""}.`);
  }
  return result.stdout.trim();
}

function parseLocalEnvironment() {
  const output = runQuiet("supabase", ["status", "-o", "env"]);
  const values = Object.fromEntries(output.split(/\r?\n/u).flatMap((line) => {
    const match = line.match(/^([^=]+)="?(.*?)"?$/u);
    return match ? [[match[1], match[2].replace(/"$/u, "")]] : [];
  }));
  for (const key of ["API_URL", "MAILPIT_URL", "PUBLISHABLE_KEY"]) {
    if (typeof values[key] !== "string" || values[key].length < 1) {
      throw new Error("M1.13 local Supabase environment was incomplete.");
    }
  }
  for (const key of ["API_URL", "MAILPIT_URL"]) {
    const url = new URL(values[key]);
    if (url.protocol !== "http:" || url.hostname !== "127.0.0.1") {
      throw new Error("M1.13 refused a non-loopback local service.");
    }
  }
  return Object.freeze(values);
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
  throw new Error(`M1.13 ${label} did not become ready.`);
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
    throw new Error("M1.13 requires exclusive ownership of 127.0.0.1:3000.");
  }
  const child = spawn(process.execPath, [
    nextCli, "start", "--hostname", "127.0.0.1", "--port", "3000",
  ], {
    cwd: portalDirectory,
    env: portalEnvironment(environment),
    detached: process.platform !== "win32",
    stdio: ["ignore", "pipe", "pipe"],
  });
  child.stdout?.resume();
  child.stderr?.resume();
  await waitForCondition(async () => {
    if (child.exitCode !== null || child.signalCode !== null) {
      throw new Error("M1.13 owned portal exited during startup.");
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
  if (!stopped) throw new Error("M1.13 owned portal did not stop.");
}

async function preflight() {
  if (!process.argv.includes("--clean")) {
    throw new Error(
      "M1.13 refuses to reset the local database implicitly. " +
      "Run `node tools/portal-e2e/run.mjs --clean`.",
    );
  }
  if (process.versions.node !== expectedNode) {
    throw new Error(`M1.13 requires the isolated Node ${expectedNode} runtime.`);
  }
  const config = await readFile(join(workspace, "supabase", "config.toml"), "utf8");
  if (!/^project_id\s*=\s*"Dog-RGB-1"\s*$/mu.test(config)) {
    throw new Error("M1.13 refused an unexpected Supabase project.");
  }
  const pinnedSupabase = (await readFile(join(workspace, ".supabase-version"), "utf8")).trim();
  if (runQuiet("supabase", ["--version"]) !== pinnedSupabase) {
    throw new Error("M1.13 requires the repository-pinned Supabase CLI.");
  }
  const playwrightPackage = JSON.parse(await readFile(
    join(workspace, "node_modules", "@playwright", "test", "package.json"),
    "utf8",
  ));
  if (playwrightPackage.version !== expectedPlaywright) {
    throw new Error("M1.13 requires the repository-pinned Playwright version.");
  }
  if (!existsSync(chromium.executablePath())) {
    throw new Error("M1.13 requires the pinned Playwright Chromium installation.");
  }
  if (!await portIsFree()) {
    throw new Error("M1.13 requires exclusive ownership of 127.0.0.1:3000.");
  }
  const resolvedArtifacts = resolve(artifactDirectory);
  if (!resolvedArtifacts.startsWith(`${workspace}\\`) && !resolvedArtifacts.startsWith(`${workspace}/`)) {
    throw new Error("M1.13 artifact boundary is invalid.");
  }
}

await preflight();
await rm(artifactDirectory, { recursive: true, force: true });
await mkdir(artifactDirectory, { recursive: true });

let portal = null;
let environment = null;
let completed = false;
try {
  await writeFile(localSecretsPath, [
    `CLAIM_HMAC_PEPPER=${randomBytes(32).toString("base64url")}`,
    `DEVICE_CREDENTIAL_PEPPER=${randomBytes(32).toString("base64url")}`,
    "",
  ].join("\n"), { encoding: "utf8", mode: 0o600 });

  console.log("M1.13: replacing this repository's disposable local Supabase stack...");
  runQuiet("supabase", ["stop", "--no-backup"], { allowFailure: true });
  runQuiet("supabase", ["start"]);
  environment = parseLocalEnvironment();
  await verifyServices(environment);

  console.log("M1.13: building the portal once with the reviewed local environment...");
  run(process.execPath, [nextCli, "build"], {
    cwd: portalDirectory,
    env: portalEnvironment(environment),
  });

  for (const cycle of [1, 2]) {
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
      await rm(join(artifactDirectory, "test-results"), {
        recursive: true,
        force: true,
      });
      await clearMailbox().catch(() => undefined);
    }
  }
  completed = true;
  console.log("M1.13: both independent clean owner journeys passed.");
} finally {
  await stopPortal(portal).catch(() => undefined);
  if (environment) {
    process.env.M113_MAILPIT_URL = environment.MAILPIT_URL;
    await clearMailbox().catch(() => undefined);
    runQuiet("supabase", ["db", "reset"], { allowFailure: true });
  }
  await unlink(localSecretsPath).catch((error) => {
    if (error?.code !== "ENOENT") throw error;
  });
  if (!completed) {
    console.error("M1.13 failed; see the sanitized cycle artifact for its last completed checkpoint.");
  }
}
