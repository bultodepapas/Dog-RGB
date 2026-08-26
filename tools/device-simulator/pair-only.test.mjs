import assert from "node:assert/strict";
import test from "node:test";

import { createPairOnlySimulator } from "./pair-only.mjs";

const CLAIM_CODE = "0123ABCD4567EFGH";
const IDS = [
  "71000000-0000-4000-8000-000000000001",
  "72000000-0000-4000-8000-000000000001",
  "73000000-0000-4000-8000-000000000001",
  "74000000-0000-4000-8000-000000000001",
  "75000000-0000-4000-8000-000000000001",
  "76000000-0000-4000-8000-000000000001",
];
const SECRET = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE";

function problemResponse(code, status, requestId, headers = {}, optionalBody = {}) {
  return Response.json({
    type: `urn:dog-rgb:problem:${code}`,
    title: "Safe title",
    status,
    detail: "Safe detail.",
    code,
    request_id: requestId,
    ...optionalBody,
  }, {
    status,
    headers: {
      "cache-control": "no-store",
      "content-type": "application/problem+json",
      ...headers,
    },
  });
}

function successResponse(request) {
  return Response.json({
    protocol_version: 1,
    request_id: request.request_id,
    server_time: "2026-08-25T20:00:00.000Z",
    pairing: {
      device_id: request.device.device_id,
      credential_id: request.credential_id,
      collar_id: "77000000-0000-4000-8000-000000000001",
      dog_id: "30000000-0000-4000-8000-000000000003",
      state: "paired",
      accepted_capability_hash: request.device.capability_hash,
    },
    next_sync_after_seconds: 30,
  }, { headers: { "cache-control": "private, no-store" } });
}

function replayHarness() {
  const rawBodies = [];
  let exactRaw = null;
  const fetchImpl = async (_url, init) => {
    const raw = init.body;
    rawBodies.push(raw);
    const request = JSON.parse(raw);
    if (exactRaw === null) exactRaw = raw;
    if (raw === exactRaw) return successResponse(request);
    const original = JSON.parse(exactRaw);
    if (request.request_id === original.request_id &&
        request.device.device_id === original.device.device_id) {
      return problemResponse("request_id_reused", 409, request.request_id);
    }
    return problemResponse("claim_unavailable", 401, request.request_id, {
      "www-authenticate": 'DogRGBClaim realm="dog-rgb-pairing"',
    });
  };
  return { fetchImpl, rawBodies };
}

test("pair-only proof serializes once and keeps claim credentials out of its result", async () => {
  const { fetchImpl, rawBodies } = replayHarness();
  let index = 0;
  const simulator = await createPairOnlySimulator({
    claimCode: CLAIM_CODE,
    apiUrl: "http://127.0.0.1:56321",
    fetchImpl,
    createUuid: () => IDS[index++],
    createSecret: () => SECRET,
  });

  const proof = await simulator.proveReplaySafety();
  assert.equal(proof.ok, true);
  assert.equal(rawBodies.length, 7);
  assert.equal(rawBodies.slice(0, 4).every((raw) => raw === rawBodies[0]), true);
  assert.equal(rawBodies[4] === `${rawBodies[0]} `, true);
  const first = JSON.parse(rawBodies[0]);
  const changedRequest = JSON.parse(rawBodies[5]);
  const changedDevice = JSON.parse(rawBodies[6]);
  assert.equal(first.protocol_version, 1);
  assert.equal(first.claim_code, CLAIM_CODE);
  assert.equal(first.credential_secret.length, 43);
  assert.equal(changedRequest.request_id !== first.request_id, true);
  assert.equal(changedRequest.device.device_id, first.device.device_id);
  assert.equal(changedRequest.credential_id, first.credential_id);
  assert.equal(changedDevice.request_id, first.request_id);
  assert.equal(changedDevice.device.device_id !== first.device.device_id, true);
  assert.equal(changedDevice.credential_id, first.credential_id);
  const artifact = JSON.stringify(proof);
  assert.equal(artifact.includes(CLAIM_CODE), false);
  assert.equal(artifact.includes(SECRET), false);
  assert.equal(artifact.includes(first.credential_id), false);
  assert.equal(simulator.artifactContainsPrivateMaterial("sanitized output"), false);
  assert.equal(simulator.artifactContainsPrivateMaterial(CLAIM_CODE), true);
  assert.equal(simulator.artifactContainsPrivateMaterial(SECRET), true);
  assert.equal(simulator.artifactContainsPrivateMaterial(Buffer.from(first.credential_id)), true);
  assert.equal(
    simulator.artifactContainsPrivateMaterial(
      `Bearer drgb_v1_${first.credential_id}.${SECRET}`,
    ),
    true,
  );
});

test("invalid claim codes fail before fixture transmission", async () => {
  let calls = 0;
  for (const claimCode of [undefined, "", "0123ABCD4567EFG", "0123ABCD4567EFGO"]) {
    await assert.rejects(
      createPairOnlySimulator({
        claimCode,
        fetchImpl: async () => {
          calls += 1;
          throw new Error("unexpected request");
        },
      }),
      /claim code is invalid/u,
    );
  }
  assert.equal(calls, 0);
});

test("stable gateway problems are reduced to bounded metadata", async () => {
  const cases = [
    ["claim_unavailable", 401, { "www-authenticate": 'DogRGBClaim realm="dog-rgb-pairing"' }],
    ["invalid_device_credential", 401, {}],
    ["device_revoked", 403, {}],
    ["malformed_json", 400, {}],
    ["invalid_envelope", 400, {}, {
      errors: [{ path: "/device", code: "required" }],
    }],
    ["method_not_allowed", 405, {}],
    ["length_required", 411, {}],
    ["payload_too_large", 413, {}],
    ["unsupported_media_type", 415, {}],
    ["unsupported_protocol", 422, {}, {
      supported: { protocol_versions: [1] },
    }],
    ["unsupported_schema", 422, {}],
    ["invalid_capabilities", 422, {}],
    ["clock_skew", 422, {}],
    ["request_id_reused", 409, {}],
    ["device_identity_conflict", 409, {}],
    ["rate_limited", 429, { "retry-after": "900" }, { retry_after_seconds: 900 }],
    ["internal_error", 500, {}, { retry_after_seconds: 30 }],
    ["transaction_conflict", 503, {}],
    ["server_busy", 503, {}],
    ["gateway_timeout", 504, {}],
  ];

  for (const [code, status, headers, optionalBody] of cases) {
    const simulator = await createPairOnlySimulator({
      claimCode: CLAIM_CODE,
      fetchImpl: async (_url, init) => {
        const request = JSON.parse(init.body);
        return problemResponse(code, status, request.request_id, headers, optionalBody);
      },
    });
    const result = await simulator.attempt();
    assert.equal(result.ok, false);
    assert.equal(result.code, code);
    assert.equal(result.status, status);
    assert.equal(result.retryAfter, optionalBody?.retry_after_seconds ?? null);
    assert.equal(Object.hasOwn(result, "detail"), false);
    assert.equal(Object.hasOwn(result, "title"), false);
  }
});

test("a pairing response for another dog is rejected", async () => {
  const simulator = await createPairOnlySimulator({
    claimCode: CLAIM_CODE,
    expectedDogId: "30000000-0000-4000-8000-000000000004",
    fetchImpl: async (_url, init) => successResponse(JSON.parse(init.body)),
  });
  await assert.rejects(simulator.attempt(), /invalid pairing response/u);
});

test("malformed, cacheable, or identity-drifting responses fail without echoing bodies", async () => {
  const responses = [
    new Response("not-json", {
      status: 200,
      headers: { "cache-control": "no-store", "content-type": "application/json" },
    }),
    Response.json({}, { status: 200, headers: { "cache-control": "public" } }),
    Response.json({ database_detail: "private" }, {
      status: 200,
      headers: { "cache-control": "no-store" },
    }),
  ];
  for (const response of responses) {
    const simulator = await createPairOnlySimulator({
      claimCode: CLAIM_CODE,
      fetchImpl: async () => response,
    });
    await assert.rejects(
      simulator.attempt(),
      (error) => error instanceof Error &&
        !error.message.includes(CLAIM_CODE) &&
        !error.message.includes(SECRET) &&
        !error.message.includes("private"),
    );
  }
});

test("transport failures cannot echo the secret-bearing request", async () => {
  const simulator = await createPairOnlySimulator({
    claimCode: CLAIM_CODE,
    createSecret: () => SECRET,
    fetchImpl: async (_url, init) => {
      throw new Error(init.body);
    },
  });
  await assert.rejects(
    simulator.attempt(),
    (error) => error instanceof Error &&
      error.message === "Pair-only simulator: gateway request failed" &&
      !error.message.includes(CLAIM_CODE) &&
      !error.message.includes(SECRET),
  );
});

test("a stalled gateway attempt is aborted by a bounded timer", async () => {
  let observedSignal;
  const simulator = await createPairOnlySimulator({
    claimCode: CLAIM_CODE,
    timeoutMs: 10,
    fetchImpl: async (_url, init) => {
      observedSignal = init.signal;
      await new Promise((resolve, reject) => {
        init.signal.addEventListener("abort", () => reject(init.signal.reason), { once: true });
      });
    },
  });
  await assert.rejects(simulator.attempt(), /gateway request failed/u);
  assert.equal(observedSignal.aborted, true);
});
