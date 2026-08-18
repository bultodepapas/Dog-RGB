import { spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const SQL_PATH = resolve(ROOT, "tools", "cloud_capacity", "phase1_benchmark.sql");
const EVIDENCE_PATH = resolve(ROOT, "test-results", "capacity", "phase1-local.txt");

function executable(command) {
  if (process.platform !== "win32") return command;
  return command === "supabase" ? "supabase.exe" : `${command}.exe`;
}

function invoke(command, args, options = {}) {
  const result = spawnSync(executable(command), args, {
    cwd: ROOT,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
    ...options,
  });
  return result;
}

function requireSuccess(command, args, options = {}) {
  const result = invoke(command, args, options);
  if (result.stdout) process.stdout.write(result.stdout);
  if (result.stderr) process.stderr.write(result.stderr);
  if (result.status !== 0) {
    throw new Error(
      `${command} ${args.join(" ")} failed with status ${result.status ?? result.error?.code ?? "unknown"}`,
    );
  }
  return result;
}

async function waitForLocalStack() {
  let lastResult;
  for (let attempt = 1; attempt <= 60; attempt += 1) {
    lastResult = invoke("supabase", ["status", "-o", "json"]);
    if (lastResult.status === 0) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 1_000));
  }
  if (lastResult?.stdout) process.stdout.write(lastResult.stdout);
  if (lastResult?.stderr) process.stderr.write(lastResult.stderr);
  throw new Error("The Dog-RGB-1 local Supabase stack did not become ready within 60 seconds.");
}

if (!process.argv.includes("--clean")) {
  throw new Error(
    "Refusing to replace the disposable local database implicitly. " +
      "Run `npm run phase1:capacity -- --clean` to authorize the benchmark resets.",
  );
}

const unexpected = process.argv.slice(2).filter((argument) => argument !== "--clean");
if (unexpected.length > 0) {
  throw new Error(`Unknown arguments: ${unexpected.join(", ")}`);
}

let benchmarkOutput = "";
let benchmarkError;

try {
  await waitForLocalStack();
  console.log("Resetting the disposable local database before the Phase 1 capacity gate...");
  requireSuccess("supabase", ["db", "reset"]);
  await waitForLocalStack();

  const containers = requireSuccess("docker", [
    "ps",
    "--filter",
    "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter",
    "name=^/supabase_db_Dog-RGB-1$",
    "--format",
    "{{.ID}}",
  ]).stdout.trim().split(/\r?\n/).filter(Boolean);

  if (containers.length !== 1) {
    throw new Error(
      `Expected exactly one Dog-RGB-1 local database container; found ${containers.length}.`,
    );
  }

  const sql = readFileSync(SQL_PATH, "utf8");
  const benchmark = invoke("docker", [
    "exec",
    "-i",
    containers[0],
    "psql",
    "-X",
    "-v",
    "ON_ERROR_STOP=1",
    "-U",
    "postgres",
    "-d",
    "postgres",
  ], { input: sql });

  benchmarkOutput = [benchmark.stdout, benchmark.stderr].filter(Boolean).join("\n");
  process.stdout.write(benchmark.stdout ?? "");
  process.stderr.write(benchmark.stderr ?? "");
  if (benchmark.status !== 0) {
    throw new Error(
      `Phase 1 capacity SQL failed with status ${benchmark.status ?? benchmark.error?.code ?? "unknown"}.`,
    );
  }
} catch (error) {
  benchmarkError = error;
} finally {
  mkdirSync(dirname(EVIDENCE_PATH), { recursive: true });
  writeFileSync(EVIDENCE_PATH, benchmarkOutput, "utf8");
  console.log(`Capacity evidence written to ${EVIDENCE_PATH}.`);
  console.log("Restoring the disposable local database to its seeded state...");
  const reset = invoke("supabase", ["db", "reset"]);
  if (reset.stdout) process.stdout.write(reset.stdout);
  if (reset.stderr) process.stderr.write(reset.stderr);
  if (reset.status !== 0 && !benchmarkError) {
    benchmarkError = new Error(
      `Final Supabase reset failed with status ${reset.status ?? reset.error?.code ?? "unknown"}.`,
    );
  }
}

if (benchmarkError) throw benchmarkError;
console.log("Phase 1 capacity gate passed and the local database was restored.");
