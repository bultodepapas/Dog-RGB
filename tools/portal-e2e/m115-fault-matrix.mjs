import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash, randomBytes, randomUUID } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";

const OWNER_EMAIL = "owner@example.test";
const OWNER_PASSWORD = "local-owner-password";
const OWNER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "30000000-0000-4000-8000-000000000003";
const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const root = new URL("../../", import.meta.url);
const claimFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/device-v1-claim-request.json",
  root,
);
const syncFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/device-v1-sync-request.json",
  root,
);
const gpsQualityFixtureUrl = new URL(
  "contracts/device-v1/fixtures/valid/config-gps-quality.json",
  root,
);

export const M115_FAULTS = Object.freeze([
  "committed-response-loss",
  "exact-resend-after-restart",
  "same-id-different-body",
  "out-of-order-chunks",
  "overlapping-chunk",
  "revoked-credential",
  "sync-revoke-sync-first",
  "sync-revoke-revoke-first",
  "stale-desired-version",
  "unknown-clock",
]);

export const M115_CHECKPOINTS = Object.freeze([
  "response-lost-after-commit",
  "exact-replay-after-preserved-restart",
  "same-id-conflict-zero-effect",
  "separate-request-out-of-order-accepted",
  "persisted-overlap-zero-effect",
  "unknown-clock-server-receipt",
  "stale-report-keeps-current-desired-pending",
  "revoked-credential-zero-effect",
  "forced-sync-first-and-revoke-first",
]);

function assertLocalUrl(apiUrl) {
  const url = new URL(apiUrl);
  if (
    url.protocol !== "http:" ||
    (url.hostname !== "127.0.0.1" && url.hostname !== "localhost")
  ) {
    throw new Error("M1.15 refused a non-local Supabase stack.");
  }
}

function canonicalJson(value) {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  return `{${Object.keys(value).sort().map((key) =>
    `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(",")}}`;
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

function bodyHash(body) {
  return createHash("sha256").update(canonicalJson(body)).digest("base64url");
}

function rawHash(raw) {
  return createHash("sha256").update(raw).digest("hex");
}

function byteaHex(base64url) {
  return `\\x${Buffer.from(base64url, "base64url").toString("hex")}`;
}

function sqlUuid(value) {
  if (!UUID_PATTERN.test(value)) throw new Error("M1.15 rejected an unsafe UUID.");
  return `'${value}'::uuid`;
}

function run(command, args, options = {}) {
  const executable = process.platform === "win32" && command === "docker"
    ? "docker.exe"
    : command;
  const result = spawnSync(executable, args, {
    encoding: "utf8",
    maxBuffer: 4 * 1024 * 1024,
    timeout: 15_000,
    ...options,
  });
  if (result.status !== 0) {
    throw new Error(`M1.15 local command failed: ${command} ${args[0] ?? ""}.`);
  }
  return result.stdout.trim();
}

function databaseContainer() {
  const ids = run("docker", [
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ]).split(/\r?\n/u).filter(Boolean);
  if (ids.length !== 1) {
    throw new Error("M1.15 could not locate the repository-owned local database.");
  }
  return ids[0];
}

function psql(sql) {
  return run("docker", [
    "exec", "-i", databaseContainer(),
    "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
    "-U", "supabase_admin", "-d", "postgres",
  ], { input: sql });
}

function queryJson(sql) {
  const output = psql(sql);
  try {
    return JSON.parse(output);
  } catch {
    throw new Error("M1.15 received an invalid persisted-state checkpoint.");
  }
}

async function requestRaw(apiUrl, path, raw, headers = {}) {
  return fetch(`${apiUrl}${path}`, {
    method: "POST",
    headers: {
      accept: "application/json, application/problem+json",
      "content-type": "application/json",
      "content-length": String(Buffer.byteLength(raw)),
      ...headers,
    },
    body: raw,
    signal: AbortSignal.timeout(15_000),
  });
}

async function readJson(response) {
  const raw = await response.text();
  try {
    return JSON.parse(raw);
  } catch {
    throw new Error(`M1.15 endpoint returned non-JSON HTTP ${response.status}.`);
  }
}

function assertNoStore(response) {
  const directives = response.headers.get("cache-control")
    ?.split(",")
    .map((directive) => directive.trim().toLowerCase());
  assert.equal(directives?.includes("no-store"), true, "M1.15 response must be no-store");
}

async function success(response, requestId) {
  assertNoStore(response);
  assert.equal(response.status, 200, `M1.15 expected HTTP 200, received ${response.status}`);
  assert.equal(response.headers.get("content-type")?.startsWith("application/json"), true);
  const body = await readJson(response);
  assert.equal(body.protocol_version, 1);
  assert.equal(body.request_id, requestId);
  return body;
}

async function problem(response, status, code, requestId) {
  assertNoStore(response);
  assert.equal(response.status, status, `M1.15 expected ${status} ${code}`);
  assert.equal(
    response.headers.get("content-type")?.startsWith("application/problem+json"),
    true,
  );
  const body = await readJson(response);
  assert.deepEqual(Object.keys(body).sort(), [
    "code", "detail", "request_id", "status", "title", "type",
  ]);
  assert.equal(body.code, code);
  assert.equal(body.status, status);
  assert.equal(body.request_id, requestId);
  assert.equal(response.headers.has("retry-after"), false);
  return body;
}

async function postJson(apiUrl, path, body, headers = {}) {
  const raw = JSON.stringify(body);
  const response = await requestRaw(apiUrl, path, raw, headers);
  const value = await readJson(response);
  if (!response.ok) {
    throw new Error(`M1.15 fixture setup failed at ${path} with HTTP ${response.status}.`);
  }
  return value;
}

async function prepareDevice({ apiUrl, publishableKey }) {
  const login = await postJson(apiUrl, "/auth/v1/token?grant_type=password", {
    email: OWNER_EMAIL,
    password: OWNER_PASSWORD,
  }, { apikey: publishableKey });
  assert.equal(typeof login.access_token, "string");

  const issued = await postJson(apiUrl, "/functions/v1/user-v1-issue-claim", {
    protocol_version: 1,
    request_id: randomUUID(),
    dog_id: DOG_ID,
  }, {
    apikey: publishableKey,
    authorization: `Bearer ${login.access_token}`,
  });
  assert.equal(typeof issued.claim?.code, "string");

  const claim = JSON.parse(await readFile(claimFixtureUrl, "utf8"));
  const deviceId = randomUUID();
  const credentialId = randomUUID();
  const credentialSecret = randomBytes(32).toString("base64url");
  claim.request_id = randomUUID();
  claim.claim_code = issued.claim.code;
  claim.credential_id = credentialId;
  claim.credential_secret = credentialSecret;
  claim.device.device_id = deviceId;
  const paired = await postJson(apiUrl, "/functions/v1/device-v1-claim", claim);
  assert.equal(paired.pairing?.device_id, deviceId);
  assert.equal(paired.pairing?.credential_id, credentialId);
  assert.equal(paired.pairing?.dog_id, DOG_ID);

  return Object.freeze({
    accessToken: login.access_token,
    bearer: `Bearer drgb_v1_${credentialId}.${credentialSecret}`,
    capabilityHash: claim.device.capability_hash,
    collarId: paired.pairing.collar_id,
    credentialId,
    credentialSecret,
    deviceId,
    claimCode: issued.claim.code,
  });
}

function makeChunk({ bootSequence, chunkSequence, firstPointSequence, points, timeQuality = 4 }) {
  return {
    telemetry_schema: 3,
    boot_sequence: bootSequence,
    chunk_sequence: chunkSequence,
    first_point_sequence: firstPointSequence,
    point_count: points.length,
    time_quality: timeQuality,
    content_sha256: pointHash(points),
    is_final: false,
    points,
  };
}

function createPendingSync(requestBody, expectedChunk = null) {
  const retained = Object.freeze({
    requestId: requestBody.request_id,
    raw: JSON.stringify(requestBody),
    rawSha256: rawHash(JSON.stringify(requestBody)),
  });
  let state = "pending";
  return Object.freeze({
    get pending() { return state === "pending"; },
    get requestId() { return retained.requestId; },
    get rawSha256() { return retained.rawSha256; },
    requestBytes() {
      assert.equal(state, "pending", "M1.15 reclaimed a request before its retry completed");
      return retained.raw;
    },
    async discardSuccess(response) {
      assert.equal(state, "pending");
      assertNoStore(response);
      assert.equal(response.status, 200);
      await response.body?.cancel();
      assert.equal(state, "pending", "M1.15 reclaimed a request from response headers alone");
    },
    async accept(response, storedResponse) {
      assert.equal(state, "pending");
      const body = await success(response, retained.requestId);
      if (expectedChunk !== null) exactAcceptedChunk(body, expectedChunk);
      assert.deepEqual(body, storedResponse);
      state = "acknowledged";
      return body;
    },
    async retainAfterProblem(response, status, code) {
      assert.equal(state, "pending");
      await problem(response, status, code, retained.requestId);
      assert.equal(state, "pending", "M1.15 reclaimed a permanently denied pending request");
      assert.equal(rawHash(retained.raw), retained.rawSha256);
    },
  });
}

function makeSyncFactory(fixture, device, cycle) {
  const base = structuredClone(fixture);
  base.device.device_id = device.deviceId;
  base.device.capability_hash = device.capabilityHash;
  base.device.boot_sequence = 500 + cycle;
  base.capabilities = null;

  return ({ chunks = [], mutations = [], reported = [], unknownClock = false } = {}) => {
    const request = structuredClone(base);
    request.request_id = randomUUID();
    request.clock = unknownClock
      ? { utc_ms: null, quality: "unknown", uncertainty_ms: null }
      : { utc_ms: Date.now(), quality: "sntp_synced", uncertainty_ms: 2_500 };
    request.upload = { chunks, summaries: [], loss_markers: [] };
    request.configuration = { mutations, reported };
    request.diagnostics = {
      outbox_chunks: chunks.length,
      outbox_points: chunks.reduce((total, chunk) => total + chunk.point_count, 0),
      outbox_used_bytes: chunks.reduce((total, chunk) => total + chunk.point_count * 16, 0),
      outbox_capacity_bytes: 1_376_256,
      oldest_unacknowledged_utc_ms: chunks.length === 0 || unknownClock
        ? null
        : Math.min(...chunks.flatMap((chunk) => chunk.points.map((point) => point[2] * 1_000))),
      dropped_points_total: 0,
      last_error_code: null,
    };
    return request;
  };
}

function collarSnapshot(collarId) {
  return queryJson(`
    select jsonb_build_object(
      'collar', (
        select to_jsonb(c) - 'display_name' - 'device_public_id' - 'dog_id'
        from api.collars c where c.id = ${sqlUuid(collarId)}
      ),
      'credentials', coalesce((
        select jsonb_agg(to_jsonb(dc) - 'secret_digest' order by dc.credential_id)
        from private.device_credentials dc where dc.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'receipts', coalesce((
        select jsonb_agg(jsonb_build_object(
          'request_id', sr.request_id,
          'request_sha256', encode(sr.request_sha256, 'hex'),
          'protocol_version', sr.protocol_version,
          'received_at', sr.received_at,
          'committed_at', sr.committed_at,
          'status', sr.status,
          'response', sr.response_json
        ) order by sr.request_id)
        from private.sync_requests sr where sr.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'chunks', coalesce((
        select jsonb_agg(to_jsonb(tc) order by tc.boot_sequence, tc.chunk_sequence)
        from private.telemetry_chunks tc where tc.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'points', coalesce((
        select jsonb_agg(to_jsonb(tp) order by tp.boot_sequence, tp.point_sequence)
        from api.telemetry_points tp where tp.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'recordings', coalesce((
        select jsonb_agg(to_jsonb(r) order by r.boot_sequence)
        from api.recordings r where r.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'revisions', coalesce((
        select jsonb_agg(to_jsonb(cr) order by cr.resource_key, cr.server_version, cr.mutation_id)
        from api.config_revisions cr where cr.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'heads', coalesce((
        select jsonb_agg(to_jsonb(ch) order by ch.resource_key)
        from api.config_resource_heads ch where ch.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'reported', coalesce((
        select jsonb_agg(to_jsonb(cp) order by cp.resource_key)
        from api.config_reported cp where cp.collar_id = ${sqlUuid(collarId)}
      ), '[]'::jsonb),
      'hlc_state', (
        select to_jsonb(h) from private.config_hlc_state h
        where h.collar_id = ${sqlUuid(collarId)}
      )
    )::text;
  `);
}

function snapshotCounts(snapshot) {
  return Object.freeze({
    receipts: snapshot.receipts.length,
    chunks: snapshot.chunks.length,
    points: snapshot.points.length,
    recordings: snapshot.recordings.length,
    revisions: snapshot.revisions.length,
    heads: snapshot.heads.length,
    reported: snapshot.reported.length,
  });
}

function exactAcceptedChunk(response, chunk) {
  const accepted = response.telemetry?.accepted_chunks;
  assert.deepEqual(accepted, [{
    boot_sequence: chunk.boot_sequence,
    chunk_sequence: chunk.chunk_sequence,
    through_point_sequence: chunk.first_point_sequence + chunk.point_count - 1,
    accepted_point_count: chunk.point_count,
    content_sha256: chunk.content_sha256,
  }]);
  assert.deepEqual(response.telemetry.rejected_chunks, []);
}

async function invokeOwnerRpc(apiUrl, publishableKey, accessToken, name, body) {
  const response = await requestRaw(
    apiUrl,
    `/rest/v1/rpc/${name}`,
    JSON.stringify(body),
    {
      apikey: publishableKey,
      authorization: `Bearer ${accessToken}`,
      "content-profile": "api",
    },
  );
  const payload = await readJson(response);
  assert.equal(response.status, 200, `M1.15 owner RPC ${name} failed`);
  return Array.isArray(payload) ? payload[0] : payload;
}

function raceRequest({ deviceId, capabilityHash, requestId }) {
  return {
    protocol_version: 1,
    request_id: requestId,
    device: {
      device_id: deviceId,
      boot_sequence: 1,
      firmware_version: "2.0.0-cloud.1",
      hardware_revision: "xiao-s3-r1",
      telemetry_schema: 3,
      config_schema: 7,
      capability_hash: capabilityHash.toString("base64url"),
    },
    clock: { utc_ms: null, quality: "unknown", uncertainty_ms: null },
    capabilities: null,
    diagnostics: {
      outbox_chunks: 0,
      outbox_points: 0,
      outbox_used_bytes: 0,
      outbox_capacity_bytes: 4096,
      oldest_unacknowledged_utc_ms: null,
      dropped_points_total: 0,
      last_error_code: null,
    },
    upload: { chunks: [], summaries: [], loss_markers: [] },
    configuration: { mutations: [], reported: [] },
  };
}

function spawnPsqlSession(container) {
  const child = spawn(process.platform === "win32" ? "docker.exe" : "docker", [
    "exec", "-i", container,
    "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
    "-U", "supabase_admin", "-d", "postgres",
  ], { stdio: ["pipe", "pipe", "pipe"] });
  child.stdout.setEncoding("utf8");
  child.stderr.setEncoding("utf8");
  let stdout = "";
  let stderr = "";
  child.stdout.on("data", (chunk) => { stdout += chunk; });
  child.stderr.on("data", (chunk) => { stderr += chunk; });
  const closed = new Promise((resolve) => {
    child.on("close", (status) => resolve({ status, stdout, stderr }));
  });
  let inputEnded = false;
  return {
    child,
    closed,
    get inputEnded() { return inputEnded; },
    output: () => stdout,
    errors: () => stderr,
    write(sql) {
      if (inputEnded) throw new Error("M1.15 attempted to write a closed psql input.");
      child.stdin.write(sql);
    },
    end() {
      if (!inputEnded) {
        inputEnded = true;
        child.stdin.end();
      }
    },
  };
}

async function waitForOutput(session, marker, timeoutMs = 10_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (session.output().includes(marker)) return;
    if (session.child.exitCode !== null) {
      throw new Error("M1.15 lock-holder session exited before its checkpoint.");
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 25));
  }
  throw new Error("M1.15 lock-holder checkpoint timed out.");
}

async function closePsqlSession(session) {
  if (session.child.exitCode !== null) return;
  if (!session.inputEnded) {
    try {
      session.write("rollback;\n");
      session.end();
    } catch {
      // A concurrently closing psql process has already released its transaction.
    }
  }
  const closed = await Promise.race([
    session.closed.then(() => true),
    new Promise((resolveWait) => setTimeout(() => resolveWait(false), 1_000)),
  ]);
  if (!closed && session.child.exitCode === null) session.child.kill();
}

function asyncPsql(container, sql) {
  const session = spawnPsqlSession(container);
  session.write(sql);
  session.end();
  return session;
}

async function waitForBlocked(applicationName, blockerName, timeoutMs = 5_000) {
  if (![applicationName, blockerName].every((name) => /^[a-z0-9-]+$/u.test(name))) {
    throw new Error("M1.15 rejected an unsafe lock-waiter name.");
  }
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const count = Number(psql(`
      select count(*)
      from pg_stat_activity waiter
      where waiter.application_name = '${applicationName}'
        and exists (
          select 1
          from unnest(pg_blocking_pids(waiter.pid)) blocker(pid)
          join pg_stat_activity holder on holder.pid = blocker.pid
          where holder.application_name = '${blockerName}'
        );
    `));
    if (count === 1) return;
    await new Promise((resolveWait) => setTimeout(resolveWait, 25));
  }
  throw new Error("M1.15 did not observe the expected database lock waiter.");
}

function seedRaceFixtures(fixtures) {
  psql(`
    insert into api.collars (
      id, device_public_id, dog_id, display_name, state,
      protocol_version, hardware_revision, firmware_version,
      telemetry_schema, config_schema, capability_manifest, capability_hash, linked_at
    ) values
      ${fixtures.map((fixture, index) => `(
        ${sqlUuid(fixture.collarId)}, ${sqlUuid(fixture.deviceId)}, '${DOG_ID}',
        'M1.15 race ${index + 1}', 'active', 1, 'xiao-s3-r1', '2.0.0-cloud.1', 3, 7,
        '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7]}'::jsonb,
        decode('${fixture.capabilityHash.toString("hex")}', 'hex'), statement_timestamp()
      )`).join(",\n")};
    insert into private.device_credentials (credential_id, collar_id, secret_digest, state)
    values ${fixtures.map((fixture) => `(
      ${sqlUuid(fixture.credentialId)}, ${sqlUuid(fixture.collarId)},
      decode('${fixture.secretDigest.toString("hex")}', 'hex'), 'active'
    )`).join(",\n")};
  `);
}

function raceSql(fixture) {
  const requestId = randomUUID();
  const request = raceRequest({
    deviceId: fixture.deviceId,
    capabilityHash: fixture.capabilityHash,
    requestId,
  });
  const raw = JSON.stringify(request);
  return {
    requestId,
    requestSha256: rawHash(raw),
    sync: `select api.device_sync_gateway_v1(
      ${sqlUuid(fixture.credentialId)}, decode('${fixture.secretDigest.toString("hex")}', 'hex'),
      ${sqlUuid(requestId)}, decode('${rawHash(raw)}', 'hex'),
      $request$${raw}$request$::jsonb
    )::text;`,
    revoke: `
      set local role authenticated;
      set local "request.jwt.claim.sub" = '${OWNER_ID}';
      select api.revoke_collar_v1(${sqlUuid(fixture.collarId)});
    `,
  };
}

function singleJsonLine(output, label) {
  const jsonLines = output.split(/\r?\n/u).map((line) => line.trim()).filter((line) =>
    line.startsWith("{") && line.endsWith("}")
  );
  assert.equal(jsonLines.length, 1, `M1.15 ${label} emitted an unexpected result shape`);
  return JSON.parse(jsonLines[0]);
}

function assertRaceState(fixture, expectedReceipt, expectedSyncMetadata) {
  const state = queryJson(`
    select jsonb_build_object(
      'collar_state', c.state,
      'credential_state', dc.state,
      'matching_revocation', c.revoked_at is not null and c.revoked_at = dc.revoked_at,
      'receipt', (
        select jsonb_build_object(
          'request_id', sr.request_id,
          'request_sha256', encode(sr.request_sha256, 'hex'),
          'status', sr.status,
          'response', sr.response_json
        )
        from private.sync_requests sr where sr.collar_id = c.id
      ),
      'sync_metadata_present', c.last_sync_at is not null and dc.last_used_at is not null,
      'processing', (select count(*) from private.sync_requests sr where sr.collar_id = c.id and sr.status = 'processing'),
      'chunks', (select count(*) from private.telemetry_chunks tc where tc.collar_id = c.id),
      'points', (select count(*) from api.telemetry_points tp where tp.collar_id = c.id),
      'revisions', (select count(*) from api.config_revisions cr where cr.collar_id = c.id)
    )::text
    from api.collars c
    join private.device_credentials dc on dc.collar_id = c.id
    where c.id = ${sqlUuid(fixture.collarId)};
  `);
  assert.deepEqual(state, {
    collar_state: "revoked",
    credential_state: "revoked",
    matching_revocation: true,
    receipt: expectedReceipt,
    sync_metadata_present: expectedSyncMetadata,
    processing: 0,
    chunks: 0,
    points: 0,
    revisions: 0,
  });
}

async function runDeterministicRaces() {
  const fixtures = [1, 2].map(() => ({
    collarId: randomUUID(),
    deviceId: randomUUID(),
    credentialId: randomUUID(),
    secretDigest: randomBytes(32),
    capabilityHash: randomBytes(32),
  }));
  seedRaceFixtures(fixtures);
  const container = databaseContainer();

  const syncFirst = raceSql(fixtures[0]);
  const syncHolder = spawnPsqlSession(container);
  let revokeWaiter = null;
  let syncHolderResult;
  let revokeWaiterResult;
  try {
    syncHolder.write(`
      set application_name = 'm115-sync-holder';
      begin;
      set local statement_timeout = '10s';
      ${syncFirst.sync}
      select 'M115_SYNC_READY';
    `);
    await waitForOutput(syncHolder, "M115_SYNC_READY");
    revokeWaiter = asyncPsql(container, `
      set application_name = 'm115-revoke-waiter';
      begin;
      set local statement_timeout = '10s';
      ${syncFirst.revoke}
      commit;
    `);
    await waitForBlocked("m115-revoke-waiter", "m115-sync-holder");
    syncHolder.write("commit; select 'M115_SYNC_COMMITTED';\n");
    await waitForOutput(syncHolder, "M115_SYNC_COMMITTED");
    syncHolder.end();
    [syncHolderResult, revokeWaiterResult] = await Promise.all([
      syncHolder.closed,
      revokeWaiter.closed,
    ]);
  } finally {
    await closePsqlSession(syncHolder);
    if (revokeWaiter !== null) await closePsqlSession(revokeWaiter);
  }
  assert.equal(syncHolderResult.status, 0, "M1.15 sync-first holder failed");
  assert.equal(revokeWaiterResult.status, 0, "M1.15 sync-first revoke failed");
  const syncFirstAck = singleJsonLine(syncHolderResult.stdout, "sync-first holder");
  assert.equal(syncFirstAck.protocol_version, 1);
  assert.equal(syncFirstAck.request_id, syncFirst.requestId);
  assert.deepEqual(syncFirstAck.telemetry.accepted_chunks, []);
  assert.deepEqual(syncFirstAck.telemetry.rejected_chunks, []);
  assert.deepEqual(syncFirstAck.configuration.outcomes, []);
  assertRaceState(fixtures[0], {
    request_id: syncFirst.requestId,
    request_sha256: syncFirst.requestSha256,
    status: "committed",
    response: syncFirstAck,
  }, true);

  const revokeFirst = raceSql(fixtures[1]);
  const revokeHolder = spawnPsqlSession(container);
  let syncWaiter = null;
  let revokeHolderResult;
  let syncWaiterResult;
  try {
    revokeHolder.write(`
      set application_name = 'm115-revoke-holder';
      begin;
      set local statement_timeout = '10s';
      ${revokeFirst.revoke}
      select 'M115_REVOKE_READY';
    `);
    await waitForOutput(revokeHolder, "M115_REVOKE_READY");
    syncWaiter = asyncPsql(container, `
      \\set VERBOSITY verbose
      set application_name = 'm115-sync-waiter';
      begin;
      set local statement_timeout = '10s';
      ${revokeFirst.sync}
      commit;
    `);
    await waitForBlocked("m115-sync-waiter", "m115-revoke-holder");
    revokeHolder.write("commit; select 'M115_REVOKE_COMMITTED';\n");
    await waitForOutput(revokeHolder, "M115_REVOKE_COMMITTED");
    revokeHolder.end();
    [revokeHolderResult, syncWaiterResult] = await Promise.all([
      revokeHolder.closed,
      syncWaiter.closed,
    ]);
  } finally {
    await closePsqlSession(revokeHolder);
    if (syncWaiter !== null) await closePsqlSession(syncWaiter);
  }
  assert.equal(revokeHolderResult.status, 0, "M1.15 revoke-first holder failed");
  assert.notEqual(syncWaiterResult.status, 0, "M1.15 revoke-first sync unexpectedly committed");
  assert.equal(syncWaiterResult.stdout.trim(), "");
  assert.match(syncWaiterResult.stderr, /ERROR:\s+42501:\s+device_revoked/u);
  assert.doesNotMatch(
    `${syncHolderResult.stderr}\n${revokeWaiterResult.stderr}\n${revokeHolderResult.stderr}\n${syncWaiterResult.stderr}`,
    /deadlock|lock timeout|statement timeout/iu,
  );
  assertRaceState(fixtures[1], null, false);
}

function artifactFor(cycle, phase, checkpoints, counts) {
  return {
    schemaVersion: 1,
    phase,
    cycle,
    faults: M115_FAULTS,
    checkpoints,
    counts,
  };
}

export async function runM115FaultMatrix({
  apiUrl,
  publishableKey,
  cycle,
  artifactPath,
  restartLocalStack,
}) {
  assertLocalUrl(apiUrl);
  if (cycle !== 1 && cycle !== 2) throw new Error("M1.15 cycle must be 1 or 2.");
  if (typeof publishableKey !== "string" || publishableKey.length < 20) {
    throw new Error("M1.15 requires the local publishable key.");
  }
  if (typeof restartLocalStack !== "function") {
    throw new Error("M1.15 requires the runner-owned restart callback.");
  }

  const checkpoints = [];
  const emptyCounts = {
    receipts: 0,
    chunks: 0,
    points: 0,
    recordings: 0,
    revisions: 0,
    heads: 0,
    reported: 0,
    raceSchedules: 0,
  };
  const persistArtifact = async (phase, counts = emptyCounts) => {
    await writeFile(
      artifactPath,
      `${JSON.stringify(artifactFor(cycle, phase, checkpoints, counts), null, 2)}\n`,
      { encoding: "utf8", mode: 0o600 },
    );
  };
  const checkpoint = async (name) => {
    checkpoints.push(name);
    await persistArtifact("running");
  };

  await persistArtifact("running");
  try {
  const device = await prepareDevice({ apiUrl, publishableKey });
  const syncFixture = JSON.parse(await readFile(syncFixtureUrl, "utf8"));
  const gpsQualityFixture = JSON.parse(await readFile(gpsQualityFixtureUrl, "utf8"));
  const makeSync = makeSyncFactory(syncFixture, device, cycle);
  const now = Math.floor(Date.now() / 1_000);
  const primaryChunk = makeChunk({
    bootSequence: 510 + cycle,
    chunkSequence: 1,
    firstPointSequence: 0,
    points: [
      [468123456, -740123456, now - 10, 125, 9, 7],
      [468123500, -740123400, now - 5, 140, 9, 7],
      [468123500, -740123400, now, 0, 9, 13],
    ],
  });
  const initialBrightness = { brightness: 96 + cycle };
  const mutation = {
    mutation_id: randomUUID(),
    local_sequence: 1,
    resource_key: "brightness",
    resource_schema: 1,
    base_server_version: 0,
    authored_hlc: {
      physical_ms: Date.now(),
      logical: 0,
      actor_id: device.deviceId,
    },
    time_quality: "sntp_synced",
    origin: "ap",
    body: initialBrightness,
    body_sha256: bodyHash(initialBrightness),
  };
  const pendingBody = makeSync({ chunks: [primaryChunk], mutations: [mutation] });
  const pendingEntry = createPendingSync(pendingBody, primaryChunk);
  const committedRaw = pendingEntry.requestBytes();

  const lost = await requestRaw(apiUrl, "/functions/v1/device-v1-sync", pendingEntry.requestBytes(), {
    authorization: device.bearer,
  });
  await pendingEntry.discardSuccess(lost);
  const afterLost = collarSnapshot(device.collarId);
  assert.deepEqual(snapshotCounts(afterLost), {
    receipts: 1,
    chunks: 1,
    points: 3,
    recordings: 1,
    revisions: 1,
    heads: 1,
    reported: 0,
  });
  assert.equal(afterLost.receipts[0].status, "committed");
  assert.equal(afterLost.receipts[0].request_id, pendingEntry.requestId);
  assert.equal(afterLost.receipts[0].request_sha256, pendingEntry.rawSha256);
  assert.equal(pendingEntry.pending, true);
  await checkpoint("response-lost-after-commit");

  const restarted = await restartLocalStack();
  assert.equal(restarted.apiUrl, apiUrl);
  assert.equal(restarted.publishableKey, publishableKey);
  assert.deepEqual(collarSnapshot(device.collarId), afterLost);
  const replay = await pendingEntry.accept(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", pendingEntry.requestBytes(), {
      authorization: device.bearer,
    }),
    afterLost.receipts[0].response,
  );
  assert.equal(replay.configuration.outcomes[0].mutation_id, mutation.mutation_id);
  assert.equal(replay.configuration.outcomes[0].disposition, "winning");
  assert.equal(pendingEntry.pending, false);
  assert.deepEqual(collarSnapshot(device.collarId), afterLost);
  await checkpoint("exact-replay-after-preserved-restart");

  const changedBody = structuredClone(pendingBody);
  changedBody.diagnostics.dropped_points_total = 1;
  assert.notEqual(rawHash(JSON.stringify(changedBody)), pendingEntry.rawSha256);
  await problem(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", JSON.stringify(changedBody), {
      authorization: device.bearer,
    }),
    409,
    "request_id_reused",
    pendingEntry.requestId,
  );
  assert.deepEqual(collarSnapshot(device.collarId), afterLost);
  await checkpoint("same-id-conflict-zero-effect");

  const outOfOrderBoot = 520 + cycle;
  const laterChunk = makeChunk({
    bootSequence: outOfOrderBoot,
    chunkSequence: 2,
    firstPointSequence: 2,
    points: [[468124000, -740124000, now + 3, 110, 8, 7]],
  });
  const laterRequest = makeSync({ chunks: [laterChunk] });
  const laterRaw = JSON.stringify(laterRequest);
  const laterAck = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", laterRaw, {
      authorization: device.bearer,
    }),
    laterRequest.request_id,
  );
  exactAcceptedChunk(laterAck, laterChunk);

  const earlierChunk = makeChunk({
    bootSequence: outOfOrderBoot,
    chunkSequence: 1,
    firstPointSequence: 0,
    points: [
      [468123800, -740123800, now + 1, 90, 8, 7],
      [468123900, -740123900, now + 2, 100, 8, 7],
    ],
  });
  const earlierRequest = makeSync({ chunks: [earlierChunk] });
  const earlierRaw = JSON.stringify(earlierRequest);
  const earlierAck = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", earlierRaw, {
      authorization: device.bearer,
    }),
    earlierRequest.request_id,
  );
  exactAcceptedChunk(earlierAck, earlierChunk);
  const afterOutOfOrder = collarSnapshot(device.collarId);
  assert.deepEqual(
    afterOutOfOrder.chunks
      .filter((point) => point.boot_sequence === outOfOrderBoot)
      .map((chunk) => ({
        chunk_sequence: chunk.chunk_sequence,
        first_point_sequence: chunk.first_point_sequence,
        last_point_sequence: chunk.last_point_sequence,
        point_count: chunk.point_count,
        content_sha256: chunk.content_sha256,
        request_id: chunk.request_id,
      })),
    [
      {
        chunk_sequence: 1,
        first_point_sequence: 0,
        last_point_sequence: 1,
        point_count: 2,
        content_sha256: byteaHex(earlierChunk.content_sha256),
        request_id: earlierRequest.request_id,
      },
      {
        chunk_sequence: 2,
        first_point_sequence: 2,
        last_point_sequence: 2,
        point_count: 1,
        content_sha256: byteaHex(laterChunk.content_sha256),
        request_id: laterRequest.request_id,
      },
    ],
  );
  assert.deepEqual(
    afterOutOfOrder.points
      .filter((point) => point.boot_sequence === outOfOrderBoot)
      .map((point) => ({
        point_sequence: point.point_sequence,
        chunk_sequence: point.chunk_sequence,
      })),
    [
      { point_sequence: 0, chunk_sequence: 1 },
      { point_sequence: 1, chunk_sequence: 1 },
      { point_sequence: 2, chunk_sequence: 2 },
    ],
  );
  const outOfOrderRecording = afterOutOfOrder.recordings.find(
    (recording) => recording.boot_sequence === outOfOrderBoot,
  );
  assert.deepEqual({
    first_point_sequence: outOfOrderRecording.first_point_sequence,
    last_point_sequence: outOfOrderRecording.last_point_sequence,
    point_count: outOfOrderRecording.point_count,
    clock_quality: outOfOrderRecording.clock_quality,
  }, {
    first_point_sequence: 0,
    last_point_sequence: 2,
    point_count: 3,
    clock_quality: "gnss_trusted",
  });
  for (const [request, raw, ack] of [
    [laterRequest, laterRaw, laterAck],
    [earlierRequest, earlierRaw, earlierAck],
  ]) {
    const receipt = afterOutOfOrder.receipts.find((item) => item.request_id === request.request_id);
    assert.equal(receipt.status, "committed");
    assert.equal(receipt.request_sha256, rawHash(raw));
    assert.deepEqual(receipt.response, ack);
  }
  assert.deepEqual(snapshotCounts(afterOutOfOrder), {
    receipts: 3,
    chunks: 3,
    points: 6,
    recordings: 2,
    revisions: 1,
    heads: 1,
    reported: 0,
  });
  await checkpoint("separate-request-out-of-order-accepted");

  const overlapChunk = makeChunk({
    bootSequence: outOfOrderBoot,
    chunkSequence: 3,
    firstPointSequence: 1,
    points: [[468123950, -740123950, now + 2, 105, 8, 7]],
  });
  const overlapRequest = makeSync({ chunks: [overlapChunk] });
  await problem(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", JSON.stringify(overlapRequest), {
      authorization: device.bearer,
    }),
    422,
    "invalid_telemetry",
    overlapRequest.request_id,
  );
  assert.deepEqual(collarSnapshot(device.collarId), afterOutOfOrder);
  await checkpoint("persisted-overlap-zero-effect");

  const unknownChunk = makeChunk({
    bootSequence: 530 + cycle,
    chunkSequence: 1,
    firstPointSequence: 0,
    points: [[0, 0, 0, 65535, 0, 32]],
    timeQuality: 0,
  });
  const unknownMutation = {
    mutation_id: randomUUID(),
    local_sequence: 2,
    resource_key: gpsQualityFixture.resource_key,
    resource_schema: gpsQualityFixture.resource_schema,
    base_server_version: 0,
    authored_hlc: {
      physical_ms: 0,
      logical: 1,
      actor_id: device.deviceId,
    },
    time_quality: "unknown",
    origin: "ap",
    body: gpsQualityFixture.body,
    body_sha256: bodyHash(gpsQualityFixture.body),
  };
  const unknownRequest = makeSync({
    chunks: [unknownChunk],
    mutations: [unknownMutation],
    unknownClock: true,
  });
  const unknownRaw = JSON.stringify(unknownRequest);
  const unknownAck = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", unknownRaw, {
      authorization: device.bearer,
    }),
    unknownRequest.request_id,
  );
  exactAcceptedChunk(unknownAck, unknownChunk);
  const unknownServerTimeMs = Date.parse(unknownAck.server_time);
  assert.equal(Number.isFinite(unknownServerTimeMs), true);
  assert.equal(Number.isSafeInteger(unknownAck.server_hlc.physical_ms), true);
  assert.equal(Math.abs(unknownAck.server_hlc.physical_ms - unknownServerTimeMs) <= 5, true);
  assert.equal(unknownAck.server_hlc.logical, 0);
  assert.equal(unknownAck.server_hlc.actor_id, "00000000-0000-4000-8000-000000000001");
  const unknownOutcome = unknownAck.configuration.outcomes.find(
    (outcome) => outcome.mutation_id === unknownMutation.mutation_id,
  );
  assert.deepEqual({
    mutation_id: unknownOutcome.mutation_id,
    resource_key: unknownOutcome.resource_key,
    disposition: unknownOutcome.disposition,
    replayed: unknownOutcome.replayed,
    ordering: unknownOutcome.ordering,
    server_version: unknownOutcome.server_version,
    actor_id: unknownOutcome.accepted_hlc.actor_id,
    error_code: unknownOutcome.error_code,
  }, {
    mutation_id: unknownMutation.mutation_id,
    resource_key: "gps_quality",
    disposition: "winning",
    replayed: false,
    ordering: "fallback_received",
    server_version: 1,
    actor_id: "00000000-0000-4000-8000-0000000000ff",
    error_code: null,
  });
  assert.equal(Number.isSafeInteger(unknownOutcome.accepted_hlc.physical_ms), true);
  assert.equal(unknownOutcome.accepted_hlc.physical_ms > 0, true);
  assert.equal(Number.isSafeInteger(unknownOutcome.accepted_hlc.logical), true);
  const afterUnknown = collarSnapshot(device.collarId);
  const unknownPoints = afterUnknown.points.filter(
    (point) => point.boot_sequence === unknownChunk.boot_sequence,
  );
  const unknownRecordings = afterUnknown.recordings.filter(
    (recording) => recording.boot_sequence === unknownChunk.boot_sequence,
  );
  assert.equal(unknownPoints.length, 1);
  assert.equal(unknownRecordings.length, 1);
  assert.deepEqual({
    boot_sequence: unknownPoints[0].boot_sequence,
    point_sequence: unknownPoints[0].point_sequence,
    chunk_sequence: unknownPoints[0].chunk_sequence,
    recorded_at: unknownPoints[0].recorded_at,
    lat_e7: unknownPoints[0].lat_e7,
    lon_e7: unknownPoints[0].lon_e7,
    reported_speed_cmps: unknownPoints[0].reported_speed_cmps,
    satellites: unknownPoints[0].satellites,
    flags: unknownPoints[0].flags,
    time_quality: unknownPoints[0].time_quality,
  }, {
    boot_sequence: unknownChunk.boot_sequence,
    point_sequence: 0,
    chunk_sequence: 1,
    recorded_at: null,
    lat_e7: null,
    lon_e7: null,
    reported_speed_cmps: null,
    satellites: 0,
    flags: 32,
    time_quality: "unknown",
  });
  assert.equal(unknownPoints[0].received_at !== null, true);
  assert.deepEqual({
    boot_sequence: unknownRecordings[0].boot_sequence,
    started_at: unknownRecordings[0].started_at,
    ended_at: unknownRecordings[0].ended_at,
    state: unknownRecordings[0].state,
    first_point_sequence: unknownRecordings[0].first_point_sequence,
    last_point_sequence: unknownRecordings[0].last_point_sequence,
    point_count: unknownRecordings[0].point_count,
    clock_quality: unknownRecordings[0].clock_quality,
  }, {
    boot_sequence: unknownChunk.boot_sequence,
    started_at: null,
    ended_at: null,
    state: "open",
    first_point_sequence: 0,
    last_point_sequence: 0,
    point_count: 1,
    clock_quality: "unknown",
  });
  const unknownReceipt = afterUnknown.receipts.find(
    (receipt) => receipt.request_id === unknownRequest.request_id,
  );
  assert.equal(unknownReceipt.status, "committed");
  assert.equal(unknownReceipt.request_sha256, rawHash(unknownRaw));
  assert.deepEqual(unknownReceipt.response, unknownAck);
  const unknownRevision = afterUnknown.revisions.find(
    (revision) => revision.mutation_id === unknownMutation.mutation_id,
  );
  const unknownHead = afterUnknown.heads.find((head) => head.resource_key === "gps_quality");
  assert.deepEqual({
    submitted_hlc_physical_ms: unknownRevision.submitted_hlc_physical_ms,
    submitted_hlc_logical: unknownRevision.submitted_hlc_logical,
    submitted_actor_id: unknownRevision.submitted_actor_id,
    submitted_time_quality: unknownRevision.submitted_time_quality,
    accepted_hlc_physical_ms: unknownRevision.accepted_hlc_physical_ms,
    accepted_hlc_logical: unknownRevision.accepted_hlc_logical,
    accepted_actor_id: unknownRevision.accepted_actor_id,
    ordering_mode: unknownRevision.ordering_mode,
    disposition: unknownRevision.disposition,
    server_version: unknownRevision.server_version,
    body: unknownRevision.body,
    body_sha256: unknownRevision.body_sha256,
  }, {
    submitted_hlc_physical_ms: 0,
    submitted_hlc_logical: 1,
    submitted_actor_id: device.deviceId,
    submitted_time_quality: "unknown",
    accepted_hlc_physical_ms: unknownOutcome.accepted_hlc.physical_ms,
    accepted_hlc_logical: unknownOutcome.accepted_hlc.logical,
    accepted_actor_id: "00000000-0000-4000-8000-0000000000ff",
    ordering_mode: "fallback_received",
    disposition: "winning",
    server_version: 1,
    body: gpsQualityFixture.body,
    body_sha256: byteaHex(unknownMutation.body_sha256),
  });
  assert.deepEqual({
    resource_key: unknownHead.resource_key,
    server_version: unknownHead.server_version,
    body: unknownHead.body,
    body_sha256: unknownHead.body_sha256,
    accepted_hlc_physical_ms: unknownHead.accepted_hlc_physical_ms,
    accepted_hlc_logical: unknownHead.accepted_hlc_logical,
    accepted_actor_id: unknownHead.accepted_actor_id,
  }, {
    resource_key: "gps_quality",
    server_version: 1,
    body: gpsQualityFixture.body,
    body_sha256: byteaHex(unknownMutation.body_sha256),
    accepted_hlc_physical_ms: unknownOutcome.accepted_hlc.physical_ms,
    accepted_hlc_logical: unknownOutcome.accepted_hlc.logical,
    accepted_actor_id: "00000000-0000-4000-8000-0000000000ff",
  });
  assert.equal(afterUnknown.hlc_state.collar_id, device.collarId);
  assert.equal(afterUnknown.hlc_state.physical_ms, unknownOutcome.accepted_hlc.physical_ms);
  assert.equal(afterUnknown.hlc_state.logical, unknownOutcome.accepted_hlc.logical);
  assert.deepEqual(snapshotCounts(afterUnknown), {
    receipts: 4,
    chunks: 4,
    points: 7,
    recordings: 3,
    revisions: 2,
    heads: 2,
    reported: 0,
  });
  const unknownReplay = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", unknownRaw, {
      authorization: device.bearer,
    }),
    unknownRequest.request_id,
  );
  assert.deepEqual(unknownReplay, unknownAck);
  assert.deepEqual(collarSnapshot(device.collarId), afterUnknown);
  await checkpoint("unknown-clock-server-receipt");

  const initialDesiredRequest = makeSync();
  const initialDesiredRaw = JSON.stringify(initialDesiredRequest);
  const initialDesired = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", initialDesiredRaw, {
      authorization: device.bearer,
    }),
    initialDesiredRequest.request_id,
  );
  const desiredV1 = initialDesired.configuration.desired_resources.find(
    (resource) => resource.resource_key === "brightness",
  );
  assert.equal(desiredV1.server_version, 1);
  assert.deepEqual(desiredV1.body, initialBrightness);

  const desiredV2Body = { brightness: 140 + cycle };
  const desiredV2 = await invokeOwnerRpc(
    apiUrl,
    publishableKey,
    device.accessToken,
    "mutate_config_resource_v1",
    {
      p_collar_id: device.collarId,
      p_resource_key: "brightness",
      p_resource_schema: 1,
      p_mutation_id: randomUUID(),
      p_base_server_version: 1,
      p_body: desiredV2Body,
      p_body_sha256: `\\x${Buffer.from(bodyHash(desiredV2Body), "base64url").toString("hex")}`,
    },
  );
  assert.equal(Number(desiredV2.server_version), 2);
  assert.equal(desiredV2.body_sha256, bodyHash(desiredV2Body));

  const staleReportRequest = makeSync({
    reported: [{
      resource_key: "brightness",
      server_version: 1,
      body_sha256: desiredV1.body_sha256,
      status: "applied",
      error_code: null,
      device_applied_at: new Date().toISOString(),
    }],
  });
  const staleReportRaw = JSON.stringify(staleReportRequest);
  const staleReportAck = await success(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", staleReportRaw, {
      authorization: device.bearer,
    }),
    staleReportRequest.request_id,
  );
  const returnedV2 = staleReportAck.configuration.desired_resources.find(
    (resource) => resource.resource_key === "brightness",
  );
  assert.equal(returnedV2.server_version, 2);
  assert.deepEqual(returnedV2.body, desiredV2Body);
  assert.equal(returnedV2.body_sha256, bodyHash(desiredV2Body));
  const afterStaleReport = collarSnapshot(device.collarId);
  const brightnessHead = afterStaleReport.heads.find((head) => head.resource_key === "brightness");
  const brightnessReported = afterStaleReport.reported.find(
    (reported) => reported.resource_key === "brightness",
  );
  assert.deepEqual({
    resource_key: brightnessHead.resource_key,
    server_version: brightnessHead.server_version,
    body: brightnessHead.body,
    body_sha256: brightnessHead.body_sha256,
  }, {
    resource_key: "brightness",
    server_version: 2,
    body: desiredV2Body,
    body_sha256: byteaHex(bodyHash(desiredV2Body)),
  });
  assert.deepEqual({
    resource_key: brightnessReported.resource_key,
    reported_server_version: brightnessReported.reported_server_version,
    reported_body_sha256: brightnessReported.reported_body_sha256,
    status: brightnessReported.status,
    error_code: brightnessReported.error_code,
  }, {
    resource_key: "brightness",
    reported_server_version: 1,
    reported_body_sha256: byteaHex(desiredV1.body_sha256),
    status: "applied",
    error_code: null,
  });
  const initialDesiredReceipt = afterStaleReport.receipts.find(
    (receipt) => receipt.request_id === initialDesiredRequest.request_id,
  );
  const staleReportReceipt = afterStaleReport.receipts.find(
    (receipt) => receipt.request_id === staleReportRequest.request_id,
  );
  assert.equal(initialDesiredReceipt.request_sha256, rawHash(initialDesiredRaw));
  assert.equal(initialDesiredReceipt.status, "committed");
  assert.deepEqual(initialDesiredReceipt.response, initialDesired);
  assert.equal(staleReportReceipt.request_sha256, rawHash(staleReportRaw));
  assert.equal(staleReportReceipt.status, "committed");
  assert.deepEqual(staleReportReceipt.response, staleReportAck);
  assert.deepEqual(snapshotCounts(afterStaleReport), {
    receipts: 6,
    chunks: 4,
    points: 7,
    recordings: 3,
    revisions: 3,
    heads: 2,
    reported: 1,
  });
  await checkpoint("stale-report-keeps-current-desired-pending");

  const deniedRequest = makeSync();
  const deniedEntry = createPendingSync(deniedRequest);
  await invokeOwnerRpc(
    apiUrl,
    publishableKey,
    device.accessToken,
    "revoke_collar_v1",
    { p_collar_id: device.collarId },
  );
  const afterRevoke = collarSnapshot(device.collarId);
  assert.equal(afterRevoke.collar.state, "revoked");
  assert.equal(afterRevoke.credentials.every((credential) => credential.state === "revoked"), true);
  await deniedEntry.retainAfterProblem(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", deniedEntry.requestBytes(), {
      authorization: device.bearer,
    }),
    403,
    "device_revoked",
  );
  assert.equal(deniedEntry.pending, true);
  assert.equal(rawHash(deniedEntry.requestBytes()), deniedEntry.rawSha256);
  assert.deepEqual(collarSnapshot(device.collarId), afterRevoke);
  await problem(
    await requestRaw(apiUrl, "/functions/v1/device-v1-sync", committedRaw, {
      authorization: device.bearer,
    }),
    403,
    "device_revoked",
    pendingEntry.requestId,
  );
  assert.deepEqual(collarSnapshot(device.collarId), afterRevoke);
  await checkpoint("revoked-credential-zero-effect");

  await runDeterministicRaces();
  await checkpoint("forced-sync-first-and-revoke-first");

  const finalCounts = {
    ...snapshotCounts(afterRevoke),
    raceSchedules: 2,
  };
  await persistArtifact("passed", finalCounts);
  return Object.freeze({
    artifactContainsPrivateMaterial(value) {
      const text = Buffer.isBuffer(value) ? value.toString("utf8") : String(value);
      return [
        device.bearer,
        device.credentialId,
        device.credentialSecret,
        device.claimCode,
        device.deviceId,
        device.collarId,
        device.accessToken,
        pendingEntry.requestId,
        pendingEntry.rawSha256,
        deniedEntry.requestId,
        deniedEntry.rawSha256,
      ].some((secret) => text.includes(secret));
    },
    counts: Object.freeze(finalCounts),
  });
  } catch (error) {
    await persistArtifact("failed").catch(() => undefined);
    throw error;
  }
}
