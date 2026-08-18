import { spawnSync } from "node:child_process";
import { createHash, randomBytes, randomUUID } from "node:crypto";
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
const DOG_ID = "30000000-0000-4000-8000-000000000003";
const DELETION_BATCH_SIZE = 2;
const MAX_DELETION_BATCHES = 64;

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

function databaseName(purpose) {
  if (!/^(restore|delete_source)$/.test(purpose)) {
    throw new Error("Unknown isolated database purpose.");
  }
  const value = `dog_rgb_${purpose}_${process.pid}_${randomBytes(4).toString("hex")}`;
  if (!/^dog_rgb_(restore|delete_source)_[0-9]+_[0-9a-f]{8}$/.test(value) || value.length > 63) {
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

function jsonSql(container, database, sql, label) {
  const output = psql(container, database, sql);
  try {
    return JSON.parse(output);
  } catch {
    throw new Error(`${label} for ${database} was not one valid JSON value.`);
  }
}

function sqlLiteral(value) {
  return `'${String(value).replaceAll("'", "''")}'`;
}

function restoreBackup(container, database, backup) {
  const restore = invoke([
    "exec", "-i", container, "pg_restore", "-U", "supabase_admin",
    "-d", database, "--exit-on-error",
  ], { input: backup });
  if (restore.status !== 0) {
    if (restore.stdout) process.stdout.write(restore.stdout);
    if (restore.stderr) process.stderr.write(restore.stderr);
    throw resultError(`pg_restore ${database}`, restore);
  }
}

function requestDeletion(container, database, requestId) {
  return jsonSql(container, database, `
    begin;
    set local role authenticated;
    set local "request.jwt.claim.sub" = '${OWNER_ID}';
    select api.request_dog_deletion_v1(
      '${DOG_ID}', '${requestId}', 'dog-delete-v1'
    )::text;
    commit;
  `, "deletion request");
}

function processDeletion(container, database) {
  for (let batch = 1; batch <= MAX_DELETION_BATCHES; batch += 1) {
    const result = jsonSql(container, database, `
      begin;
      set local role service_role;
      select private.process_dog_deletion_batch_v1(${DELETION_BATCH_SIZE})::text;
      commit;
    `, "deletion worker result");
    if (result.status === "completed") return { result, batches: batch };
    if (result.disposition === "idle") {
      throw new Error("Deletion worker became idle before the replay job completed.");
    }
    if (result.status === "failed") {
      throw new Error(`Deletion worker failed closed with ${result.last_error_code ?? "unknown"}.`);
    }
  }
  throw new Error(`Deletion worker exceeded ${MAX_DELETION_BATCHES} bounded batches.`);
}

function exportTombstone(container, database, requestId) {
  const page = jsonSql(container, database, `
    begin;
    set local role service_role;
    select private.export_deletion_tombstones_v1(null, null, 1000)::text;
    commit;
  `, "tombstone export");
  if (page.schema_version !== "dog-deletion-tombstone-export-v1" || !Array.isArray(page.items)) {
    throw new Error("Deletion tombstone export returned an invalid envelope.");
  }
  const matches = page.items.filter((item) => item.request_id === requestId);
  if (matches.length !== 1) {
    throw new Error(`Expected one exported tombstone for the drill; found ${matches.length}.`);
  }
  return { page, item: matches[0] };
}

function replayTombstone(container, database, item) {
  return jsonSql(container, database, `
    begin;
    set local role service_role;
    select private.replay_dog_deletion_tombstone_v1(
      ${sqlLiteral(JSON.stringify(item))}::jsonb
    )::text;
    commit;
  `, "tombstone replay");
}

function rejectTamperedTombstone(container, database, item) {
  const tampered = { ...item, scope_id: "30000000-0000-4000-8000-000000000099" };
  const result = invoke([
    "exec", "-i", container, "psql", "-X", "-q", "-A", "-t",
    "-v", "ON_ERROR_STOP=1", "-U", "supabase_admin", "-d", database,
  ], {
    input: `
      begin;
      set local role service_role;
      select private.replay_dog_deletion_tombstone_v1(
        ${sqlLiteral(JSON.stringify(tampered))}::jsonb
      );
      rollback;
    `,
  });
  if (result.status === 0 || !result.stderr?.includes("invalid_deletion_tombstone_hash")) {
    throw new Error("A modified tombstone did not fail closed with its integrity error.");
  }
}

function deletionState(container, database, requestId) {
  return jsonSql(container, database, `
    select jsonb_build_object(
      'scope_rows', private.dog_deletion_counts_v1('${DOG_ID}'),
      'tombstones', (
        select count(*) from private.deletion_tombstones
        where request_id = '${requestId}'
      ),
      'completed_jobs', (
        select count(*)
        from private.deletion_jobs job
        join private.deletion_tombstones tombstone on tombstone.id = job.tombstone_id
        where tombstone.request_id = '${requestId}' and job.status = 'completed'
      ),
      'receipts', (
        select count(*)
        from private.deletion_receipts receipt
        join private.deletion_tombstones tombstone on tombstone.id = receipt.tombstone_id
        where tombstone.request_id = '${requestId}'
      )
    )::text;
  `, "deletion state");
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
const restoreDatabase = databaseName("restore");
const deletionSourceDatabase = databaseName("delete_source");
const evidence = {
  status: "failed",
  isolation: "two_separate_databases_in_disposable_local_supabase_cluster",
  backup_format: "pg_dump_custom_full_database",
  backup_persisted: false,
  tombstone_payload_persisted: false,
  coordinate_payload_persisted: false,
};
let container;
const createdDatabases = [];
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
  createdDatabases.push(restoreDatabase);
  requireText("createdb", [
    "exec", container, "createdb", "-U", "supabase_admin", "-T", "template0",
    deletionSourceDatabase,
  ]);
  createdDatabases.push(deletionSourceDatabase);

  console.log("Restoring one snapshot into two separately named isolated databases...");
  const restoreStartedAt = Date.now();
  restoreBackup(container, restoreDatabase, backupBuffer);
  restoreBackup(container, deletionSourceDatabase, backupBuffer);
  backupBuffer.fill(0);
  backupBuffer = undefined;
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

  const requestId = randomUUID();
  console.log("Creating and exporting a deletion newer than the shared restore point...");
  const sourceRequest = requestDeletion(container, deletionSourceDatabase, requestId);
  if (sourceRequest.status !== "pending") {
    throw new Error("Deletion source did not create one pending replayable job.");
  }
  const sourceDeletion = processDeletion(container, deletionSourceDatabase);
  const tombstoneExport = exportTombstone(container, deletionSourceDatabase, requestId);
  evidence.tombstone_export_sha256 = createHash("sha256")
    .update(JSON.stringify(tombstoneExport.page), "utf8")
    .digest("hex");
  evidence.tombstone_export_items = tombstoneExport.page.items.length;
  evidence.tombstone_export_has_more = tombstoneExport.page.has_more;

  console.log("Rejecting a modified tombstone, then replaying the exact export into the old restore...");
  rejectTamperedTombstone(container, restoreDatabase, tombstoneExport.item);
  const replay = replayTombstone(container, restoreDatabase, tombstoneExport.item);
  if (replay.disposition !== "replayed" || replay.status !== "pending") {
    throw new Error("Valid restore replay did not create one pending deletion job.");
  }
  const restoredDeletion = processDeletion(container, restoreDatabase);
  const exactReplay = replayTombstone(container, restoreDatabase, tombstoneExport.item);
  if (exactReplay.disposition !== "already_present" || exactReplay.status !== "completed") {
    throw new Error("Exact tombstone replay was not idempotent after completion.");
  }

  const sourceDeletionState = deletionState(container, deletionSourceDatabase, requestId);
  const restoredDeletionState = deletionState(container, restoreDatabase, requestId);
  if (!isDeepStrictEqual(restoredDeletionState, sourceDeletionState)) {
    throw new Error("Replayed restore deletion state differs from the source deletion outcome.");
  }
  if (Object.values(restoredDeletionState.scope_rows).some((count) => count !== 0)
      || restoredDeletionState.tombstones !== 1
      || restoredDeletionState.completed_jobs !== 1
      || restoredDeletionState.receipts !== 1) {
    throw new Error("Replayed restore retained dog-scoped data or lacks durable audit rows.");
  }

  const sourceAfterDeletion = manifest(container, deletionSourceDatabase, manifestSql);
  const restoredAfterDeletion = manifest(container, restoreDatabase, manifestSql);
  const normalizeDeletionIds = (value) => {
    const normalized = structuredClone(value);
    for (const relation of [
      "private.deletion_tombstones",
      "private.deletion_jobs",
      "private.deletion_receipts",
    ]) {
      delete normalized.table_hashes[relation];
    }
    return normalized;
  };
  if (!isDeepStrictEqual(
    normalizeDeletionIds(restoredAfterDeletion),
    normalizeDeletionIds(sourceAfterDeletion),
  )) {
    throw new Error("Non-audit restore state diverged after tombstone replay.");
  }

  const ownerAfterReplay = accessProbe(container, restoreDatabase, OWNER_ID);
  if (Object.values(ownerAfterReplay).some((count) => count !== 0)) {
    throw new Error("The original owner can still read the replay-deleted scope.");
  }

  evidence.status = "passed";
  evidence.manifest = sourceManifest;
  evidence.rls_probes = {
    owner: sourceOwnerAccess,
    outsider: sourceOutsiderAccess,
    owner_after_tombstone_replay: ownerAfterReplay,
  };
  evidence.deletion_replay = {
    source_batches: sourceDeletion.batches,
    restored_batches: restoredDeletion.batches,
    tombstones: restoredDeletionState.tombstones,
    completed_jobs: restoredDeletionState.completed_jobs,
    receipts: restoredDeletionState.receipts,
    tamper_rejected: true,
    exact_replay_idempotent: true,
  };
  console.log(
    "Restore equivalence and post-restore tombstone replay passed with user/device access closed.",
  );
} catch (error) {
  primaryError = error;
  evidence.error = error instanceof Error ? error.message : String(error);
} finally {
  backupBuffer?.fill(0);
  if (container) {
    for (const database of createdDatabases.reverse()) {
      const cleanup = invoke([
        "exec", container, "dropdb", "-U", "supabase_admin", "--if-exists", database,
      ]);
      if (cleanup.status !== 0) {
        if (cleanup.stdout) process.stdout.write(cleanup.stdout);
        if (cleanup.stderr) process.stderr.write(cleanup.stderr);
        if (!primaryError) primaryError = resultError(`${database} cleanup`, cleanup);
        evidence.status = "failed";
        evidence.error ??= "An isolated restore database could not be removed.";
      }
    }
  }
  evidence.total_duration_ms = Date.now() - startedAt;
  mkdirSync(dirname(EVIDENCE_PATH), { recursive: true });
  writeFileSync(EVIDENCE_PATH, `${JSON.stringify(evidence, null, 2)}\n`, "utf8");
  console.log(`Coordinate-free restore evidence written to ${EVIDENCE_PATH}.`);
}

if (primaryError) throw primaryError;
console.log("Phase 1 restore/tombstone drill passed and both temporary databases were removed.");
