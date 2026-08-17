import { execFileSync } from "node:child_process";

const files = execFileSync("git", ["ls-files", "--cached", "--others", "--exclude-standard"], { encoding: "utf8" })
  .trim().split(/\r?\n/).filter(Boolean);
const forbidden = [
  /sb_secret_[A-Za-z0-9_-]{16,}/,
  /drgb_v1_[0-9a-f-]{36}\.[A-Za-z0-9_-]{43}/i,
  /-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/,
];
const sensitiveAssignment = /^(SUPABASE_(?:SERVICE_ROLE_KEY|SECRET_KEY|SECRET_KEYS)|CLAIM_HMAC_PEPPER|DEVICE_CREDENTIAL_PEPPER)\s*=\s*(\S+)\s*$/gm;
const isPlaceholder = (value) => /^(?:replace-me|example|changeme|local-only)(?:-|$)/i.test(value);
const offenders = [];
for (const file of files) {
  if (/\.(?:png|jpg|jpeg|gif|webp|woff2?|zip|gz|bin)$/i.test(file)) continue;
  let text;
  try { text = await import("node:fs/promises").then(({ readFile }) => readFile(file, "utf8")); } catch { continue; }
  sensitiveAssignment.lastIndex = 0;
  const hasSensitiveAssignment = [...text.matchAll(sensitiveAssignment)]
    .some((match) => !isPlaceholder(match[2]));
  if (hasSensitiveAssignment || forbidden.some((pattern) => pattern.test(text))) offenders.push(file);
}
if (offenders.length) {
  console.error(`Potential secrets in tracked or untracked candidate files: ${offenders.join(", ")}`);
  process.exitCode = 1;
} else {
  console.log("No secret patterns found in tracked or untracked candidate files.");
}
