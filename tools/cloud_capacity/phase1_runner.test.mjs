import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));

test("capacity SQL fails through ON_ERROR_STOP-compatible exceptions", async () => {
  const sql = await readFile(resolve(HERE, "phase1_benchmark.sql"), "utf8");

  assert.doesNotMatch(sql, /\\quit(?:\s|$)/u);
  assert.match(sql, /phase1_capacity_bytes_per_point_exceeded/u);
  assert.match(sql, /phase1_capacity_cross_user_isolation_failed/u);
  assert.equal((sql.match(/raise exception/gu) ?? []).length, 2);
});

test("capacity runner propagates SQL failure and always restores the database", async () => {
  const runner = await readFile(resolve(HERE, "phase1_run.mjs"), "utf8");

  assert.match(runner, /if \(benchmark\.status !== 0\)/u);
  assert.match(runner, /benchmarkError = error/u);
  assert.match(runner, /finally \{/u);
  assert.match(runner, /invoke\("supabase", \["db", "reset"\]\)/u);
  assert.match(runner, /if \(benchmarkError\) throw benchmarkError/u);
});
