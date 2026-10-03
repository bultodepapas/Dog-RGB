import { execFileSync } from "node:child_process";

// Explicit local maintenance, no scheduler or hosted credentials. Point
// limits live in SQL; the outer statement timeout actually bounds execution.
const args = process.argv.slice(2);
const batches = args.length === 0 ? 64 : args.length === 2 && args[0] === "--max-batches" ? Number(args[1]) : NaN;
if (!Number.isSafeInteger(batches) || batches < 1 || batches > 1024) throw new Error("Usage: node tools/cloud_analytics/run.mjs [--max-batches 1..1024]");
const ids = execFileSync("docker", ["ps", "--filter", "label=com.supabase.cli.project=Dog-RGB-1", "--filter", "name=^/supabase_db_Dog-RGB-1$", "--format", "{{.ID}}"], { encoding: "utf8" }).trim().split(/\s+/u).filter(Boolean);
if (ids.length !== 1) throw new Error("Expected this repository's running local Supabase database.");

let remaining = null;
for (let batch = 1; batch <= batches; batch++) {
  const output = execFileSync("docker", ["exec", "-i", ids[0], "psql", "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-U", "postgres", "-d", "postgres"], {
    encoding: "utf8", timeout: 15_000, maxBuffer: 4096, input: `
begin;
set local statement_timeout = '10s';
set local role service_role;
select private.recompute_dirty_summaries_v1(4);
reset role;
select count(*) from private.dirty_summary_days;
commit;
`,
  }).trim().split(/\r?\n/u);
  remaining = Number(output.at(-1));
  if (!Number.isSafeInteger(remaining) || remaining < 0) throw new Error("Invalid worker status.");
  console.log(JSON.stringify({ batch, remainingDirtyDays: remaining }));
  if (remaining === 0) break;
}
if (remaining !== 0) {
  console.error("Bounded run ended with pending work; rerun to continue. No work was discarded.");
  process.exitCode = 2;
}
