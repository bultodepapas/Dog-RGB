import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { once } from "node:events";
import { readdir, readFile } from "node:fs/promises";
import { createServer } from "node:net";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { createPairOnlySimulator } from "./pair-only.mjs";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const CLAIM_CODE_PATTERN = /^[0-9A-HJKMNP-TV-Z]{16}$/u;
const workspace = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const portalDirectory = join(workspace, "apps", "portal");
const nextCli = join(workspace, "node_modules", "next", "dist", "bin", "next");
const apiUrl = process.env.SUPABASE_URL;
const publishableKey = process.env.SUPABASE_PUBLISHABLE_KEY;

if (!apiUrl || !publishableKey) {
  throw new Error("Local public Supabase environment is required for browser pairing.");
}
delete process.env.DEBUG;
delete process.env.PWDEBUG;
const { chromium } = await import("@playwright/test");

function portalEnvironment() {
  const allowed = [
    "APPDATA", "CI", "ComSpec", "HOME", "LOCALAPPDATA",
    "PATH", "Path", "PATHEXT", "SystemRoot", "SYSTEMROOT", "TEMP", "TMP",
    "TMPDIR", "USERPROFILE", "WINDIR",
  ];
  const environment = Object.fromEntries(
    allowed.flatMap((name) => process.env[name] === undefined
      ? []
      : [[name, process.env[name]]]),
  );
  return {
    ...environment,
    NEXT_PUBLIC_SUPABASE_URL: apiUrl,
    NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY: publishableKey,
    NEXT_TELEMETRY_DISABLED: "1",
  };
}

async function availablePort() {
  const server = createServer();
  server.unref();
  await new Promise((resolveListen, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolveListen);
  });
  const address = server.address();
  const port = typeof address === "object" && address ? address.port : null;
  await new Promise((resolveClose, reject) => {
    server.close((error) => error ? reject(error) : resolveClose());
  });
  if (!Number.isInteger(port)) throw new Error("Unable to reserve a local portal port.");
  return port;
}

function capture(stream, append) {
  stream?.setEncoding("utf8");
  stream?.on("data", (chunk) => append(String(chunk)));
}

async function waitForPortal(url, processHandle) {
  for (let attempt = 0; attempt < 120; attempt += 1) {
    if (processHandle.exitCode !== null || processHandle.signalCode !== null) {
      throw new Error("The local portal exited before browser pairing.");
    }
    try {
      const response = await fetch(`${url}/login`, {
        signal: AbortSignal.timeout(1_000),
      });
      if (response.ok) return;
    } catch {
      // The development server is still starting.
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 250));
  }
  throw new Error("Timed out waiting for the local portal.");
}

async function stopProcess(processHandle) {
  if (processHandle.exitCode !== null || processHandle.signalCode !== null) return;
  const closed = once(processHandle, "close").then(() => true);
  const waitForClose = (timeoutMs) => {
    return Promise.race([
      closed,
      new Promise((resolveWait) => setTimeout(() => resolveWait(false), timeoutMs)),
    ]);
  };
  if (process.platform === "win32") {
    spawnSync("taskkill", ["/PID", String(processHandle.pid), "/T", "/F"], {
      stdio: "ignore",
    });
  } else {
    try {
      process.kill(-processHandle.pid, "SIGTERM");
    } catch (error) {
      if (error?.code !== "ESRCH") throw error;
    }
  }
  if (await waitForClose(5_000)) return;
  if (process.platform !== "win32") {
    try {
      process.kill(-processHandle.pid, "SIGKILL");
    } catch (error) {
      if (error?.code !== "ESRCH") throw error;
    }
  }
  if (!await waitForClose(5_000)) {
    throw new Error("The local portal process tree did not stop.");
  }
}

async function scanRuntimeArtifacts(containsPrivateMaterial) {
  const excludedDirectories = new Set([".git", ".pio", "node_modules"]);
  const pending = [workspace];
  let scannedFiles = 0;
  while (pending.length > 0) {
    const directory = pending.pop();
    const entries = await readdir(directory, { withFileTypes: true });
    for (const entry of entries) {
      const path = join(directory, entry.name);
      if (entry.isDirectory()) {
        if (!excludedDirectories.has(entry.name)) pending.push(path);
        continue;
      }
      if (!entry.isFile()) continue;
      let content;
      try {
        content = await readFile(path);
      } catch {
        continue;
      }
      scannedFiles += 1;
      if (containsPrivateMaterial(content)) {
        throw new Error(`Private pairing material reached a runtime artifact: ${relative(workspace, path)}`);
      }
    }
  }
  return scannedFiles;
}

async function sensitiveArtifactManifest() {
  const excludedDirectories = new Set([".git", ".pio", "node_modules"]);
  const artifactDirectories = new Set([".playwright-cli", "playwright-report", "test-results"]);
  const artifactExtensions = new Set([
    ".gif", ".har", ".jpeg", ".jpg", ".mp4", ".png", ".webm", ".webp", ".zip",
  ]);
  const pending = [workspace];
  const files = new Set();
  while (pending.length > 0) {
    const directory = pending.pop();
    const entries = await readdir(directory, { withFileTypes: true });
    for (const entry of entries) {
      const path = join(directory, entry.name);
      if (entry.isDirectory()) {
        if (!excludedDirectories.has(entry.name)) pending.push(path);
        continue;
      }
      if (!entry.isFile()) continue;
      const workspacePath = relative(workspace, path);
      const topDirectory = workspacePath.split(/[\\/]/u)[0];
      const extension = entry.name.includes(".")
        ? entry.name.slice(entry.name.lastIndexOf(".")).toLowerCase()
        : "";
      if (artifactDirectories.has(topDirectory) || artifactExtensions.has(extension)) {
        files.add(workspacePath);
      }
    }
  }
  return files;
}

function verifyNoNewSensitiveArtifacts(before, after) {
  const created = [...after].filter((path) => !before.has(path));
  if (created.length > 0) {
    throw new Error("Browser pairing created a retained trace, capture, or report artifact.");
  }
}

function scanSupabaseLogs(containsPrivateMaterial) {
  const listed = spawnSync("docker", ["ps", "--format", "{{.Names}}"], {
    encoding: "utf8",
    maxBuffer: 2 * 1024 * 1024,
  });
  if (listed.status !== 0) throw new Error("Unable to enumerate local Supabase logs.");
  const projectSuffix = `_${workspace.split(/[\\/]/u).at(-1)}`;
  const containers = listed.stdout.split(/\r?\n/u).filter(
    (name) => name.startsWith("supabase_") && name.endsWith(projectSuffix),
  );
  if (containers.length === 0) throw new Error("No local Supabase containers were available for log scanning.");
  for (const container of containers) {
    const logs = spawnSync("docker", ["logs", container], {
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
    });
    if (logs.status !== 0) throw new Error("Unable to read a local Supabase log.");
    if (containsPrivateMaterial(`${logs.stdout}${logs.stderr}`)) {
      throw new Error("Private pairing material reached a local Supabase log.");
    }
  }
  return containers.length;
}

function databaseContainer() {
  const projectName = workspace.split(/[\\/]/u).at(-1);
  const listed = spawnSync("docker", [
    "ps",
    "--filter", `label=com.supabase.cli.project=${projectName}`,
    "--filter", `name=^/supabase_db_${projectName}$`,
    "--format", "{{.ID}}",
  ], { encoding: "utf8", maxBuffer: 1024 * 1024 });
  const containers = listed.status === 0
    ? listed.stdout.split(/\r?\n/u).filter(Boolean)
    : [];
  if (containers.length !== 1) throw new Error("Unable to locate the local pairing database.");
  return containers[0];
}

function verifyPairingPersistence(pairing) {
  const sql = `
    select json_build_object(
      'claim_count', (select count(*) from private.device_claims
        where consumed_by_device_id = '${pairing.deviceId}'::uuid),
      'valid_consumed_claim_count', (select count(*) from private.device_claims
        where consumed_by_device_id = '${pairing.deviceId}'::uuid
          and state = 'consumed' and attempt_count = 1
          and octet_length(request_sha256) = 32
          and not (response_json ?| array['claim_code', 'credential_id', 'credential_secret'])
          and response_json::text !~ '(claim_code|credential_id|credential_secret|drgb_v1_)'),
      'collar_count', (select count(*) from api.collars
        where id = '${pairing.collarId}'::uuid
          and device_public_id = '${pairing.deviceId}'::uuid
          and dog_id = '${pairing.dogId}'::uuid and state = 'active'),
      'credential_count', (select count(*) from private.device_credentials
        where collar_id = '${pairing.collarId}'::uuid
          and state = 'active' and octet_length(secret_digest) = 32),
      'side_effect_count', (
        (select count(*) from private.sync_requests where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from api.recordings where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from private.telemetry_chunks where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from api.telemetry_points where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from private.telemetry_loss_markers where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from private.device_daily_summaries where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from api.daily_summaries where dog_id = '${pairing.dogId}'::uuid) +
        (select count(*) from private.dirty_summary_days where dog_id = '${pairing.dogId}'::uuid) +
        (select count(*) from api.config_revisions where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from api.config_resource_heads where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from api.config_reported where collar_id = '${pairing.collarId}'::uuid) +
        (select count(*) from private.config_hlc_state where collar_id = '${pairing.collarId}'::uuid)
      )
    )::text;
  `;
  const queried = spawnSync("docker", [
    "exec", "-i", databaseContainer(), "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", "postgres",
  ], {
    input: sql,
    encoding: "utf8",
    maxBuffer: 2 * 1024 * 1024,
  });
  if (queried.status !== 0) throw new Error("Unable to verify local pairing persistence.");
  let evidence;
  try {
    evidence = JSON.parse(queried.stdout.trim());
  } catch {
    throw new Error("Local pairing persistence returned invalid evidence.");
  }
  assert.deepEqual(evidence, {
    claim_count: 1,
    valid_consumed_claim_count: 1,
    collar_count: 1,
    credential_count: 1,
    side_effect_count: 0,
  });
}

const port = await availablePort();
const portalUrl = `http://127.0.0.1:${port}`;
let serverOutput = "";
const appendServerOutput = (chunk) => {
  serverOutput += chunk;
};
const environment = portalEnvironment();
const build = spawnSync(process.execPath, [nextCli, "build"], {
  cwd: portalDirectory,
  env: environment,
  encoding: "utf8",
  maxBuffer: 16 * 1024 * 1024,
});
serverOutput += `${build.stdout ?? ""}${build.stderr ?? ""}`;
if (build.status !== 0) throw new Error("The local portal production build failed.");
const initialSensitiveArtifacts = await sensitiveArtifactManifest();
const portalProcess = spawn(
  process.execPath,
  [nextCli, "start", "--hostname", "127.0.0.1", "--port", String(port)],
  {
    cwd: portalDirectory,
    env: environment,
    detached: process.platform !== "win32",
    stdio: ["ignore", "pipe", "pipe"],
  },
);
capture(portalProcess.stdout, appendServerOutput);
capture(portalProcess.stderr, appendServerOutput);

let browser;
let claimCode = null;
let containsPrivateMaterial = null;
let privacyScanned = false;
let scannedFiles = 0;
let scannedContainers = 0;
async function sealAndScan() {
  await stopProcess(portalProcess);
  if (!containsPrivateMaterial || privacyScanned) return;
  assert.equal(containsPrivateMaterial(serverOutput), false);
  verifyNoNewSensitiveArtifacts(
    initialSensitiveArtifacts,
    await sensitiveArtifactManifest(),
  );
  scannedFiles = await scanRuntimeArtifacts(containsPrivateMaterial);
  scannedContainers = scanSupabaseLogs(containsPrivateMaterial);
  privacyScanned = true;
}
try {
  await waitForPortal(portalUrl, portalProcess);
  browser = await chromium.launch({
    headless: true,
    args: ["--disk-cache-size=0", "--media-cache-size=0"],
  });
  const context = await browser.newContext({ serviceWorkers: "block" });
  const page = await context.newPage();
  const collarsPath = `/app/${DOG_ID}/collars`;
  await page.goto(`${portalUrl}/login?next=${encodeURIComponent(collarsPath)}`);
  await page.locator('input[name="email"]').fill("owner@example.test");
  await page.locator('input[name="password"]').fill("local-owner-password");
  await Promise.all([
    page.waitForURL((url) => url.pathname === collarsPath),
    page.getByRole("button", { name: "INICIAR SESIÓN" }).click(),
  ]);
  await page.getByRole("button", { name: "Generar código" }).click();
  const codeElement = page.locator('[role="status"] code');
  await codeElement.waitFor({ state: "visible" });
  claimCode = await codeElement.textContent();
  assert.equal(typeof claimCode === "string" && CLAIM_CODE_PATTERN.test(claimCode), true);
  const issuedCode = claimCode;
  containsPrivateMaterial = (value) => typeof value === "string"
    ? value.includes(issuedCode)
    : Buffer.isBuffer(value) && value.includes(Buffer.from(issuedCode));

  const browserStorageSafe = await page.evaluate((code) => ({
    local: !Object.values(localStorage).some((value) => value.includes(code)),
    session: !Object.values(sessionStorage).some((value) => value.includes(code)),
    url: !location.href.includes(code),
  }), claimCode);
  const cookies = await context.cookies();
  assert.equal(browserStorageSafe.local, true);
  assert.equal(browserStorageSafe.session, true);
  assert.equal(browserStorageSafe.url, true);
  assert.equal(cookies.every((cookie) => !cookie.value.includes(claimCode)), true);
  await context.close();
  await browser.close();
  browser = undefined;

  const simulator = await createPairOnlySimulator({
    claimCode,
    apiUrl,
    expectedDogId: DOG_ID,
  });
  containsPrivateMaterial = (value) => simulator.artifactContainsPrivateMaterial(value);
  const proof = await simulator.proveReplaySafety();
  assert.equal(proof.ok, true);
  assert.equal(proof.pairing.dogId, DOG_ID);
  verifyPairingPersistence(proof.pairing);
  const anonymousCollar = await fetch(
    `${apiUrl}/rest/v1/collars?id=eq.${proof.pairing.collarId}&select=id`,
    { headers: { apikey: publishableKey } },
  );
  assert.equal([401, 403].includes(anonymousCollar.status), true);
  assert.equal(simulator.artifactContainsPrivateMaterial(await anonymousCollar.text()), false);
  await sealAndScan();
  claimCode = null;

  console.log(JSON.stringify({
    ok: true,
    scenarios: ["browser-memory-handoff", ...proof.scenarios],
    browser_pairing_persistence_verified: true,
    runtime_artifact_files_scanned: scannedFiles,
    supabase_container_logs_scanned: scannedContainers,
  }));
} finally {
  claimCode = null;
  await browser?.close().catch(() => {});
  await sealAndScan();
}
