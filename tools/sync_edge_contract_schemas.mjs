import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const sourceDir = join(root, "contracts", "device-v1", "schemas");
const targetDir = join(root, "supabase", "functions", "_shared", "contracts");
const schemas = [
  "common.schema.json",
  "capabilities.schema.json",
  "config-resource.schema.json",
  "telemetry.schema.json",
  "user-v1-issue-claim-request.schema.json",
  "device-v1-claim-request.schema.json",
  "device-v1-sync-request.schema.json",
  "device-v1-revoke-request.schema.json",
];
const write = process.argv.includes("--write");
const stale = [];

if (write) await mkdir(targetDir, { recursive: true });
for (const name of schemas) {
  const expected = await readFile(join(sourceDir, name), "utf8");
  if (write) {
    await writeFile(join(targetDir, name), expected, "utf8");
    continue;
  }
  const actual = await readFile(join(targetDir, name), "utf8").catch(() => null);
  if (actual !== expected) stale.push(name);
}

if (stale.length > 0) {
  throw new Error(`Generated Edge contract schemas are stale: ${stale.join(", ")}. Run npm run contracts:edge:sync.`);
}
console.log(write ? `Synchronized ${schemas.length} Edge contract schemas.` : `Verified ${schemas.length} Edge contract schemas.`);
