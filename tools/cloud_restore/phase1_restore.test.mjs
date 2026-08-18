import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));

test("restore drill keeps its snapshot and exported tombstone memory-only", async () => {
  const runner = await readFile(resolve(HERE, "phase1_restore.mjs"), "utf8");

  assert.match(runner, /backup_persisted: false/u);
  assert.match(runner, /tombstone_payload_persisted: false/u);
  assert.match(runner, /backupBuffer\.fill\(0\)/u);
  assert.doesNotMatch(runner, /writeFileSync\([^)]*backupBuffer/u);
  assert.doesNotMatch(runner, /evidence\.(?:item|tombstone_export)\s*=/u);
});

test("restore drill uses two isolated databases and cleans both in finally", async () => {
  const runner = await readFile(resolve(HERE, "phase1_restore.mjs"), "utf8");

  assert.match(runner, /databaseName\("restore"\)/u);
  assert.match(runner, /databaseName\("delete_source"\)/u);
  assert.equal(
    (runner.match(/restoreBackup\(container, (?:restoreDatabase|deletionSourceDatabase)/gu) ?? []).length,
    2,
  );
  assert.match(runner, /finally \{[\s\S]*createdDatabases\.reverse\(\)/u);
  assert.match(runner, /dropdb[\s\S]*--if-exists/u);
});

test("restore activation gate rejects tampering and requires exact replay idempotency", async () => {
  const runner = await readFile(resolve(HERE, "phase1_restore.mjs"), "utf8");

  assert.match(runner, /generateKeyPairSync\("ed25519"\)/u);
  assert.match(runner, /expectedContextId: LOCAL_ARTIFACT_CONTEXT_ID/u);
  assert.match(runner, /signed_artifact_chain_complete: verifiedExport\.complete/u);
  assert.match(runner, /invalid_deletion_tombstone_hash/u);
  assert.match(runner, /replay\.disposition !== "replayed"/u);
  assert.match(runner, /exactReplay\.disposition !== "already_present"/u);
  assert.match(runner, /Object\.values\(ownerAfterReplay\).*count\) => count !== 0/su);
});
