import { spawnSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const versionPath = join(root, ".supabase-version");
const outputPath = join(root, "apps", "portal", "lib", "database.generated.ts");
const write = process.argv.includes("--write");

function executable(command) {
  if (process.platform !== "win32") return command;
  return command === "supabase" ? "supabase.exe" : command;
}

function runSupabase(args) {
  const result = spawnSync(executable("supabase"), args, {
    cwd: root,
    encoding: "utf8",
    maxBuffer: 4 * 1024 * 1024,
  });
  if (result.status !== 0) {
    const detail = result.stderr.trim();
    throw new Error(
      `supabase ${args.join(" ")} failed with status ${result.status ?? result.error?.code ?? "unknown"}` +
      (detail ? `:\n${detail}` : ""),
    );
  }
  return result.stdout;
}

function normalize(text) {
  return text.replace(/^\uFEFF/, "").replace(/\r\n/g, "\n").replace(/\n*$/, "\n");
}

const expectedCliVersion = (await readFile(versionPath, "utf8")).trim();
const actualCliVersion = runSupabase(["--version"]).trim();
if (actualCliVersion !== expectedCliVersion) {
  throw new Error(
    `Supabase CLI ${expectedCliVersion} is required to generate stable database types; found ${actualCliVersion}.`,
  );
}

const generated = normalize(runSupabase([
  "gen",
  "types",
  "typescript",
  "--local",
  "--schema",
  "api",
]));

if (!generated.includes("export type Json") || !/\n\s*api:\s*\{/.test(generated)) {
  throw new Error("Supabase generated an unexpected TypeScript artifact for the api schema.");
}

const expected = normalize([
  "// Generated from local Supabase migrations. Do not edit by hand.",
  "// Regenerate with: npm run cloud:types:generate",
  "",
  generated,
].join("\n"));

if (write) {
  await mkdir(dirname(outputPath), { recursive: true });
  await writeFile(outputPath, expected, "utf8");
  console.log("Generated apps/portal/lib/database.generated.ts from the local api schema.");
} else {
  const actual = await readFile(outputPath, "utf8").catch((error) => {
    if (error?.code === "ENOENT") return null;
    throw error;
  });
  if (actual === null || normalize(actual) !== expected) {
    throw new Error(
      "Generated Supabase database types are stale. Start the migrated local stack and run npm run cloud:types:generate.",
    );
  }
  console.log("Verified generated Supabase database types for the api schema.");
}
