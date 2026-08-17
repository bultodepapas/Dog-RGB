import assert from "node:assert/strict";
import { randomBytes, randomUUID } from "node:crypto";

const apiUrl = process.env.SUPABASE_URL ?? "http://127.0.0.1:56321";
const publishableKey = process.env.SUPABASE_PUBLISHABLE_KEY;
if (!publishableKey) throw new Error("SUPABASE_PUBLISHABLE_KEY is required (use `supabase status -o env`).");

let scenarios = 0;
let normalizedChunkedRequests = 0;

function byteLength(body) {
  return typeof body === "string" ? Buffer.byteLength(body) : body.byteLength;
}

async function request(endpoint, {
  method = "POST",
  body,
  contentType = "application/json",
  omitLength = false,
  headers = {},
} = {}) {
  const requestHeaders = {
    accept: "application/json, application/problem+json",
    ...endpoint.headers,
    ...headers,
  };
  if (contentType !== null) requestHeaders["content-type"] = contentType;

  const init = { method, headers: requestHeaders, signal: AbortSignal.timeout(15_000) };
  if (body !== undefined) {
    if (omitLength) {
      const bytes = typeof body === "string" ? Buffer.from(body) : body;
      init.body = new ReadableStream({
        start(controller) {
          controller.enqueue(bytes);
          controller.close();
        },
      });
      init.duplex = "half";
    } else {
      requestHeaders["content-length"] = String(byteLength(body));
      init.body = body;
    }
  }
  return fetch(`${apiUrl}${endpoint.path}`, init);
}

async function expectDogProblem(response, status, code, requestId = null, expectedHeaders = {}) {
  const raw = await response.text();
  let body;
  try { body = JSON.parse(raw); } catch {
    assert.fail(`HTTP ${response.status} did not contain JSON: ${raw.slice(0, 200)}`);
  }
  assert.equal(response.status, status, raw);
  assert.equal(response.headers.get("content-type")?.startsWith("application/problem+json"), true);
  assert.equal(response.headers.get("cache-control"), "no-store");
  assert.deepEqual(Object.keys(body).sort(), ["code", "detail", "request_id", "status", "title", "type"]);
  assert.equal(body.type, `urn:dog-rgb:problem:${code}`);
  assert.equal(body.status, status);
  assert.equal(body.code, code);
  assert.equal(body.request_id, requestId);
  assert.equal(typeof body.title === "string" && body.title.length > 0, true);
  assert.equal(typeof body.detail === "string" && body.detail.length > 0, true);
  assert.doesNotMatch(raw, /password|credential_secret|sqlstate|stack trace|latitude|longitude/i);
  for (const [name, value] of Object.entries(expectedHeaders)) {
    assert.equal(response.headers.get(name), value);
  }
  scenarios += 1;
}

async function authenticateOwner() {
  const raw = JSON.stringify({
    email: "owner@example.test",
    password: "local-owner-password",
  });
  const response = await fetch(`${apiUrl}/auth/v1/token?grant_type=password`, {
    method: "POST",
    signal: AbortSignal.timeout(15_000),
    headers: {
      apikey: publishableKey,
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(raw)),
    },
    body: raw,
  });
  const body = await response.json();
  assert.equal(response.ok, true, `Owner authentication failed: ${response.status} ${JSON.stringify(body)}`);
  return body.access_token;
}

const ownerToken = await authenticateOwner();
const syntacticDeviceBearer = `Bearer drgb_v1_${randomUUID()}.${randomBytes(32).toString("base64url")}`;
const endpoints = [
  {
    name: "user-v1-issue-claim",
    path: "/functions/v1/user-v1-issue-claim",
    maxBytes: 4096,
    requiresLength: false,
    headers: { apikey: publishableKey, authorization: `Bearer ${ownerToken}` },
  },
  {
    name: "device-v1-claim",
    path: "/functions/v1/device-v1-claim",
    maxBytes: 32 * 1024,
    requiresLength: true,
    headers: {},
  },
  {
    name: "device-v1-sync",
    path: "/functions/v1/device-v1-sync",
    maxBytes: 128 * 1024,
    requiresLength: true,
    headers: { authorization: syntacticDeviceBearer },
  },
  {
    name: "device-v1-revoke",
    path: "/functions/v1/device-v1-revoke",
    maxBytes: 4096,
    requiresLength: true,
    headers: { authorization: syntacticDeviceBearer },
  },
];

for (const endpoint of endpoints) {
  await expectDogProblem(
    await request(endpoint, { method: "GET", contentType: null }),
    405,
    "method_not_allowed",
    null,
    { allow: "POST" },
  );
  await expectDogProblem(
    await request(endpoint, { body: "{}", contentType: null }),
    415,
    "unsupported_media_type",
  );
  await expectDogProblem(
    await request(endpoint, { body: "{}", contentType: "application/jsonp" }),
    415,
    "unsupported_media_type",
  );
  await expectDogProblem(
    await request(endpoint, { body: "{" }),
    400,
    "malformed_json",
  );
  await expectDogProblem(
    await request(endpoint, { body: Buffer.from([0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xc3, 0x28, 0x22, 0x7d]) }),
    400,
    "malformed_json",
  );
  const nested = `${'{"value":'.repeat(32)}null${"}".repeat(32)}`;
  await expectDogProblem(
    await request(endpoint, { body: nested }),
    400,
    "malformed_json",
  );
  await expectDogProblem(
    await request(endpoint, { body: "[]" }),
    400,
    "invalid_envelope",
  );
  const requestId = randomUUID();
  await expectDogProblem(
    await request(endpoint, { body: JSON.stringify({ protocol_version: 2, request_id: requestId }) }),
    422,
    "unsupported_protocol",
    requestId,
  );
  const oversized = `{"padding":"${"x".repeat(endpoint.maxBytes)}"}`;
  await expectDogProblem(
    await request(endpoint, { body: oversized }),
    413,
    "payload_too_large",
  );
  await expectDogProblem(
    await request(endpoint, { body: "{}", contentType: "Application/JSON ; charset=utf-8" }),
    422,
    "unsupported_protocol",
  );
  const chunked = await request(endpoint, { body: "{}", omitLength: true });
  if (endpoint.requiresLength && chunked.status === 411) {
    await expectDogProblem(chunked, 411, "length_required");
  } else {
    // Kong and Envoy may normalize a chunked public request into a length-delimited
    // upstream request. The direct boundedJson unit test preserves the 411 fallback.
    if (endpoint.requiresLength) normalizedChunkedRequests += 1;
    await expectDogProblem(chunked, 422, "unsupported_protocol");
  }
  console.log(`Boundary matrix: ${endpoint.name} passed.`);
}

const syncEndpoint = endpoints.find((endpoint) => endpoint.name === "device-v1-sync");
const extremeNesting = `${'{"value":'.repeat(10_000)}null${"}".repeat(10_000)}`;
await expectDogProblem(
  await request(syncEndpoint, { body: extremeNesting }),
  400,
  "malformed_json",
);
console.log("Boundary matrix: extreme nesting passed.");

for (const endpoint of endpoints.filter((candidate) => candidate.name === "device-v1-sync" || candidate.name === "device-v1-revoke")) {
  const requestId = randomUUID();
  await expectDogProblem(
    await request({ ...endpoint, headers: {} }, {
      body: JSON.stringify({ protocol_version: 1, request_id: requestId }),
    }),
    401,
    "device_credential_invalid",
    requestId,
    { "www-authenticate": 'Bearer realm="dog-rgb-device"' },
  );
}

const unauthenticatedUser = await request({ ...endpoints[0], headers: { apikey: publishableKey } }, { body: "{}" });
assert.equal(unauthenticatedUser.status, 401, await unauthenticatedUser.text());
scenarios += 1;

const preflight = await fetch(`${apiUrl}${endpoints[0].path}`, {
  method: "OPTIONS",
  signal: AbortSignal.timeout(15_000),
  headers: {
    origin: "http://127.0.0.1:3000",
    "access-control-request-method": "POST",
    "access-control-request-headers": "authorization, apikey, content-type",
  },
});
assert.equal(preflight.ok, true, `CORS preflight failed with HTTP ${preflight.status}`);
assert.equal(preflight.headers.get("access-control-allow-origin") !== null, true);
scenarios += 1;

console.log(
  `HTTP boundary matrix passed ${scenarios} adversarial scenarios across ${endpoints.length} Edge endpoints ` +
  `(${normalizedChunkedRequests} chunked requests normalized by the public gateway).`,
);
