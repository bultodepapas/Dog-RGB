const encoder = new TextEncoder();

export class HttpProblem extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
    readonly title: string,
    readonly detail: string,
    readonly retryAfterSeconds?: number,
  ) {
    super(detail);
  }
}

export function problem(error: unknown, requestId: string | null = null): Response {
  const value = error instanceof HttpProblem
    ? error
    : new HttpProblem(500, "internal_error", "Internal error", "The request could not be completed.");
  const headers: Record<string, string> = {
    "content-type": "application/problem+json",
    "cache-control": "no-store",
  };
  if (value.retryAfterSeconds !== undefined) {
    headers["retry-after"] = String(value.retryAfterSeconds);
  }
  return Response.json({
    type: `urn:dog-rgb:problem:${value.code}`,
    title: value.title,
    status: value.status,
    detail: value.detail,
    code: value.code,
    request_id: requestId,
  }, {
    status: value.status,
    headers,
  });
}

function maxDepth(value: unknown, depth = 0): number {
  if (value === null || typeof value !== "object") return depth;
  const values = Array.isArray(value) ? value : Object.values(value);
  return values.reduce((max, child) => Math.max(max, maxDepth(child, depth + 1)), depth);
}

export async function boundedJson(req: Request, maxBytes: number, requireLength = true) {
  if (req.method !== "POST") throw new HttpProblem(405, "method_not_allowed", "Method not allowed", "Use POST.");
  if (!req.headers.get("content-type")?.toLowerCase().startsWith("application/json")) {
    throw new HttpProblem(415, "unsupported_media_type", "Unsupported media type", "Content-Type must be application/json.");
  }
  const declared = req.headers.get("content-length");
  if (requireLength && declared === null) {
    throw new HttpProblem(411, "length_required", "Content length required", "Content-Length is required.");
  }
  if (declared !== null && (!/^\d+$/.test(declared) || Number(declared) > maxBytes)) {
    throw new HttpProblem(413, "payload_too_large", "Payload too large", "The request exceeds the endpoint limit.");
  }
  const raw = await req.text();
  if (encoder.encode(raw).byteLength > maxBytes) {
    throw new HttpProblem(413, "payload_too_large", "Payload too large", "The request exceeds the endpoint limit.");
  }
  let body: unknown;
  try { body = JSON.parse(raw); } catch {
    throw new HttpProblem(400, "malformed_json", "Malformed JSON", "The body must be valid UTF-8 JSON.");
  }
  if (maxDepth(body) > 12) throw new HttpProblem(400, "malformed_json", "Malformed JSON", "JSON nesting is too deep.");
  if (!body || Array.isArray(body) || typeof body !== "object") {
    throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "The body must be a JSON object.");
  }
  return { raw, body: body as Record<string, unknown> };
}

function bytesToHex(value: Uint8Array): string {
  return Array.from(value, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function postgresBytea(value: Uint8Array): string {
  return `\\x${bytesToHex(value)}`;
}

export async function sha256(value: string | Uint8Array): Promise<Uint8Array> {
  const bytes = typeof value === "string" ? encoder.encode(value) : value;
  return new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
}

export async function hmacSha256(secret: string, value: string | Uint8Array): Promise<Uint8Array> {
  const key = await crypto.subtle.importKey(
    "raw", encoder.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"],
  );
  const bytes = typeof value === "string" ? encoder.encode(value) : value;
  return new Uint8Array(await crypto.subtle.sign("HMAC", key, bytes));
}

export function base64UrlDecode(value: string): Uint8Array {
  if (!/^[A-Za-z0-9_-]+$/.test(value)) throw new HttpProblem(401, "device_credential_invalid", "Device credential invalid", "Device authentication failed.");
  const padded = value.replaceAll("-", "+").replaceAll("_", "/") + "=".repeat((4 - value.length % 4) % 4);
  try { return Uint8Array.from(atob(padded), (char) => char.charCodeAt(0)); } catch {
    throw new HttpProblem(401, "device_credential_invalid", "Device credential invalid", "Device authentication failed.");
  }
}

export function base64UrlEncode(value: Uint8Array): string {
  let binary = "";
  for (const byte of value) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

export function canonicalJson(value: unknown): string {
  if (value === null || typeof value === "boolean" || typeof value === "string") return JSON.stringify(value);
  if (typeof value === "number") {
    if (!Number.isFinite(value)) throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "JSON numbers must be finite.");
    return JSON.stringify(value);
  }
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  if (typeof value === "object") {
    const entries = Object.entries(value as Record<string, unknown>)
      .sort(([left], [right]) => left < right ? -1 : left > right ? 1 : 0)
      .map(([key, child]) => `${JSON.stringify(key)}:${canonicalJson(child)}`);
    return `{${entries.join(",")}}`;
  }
  throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "JSON contains an unsupported value.");
}

async function assertHash(
  expected: unknown,
  bytes: string | Uint8Array,
  code: "invalid_capabilities" | "invalid_telemetry" | "invalid_envelope",
) {
  if (typeof expected !== "string" || base64UrlEncode(await sha256(bytes)) !== expected) {
    const status = code === "invalid_envelope" ? 400 : 422;
    const title = code === "invalid_capabilities"
      ? "Invalid capability manifest"
      : code === "invalid_telemetry"
      ? "Invalid telemetry"
      : "Invalid request envelope";
    throw new HttpProblem(status, code, title, "A payload digest does not match its content.");
  }
}

export async function validateCapabilityHash(device: Record<string, unknown>, capabilities: unknown) {
  await assertHash(device.capability_hash, canonicalJson(capabilities), "invalid_capabilities");
}

export async function validateSyncSemantics(body: Record<string, unknown>) {
  const device = body.device as Record<string, unknown> | undefined;
  const upload = body.upload as Record<string, unknown> | undefined;
  const configuration = body.configuration as Record<string, unknown> | undefined;
  if (!device || !upload || !configuration || typeof device.device_id !== "string") {
    throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "Required sync sections are missing.");
  }
  const chunks = Array.isArray(upload.chunks) ? upload.chunks as Array<Record<string, unknown>> : [];
  let totalPoints = 0;
  const identities = new Set<string>();
  for (const chunk of chunks) {
    const points = Array.isArray(chunk.points) ? chunk.points : [];
    totalPoints += points.length;
    if (points.length < 1 || points.length > 96 || chunk.point_count !== points.length) {
      throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "A chunk point count is invalid.");
    }
    const identity = `${chunk.boot_sequence}:${chunk.chunk_sequence}`;
    if (identities.has(identity)) throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "A chunk identity occurs more than once.");
    identities.add(identity);
    const raw = new Uint8Array(points.length * 16);
    const view = new DataView(raw.buffer);
    points.forEach((candidate, index) => {
      if (!Array.isArray(candidate) || candidate.length !== 6 || !candidate.every(Number.isInteger)) {
        throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "A Track v3 point is malformed.");
      }
      const [lat, lon, utc, speed, satellites, flags] = candidate as number[];
      const validFix = (flags & 1) !== 0;
      const gap = (flags & 32) !== 0;
      if (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000 ||
          utc < 0 || utc > 4294967295 || speed < 0 || speed > 65535 || satellites < 0 || satellites > 255 ||
          flags < 0 || flags > 127 || ((utc !== 0) !== ((flags & 4) !== 0)) ||
          ((flags & 2) !== 0 && (flags & 8) !== 0) || (gap && (validFix || (flags & 10) !== 0)) ||
          (!validFix && (lat !== 0 || lon !== 0 || speed !== 65535))) {
        throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "A Track v3 point violates its flag or range invariants.");
      }
      const offset = index * 16;
      view.setInt32(offset, lat, true);
      view.setInt32(offset + 4, lon, true);
      view.setUint32(offset + 8, utc, true);
      view.setUint16(offset + 12, speed, true);
      view.setUint8(offset + 14, satellites);
      view.setUint8(offset + 15, flags);
    });
    await assertHash(chunk.content_sha256, raw, "invalid_telemetry");
  }
  if (chunks.length > 8 || totalPoints > 384) {
    throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "The sync batch exceeds the device-v1 limits.");
  }
  const mutations = Array.isArray(configuration.mutations) ? configuration.mutations as Array<Record<string, unknown>> : [];
  const mutationIds = new Set<unknown>();
  const sequences = new Set<unknown>();
  for (const mutation of mutations) {
    if (mutationIds.has(mutation.mutation_id) || sequences.has(mutation.local_sequence) ||
        (mutation.authored_hlc as Record<string, unknown> | undefined)?.actor_id !== device.device_id) {
      throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "Mutation identity, sequence, or actor is invalid.");
    }
    mutationIds.add(mutation.mutation_id);
    sequences.add(mutation.local_sequence);
    await assertHash(mutation.body_sha256, canonicalJson(mutation.body), "invalid_envelope");
  }
}

export function parseDeviceBearer(req: Request): { credentialId: string; secret: Uint8Array } {
  const match = req.headers.get("authorization")?.match(
    /^Bearer drgb_v1_([0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\.([A-Za-z0-9_-]{43})$/i,
  );
  if (!match) throw new HttpProblem(401, "device_credential_invalid", "Device credential invalid", "Device authentication failed.");
  const secret = base64UrlDecode(match[2]);
  if (secret.byteLength !== 32) throw new HttpProblem(401, "device_credential_invalid", "Device credential invalid", "Device authentication failed.");
  return { credentialId: match[1].toLowerCase(), secret };
}

export function requiredEnv(name: string): string {
  const value = Deno.env.get(name);
  if (!value) throw new HttpProblem(503, "server_busy", "Service temporarily unavailable", "The gateway is not configured.", 30);
  return value;
}

type AdminRpcClient = {
  schema(name: string): {
    rpc(name: string, args: Record<string, unknown>): PromiseLike<{
      data: unknown;
      error: { message?: string; code?: string } | null;
    }>;
  };
};

export async function serviceRpc(
  client: AdminRpcClient,
  name: string,
  args: Record<string, unknown>,
): Promise<unknown> {
  const { data, error } = await client.schema("api").rpc(name, args);
  if (!error) return data;

  const message = error.message ?? "database_rejected_request";
  if (["request_id_conflict", "mutation_id_conflict", "chunk_identity_conflict"].includes(message)) {
    throw new HttpProblem(409, "request_id_reused", "Request identifier reused", "The request identity was already used with different content.");
  }
  if (["device_already_linked", "device_identity_mismatch"].includes(message)) {
    throw new HttpProblem(409, "device_identity_conflict", "Device identity conflict", "The supplied device identity conflicts with an existing link.");
  }
  if (message === "active_claim_exists") {
    throw new HttpProblem(409, "active_claim_exists", "Active claim already exists", "Use or expire the active claim before requesting another.");
  }
  if (message === "claim_not_available") {
    throw new HttpProblem(401, "claim_unavailable", "Claim unavailable", "The supplied claim is unavailable or expired.");
  }
  if (message === "invalid_device_credential") {
    throw new HttpProblem(401, "device_credential_invalid", "Device credential invalid", "Device authentication failed.");
  }
  if (message === "device_credential_expired") {
    throw new HttpProblem(401, "device_credential_expired", "Device credential expired", "The device credential has expired.");
  }
  if (message === "device_revoked") {
    throw new HttpProblem(403, "device_revoked", "Device revoked", "This collar has been revoked and can no longer synchronize.");
  }
  if (message === "rate_limited_claim_issue") {
    throw new HttpProblem(429, "rate_limited", "Too many requests", "The claim issue limit was reached.", 3600);
  }
  if (message === "rate_limited_sync_burst") {
    throw new HttpProblem(429, "rate_limited", "Too many requests", "The collar sync limit was reached.", 60);
  }
  if (message === "rate_limited_sync_sustained") {
    throw new HttpProblem(429, "rate_limited", "Too many requests", "The collar sustained sync limit was reached.", 3600);
  }
  if (message === "quota_exceeded") {
    throw new HttpProblem(429, "quota_exceeded", "Device quota exceeded", "The collar telemetry quota was reached.", 3600);
  }
  if (message === "not_authorized") {
    throw new HttpProblem(403, "dog_access_denied", "Dog access denied", "The authenticated user cannot modify this dog.");
  }
  if (["request_in_progress", "stale_base_server_version"].includes(message) || error.code === "40001") {
    throw new HttpProblem(503, "transaction_conflict", "Transaction temporarily conflicted", "Retry the identical request with backoff.");
  }
  if (message.includes("capabilit")) {
    throw new HttpProblem(422, "invalid_capabilities", "Invalid capability manifest", "The capability manifest failed validation.");
  }
  if (message.includes("telemetry") || message.includes("chunk") || message.includes("point")) {
    throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "The telemetry payload failed validation.");
  }
  throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "The request failed validation.");
}

const UUID_V4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

export function requestIdFrom(body: Record<string, unknown>): string | null {
  return typeof body.request_id === "string" && UUID_V4.test(body.request_id) ? body.request_id : null;
}

export function assertProtocolRequest(body: Record<string, unknown>): { requestId: string } {
  if (body.protocol_version !== 1 || typeof body.request_id !== "string" || !UUID_V4.test(body.request_id)) {
    throw new HttpProblem(422, "unsupported_protocol", "Unsupported protocol version", "Protocol version 1 and a UUIDv4 request_id are required.");
  }
  return { requestId: body.request_id };
}

export async function deviceDigest(secret: Uint8Array): Promise<Uint8Array> {
  return hmacSha256(requiredEnv("DEVICE_CREDENTIAL_PEPPER"), secret);
}
