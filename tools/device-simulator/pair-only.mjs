import { randomBytes, randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";

const root = new URL("../../", import.meta.url);
const claimFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/device-v1-claim-request.json",
  root,
);
const CLAIM_CODE_PATTERN = /^[0-9A-HJKMNP-TV-Z]{16}$/u;
const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const UUID_V4_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const SHA256_BASE64URL_PATTERN = /^[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/u;
const MACHINE_CODE_PATTERN = /^[a-z][a-z0-9_]{0,63}$/u;
const PROBLEM_CODES = new Map([
  ["malformed_json", { status: 400, retryAfterAllowed: false }],
  ["invalid_envelope", { status: 400, retryAfterAllowed: false }],
  ["claim_unavailable", { status: 401, retryAfterAllowed: false }],
  ["method_not_allowed", { status: 405, retryAfterAllowed: false }],
  ["request_id_reused", { status: 409, retryAfterAllowed: false }],
  ["device_identity_conflict", { status: 409, retryAfterAllowed: false }],
  ["length_required", { status: 411, retryAfterAllowed: false }],
  ["payload_too_large", { status: 413, retryAfterAllowed: false }],
  ["unsupported_media_type", { status: 415, retryAfterAllowed: false }],
  ["unsupported_protocol", { status: 422, retryAfterAllowed: false }],
  ["unsupported_schema", { status: 422, retryAfterAllowed: false }],
  ["invalid_capabilities", { status: 422, retryAfterAllowed: false }],
  ["clock_skew", { status: 422, retryAfterAllowed: false }],
  ["rate_limited", { status: 429, retryAfterAllowed: true }],
  ["internal_error", { status: 500, retryAfterAllowed: true }],
  ["transaction_conflict", { status: 503, retryAfterAllowed: true }],
  ["server_busy", { status: 503, retryAfterAllowed: true }],
  ["gateway_timeout", { status: 504, retryAfterAllowed: true }],
]);

function fail(message) {
  throw new Error(`Pair-only simulator: ${message}`);
}

function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function hasExactKeys(value, expected) {
  const actual = Object.keys(value).sort();
  const sortedExpected = [...expected].sort();
  return actual.length === sortedExpected.length &&
    actual.every((key, index) => key === sortedExpected[index]);
}

function isCanonicalTimestamp(value) {
  if (typeof value !== "string") return false;
  const milliseconds = Date.parse(value);
  return Number.isFinite(milliseconds) &&
    new Date(milliseconds).toISOString() === value;
}

function validateNoStore(response) {
  const directives = response.headers.get("cache-control")
    ?.split(",")
    .map((directive) => directive.trim().toLowerCase());
  if (!directives?.includes("no-store")) {
    fail("gateway response is cacheable");
  }
}

function validateContentType(response, expected) {
  const contentType = response.headers.get("content-type")?.split(";", 1)[0].trim().toLowerCase();
  if (contentType !== expected) fail("gateway returned an invalid content type");
}

function validProblemErrors(value) {
  return Array.isArray(value) && value.length <= 16 && value.every((error) =>
    isRecord(error) && hasExactKeys(error, ["code", "path"]) &&
    typeof error.path === "string" && error.path.length <= 128 && /^\//u.test(error.path) &&
    typeof error.code === "string" && MACHINE_CODE_PATTERN.test(error.code));
}

function validSupportedVersions(value) {
  if (!isRecord(value)) return false;
  const keys = Object.keys(value);
  const allowed = ["config_schemas", "protocol_versions", "telemetry_schemas"];
  return keys.length >= 1 && keys.every((key) => allowed.includes(key)) &&
    keys.every((key) => Array.isArray(value[key]) && value[key].length <= 8 &&
      value[key].every((item) => Number.isInteger(item) && item >= 1 && item <= 255) &&
      new Set(value[key]).size === value[key].length);
}

async function safeJson(response) {
  try {
    return await response.json();
  } catch {
    fail("gateway returned malformed JSON");
  }
}

async function parseSuccess(response, expected) {
  validateNoStore(response);
  validateContentType(response, "application/json");
  if (response.status !== 200) fail("gateway rejected an expected pairing request");
  const body = await safeJson(response);
  if (
    !isRecord(body) ||
    !hasExactKeys(body, [
      "next_sync_after_seconds",
      "pairing",
      "protocol_version",
      "request_id",
      "server_time",
    ]) ||
    body.protocol_version !== 1 ||
    body.request_id !== expected.requestId ||
    !isCanonicalTimestamp(body.server_time) ||
    !Number.isInteger(body.next_sync_after_seconds) ||
    body.next_sync_after_seconds < 30 ||
    body.next_sync_after_seconds > 86_400 ||
    !isRecord(body.pairing) ||
    !hasExactKeys(body.pairing, [
      "accepted_capability_hash",
      "collar_id",
      "credential_id",
      "device_id",
      "dog_id",
      "state",
    ]) ||
    body.pairing.device_id !== expected.deviceId ||
    body.pairing.credential_id !== expected.credentialId ||
    (expected.dogId !== undefined && body.pairing.dog_id !== expected.dogId) ||
    body.pairing.accepted_capability_hash !== expected.capabilityHash ||
    body.pairing.state !== "paired" ||
    !UUID_PATTERN.test(String(body.pairing.collar_id)) ||
    !UUID_PATTERN.test(String(body.pairing.dog_id)) ||
    !SHA256_BASE64URL_PATTERN.test(String(body.pairing.accepted_capability_hash))
  ) {
    fail("gateway returned an invalid pairing response");
  }
  return body;
}

async function parseProblem(response, expectedRequestId) {
  validateNoStore(response);
  validateContentType(response, "application/problem+json");
  const body = await safeJson(response);
  const metadata = isRecord(body) && typeof body.code === "string"
    ? PROBLEM_CODES.get(body.code)
    : undefined;
  const requiredKeys = ["code", "detail", "request_id", "status", "title", "type"];
  const allowedKeys = [...requiredKeys, "errors", "retry_after_seconds", "supported"];
  if (
    !isRecord(body) ||
    !requiredKeys.every((key) => Object.hasOwn(body, key)) ||
    !Object.keys(body).every((key) => allowedKeys.includes(key)) ||
    metadata === undefined ||
    response.status !== metadata.status ||
    body.status !== metadata.status ||
    body.type !== `urn:dog-rgb:problem:${body.code}` ||
    body.request_id !== expectedRequestId ||
    typeof body.title !== "string" || body.title.length < 1 || body.title.length > 80 ||
    typeof body.detail !== "string" || body.detail.length < 1 || body.detail.length > 240 ||
    (Object.hasOwn(body, "errors") && !validProblemErrors(body.errors)) ||
    (Object.hasOwn(body, "supported") && !validSupportedVersions(body.supported))
  ) {
    fail("gateway returned an invalid problem response");
  }

  let retryAfter = null;
  const headerRetryAfter = response.headers.has("retry-after")
    ? Number(response.headers.get("retry-after"))
    : null;
  const bodyRetryAfter = Object.hasOwn(body, "retry_after_seconds")
    ? body.retry_after_seconds
    : null;
  if ((headerRetryAfter !== null || bodyRetryAfter !== null) && !metadata.retryAfterAllowed) {
    fail("gateway returned an unexpected retry interval");
  }
  for (const interval of [headerRetryAfter, bodyRetryAfter]) {
    if (interval !== null &&
        (!Number.isInteger(interval) || interval < 1 || interval > 86_400)) {
      fail("gateway returned an invalid retry interval");
    }
  }
  if (headerRetryAfter !== null && bodyRetryAfter !== null && headerRetryAfter !== bodyRetryAfter) {
    fail("gateway returned conflicting retry intervals");
  }
  retryAfter = headerRetryAfter ?? bodyRetryAfter;
  if (
    body.code === "claim_unavailable" &&
    response.headers.get("www-authenticate") !== 'DogRGBClaim realm="dog-rgb-pairing"'
  ) {
    fail("gateway returned an invalid claim challenge");
  }

  return Object.freeze({
    ok: false,
    status: metadata.status,
    code: body.code,
    requestId: expectedRequestId,
    retryAfter,
  });
}

function publicSuccess(body) {
  return Object.freeze({
    ok: true,
    protocolVersion: body.protocol_version,
    requestId: body.request_id,
    serverTime: body.server_time,
    pairing: Object.freeze({
      deviceId: body.pairing.device_id,
      collarId: body.pairing.collar_id,
      dogId: body.pairing.dog_id,
      state: body.pairing.state,
      acceptedCapabilityHash: body.pairing.accepted_capability_hash,
    }),
    nextSyncAfterSeconds: body.next_sync_after_seconds,
  });
}

async function loadRequestFixture() {
  return JSON.parse(await readFile(claimFixtureUrl, "utf8"));
}

export async function createPairOnlySimulator({
  claimCode,
  apiUrl = process.env.SUPABASE_URL ?? "http://127.0.0.1:56321",
  fetchImpl = fetch,
  createUuid = randomUUID,
  createSecret = () => randomBytes(32).toString("base64url"),
  expectedDogId,
  timeoutMs = 15_000,
} = {}) {
  if (typeof claimCode !== "string" || !CLAIM_CODE_PATTERN.test(claimCode)) {
    fail("claim code is invalid");
  }
  let endpoint;
  try {
    endpoint = new URL("/functions/v1/device-v1-claim", apiUrl).toString();
  } catch {
    fail("gateway URL is invalid");
  }
  if (typeof fetchImpl !== "function") fail("fetch implementation is invalid");
  if (expectedDogId !== undefined &&
      (typeof expectedDogId !== "string" || !UUID_PATTERN.test(expectedDogId))) {
    fail("expected dog identity is invalid");
  }
  if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 60_000) {
    fail("request timeout is invalid");
  }

  const request = structuredClone(await loadRequestFixture());
  const requestId = createUuid();
  const deviceId = createUuid();
  const credentialId = createUuid();
  const credentialSecret = createSecret();
  if (
    !UUID_V4_PATTERN.test(requestId) ||
    !UUID_V4_PATTERN.test(deviceId) ||
    !UUID_V4_PATTERN.test(credentialId) ||
    typeof credentialSecret !== "string" ||
    !SHA256_BASE64URL_PATTERN.test(credentialSecret)
  ) {
    fail("generated identity is invalid");
  }

  request.request_id = requestId;
  request.claim_code = claimCode;
  request.credential_id = credentialId;
  request.credential_secret = credentialSecret;
  request.device.device_id = deviceId;
  const capabilityHash = request.device.capability_hash;
  const exactRaw = JSON.stringify(request);
  const deviceBearer = `Bearer drgb_v1_${credentialId}.${credentialSecret}`;
  const expected = Object.freeze({
    requestId,
    deviceId,
    credentialId,
    capabilityHash,
    dogId: expectedDogId,
  });

  const postRaw = async (raw) => {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
      return await fetchImpl(endpoint, {
        method: "POST",
        headers: {
          "content-type": "application/json",
          "content-length": String(Buffer.byteLength(raw)),
        },
        body: raw,
        signal: controller.signal,
      });
    } catch {
      fail("gateway request failed");
    } finally {
      clearTimeout(timeout);
    }
  };

  const readExactResponse = async (response) => {
    if (!response.ok) return parseProblem(response, requestId);
    return publicSuccess(await parseSuccess(response, expected));
  };
  const exactAttempt = async () => readExactResponse(await postRaw(exactRaw));

  return Object.freeze({
    artifactContainsPrivateMaterial(value) {
      if (typeof value === "string") {
        return [claimCode, credentialId, credentialSecret, deviceBearer]
          .some((secret) => value.includes(secret));
      }
      if (Buffer.isBuffer(value)) {
        return [claimCode, credentialId, credentialSecret, deviceBearer]
          .some((secret) => value.includes(Buffer.from(secret)));
      }
      return false;
    },
    async attempt() {
      return exactAttempt();
    },
    async proveReplaySafety() {
      const [lostResponse, concurrentResponseA, concurrentResponseB] = await Promise.all([
        postRaw(exactRaw),
        postRaw(exactRaw),
        postRaw(exactRaw),
      ]);
      validateNoStore(lostResponse);
      if (lostResponse.status !== 200) {
        return parseProblem(lostResponse, requestId);
      }
      validateContentType(lostResponse, "application/json");
      await lostResponse.body?.cancel();

      const concurrentA = await readExactResponse(concurrentResponseA);
      const concurrentB = await readExactResponse(concurrentResponseB);
      if (
        !concurrentA.ok ||
        !concurrentB.ok ||
        JSON.stringify(concurrentA) !== JSON.stringify(concurrentB)
      ) {
        fail("concurrent first use did not converge");
      }
      const replay = await exactAttempt();
      if (
        !replay.ok ||
        JSON.stringify(replay) !== JSON.stringify(concurrentA)
      ) {
        fail("exact replay did not converge");
      }

      const changedBytes = await postRaw(`${exactRaw} `);
      const changedBytesProblem = await parseProblem(changedBytes, requestId);
      if (changedBytesProblem.code !== "request_id_reused") {
        fail("changed request bytes were not rejected");
      }

      const changedRequest = structuredClone(request);
      changedRequest.request_id = createUuid();
      const changedRequestResponse = await postRaw(JSON.stringify(changedRequest));
      const changedRequestProblem = await parseProblem(
        changedRequestResponse,
        changedRequest.request_id,
      );
      if (changedRequestProblem.code !== "claim_unavailable") {
        fail("consumed claim accepted a different request identity");
      }

      const changedDevice = structuredClone(request);
      changedDevice.device.device_id = createUuid();
      const changedDeviceResponse = await postRaw(JSON.stringify(changedDevice));
      const changedDeviceProblem = await parseProblem(changedDeviceResponse, requestId);
      if (changedDeviceProblem.code !== "claim_unavailable") {
        fail("consumed claim accepted a different device identity");
      }

      return Object.freeze({
        ok: true,
        scenarios: Object.freeze([
          "discarded-response-exact-replay",
          "concurrent-first-use-convergence",
          "changed-bytes-rejected",
          "changed-request-rejected",
          "changed-device-rejected",
        ]),
        pairing: replay.pairing,
      });
    },
  });
}
