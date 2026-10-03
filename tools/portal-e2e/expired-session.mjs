import { createPrivateKey, createPublicKey, sign, verify } from "node:crypto";
import { execFileSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repositoryRoot = resolve(fileURLToPath(new URL("../..", import.meta.url)));
// Supabase project_id is fixed; checkout directory names differ on CI.
const projectName = "Dog-RGB-1";
const authContainerName = `supabase_auth_${projectName}`;
const userIdPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const privateJwkFields = ["d", "p", "q", "dp", "dq", "qi", "k", "oth"];

function fixtureError(message) {
  throw new Error(`Expired-session fixture ${message}.`);
}

function isLoopback(hostname) {
  return hostname === "localhost" || hostname === "127.0.0.1" || hostname === "[::1]" || hostname === "::1";
}

function localApiUrl(value) {
  let url;
  try {
    url = new URL(value);
  } catch {
    fixtureError("requires a loopback local Supabase API URL");
  }
  if (url.protocol !== "http:" || !isLoopback(url.hostname) || url.username || url.password ||
      (url.pathname !== "" && url.pathname !== "/") || url.search || url.hash) {
    fixtureError("requires a loopback local Supabase API URL");
  }
  return url;
}

function normalizedPath(value) {
  return resolve(value).replaceAll("\\", "/").replace(/\/+$/u, "").toLowerCase();
}

function assertOwnedAuthContainer(containerName, labels) {
  const expectedName = `/${authContainerName}`;
  if (containerName !== expectedName || labels?.["com.docker.compose.project"] !== projectName ||
      labels?.["com.supabase.cli.project"] !== projectName ||
      typeof labels?.["com.supabase.cli.workdir"] !== "string" ||
      normalizedPath(labels["com.supabase.cli.workdir"]) !== normalizedPath(repositoryRoot)) {
    fixtureError("refused a Supabase Auth container outside this repository project");
  }
}

function dockerInspect(format, name = authContainerName) {
  try {
    return execFileSync("docker", ["inspect", "--format", format, name], {
      encoding: null,
      maxBuffer: 64 * 1024,
      timeout: 5_000,
      windowsHide: true,
    });
  } catch {
    fixtureError("could not inspect the local Supabase Auth container");
  }
}

function localAuthIssuer() {
  const metadataFormat = '{{json .Name}}{{"\\n"}}{{json .Config.Labels}}{{"\\n"}}{{range .Config.Env}}{{if eq (index (split . "=") 0) "GOTRUE_JWT_ISSUER"}}{{.}}{{end}}{{end}}';
  const output = dockerInspect(metadataFormat);
  let issuerEntry;
  try {
    const lines = output.toString("utf8").trimEnd().split(/\r?\n/u);
    const containerName = JSON.parse(lines[0]);
    const labels = JSON.parse(lines[1]);
    issuerEntry = lines[2];
    assertOwnedAuthContainer(containerName, labels);
  } finally {
    output.fill(0);
  }
  const prefix = "GOTRUE_JWT_ISSUER=";
  if (typeof issuerEntry !== "string" || !issuerEntry.startsWith(prefix)) {
    fixtureError("could not verify the local Auth issuer");
  }
  return issuerEntry.slice(prefix.length);
}

function signingJwkCandidates() {
  const keyFormat = '{{range .Config.Env}}{{if eq (index (split . "=") 0) "GOTRUE_JWT_KEYS"}}{{.}}{{end}}{{end}}';
  const output = dockerInspect(keyFormat);
  try {
    const line = output.toString("utf8").trim();
    const prefix = "GOTRUE_JWT_KEYS=";
    if (!line.startsWith(prefix)) fixtureError("could not read the local asymmetric signing key");
    const parsed = JSON.parse(line.slice(prefix.length));
    if (Array.isArray(parsed)) return parsed;
    if (Array.isArray(parsed?.keys)) return parsed.keys;
    if (parsed && typeof parsed === "object" && typeof parsed.kty === "string") return [parsed];
    fixtureError("found an unsupported local signing-key configuration");
  } catch (error) {
    if (error instanceof Error && error.message.startsWith("Expired-session fixture")) throw error;
    fixtureError("could not parse the local asymmetric signing key");
  } finally {
    output.fill(0);
  }
}

function clearPrivateJwk(jwk) {
  if (!jwk || typeof jwk !== "object") return;
  for (const field of privateJwkFields) delete jwk[field];
}

function parseJwt(token) {
  if (typeof token !== "string") fixtureError("requires an authenticated access token");
  const parts = token.split(".");
  if (parts.length !== 3 || parts.some(part => !/^[A-Za-z0-9_-]+$/u.test(part))) {
    fixtureError("received an invalid authenticated access token");
  }
  try {
    const header = JSON.parse(Buffer.from(parts[0], "base64url").toString("utf8"));
    const claims = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
    const signature = Buffer.from(parts[2], "base64url");
    if (Buffer.from(parts[0], "base64url").toString("base64url") !== parts[0] ||
        Buffer.from(parts[1], "base64url").toString("base64url") !== parts[1] ||
        signature.toString("base64url") !== parts[2] ||
        !header || typeof header !== "object" || Array.isArray(header) ||
        !claims || typeof claims !== "object" || Array.isArray(claims)) {
      fixtureError("received an invalid authenticated access token");
    }
    return { header, claims, signature, signingInput: Buffer.from(`${parts[0]}.${parts[1]}`, "ascii") };
  } catch (error) {
    if (error instanceof Error && error.message.startsWith("Expired-session fixture")) throw error;
    fixtureError("received an invalid authenticated access token");
  }
}

function supportedJwk(jwk) {
  if (!jwk || typeof jwk !== "object" || typeof jwk.kid !== "string" || !jwk.kid ||
      (jwk.use !== undefined && jwk.use !== "sig")) return false;
  if (jwk.alg === "ES256") return jwk.kty === "EC" && jwk.crv === "P-256" && typeof jwk.d === "string";
  if (jwk.alg === "RS256") return jwk.kty === "RSA" && typeof jwk.d === "string" &&
    typeof jwk.p === "string" && typeof jwk.q === "string";
  return false;
}

function signingKey(jwk) {
  if (!supportedJwk(jwk)) fixtureError("requires a local ES256 or RS256 signing key");
  try {
    return createPrivateKey({ key: jwk, format: "jwk" });
  } catch {
    fixtureError("could not import the local asymmetric signing key");
  }
}

function verifyWithKey(algorithm, data, key, signature) {
  if (algorithm === "ES256") {
    return verify("sha256", data, { key, dsaEncoding: "ieee-p1363" }, signature);
  }
  if (algorithm === "RS256") return verify("RSA-SHA256", data, key, signature);
  return false;
}

function signWithKey(algorithm, data, key) {
  if (algorithm === "ES256") return sign("sha256", data, { key, dsaEncoding: "ieee-p1363" });
  if (algorithm === "RS256") return sign("RSA-SHA256", data, key);
  fixtureError("requires a local ES256 or RS256 signing key");
}

function encodeSegment(value) {
  return Buffer.from(JSON.stringify(value), "utf8").toString("base64url");
}

function signJwt(claims, jwk, key) {
  const header = { alg: jwk.alg, kid: jwk.kid, typ: "JWT" };
  const input = `${encodeSegment(header)}.${encodeSegment(claims)}`;
  const signature = signWithKey(jwk.alg, Buffer.from(input, "ascii"), key);
  return `${input}.${signature.toString("base64url")}`;
}

function createSessionPair(accessToken, issuer, jwk, nowSeconds) {
  const source = parseJwt(accessToken);
  if (source.header.alg !== jwk.alg || source.header.kid !== jwk.kid) {
    fixtureError("access token was not signed by this local Auth signing key");
  }

  const privateKey = signingKey(jwk);
  const publicKey = createPublicKey(privateKey);
  if (!verifyWithKey(jwk.alg, source.signingInput, publicKey, source.signature)) {
    fixtureError("access token signature does not match the local Auth signing key");
  }

  const claims = source.claims;
  if (claims.iss !== issuer || claims.role !== "authenticated" || !userIdPattern.test(claims.sub ?? "") ||
      !Number.isSafeInteger(claims.iat) || claims.iat > nowSeconds + 60 ||
      !Number.isSafeInteger(claims.exp) || claims.exp <= nowSeconds ||
      (claims.aud !== "authenticated" && !(Array.isArray(claims.aud) && claims.aud.includes("authenticated")))) {
    fixtureError("access token is not a current authenticated session for this local project");
  }

  const controlClaims = { ...claims, iat: nowSeconds - 120, exp: nowSeconds + 180 };
  const expiredClaims = { ...claims, iat: nowSeconds - 120, exp: nowSeconds - 60 };
  return Object.freeze({
    validAccessToken: signJwt(controlClaims, jwk, privateKey),
    expiredAccessToken: signJwt(expiredClaims, jwk, privateKey),
  });
}

export function createLocalExpiredSessionPair({ apiUrl, accessToken } = {}) {
  const url = localApiUrl(apiUrl);
  const issuer = localAuthIssuer();
  const expectedIssuer = `${url.origin}/auth/v1`;
  if (issuer !== expectedIssuer) fixtureError("refused an Auth container for a different local project URL");

  const candidates = signingJwkCandidates();
  try {
    const header = parseJwt(accessToken).header;
    const jwk = candidates.find(candidate => candidate?.kid === header.kid && candidate?.alg === header.alg);
    if (!jwk) fixtureError("access token key does not match this local Auth project");
    return createSessionPair(accessToken, issuer, jwk, Math.floor(Date.now() / 1000));
  } finally {
    for (const candidate of candidates) clearPrivateJwk(candidate);
  }
}
