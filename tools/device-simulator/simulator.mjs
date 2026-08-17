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

async function problem(response, status, code, requestId, retryAfter = null) {
  const body = await response.json();
  assert.equal(response.status, status, JSON.stringify(body));
  assert.equal(response.headers.get("content-type")?.startsWith("application/problem+json"), true);
  assert.equal(body.status, status);
  assert.equal(body.code, code);
  assert.equal(body.request_id, requestId);
  if (retryAfter !== null) assert.equal(response.headers.get("retry-after"), String(retryAfter));
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

function canonicalJson(value) {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(",")}}`;
}

function bodyHash(body) {
  return createHash("sha256").update(canonicalJson(body)).digest("base64url");
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

  const duplicateClaimIssue = structuredClone(claimIssue);
  duplicateClaimIssue.request_id = randomUUID();
  await problem(
    await post("/functions/v1/user-v1-issue-claim", duplicateClaimIssue, {
      apikey: publishableKey,
      authorization: `Bearer ${login.access_token}`,
    }),
    409,
    "active_claim_exists",
    duplicateClaimIssue.request_id,
  );

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
  const makeChunkSync = (chunkSequence, pointSequence, point, bootSequence = 42, isFinal = false) => {
    const value = emptySync();
    value.upload.chunks = [{
      telemetry_schema: 3,
      boot_sequence: bootSequence,
      chunk_sequence: chunkSequence,
      first_point_sequence: pointSequence,
      point_count: 1,
      time_quality: 4,
      content_sha256: pointHash([point]),
      is_final: isFinal,
      points: [point],
    }];
    return value;
  };
  const nowSeconds = Math.floor(Date.now() / 1000);
  const persistedOverlap = makeChunkSync(9, 1, [468123600, -740123300, nowSeconds - 5, 100, 9, 7]);
  await problem(
    await post("/functions/v1/device-v1-sync", persistedOverlap, { authorization: bearer }),
    422,
    "invalid_telemetry",
    persistedOverlap.request_id,
  );

  const lossMarker = {
    marker_id: randomUUID(),
    boot_sequence: 42,
    first_missing_point_sequence: 4,
    last_missing_point_sequence: 5,
    lost_points: 2,
    reason: "storage_pressure",
    recorded_utc_ms: Date.now(),
  };
  const integritySync = emptySync();
  integritySync.upload.chunks = [
    makeChunkSync(10, 6, [468123700, -740123100, nowSeconds, 110, 9, 7]).upload.chunks[0],
    makeChunkSync(9, 3, [468123600, -740123300, nowSeconds - 5, 100, 9, 7]).upload.chunks[0],
    makeChunkSync(1, 0, [468123500, -740123400, nowSeconds, 90, 8, 7], 43, true).upload.chunks[0],
  ];
  integritySync.upload.loss_markers = [lossMarker];
  const integrityResult = await json(
    await post("/functions/v1/device-v1-sync", integritySync, { authorization: bearer }),
  );
  assert.deepEqual(integrityResult.telemetry.accepted_loss_marker_ids, [lossMarker.marker_id]);

  const afterFinal = makeChunkSync(2, 1, [468123510, -740123390, nowSeconds + 1, 90, 8, 7], 43);
  await problem(
    await post("/functions/v1/device-v1-sync", afterFinal, { authorization: bearer }),
    422,
    "invalid_telemetry",
    afterFinal.request_id,
  );

  const artifactReplay = emptySync();
  artifactReplay.upload.summaries = [structuredClone(sync.upload.summaries[0])];
  artifactReplay.upload.loss_markers = [structuredClone(lossMarker)];
  const replayedArtifacts = await json(
    await post("/functions/v1/device-v1-sync", artifactReplay, { authorization: bearer }),
  );
  assert.deepEqual(replayedArtifacts.telemetry.accepted_summary_ids, [sync.upload.summaries[0].summary_id]);
  assert.deepEqual(replayedArtifacts.telemetry.accepted_loss_marker_ids, [lossMarker.marker_id]);

  const changedSummary = emptySync();
  changedSummary.upload.summaries = [structuredClone(sync.upload.summaries[0])];
  changedSummary.upload.summaries[0].distance_m += 1;
  await problem(
    await post("/functions/v1/device-v1-sync", changedSummary, { authorization: bearer }),
    409,
    "request_id_reused",
    changedSummary.request_id,
  );

  const changedLoss = emptySync();
  changedLoss.upload.loss_markers = [{ ...lossMarker, reason: "corrupt_chunk" }];
  await problem(
    await post("/functions/v1/device-v1-sync", changedLoss, { authorization: bearer }),
    409,
    "request_id_reused",
    changedLoss.request_id,
  );

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
    `${apiUrl}/rest/v1/telemetry_points?collar_id=eq.${paired.pairing.collar_id}&select=boot_sequence,point_sequence&order=boot_sequence.asc,point_sequence.asc`,
    {
      headers: {
        apikey: publishableKey,
        authorization: `Bearer ${login.access_token}`,
        "accept-profile": "api",
      },
    },
  ).then(json);
  assert.deepEqual(points, [
    { boot_sequence: 42, point_sequence: 0 },
    { boot_sequence: 42, point_sequence: 1 },
    { boot_sequence: 42, point_sequence: 2 },
    { boot_sequence: 42, point_sequence: 3 },
    { boot_sequence: 42, point_sequence: 6 },
    { boot_sequence: 43, point_sequence: 0 },
  ], "out-of-order chunks remain independently addressable inside each boot namespace");

  const revoke = await readFixture("device-v1-revoke-request.json");
  revoke.request_id = randomUUID();
  revoke.device_id = deviceId;
  revoke.credential_id = credentialId;
  const raceSync = emptySync();
  const [raceSyncResponse, revokeResponse] = await Promise.all([
    post("/functions/v1/device-v1-sync", raceSync, { authorization: bearer }),
    post("/functions/v1/device-v1-revoke", revoke, { authorization: bearer }),
  ]);
  const revoked = await json(revokeResponse);
  assert.equal(revoked.state, "revoked");
  if (raceSyncResponse.ok) {
    await json(raceSyncResponse);
  } else {
    await problem(raceSyncResponse, 403, "device_revoked", raceSync.request_id);
  }
  const afterRevoke = emptySync();
  await problem(
    await post("/functions/v1/device-v1-sync", afterRevoke, { authorization: bearer }),
    403,
    "device_revoked",
    afterRevoke.request_id,
  );

  const lwwIssue = structuredClone(claimIssue);
  lwwIssue.request_id = randomUUID();
  const lwwIssued = await json(await post("/functions/v1/user-v1-issue-claim", lwwIssue, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
  }));
  const lwwClaim = await readFixture("device-v1-claim-request.json");
  const lwwDeviceId = "ffffffff-ffff-4fff-bfff-ffffffffffff";
  const lwwCredentialId = randomUUID();
  const lwwSecret = randomBytes(32).toString("base64url");
  lwwClaim.request_id = randomUUID();
  lwwClaim.claim_code = lwwIssued.claim.code;
  lwwClaim.credential_id = lwwCredentialId;
  lwwClaim.credential_secret = lwwSecret;
  lwwClaim.device.device_id = lwwDeviceId;
  const lwwPaired = await json(await post("/functions/v1/device-v1-claim", lwwClaim));
  const lwwBearer = `Bearer drgb_v1_${lwwCredentialId}.${lwwSecret}`;
  const lwwSync = () => {
    const value = structuredClone(sync);
    value.request_id = randomUUID();
    value.device.device_id = lwwDeviceId;
    value.upload = { chunks: [], summaries: [], loss_markers: [] };
    value.configuration = { mutations: [], reported: [] };
    return value;
  };
  const webConfig = async (resourceKey, body, baseServerVersion) => json(await post(
    "/rest/v1/rpc/mutate_config_resource_v1",
    {
      p_collar_id: lwwPaired.pairing.collar_id,
      p_resource_key: resourceKey,
      p_resource_schema: 1,
      p_mutation_id: randomUUID(),
      p_base_server_version: baseServerVersion,
      p_body: body,
      p_body_sha256: `\\x${Buffer.from(bodyHash(body), "base64url").toString("hex")}`,
    },
    {
      apikey: publishableKey,
      authorization: `Bearer ${login.access_token}`,
      "content-profile": "api",
    },
  ));
  const configHead = async (resourceKey) => {
    const result = await fetch(
      `${apiUrl}/rest/v1/config_resource_heads?collar_id=eq.${lwwPaired.pairing.collar_id}` +
        `&resource_key=eq.${resourceKey}&select=resource_key,server_version,body,accepted_hlc_physical_ms,accepted_hlc_logical,accepted_actor_id`,
      {
        headers: {
          apikey: publishableKey,
          authorization: `Bearer ${login.access_token}`,
          "accept-profile": "api",
        },
      },
    ).then(json);
    assert.equal(result.length, 1, `missing ${resourceKey} head`);
    return result[0];
  };
  const deviceMutation = (resourceKey, body, localSequence, authoredHlc, timeQuality, baseServerVersion) => ({
    mutation_id: randomUUID(),
    local_sequence: localSequence,
    resource_key: resourceKey,
    resource_schema: 1,
    base_server_version: baseServerVersion,
    authored_hlc: { ...authoredHlc, actor_id: lwwDeviceId },
    time_quality: timeQuality,
    origin: "ap",
    body,
    body_sha256: bodyHash(body),
  });

  await webConfig("brightness", { brightness: 80 }, 0);
  const webFirst = await configHead("brightness");
  const webThenAp = lwwSync();
  webThenAp.configuration.mutations = [deviceMutation(
    "brightness", { brightness: 100 }, 1,
    { physical_ms: webFirst.accepted_hlc_physical_ms + 1, logical: 0 },
    "sntp_synced", webFirst.server_version,
  )];
  const webThenApResult = await json(await post("/functions/v1/device-v1-sync", webThenAp, { authorization: lwwBearer }));
  assert.equal(webThenApResult.configuration.outcomes[0].disposition, "winning");
  assert.equal(webThenApResult.configuration.outcomes[0].ordering, "authored");
  assert.deepEqual((await configHead("brightness")).body, { brightness: 100 }, "later trusted AP must beat web");

  const apVisual = lwwSync();
  const visualBody = { day_mode_enabled: true, mode: "speed" };
  apVisual.configuration.mutations = [deviceMutation(
    "visual_mode", visualBody, 2,
    { physical_ms: Date.now(), logical: 0 }, "gnss_trusted", 0,
  )];
  assert.equal(
    (await json(await post("/functions/v1/device-v1-sync", apVisual, { authorization: lwwBearer })))
      .configuration.outcomes[0].disposition,
    "winning",
  );
  const webVisualBody = { day_mode_enabled: false, mode: "show" };
  await webConfig("visual_mode", webVisualBody, 1);
  assert.deepEqual((await configHead("visual_mode")).body, webVisualBody, "web must beat an earlier trusted AP edit");

  const gpsBody = {
    hdop_factor: 1.5,
    max_gga_age_ms: 3000,
    max_hdop: 4,
    max_min_segment_m: 20,
    min_fix_quality: 1,
    min_satellites: 5,
    min_segment_m: 2,
  };
  await webConfig("gps_quality", gpsBody, 0);
  assert.deepEqual((await configHead("brightness")).body, { brightness: 100 });
  assert.deepEqual((await configHead("gps_quality")).body, gpsBody, "different LWW resources must survive independently");

  const beforeTie = await configHead("brightness");
  await webConfig("brightness", { brightness: 90 }, beforeTie.server_version);
  const tiedWeb = await configHead("brightness");
  const actorTie = lwwSync();
  actorTie.configuration.mutations = [deviceMutation(
    "brightness", { brightness: 110 }, 3,
    {
      physical_ms: tiedWeb.accepted_hlc_physical_ms,
      logical: tiedWeb.accepted_hlc_logical,
    },
    "server_anchored", tiedWeb.server_version,
  )];
  const actorTieResult = await json(await post("/functions/v1/device-v1-sync", actorTie, { authorization: lwwBearer }));
  assert.equal(actorTieResult.configuration.outcomes[0].disposition, "winning");
  assert.equal(actorTieResult.configuration.outcomes[0].accepted_hlc.actor_id, lwwDeviceId);
  assert.deepEqual((await configHead("brightness")).body, { brightness: 110 }, "actor UUID must break a full HLC tie");

  await webConfig("geofence_policy", { fence_max_m: 400 }, 0);
  const fallbackBatch = lwwSync();
  const unknownMutation = deviceMutation(
    "geofence_policy", { fence_max_m: 500 }, 7,
    { physical_ms: 0, logical: 1 }, "unknown", 1,
  );
  const implausibleTrusted = deviceMutation(
    "geofence_policy", { fence_max_m: 600 }, 9,
    { physical_ms: Date.now() + 700_000, logical: 0 }, "sntp_synced", 1,
  );
  fallbackBatch.configuration.mutations = [implausibleTrusted, unknownMutation];
  const fallbackResult = await json(await post("/functions/v1/device-v1-sync", fallbackBatch, { authorization: lwwBearer }));
  assert.deepEqual(
    fallbackResult.configuration.outcomes.map((outcome) => outcome.mutation_id),
    [unknownMutation.mutation_id, implausibleTrusted.mutation_id],
    "fallback mutations must use persisted local_sequence instead of array order",
  );
  assert.equal(fallbackResult.configuration.outcomes.every((outcome) => outcome.ordering === "fallback_received"), true);
  assert.equal(fallbackResult.configuration.outcomes.every(
    (outcome) => outcome.accepted_hlc.actor_id === "00000000-0000-4000-8000-0000000000ff"
  ), true);
  assert.equal(
    fallbackResult.configuration.outcomes[1].accepted_hlc.logical,
    fallbackResult.configuration.outcomes[0].accepted_hlc.logical + 1,
  );
  assert.deepEqual((await configHead("geofence_policy")).body, { fence_max_m: 600 });

  const mutationReplay = lwwSync();
  mutationReplay.configuration.mutations = [structuredClone(implausibleTrusted)];
  const mutationReplayResult = await json(
    await post("/functions/v1/device-v1-sync", mutationReplay, { authorization: lwwBearer }),
  );
  assert.equal(mutationReplayResult.configuration.outcomes[0].replayed, true);
  assert.deepEqual(
    mutationReplayResult.configuration.outcomes[0].accepted_hlc,
    fallbackResult.configuration.outcomes[1].accepted_hlc,
    "a mutation replay under a new request must preserve its accepted HLC",
  );

  const lwwRevoke = await readFixture("device-v1-revoke-request.json");
  lwwRevoke.request_id = randomUUID();
  lwwRevoke.device_id = lwwDeviceId;
  lwwRevoke.credential_id = lwwCredentialId;
  assert.equal(
    (await json(await post("/functions/v1/device-v1-revoke", lwwRevoke, { authorization: lwwBearer }))).state,
    "revoked",
  );

  const secondIssue = structuredClone(claimIssue);
  secondIssue.request_id = randomUUID();
  const secondIssued = await json(await post("/functions/v1/user-v1-issue-claim", secondIssue, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
  }));
  const secondClaim = await readFixture("device-v1-claim-request.json");
  const secondDeviceId = randomUUID();
  const secondCredentialId = randomUUID();
  const secondSecret = randomBytes(32).toString("base64url");
  secondClaim.request_id = randomUUID();
  secondClaim.claim_code = secondIssued.claim.code;
  secondClaim.credential_id = secondCredentialId;
  secondClaim.credential_secret = secondSecret;
  secondClaim.device.device_id = secondDeviceId;
  const secondPaired = await json(await post("/functions/v1/device-v1-claim", secondClaim));
  assert.equal(secondPaired.pairing.device_id, secondDeviceId);
  const secondBearer = `Bearer drgb_v1_${secondCredentialId}.${secondSecret}`;
  const rateProbe = () => {
    const value = structuredClone(sync);
    value.request_id = randomUUID();
    value.device.device_id = secondDeviceId;
    value.upload = { chunks: [], summaries: [], loss_markers: [] };
    value.configuration = { mutations: [], reported: [] };
    return value;
  };
  let replayableProbe;
  let replayableResponse;
  for (let attempt = 1; attempt <= 6; attempt += 1) {
    const probe = rateProbe();
    const response = await json(await post("/functions/v1/device-v1-sync", probe, { authorization: secondBearer }));
    if (attempt === 6) {
      replayableProbe = probe;
      replayableResponse = response;
    }
  }
  const limitedProbe = rateProbe();
  await problem(
    await post("/functions/v1/device-v1-sync", limitedProbe, { authorization: secondBearer }),
    429,
    "rate_limited",
    limitedProbe.request_id,
    60,
  );
  const exactReplayAfterLimit = await json(
    await post("/functions/v1/device-v1-sync", replayableProbe, { authorization: secondBearer }),
  );
  assert.deepEqual(
    exactReplayAfterLimit,
    replayableResponse,
    "an exact committed replay must bypass the rate limit",
  );

  const secondRevoke = structuredClone(revoke);
  secondRevoke.request_id = randomUUID();
  secondRevoke.device_id = secondDeviceId;
  secondRevoke.credential_id = secondCredentialId;
  assert.equal(
    (await json(await post("/functions/v1/device-v1-revoke", secondRevoke, { authorization: secondBearer }))).state,
    "revoked",
  );

  const raceIssue = structuredClone(claimIssue);
  raceIssue.request_id = randomUUID();
  const raceIssued = await json(await post("/functions/v1/user-v1-issue-claim", raceIssue, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
  }));
  const raceClaim = await readFixture("device-v1-claim-request.json");
  const raceDeviceId = randomUUID();
  const raceCredentialId = randomUUID();
  const raceSecret = randomBytes(32).toString("base64url");
  raceClaim.request_id = randomUUID();
  raceClaim.claim_code = raceIssued.claim.code;
  raceClaim.credential_id = raceCredentialId;
  raceClaim.credential_secret = raceSecret;
  raceClaim.device.device_id = raceDeviceId;
  const raceBearer = `Bearer drgb_v1_${raceCredentialId}.${raceSecret}`;
  const claimRevoke = await readFixture("device-v1-revoke-request.json");
  claimRevoke.request_id = randomUUID();
  claimRevoke.device_id = raceDeviceId;
  claimRevoke.credential_id = raceCredentialId;

  const [claimRaceResponse, revokeRaceResponse] = await Promise.all([
    post("/functions/v1/device-v1-claim", raceClaim),
    post("/functions/v1/device-v1-revoke", claimRevoke, { authorization: raceBearer }),
  ]);
  const racePaired = await json(claimRaceResponse);
  let firstRaceRevoke = null;
  if (revokeRaceResponse.ok) {
    firstRaceRevoke = await json(revokeRaceResponse);
  } else {
    await problem(revokeRaceResponse, 401, "device_credential_invalid", claimRevoke.request_id);
  }

  const settledRaceRevoke = await json(
    await post("/functions/v1/device-v1-revoke", claimRevoke, { authorization: raceBearer }),
  );
  assert.equal(settledRaceRevoke.disposition, "newly_revoked");
  if (firstRaceRevoke !== null) {
    assert.deepEqual(
      settledRaceRevoke,
      firstRaceRevoke,
      "an exact revoke replay after a concurrent claim must preserve its first committed response",
    );
  }
  const claimReplayAfterRevoke = await json(
    await post("/functions/v1/device-v1-claim", raceClaim),
  );
  assert.deepEqual(
    claimReplayAfterRevoke,
    racePaired,
    "an exact claim replay must preserve its committed response after a concurrent revoke",
  );
  const racedCollar = await fetch(
    `${apiUrl}/rest/v1/collars?id=eq.${racePaired.pairing.collar_id}&select=state`,
    {
      headers: {
        apikey: publishableKey,
        authorization: `Bearer ${login.access_token}`,
        "accept-profile": "api",
      },
    },
  ).then(json);
  assert.deepEqual(racedCollar, [{ state: "revoked" }], "the claim/revoke race must settle revoked");

  for (let attempt = 1; attempt <= 4; attempt += 1) {
    const invalidClaim = await readFixture("device-v1-claim-request.json");
    invalidClaim.request_id = randomUUID();
    invalidClaim.claim_code = randomBytes(10).toString("hex").toUpperCase()
      .replaceAll("I", "A").replaceAll("L", "B").replaceAll("O", "C").replaceAll("U", "D")
      .slice(0, 16);
    invalidClaim.device.device_id = randomUUID();
    invalidClaim.credential_id = randomUUID();
    invalidClaim.credential_secret = randomBytes(32).toString("base64url");
    await problem(
      await post("/functions/v1/device-v1-claim", invalidClaim),
      401,
      "claim_unavailable",
      invalidClaim.request_id,
    );
  }
  const cooldownClaim = await readFixture("device-v1-claim-request.json");
  cooldownClaim.request_id = randomUUID();
  cooldownClaim.claim_code = "0123456789ABCDEF";
  cooldownClaim.device.device_id = randomUUID();
  cooldownClaim.credential_id = randomUUID();
  cooldownClaim.credential_secret = randomBytes(32).toString("base64url");
  const cooldownResponse = await post("/functions/v1/device-v1-claim", cooldownClaim);
  await problem(cooldownResponse, 429, "rate_limited", cooldownClaim.request_id);
  const claimRetryAfter = Number(cooldownResponse.headers.get("retry-after"));
  assert.equal(
    Number.isInteger(claimRetryAfter) && claimRetryAfter >= 895 && claimRetryAfter <= 900,
    true,
    `unexpected claim Retry-After: ${claimRetryAfter}`,
  );

  const claimReplayDuringCooldown = await json(await sendExactClaim());
  assert.deepEqual(
    claimReplayDuringCooldown,
    paired,
    "an exact committed claim replay must bypass a later source cooldown",
  );

  console.log(JSON.stringify({
    ok: true,
    scenarios: [
      "pair", "one-active-claim", "concurrent-claim-replay", "claim-request-id-conflict",
      "schema-invalid-envelope", "unsupported-protocol-problem",
      "concurrent-exact-replay", "lost-response", "single-database-effect",
      "persisted-overlap-rejected", "out-of-order-upload", "loss-marker-persisted",
      "persisted-finality", "artifact-replay", "artifact-identity-conflict",
      "ap-mutation", "web-winner", "reported-applied",
      "lww-web-then-trusted-ap", "lww-trusted-ap-then-web",
      "lww-independent-resources", "lww-actor-tie-break",
      "lww-unknown-time-fallback", "lww-implausible-clock-fallback-order",
      "lww-mutation-replay",
      "revoke-sync-race", "revoked-credential", "per-collar-rate-limit",
      "retry-after", "exact-replay-after-rate-limit", "revoke",
      "concurrent-claim-revoke", "claim-replay-after-revoke",
      "failed-claim-accounting", "claim-source-cooldown", "claim-replay-during-cooldown",
    ],
    points: points.length,
  }));
}

await main();
