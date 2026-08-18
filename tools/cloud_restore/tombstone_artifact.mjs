import {
  createHash,
  sign as cryptoSign,
  timingSafeEqual,
  verify as cryptoVerify,
} from "node:crypto";

const ARTIFACT_SCHEMA = "dog-deletion-tombstone-signed-batch-v1";
const PAGE_SCHEMA = "dog-deletion-tombstone-export-v1";
const ITEM_SCHEMA = "dog-deletion-tombstone-v1";
const SIGNATURE_ALGORITHM = "Ed25519";
const KEY_ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/u;
const CONTEXT_ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$/u;
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const UTC_MICROSECOND_PATTERN = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z$/u;
const UTC_MILLISECOND_PATTERN = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/u;
const SHA256_PATTERN = /^[0-9a-f]{64}$/u;
const BASE64URL_PATTERN = /^[A-Za-z0-9_-]+$/u;

const ARTIFACT_KEYS = Object.freeze([
  "payload",
  "payload_sha256",
  "signature",
  "signature_algorithm",
]);
const PAYLOAD_KEYS = Object.freeze([
  "after_cursor",
  "batch_sequence",
  "context_id",
  "created_at",
  "key_id",
  "page",
  "previous_payload_sha256",
  "schema_version",
]);
const PAGE_KEYS = Object.freeze(["has_more", "items", "next_cursor", "schema_version"]);
const CURSOR_KEYS = Object.freeze(["request_id", "requested_at"]);
const ITEM_KEYS = Object.freeze([
  "confirmation_version",
  "replay_sha256",
  "request_id",
  "request_sha256",
  "requested_at",
  "requested_by_sha256",
  "schema_version",
  "scope",
  "scope_id",
  "tombstone_sha256",
]);

function fail(message) {
  throw new Error(`invalid_tombstone_artifact: ${message}`);
}

function isPlainObject(value) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) return false;
  const prototype = Object.getPrototypeOf(value);
  return prototype === Object.prototype || prototype === null;
}

function requireExactKeys(value, expected, label) {
  if (!isPlainObject(value)) fail(`${label} must be one object`);
  const actual = Object.keys(value).sort();
  if (actual.length !== expected.length
      || actual.some((key, index) => key !== expected[index])) {
    fail(`${label} has an unexpected field set`);
  }
}

function renderCanonical(value, ancestors) {
  if (value === null) return "null";
  if (typeof value === "string" || typeof value === "boolean") {
    return JSON.stringify(value);
  }
  if (typeof value === "number") {
    if (!Number.isFinite(value)) fail("non-finite numbers are forbidden");
    return JSON.stringify(value);
  }
  if (typeof value !== "object") fail(`unsupported JSON type ${typeof value}`);
  if (ancestors.has(value)) fail("cyclic values are forbidden");
  ancestors.add(value);
  try {
    if (Array.isArray(value)) {
      return `[${value.map((item) => renderCanonical(item, ancestors)).join(",")}]`;
    }
    if (!isPlainObject(value)) fail("only plain JSON objects are supported");
    if (Object.getOwnPropertySymbols(value).length > 0) {
      fail("symbol properties are forbidden");
    }
    return `{${Object.keys(value).sort().map((key) => (
      `${JSON.stringify(key)}:${renderCanonical(value[key], ancestors)}`
    )).join(",")}}`;
  } finally {
    ancestors.delete(value);
  }
}

export function canonicalJson(value) {
  return renderCanonical(value, new Set());
}

function sha256Hex(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function decodeCanonicalBase64url(value, label) {
  if (typeof value !== "string" || !BASE64URL_PATTERN.test(value)) {
    fail(`${label} is not canonical base64url`);
  }
  const decoded = Buffer.from(value, "base64url");
  if (decoded.length === 0 || decoded.toString("base64url") !== value) {
    fail(`${label} is not canonical base64url`);
  }
  return decoded;
}

function validateCursor(cursor, label) {
  if (cursor === null) return;
  requireExactKeys(cursor, CURSOR_KEYS, label);
  if (typeof cursor.requested_at !== "string"
      || !UTC_MICROSECOND_PATTERN.test(cursor.requested_at)
      || Number.isNaN(Date.parse(cursor.requested_at))) {
    fail(`${label}.requested_at is not canonical UTC microsecond text`);
  }
  if (typeof cursor.request_id !== "string" || !UUID_PATTERN.test(cursor.request_id)) {
    fail(`${label}.request_id is not a canonical UUID`);
  }
}

function cursorForItem(item) {
  return { requested_at: item.requested_at, request_id: item.request_id };
}

function cursorCompare(left, right) {
  const timeOrder = left.requested_at.localeCompare(right.requested_at);
  return timeOrder || left.request_id.localeCompare(right.request_id);
}

function validateItem(item, label) {
  requireExactKeys(item, ITEM_KEYS, label);
  if (item.schema_version !== ITEM_SCHEMA || item.scope !== "dog"
      || item.confirmation_version !== "dog-delete-v1") {
    fail(`${label} has an unsupported schema or scope`);
  }
  validateCursor(cursorForItem(item), label);
  if (typeof item.scope_id !== "string" || !UUID_PATTERN.test(item.scope_id)) {
    fail(`${label}.scope_id is not a canonical UUID`);
  }
  for (const field of [
    "request_sha256",
    "requested_by_sha256",
    "tombstone_sha256",
    "replay_sha256",
  ]) {
    const decoded = decodeCanonicalBase64url(item[field], `${label}.${field}`);
    if (decoded.length !== 32) fail(`${label}.${field} must contain 32 bytes`);
  }
}

function validatePage(page) {
  requireExactKeys(page, PAGE_KEYS, "payload.page");
  if (page.schema_version !== PAGE_SCHEMA || typeof page.has_more !== "boolean"
      || !Array.isArray(page.items)) {
    fail("payload.page has an unsupported schema or type");
  }
  if (page.items.length > 1000) fail("payload.page exceeds the database page bound");
  if (page.items.length === 0) {
    if (page.next_cursor !== null || page.has_more) {
      fail("an empty page must be terminal and have a null cursor");
    }
    return;
  }
  page.items.forEach((item, index) => validateItem(item, `payload.page.items[${index}]`));
  for (let index = 1; index < page.items.length; index += 1) {
    if (cursorCompare(cursorForItem(page.items[index - 1]), cursorForItem(page.items[index])) >= 0) {
      fail("page items are not strictly cursor ordered");
    }
  }
  validateCursor(page.next_cursor, "payload.page.next_cursor");
  if (canonicalJson(page.next_cursor) !== canonicalJson(cursorForItem(page.items.at(-1)))) {
    fail("page next_cursor does not identify its final item");
  }
}

function validatePayload(payload) {
  requireExactKeys(payload, PAYLOAD_KEYS, "payload");
  if (payload.schema_version !== ARTIFACT_SCHEMA) fail("unsupported payload schema");
  if (typeof payload.key_id !== "string" || !KEY_ID_PATTERN.test(payload.key_id)) {
    fail("invalid key_id");
  }
  if (typeof payload.context_id !== "string"
      || !CONTEXT_ID_PATTERN.test(payload.context_id)) {
    fail("invalid context_id");
  }
  if (!Number.isSafeInteger(payload.batch_sequence) || payload.batch_sequence < 0) {
    fail("invalid batch_sequence");
  }
  if (typeof payload.created_at !== "string"
      || !UTC_MILLISECOND_PATTERN.test(payload.created_at)
      || new Date(payload.created_at).toISOString() !== payload.created_at) {
    fail("created_at is not canonical UTC millisecond text");
  }
  if (payload.previous_payload_sha256 !== null
      && (typeof payload.previous_payload_sha256 !== "string"
        || !SHA256_PATTERN.test(payload.previous_payload_sha256))) {
    fail("invalid previous_payload_sha256");
  }
  validateCursor(payload.after_cursor, "payload.after_cursor");
  validatePage(payload.page);
  if (payload.after_cursor !== null && payload.page.items.length > 0
      && cursorCompare(payload.after_cursor, cursorForItem(payload.page.items[0])) >= 0) {
    fail("page does not advance beyond its input cursor");
  }
}

function trustedKeyFor(trustedPublicKeys, keyId) {
  if (trustedPublicKeys instanceof Map) return trustedPublicKeys.get(keyId);
  if (isPlainObject(trustedPublicKeys)) {
    return Object.hasOwn(trustedPublicKeys, keyId) ? trustedPublicKeys[keyId] : undefined;
  }
  fail("trustedPublicKeys must be a Map or plain object");
}

export function signTombstoneArtifact({
  page,
  keyId,
  contextId,
  privateKey,
  createdAt,
  batchSequence = 0,
  previousPayloadSha256 = null,
  afterCursor = null,
}) {
  const payload = {
    schema_version: ARTIFACT_SCHEMA,
    key_id: keyId,
    context_id: contextId,
    batch_sequence: batchSequence,
    previous_payload_sha256: previousPayloadSha256,
    after_cursor: structuredClone(afterCursor),
    created_at: createdAt,
    page: structuredClone(page),
  };
  validatePayload(payload);
  if (batchSequence === 0 && (previousPayloadSha256 !== null || afterCursor !== null)) {
    fail("the first batch must start with null chain pointers");
  }
  if (batchSequence > 0 && (previousPayloadSha256 === null || afterCursor === null)) {
    fail("a continuation batch requires both chain pointers");
  }
  const bytes = Buffer.from(canonicalJson(payload), "utf8");
  return {
    payload,
    payload_sha256: sha256Hex(bytes),
    signature_algorithm: SIGNATURE_ALGORITHM,
    signature: cryptoSign(null, bytes, privateKey).toString("base64url"),
  };
}

export function verifyTombstoneArtifact(
  artifact,
  trustedPublicKeys,
  { expectedContextId } = {},
) {
  requireExactKeys(artifact, ARTIFACT_KEYS, "artifact");
  if (artifact.signature_algorithm !== SIGNATURE_ALGORITHM) {
    fail("unsupported signature_algorithm");
  }
  validatePayload(artifact.payload);
  if (typeof expectedContextId !== "string"
      || !CONTEXT_ID_PATTERN.test(expectedContextId)) {
    fail("expectedContextId is required and must be canonical");
  }
  if (artifact.payload.context_id !== expectedContextId) fail("context_id mismatch");
  if (typeof artifact.payload_sha256 !== "string"
      || !SHA256_PATTERN.test(artifact.payload_sha256)) {
    fail("invalid payload_sha256");
  }
  const bytes = Buffer.from(canonicalJson(artifact.payload), "utf8");
  const actualDigest = Buffer.from(sha256Hex(bytes), "hex");
  const claimedDigest = Buffer.from(artifact.payload_sha256, "hex");
  if (!timingSafeEqual(actualDigest, claimedDigest)) fail("payload digest mismatch");
  const signature = decodeCanonicalBase64url(artifact.signature, "signature");
  if (signature.length !== 64) fail("Ed25519 signature must contain 64 bytes");
  const publicKey = trustedKeyFor(trustedPublicKeys, artifact.payload.key_id);
  if (!publicKey) fail("untrusted key_id");
  let verified = false;
  try {
    verified = cryptoVerify(null, bytes, publicKey, signature);
  } catch {
    fail("trusted public key is invalid");
  }
  if (!verified) fail("signature verification failed");
  return structuredClone(artifact.payload);
}

export function verifyTombstoneArtifactChain(
  artifacts,
  trustedPublicKeys,
  { expectedContextId, maxBatches = 1024, requireComplete = true } = {},
) {
  if (!Array.isArray(artifacts) || artifacts.length === 0
      || artifacts.length > maxBatches) {
    fail("artifact chain length is outside its bound");
  }
  const payloads = artifacts.map((artifact) => (
    verifyTombstoneArtifact(artifact, trustedPublicKeys, { expectedContextId })
  ));
  const requestIds = new Set();
  for (let index = 0; index < artifacts.length; index += 1) {
    const { payload_sha256: payloadSha256 } = artifacts[index];
    const payload = payloads[index];
    if (payload.batch_sequence !== index) fail("batch sequence is not contiguous");
    if (index === 0) {
      if (payload.previous_payload_sha256 !== null || payload.after_cursor !== null) {
        fail("the first batch has non-null chain pointers");
      }
    } else {
      const previousArtifact = artifacts[index - 1];
      const previousPayload = payloads[index - 1];
      if (!previousPayload.page.has_more) fail("a terminal page has a successor");
      if (payload.page.items.length === 0) fail("a continuation page is empty");
      if (payload.previous_payload_sha256 !== previousArtifact.payload_sha256) {
        fail("previous payload digest does not match");
      }
      if (canonicalJson(payload.after_cursor)
          !== canonicalJson(previousPayload.page.next_cursor)) {
        fail("continuation cursor does not match the previous page");
      }
    }
    for (const item of payload.page.items) {
      if (requestIds.has(item.request_id)) fail("request_id appears in multiple pages");
      requestIds.add(item.request_id);
    }
    if (payloadSha256 !== artifacts[index].payload_sha256) {
      fail("artifact changed during chain verification");
    }
  }
  const finalPayload = payloads.at(-1);
  if (requireComplete && finalPayload.page.has_more) fail("artifact chain is incomplete");
  return {
    payloads,
    items: payloads.flatMap((payload) => payload.page.items)
      .map((item) => structuredClone(item)),
    final_payload_sha256: artifacts.at(-1).payload_sha256,
    complete: !finalPayload.page.has_more,
  };
}
