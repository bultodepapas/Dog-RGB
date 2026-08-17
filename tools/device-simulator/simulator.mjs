import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash, randomBytes, randomUUID } from "node:crypto";

const root = new URL("../../", import.meta.url);
const readFixture = async (name) => JSON.parse(await readFile(new URL(`contracts/device-v1/fixtures/valid/${name}`, root), "utf8"));
const apiUrl = process.env.SUPABASE_URL ?? "http://127.0.0.1:56321";
const publishableKey = process.env.SUPABASE_PUBLISHABLE_KEY;
if (!publishableKey) throw new Error("SUPABASE_PUBLISHABLE_KEY is required (use `supabase status -o env`).");

async function post(path, body, headers = {}) {
  const raw = JSON.stringify(body);
  return fetch(`${apiUrl}${path}`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(raw)),
      ...headers,
    },
    body: raw,
  });
}

async function json(response) {
  const body = await response.json();
  assert.equal(response.ok, true, `${response.status}: ${JSON.stringify(body)}`);
  return body;
}

async function problem(response, status, code, requestId) {
  const body = await response.json();
  assert.equal(response.status, status, JSON.stringify(body));
  assert.equal(response.headers.get("content-type")?.startsWith("application/problem+json"), true);
  assert.equal(body.status, status);
  assert.equal(body.code, code);
  assert.equal(body.request_id, requestId);
  return body;
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

async function main() {
  const login = await json(await post("/auth/v1/token?grant_type=password", {
    email: "owner@example.test",
    password: "local-owner-password",
  }, { apikey: publishableKey }));

  const claimIssue = await readFixture("user-v1-issue-claim-request.json");
  claimIssue.request_id = randomUUID();
  claimIssue.dog_id = "30000000-0000-4000-8000-000000000003";
  const issued = await json(await post("/functions/v1/user-v1-issue-claim", claimIssue, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
  }));

  const claim = await readFixture("device-v1-claim-request.json");
  const deviceId = randomUUID();
  const credentialId = randomUUID();
  const secret = randomBytes(32).toString("base64url");
  claim.request_id = randomUUID();
  claim.claim_code = issued.claim.code;
  claim.credential_id = credentialId;
  claim.credential_secret = secret;
  claim.device.device_id = deviceId;
  const claimRaw = JSON.stringify(claim);
  const sendExactClaim = () => fetch(`${apiUrl}/functions/v1/device-v1-claim`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(claimRaw)),
    },
    body: claimRaw,
  });
  const [claimLostAttempt, claimReplayAttempt] = await Promise.all([sendExactClaim(), sendExactClaim()]);
  const paired = await json(claimLostAttempt);
  const pairedReplay = await json(claimReplayAttempt);
  assert.deepEqual(pairedReplay, paired, "an exact concurrent claim replay must return the stored result");
  assert.equal(paired.pairing.device_id, deviceId);
  assert.equal(paired.pairing.credential_id, credentialId);

  const alteredClaimRaw = `${claimRaw} `;
  const alteredClaim = await fetch(`${apiUrl}/functions/v1/device-v1-claim`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(alteredClaimRaw)),
    },
    body: alteredClaimRaw,
  });
  await problem(alteredClaim, 409, "request_id_reused", claim.request_id);

  const bearer = `Bearer drgb_v1_${credentialId}.${secret}`;
  const sync = await readFixture("device-v1-sync-request.json");
  sync.request_id = randomUUID();
  sync.device.device_id = deviceId;
  sync.device.capability_hash = claim.device.capability_hash;
  sync.configuration.mutations[0].mutation_id = randomUUID();
  sync.configuration.mutations[0].authored_hlc.actor_id = deviceId;

  const invalidEnvelope = structuredClone(sync);
  invalidEnvelope.request_id = randomUUID();
  invalidEnvelope.unexpected = true;
  await problem(
    await post("/functions/v1/device-v1-sync", invalidEnvelope, { authorization: bearer }),
    400,
    "invalid_envelope",
    invalidEnvelope.request_id,
  );

  const unsupportedProtocol = structuredClone(sync);
  unsupportedProtocol.request_id = randomUUID();
  unsupportedProtocol.protocol_version = 2;
  await problem(
    await post("/functions/v1/device-v1-sync", unsupportedProtocol, { authorization: bearer }),
    422,
    "unsupported_protocol",
    unsupportedProtocol.request_id,
  );

  const firstRaw = JSON.stringify(sync);
  const sendExactSync = () => fetch(`${apiUrl}/functions/v1/device-v1-sync`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(firstRaw)),
      authorization: bearer,
    },
    body: firstRaw,
  });
  const [lostAttempt, replayAttempt] = await Promise.all([sendExactSync(), sendExactSync()]);
  assert.equal(lostAttempt.ok, true, `initial sync failed: ${lostAttempt.status}`);
  // Deliberately discard one body to model a response lost after commit.
  const replay = await json(replayAttempt);
  assert.equal(replay.request_id, sync.request_id);
  assert.equal(replay.telemetry.accepted_chunks.length, 1);
  assert.equal(replay.telemetry.accepted_chunks[0].accepted_point_count, 3);
  assert.equal(replay.configuration.outcomes.length, 1);
  assert.equal(replay.configuration.outcomes[0].disposition, "winning");

  const emptySync = () => {
    const value = structuredClone(sync);
    value.request_id = randomUUID();
    value.upload = { chunks: [], summaries: [], loss_markers: [] };
    value.configuration = { mutations: [], reported: [] };
    return value;
  };
  const makeChunkSync = (chunkSequence, pointSequence, point) => {
    const value = emptySync();
    value.upload.chunks = [{
      telemetry_schema: 3,
      boot_sequence: 42,
      chunk_sequence: chunkSequence,
      first_point_sequence: pointSequence,
      point_count: 1,
      time_quality: 4,
      content_sha256: pointHash([point]),
      is_final: false,
      points: [point],
    }];
    return value;
  };
  const nowSeconds = Math.floor(Date.now() / 1000);
  await json(await post("/functions/v1/device-v1-sync", makeChunkSync(10, 6, [468123700, -740123100, nowSeconds, 110, 9, 7]), { authorization: bearer }));
  await json(await post("/functions/v1/device-v1-sync", makeChunkSync(9, 3, [468123600, -740123300, nowSeconds - 5, 100, 9, 7]), { authorization: bearer }));

  const webBody = { brightness: 160 };
  const webHash = createHash("sha256").update(JSON.stringify(webBody)).digest("base64url");
  const webMutation = await json(await post("/rest/v1/rpc/mutate_config_resource_v1", {
    p_collar_id: paired.pairing.collar_id,
    p_resource_key: "brightness",
    p_resource_schema: 1,
    p_mutation_id: randomUUID(),
    p_base_server_version: replay.configuration.outcomes[0].server_version,
    p_body: webBody,
    p_body_sha256: `\\x${Buffer.from(webHash, "base64url").toString("hex")}`,
  }, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
    "content-profile": "api",
  }));
  assert.equal(webMutation.disposition, "winning");

  const desired = await json(await post("/functions/v1/device-v1-sync", emptySync(), { authorization: bearer }));
  const brightness = desired.configuration.desired_resources.find((item) => item.resource_key === "brightness");
  assert.deepEqual(brightness.body, webBody, "website mutation must win and return as desired state");

  const report = emptySync();
  report.configuration.reported = [{
    resource_key: "brightness",
    server_version: brightness.server_version,
    body_sha256: brightness.body_sha256,
    status: "applied",
    error_code: null,
    device_applied_at: new Date().toISOString(),
  }];
  await json(await post("/functions/v1/device-v1-sync", report, { authorization: bearer }));

  const points = await fetch(
    `${apiUrl}/rest/v1/telemetry_points?collar_id=eq.${paired.pairing.collar_id}&select=point_sequence&order=point_sequence.asc`,
    {
      headers: {
        apikey: publishableKey,
        authorization: `Bearer ${login.access_token}`,
        "accept-profile": "api",
      },
    },
  ).then(json);
  assert.deepEqual(points.map((point) => point.point_sequence), [0, 1, 2, 3, 6], "out-of-order chunks remain independently addressable");

  const revoke = await readFixture("device-v1-revoke-request.json");
  revoke.request_id = randomUUID();
  revoke.device_id = deviceId;
  revoke.credential_id = credentialId;
  const revoked = await json(await post("/functions/v1/device-v1-revoke", revoke, { authorization: bearer }));
  assert.equal(revoked.state, "revoked");

  console.log(JSON.stringify({
    ok: true,
    scenarios: [
      "pair", "concurrent-claim-replay", "claim-request-id-conflict",
      "schema-invalid-envelope", "unsupported-protocol-problem",
      "concurrent-exact-replay", "lost-response", "single-database-effect",
      "out-of-order-upload", "ap-mutation", "web-winner", "reported-applied", "revoke",
    ],
    points: points.length,
  }));
}

await main();
