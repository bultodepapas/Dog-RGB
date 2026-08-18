import { spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { unlink, writeFile } from "node:fs/promises";

function executable(command) {
  if (process.platform !== "win32") return command;
  return { supabase: "supabase.exe", node: process.execPath }[command] ?? command;
}

function invocation(command, args) {
  if (process.platform === "win32" && command === "npm") {
    const npmCli = process.env.npm_execpath;
    if (!npmCli) throw new Error("npm_execpath is required to run the Windows npm CLI without cmd.exe.");
    return { file: process.execPath, args: [npmCli, ...args] };
  }
  return { file: executable(command), args };
}

function run(command, args, options = {}) {
  const call = invocation(command, args);
  const result = spawnSync(call.file, call.args, { stdio: "inherit", ...options });
  if (result.status !== 0) {
    throw new Error(`${command} ${args.join(" ")} failed with status ${result.status ?? result.error?.code ?? "unknown"}`);
  }
}

function runQuiet(command, args, { allowFailure = false } = {}) {
  const result = spawnSync(executable(command), args, {
    stdio: ["ignore", "ignore", "inherit"],
  });
  if (!allowFailure && result.status !== 0) {
    throw new Error(`${command} ${args.join(" ")} failed with status ${result.status ?? result.error?.code ?? "unknown"}`);
  }
}

const clean = process.argv.includes("--clean");
if (!clean) {
  throw new Error(
    "Refusing to replace the local Supabase database implicitly. " +
    "Run `npm run phase1:local -- --clean` to execute the destructive clean-room gate.",
  );
}

function localEnvironment() {
  const result = spawnSync(executable("supabase"), ["status", "-o", "env"], {
    encoding: "utf8",
  });
  if (result.status !== 0) throw new Error("Unable to read the local Supabase environment.");
  const values = Object.fromEntries(result.stdout.split(/\r?\n/).flatMap((line) => {
    const match = line.match(/^([^=]+)="?(.*?)"?$/);
    return match ? [[match[1], match[2].replace(/"$/, "")]] : [];
  }));
  return {
    ...process.env,
    SUPABASE_URL: values.API_URL,
    SUPABASE_PUBLISHABLE_KEY: values.PUBLISHABLE_KEY,
  };
}

async function waitForGateway(url) {
  for (let attempt = 0; attempt < 20; attempt += 1) {
    try {
      const response = await fetch(`${url}/functions/v1/device-v1-sync`);
      if (response.status === 405 && response.headers.get("content-type")?.includes("application/problem+json")) return;
      throw new Error(`Edge Function readiness probe returned unexpected HTTP ${response.status}.`);
    } catch (error) {
      if (error instanceof Error && error.message.startsWith("Edge Function readiness")) throw error;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error("Timed out waiting for the local Edge Function runtime.");
}

const localSecretsPath = "supabase/functions/.env";
try {
  await writeFile(localSecretsPath, [
    `CLAIM_HMAC_PEPPER=${randomBytes(32).toString("base64url")}`,
    `DEVICE_CREDENTIAL_PEPPER=${randomBytes(32).toString("base64url")}`,
    "",
  ].join("\n"), { encoding: "utf8", mode: 0o600 });

  console.log("Replacing this repository's disposable local Supabase stack (--clean)...");
  runQuiet("supabase", ["stop", "--no-backup"], { allowFailure: true });
  runQuiet("supabase", ["start"]);
  console.log("Rebuilding and testing the database...");
  run("supabase", ["db", "reset"]);
  run("supabase", ["test", "db", "supabase/tests/database", "--local"]);
  run("supabase", ["db", "lint", "--local", "--schema", "api,private", "--level", "warning", "--fail-on", "error"]);
  run("supabase", ["db", "advisors", "--local", "--type", "all", "--level", "warn", "--fail-on", "error"]);
  run("npm", ["run", "phase1:check"]);

  const environment = localEnvironment();
  console.log("Checking the Edge Function gateway started by Supabase...");
  await waitForGateway(environment.SUPABASE_URL);
  run("node", ["tools/device-simulator/boundary-matrix.mjs"], { env: environment });
  run("node", ["tools/device-simulator/simulator.mjs"], { env: environment });
  run("node", ["tools/cloud_restore/phase1_restore.mjs"]);
  run("node", ["tools/cloud_deletion/phase1_run.mjs"], { env: environment });
  console.log("Phase 1 local foundation passed from a clean database reset.");
} finally {
  await unlink(localSecretsPath).catch((error) => {
    if (error?.code !== "ENOENT") throw error;
  });
}
