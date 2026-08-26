import { createHash, randomBytes, randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";

const root = new URL("../../", import.meta.url);
const claimFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/device-v1-claim-request.json",
  root,
);
const syncFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/device-v1-sync-request.json",
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
  ["invalid_device_credential", { status: 401, retryAfterAllowed: false }],
  ["device_revoked", { status: 403, retryAfterAllowed: false }],
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

function isTimestamp(value) {
  return typeof value === "string" && value.length <= 35 &&
    Number.isFinite(Date.parse(value));
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

async function loadSyncFixture() {
  return JSON.parse(await readFile(syncFixtureUrl, "utf8"));
}

function pointHash(points) {
  const bytes = Buffer.alloc(points.length * 16);
  points.forEach(([lat, lon, utc, speed, satellites, flags], index) => {
    const offset = index * 16;
    bytes.writeInt32LE(lat, offset);
    bytes.writeInt32LE(lon, offset + 4);
    bytes.writeUInt32LE(utc, offset + 8);
    bytes.writeUInt16LE(speed, offset + 12);
    bytes.writeUInt8(satellites, offset + 14);
    bytes.writeUInt8(flags, offset + 15);
  });
  return createHash("sha256").update(bytes).digest("base64url");
}

function bogotaLocalDate(date) {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: "America/Bogota",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  }).formatToParts(date);
  const value = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${value.year}-${value.month}-${value.day}`;
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

  const syncEndpoint = new URL("/functions/v1/device-v1-sync", apiUrl).toString();
  const postSync = async (requestBody) => {
    const raw = JSON.stringify(requestBody);
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    let response;
    try {
      response = await fetchImpl(syncEndpoint, {
        method: "POST",
        headers: {
          authorization: deviceBearer,
          "content-type": "application/json",
          "content-length": String(Buffer.byteLength(raw)),
        },
        body: raw,
        signal: controller.signal,
      });
    } catch {
      fail("sync request failed");
    } finally {
      clearTimeout(timeout);
    }
    validateNoStore(response);
    if (!response.ok) return parseProblem(response, requestBody.request_id);
    validateContentType(response, "application/json");
    const body = await safeJson(response);
    if (!isRecord(body)) fail("gateway returned an invalid sync body");
    if (body.protocol_version !== 1) fail("gateway returned an invalid sync protocol");
    if (body.request_id !== requestBody.request_id) fail("gateway returned a drifting sync identity");
    if (!isTimestamp(body.server_time)) fail("gateway returned an invalid sync time");
    if (!isRecord(body.telemetry)) fail("gateway returned invalid telemetry acknowledgements");
    if (!isRecord(body.configuration)) fail("gateway returned invalid configuration state");
    return body;
  };

  const readExactResponse = async (response) => {
    if (!response.ok) return parseProblem(response, requestId);
    return publicSuccess(await parseSuccess(response, expected));
  };
  let pairing = null;
  let uploaded = false;
  let syncTemplate = null;
  const exactAttempt = async () => {
    const result = await readExactResponse(await postRaw(exactRaw));
    if (result.ok) pairing = result.pairing;
    return result;
  };

  const requirePairing = () => {
    if (!pairing) fail("pairing must complete before sync");
  };

  const emptySync = () => {
    if (!syncTemplate) fail("journey upload must complete before empty sync");
    const value = structuredClone(syncTemplate);
    value.request_id = createUuid();
    value.clock.utc_ms = Date.now();
    value.upload = { chunks: [], summaries: [], loss_markers: [] };
    value.configuration = { mutations: [], reported: [] };
    value.diagnostics = {
      outbox_chunks: 0,
      outbox_points: 0,
      outbox_used_bytes: 0,
      outbox_capacity_bytes: syncTemplate.diagnostics.outbox_capacity_bytes,
      oldest_unacknowledged_utc_ms: null,
      dropped_points_total: 0,
      last_error_code: null,
    };
    return value;
  };

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
    async uploadJourneyRecording() {
      requirePairing();
      if (uploaded) fail("journey recording was already uploaded");
      const requestBody = structuredClone(await loadSyncFixture());
      const nowSeconds = Math.floor(Date.now() / 1_000);
      const pointTimes = [nowSeconds - 65, nowSeconds - 60, nowSeconds];
      requestBody.upload.chunks[0].points.forEach((point, index) => {
        point[2] = pointTimes[index];
      });
      requestBody.upload.chunks[0].content_sha256 = pointHash(
        requestBody.upload.chunks[0].points,
      );
      requestBody.request_id = createUuid();
      requestBody.device.device_id = deviceId;
      requestBody.device.capability_hash = capabilityHash;
      requestBody.clock.utc_ms = nowSeconds * 1_000;
      requestBody.capabilities = null;
      requestBody.diagnostics.oldest_unacknowledged_utc_ms = pointTimes[0] * 1_000;
      requestBody.upload.summaries[0].summary_id = createUuid();
      requestBody.upload.summaries[0].local_date = bogotaLocalDate(
        new Date(nowSeconds * 1_000),
      );
      requestBody.upload.summaries[0].window_start = new Date(
        pointTimes[0] * 1_000,
      ).toISOString();
      requestBody.upload.summaries[0].window_end = new Date(
        pointTimes[2] * 1_000,
      ).toISOString();
      requestBody.configuration = { mutations: [], reported: [] };

      const response = await postSync(requestBody);
      if (
        response.ok === false ||
        !Array.isArray(response.telemetry.accepted_chunks) ||
        response.telemetry.accepted_chunks.length !== 1 ||
        response.telemetry.accepted_chunks[0].accepted_point_count !== 3 ||
        !Array.isArray(response.telemetry.accepted_summary_ids) ||
        response.telemetry.accepted_summary_ids.length !== 1 ||
        response.telemetry.accepted_summary_ids[0] !==
          requestBody.upload.summaries[0].summary_id
      ) {
        fail("journey upload did not receive the exact acknowledgements");
      }
      uploaded = true;
      syncTemplate = requestBody;
      return Object.freeze({
        ok: true,
        requestId: requestBody.request_id,
        summaryId: requestBody.upload.summaries[0].summary_id,
        collarId: pairing.collarId,
        bootSequence: requestBody.upload.chunks[0].boot_sequence,
        chunkSequence: requestBody.upload.chunks[0].chunk_sequence,
        acceptedPointCount: 3,
        pointSequences: Object.freeze([0, 1, 2]),
      });
    },
    async convergeBrightness(expectedBrightness) {
      requirePairing();
      if (!uploaded) fail("journey upload must complete before configuration sync");
      if (!Number.isInteger(expectedBrightness) || expectedBrightness < 1 || expectedBrightness > 255) {
        fail("expected brightness is invalid");
      }
      const pull = emptySync();
      const desiredResponse = await postSync(pull);
      if (desiredResponse.ok === false) fail("desired configuration pull failed");
      const desired = desiredResponse.configuration.desired_resources?.find(
        (resource) => resource.resource_key === "brightness",
      );
      if (
        !isRecord(desired) ||
        !isRecord(desired.body) ||
        !hasExactKeys(desired.body, ["brightness"]) ||
        desired.body.brightness !== expectedBrightness ||
        !Number.isInteger(desired.server_version) ||
        desired.server_version < 1 ||
        !SHA256_BASE64URL_PATTERN.test(String(desired.body_sha256))
      ) {
        fail("simulator did not receive the exact desired brightness");
      }

      const report = emptySync();
      report.configuration.reported = [{
        resource_key: "brightness",
        server_version: desired.server_version,
        body_sha256: desired.body_sha256,
        status: "applied",
        error_code: null,
        device_applied_at: new Date().toISOString(),
      }];
      const reportResponse = await postSync(report);
      if (reportResponse.ok === false) fail("applied configuration report failed");
      return Object.freeze({
        ok: true,
        brightness: expectedBrightness,
        serverVersion: desired.server_version,
        bodySha256Hex: Buffer.from(desired.body_sha256, "base64url").toString("hex"),
      });
    },
    async assertRevoked() {
      requirePairing();
      const response = await postSync(emptySync());
      if (response.ok !== false || response.code !== "device_revoked") {
        fail("revoked collar retained sync authority");
      }
      return Object.freeze({ ok: true, code: response.code });
    },
  });
}
