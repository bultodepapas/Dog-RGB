import { spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { isDeepStrictEqual } from "node:util";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const MANIFEST_SQL_PATH = resolve(ROOT, "tools", "cloud_restore", "manifest.sql");
const EVIDENCE_PATH = resolve(ROOT, "test-results", "restore", "phase1-local.json");
const MAX_DUMP_BYTES = 128 * 1024 * 1024;
const OWNER_ID = "10000000-0000-4000-8000-000000000001";
const OUTSIDER_ID = "20000000-0000-4000-8000-000000000002";

function executable(command) {
  if (process.platform !== "win32") return command;
  return `${command}.exe`;
}

function invoke(args, options = {}) {
  return spawnSync(executable("docker"), args, {
    cwd: ROOT,
    encoding: "utf8",
    maxBuffer: MAX_DUMP_BYTES,
    ...options,
  });
}

function resultError(label, result) {
  return new Error(
    `${label} failed with status ${result.status ?? result.error?.code ?? "unknown"}`,
  );
}

function requireText(label, args, options = {}) {
  const result = invoke(args, options);
  if (result.status !== 0) {
    if (result.stdout) process.stdout.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    throw resultError(label, result);
  }
  return result.stdout;
}

function requireBinary(label, args) {
  const result = invoke(args, { encoding: null });
  if (result.status !== 0) {
    if (result.stderr) process.stderr.write(result.stderr.toString("utf8"));
    throw resultError(label, result);
  }
  if (!Buffer.isBuffer(result.stdout) || result.stdout.length === 0) {
    throw new Error(`${label} produced an empty backup.`);
  }
  return result.stdout;
}

function databaseName() {
  const value = `dog_rgb_restore_${process.pid}_${randomBytes(4).toString("hex")}`;
  if (!/^dog_rgb_restore_[0-9]+_[0-9a-f]{8}$/.test(value) || value.length > 63) {
    throw new Error("Generated restore database name failed its safety invariant.");
  }
  return value;
}

function psql(container, database, sql) {
  return requireText(
    `psql ${database}`,
    [
      "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
      "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", database,
    ],
    { input: sql },
  ).trim();
}

function manifest(container, database, sql) {
  const output = psql(container, database, sql);
  try {
    return JSON.parse(output);
  } catch {
    throw new Error(`Restore manifest for ${database} was not one valid JSON object.`);
  }
}

function accessProbe(container, database, userId) {
  const sql = `
    begin;
    set local role authenticated;
    set local "request.jwt.claim.sub" = '${userId}';
    select jsonb_build_object(
      'dogs', (select count(*) from api.dogs),
      'collars', (select count(*) from api.collars),
      'telemetry_points', (select count(*) from api.telemetry_points),
      'config_heads', (select count(*) from api.config_resource_heads)
    )::text;
    rollback;
  `;
  const output = psql(container, database, sql);
  try {
    return JSON.parse(output);
  } catch {
    throw new Error(`RLS access probe for ${database} was not one valid JSON object.`);
  }
}

const unexpected = process.argv.slice(2);
if (unexpected.length > 0) {
  throw new Error(`Unknown arguments: ${unexpected.join(", ")}`);
}

const startedAt = Date.now();
const restoreDatabase = databaseName();
const evidence = {
  status: "failed",
  isolation: "separate_database_in_disposable_local_supabase_cluster",
  backup_format: "pg_dump_custom_full_database",
  backup_persisted: false,
};
let container;
let databaseCreated = false;
let primaryError;
let backupBuffer;

try {
  const containers = requireText("locate local Supabase database", [
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ]).trim().split(/\r?\n/).filter(Boolean);

  if (containers.length !== 1) {
    throw new Error(
      `Expected exactly one Dog-RGB-1 local database container; found ${containers.length}.`,
    );
  }
  [container] = containers;

  const manifestSql = readFileSync(MANIFEST_SQL_PATH, "utf8");
  const sourceManifest = manifest(container, "postgres", manifestSql);
  const requiredCounts = sourceManifest.table_counts ?? {};
  for (const relation of [
    "api.dogs",
    "api.collars",
    "api.telemetry_points",
    "api.config_resource_heads",
  ]) {
    if (!Number.isInteger(requiredCounts[relation]) || requiredCounts[relation] < 1) {
      throw new Error(
        `Source fixture ${relation} is empty. Run the clean Phase 1 local gate before the restore drill.`,
      );
    }
  }
  if ((sourceManifest.auth?.users ?? 0) < 2) {
    throw new Error("Source fixture needs at least the isolated owner and outsider Auth users.");
  }

  const sourceOwnerAccess = accessProbe(container, "postgres", OWNER_ID);
  const sourceOutsiderAccess = accessProbe(container, "postgres", OUTSIDER_ID);
  if (sourceOwnerAccess.dogs < 1 || sourceOwnerAccess.telemetry_points < 1) {
    throw new Error("Source owner RLS probe cannot see the seeded dog and telemetry.");
  }
  if (Object.values(sourceOutsiderAccess).some((count) => count !== 0)) {
    throw new Error("Source outsider RLS probe unexpectedly sees protected application rows.");
  }

  console.log("Creating a memory-only logical backup of the synthetic local database...");
  const dumpStartedAt = Date.now();
  backupBuffer = requireBinary("pg_dump", [
    "exec", container, "pg_dump", "-U", "supabase_admin", "-d", "postgres", "-Fc",
  ]);
  evidence.dump_bytes = backupBuffer.length;
  evidence.dump_duration_ms = Date.now() - dumpStartedAt;

  requireText("createdb", [
    "exec", container, "createdb", "-U", "supabase_admin", "-T", "template0", restoreDatabase,
  ]);
  databaseCreated = true;

  console.log("Restoring the backup into a separately named isolated database...");
  const restoreStartedAt = Date.now();
  const restore = invoke([
    "exec", "-i", container, "pg_restore", "-U", "supabase_admin",
    "-d", restoreDatabase, "--exit-on-error",
  ], { input: backupBuffer });
  backupBuffer.fill(0);
  backupBuffer = undefined;
  if (restore.status !== 0) {
    if (restore.stdout) process.stdout.write(restore.stdout);
    if (restore.stderr) process.stderr.write(restore.stderr);
    throw resultError("pg_restore", restore);
  }
  evidence.restore_duration_ms = Date.now() - restoreStartedAt;

  const restoredManifest = manifest(container, restoreDatabase, manifestSql);
  if (!isDeepStrictEqual(restoredManifest, sourceManifest)) {
    throw new Error("Restored database manifest differs from the source snapshot.");
  }

  const restoredOwnerAccess = accessProbe(container, restoreDatabase, OWNER_ID);
  const restoredOutsiderAccess = accessProbe(container, restoreDatabase, OUTSIDER_ID);
  if (!isDeepStrictEqual(restoredOwnerAccess, sourceOwnerAccess)) {
    throw new Error("Owner RLS behavior differs after restore.");
  }
  if (!isDeepStrictEqual(restoredOutsiderAccess, sourceOutsiderAccess)) {
    throw new Error("Outsider RLS behavior differs after restore.");
  }

  evidence.status = "passed";
  evidence.manifest = sourceManifest;
  evidence.rls_probes = {
    owner: sourceOwnerAccess,
    outsider: sourceOutsiderAccess,
  };
  console.log("Restore manifest, Auth linkage, functions, RLS, route hash, and config heads match.");
} catch (error) {
  primaryError = error;
  evidence.error = error instanceof Error ? error.message : String(error);
} finally {
  backupBuffer?.fill(0);
  if (databaseCreated && container) {
    const cleanup = invoke([
      "exec", container, "dropdb", "-U", "supabase_admin", "--if-exists", restoreDatabase,
    ]);
    if (cleanup.status !== 0) {
      if (cleanup.stdout) process.stdout.write(cleanup.stdout);
      if (cleanup.stderr) process.stderr.write(cleanup.stderr);
      if (!primaryError) primaryError = resultError("restore database cleanup", cleanup);
      evidence.status = "failed";
      evidence.error ??= "The isolated restore database could not be removed.";
    }
  }
  evidence.total_duration_ms = Date.now() - startedAt;
  mkdirSync(dirname(EVIDENCE_PATH), { recursive: true });
  writeFileSync(EVIDENCE_PATH, `${JSON.stringify(evidence, null, 2)}\n`, "utf8");
  console.log(`Coordinate-free restore evidence written to ${EVIDENCE_PATH}.`);
}

if (primaryError) throw primaryError;
console.log("Phase 1 isolated local restore drill passed and its temporary database was removed.");
