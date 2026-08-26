import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash, createHmac, randomBytes, randomUUID } from "node:crypto";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const OWNER_ID = "10000000-0000-4000-8000-000000000001";
const OUTSIDER_ID = "20000000-0000-4000-8000-000000000002";
const EDITOR_ID = "19200000-0000-4000-8000-000000000001";
const VIEWER_ID = "19200000-0000-4000-8000-000000000002";
const RACE_COUNT = 4;

for (const name of ["SUPABASE_URL", "SUPABASE_PUBLISHABLE_KEY", "SUPABASE_JWT_SECRET"]) {
  if (!process.env[name]) throw new Error(`${name} is required.`);
}
const apiOrigin = new URL(process.env.SUPABASE_URL);
if (
  apiOrigin.protocol !== "http:" ||
  (apiOrigin.hostname !== "127.0.0.1" && apiOrigin.hostname !== "localhost")
) {
  throw new Error("Refusing to run the M1.12 fixture against a non-local Supabase stack.");
}

function invoke(command, args, options = {}) {
  const result = spawnSync(command, args, { encoding: "utf8", ...options });
  if (result.status !== 0) {
    throw new Error(
      `${command} ${args.join(" ")} failed: ${(result.stderr || result.stdout).trim()}`,
    );
  }
  return result.stdout.trim();
}

function databaseContainer() {
  const ids = invoke("docker", [
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ]).split(/\r?\n/u).filter(Boolean);
  if (ids.length !== 1) {
    throw new Error(`Expected one local Dog-RGB database container; found ${ids.length}.`);
  }
  return ids[0];
}

function psql(container, sql) {
  return invoke("docker", [
    "exec", "-i", container,
    "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
    "-U", "supabase_admin", "-d", "postgres",
  ], { input: sql });
}

function asyncPsql(container, sql) {
  return new Promise((resolve, reject) => {
    const child = spawn("docker", [
      "exec", "-i", container,
      "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
      "-U", "supabase_admin", "-d", "postgres",
    ], { stdio: ["pipe", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => { stdout += chunk; });
    child.stderr.on("data", (chunk) => { stderr += chunk; });
    child.on("error", reject);
    child.on("close", (status) => resolve({
      status,
      stdout: stdout.trim(),
      stderr: stderr.trim(),
    }));
    child.stdin.end(sql);
  });
}

function sqlUuid(value) {
  if (!/^[0-9a-f-]{36}$/u.test(value)) throw new Error("Unsafe UUID fixture.");
  return `'${value}'::uuid`;
}

function base64url(value) {
  return Buffer.from(value).toString("base64url");
}

function userToken(userId) {
  const now = Math.floor(Date.now() / 1_000);
  const header = base64url(JSON.stringify({ alg: "HS256", typ: "JWT" }));
  const payload = base64url(JSON.stringify({
    aud: "authenticated",
    exp: now + 300,
    iat: now,
    role: "authenticated",
    sub: userId,
  }));
  const signature = createHmac("sha256", process.env.SUPABASE_JWT_SECRET)
    .update(`${header}.${payload}`)
    .digest("base64url");
  return `${header}.${payload}.${signature}`;
}

function apiHeaders(token, method) {
  return {
    apikey: process.env.SUPABASE_PUBLISHABLE_KEY,
    ...(token ? { authorization: `Bearer ${token}` } : {}),
    ...(method === "GET"
      ? { "accept-profile": "api" }
      : { "content-profile": "api", "content-type": "application/json" }),
  };
}

async function request(path, { token, method = "GET", body } = {}) {
  const response = await fetch(`${process.env.SUPABASE_URL}/rest/v1/${path}`, {
    method,
    headers: apiHeaders(token, method),
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  const text = await response.text();
  let value = null;
  try {
    value = text ? JSON.parse(text) : null;
  } catch {
    value = { message: "non_json_response" };
  }
  return { status: response.status, body: value };
}

function assertDenied(result, label) {
  assert.equal([401, 403].includes(result.status), true, `${label} must be denied`);
  assert.notEqual(result.body, null, `${label} must return a bounded body`);
}

function syncRequest({ deviceId, requestId, capabilityHash }) {
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

async function raceSyncAndRevoke(container, fixture) {
  const requestId = randomUUID();
  const request = syncRequest({
    deviceId: fixture.deviceId,
    requestId,
    capabilityHash: fixture.capabilityHash,
  });
  const requestDigest = createHash("sha256").update(JSON.stringify(request)).digest("hex");
  const syncSql = `
    begin;
    set local statement_timeout = '5s';
    set local lock_timeout = '4s';
    select api.device_sync_gateway_v1(
      ${sqlUuid(fixture.credentialId)}, decode('${fixture.secretDigest.toString("hex")}', 'hex'),
      ${sqlUuid(requestId)}, decode('${requestDigest}', 'hex'),
      $request$${JSON.stringify(request)}$request$::jsonb
    )::text;
    commit;
  `;
  const revokeSql = `
    begin;
    set local statement_timeout = '5s';
    set local lock_timeout = '4s';
    set local role authenticated;
    set local "request.jwt.claim.sub" = '${OWNER_ID}';
    select api.revoke_collar_v1(${sqlUuid(fixture.collarId)});
    commit;
  `;
  const [sync, revoke] = await Promise.all([
    asyncPsql(container, syncSql),
    asyncPsql(container, revokeSql),
  ]);
  assert.equal(revoke.status, 0, `revoke race failed: ${revoke.stderr}`);
  assert.equal(revoke.stdout, "t", "owner revoke must confirm the selected collar");
  assert.equal(
    sync.status === 0 || sync.stderr.includes("device_revoked"),
    true,
    `sync must commit first or observe revocation: ${sync.stderr}`,
  );
  assert.doesNotMatch(`${sync.stderr}\n${revoke.stderr}`, /deadlock|lock timeout|statement timeout/iu);

  const state = JSON.parse(psql(container, `
    select jsonb_build_object(
      'collar_state', c.state,
      'collar_revoked', c.revoked_at is not null,
      'credential_state', dc.state,
      'credential_revoked', dc.revoked_at is not null,
      'processing_receipts', (
        select count(*) from private.sync_requests sr
        where sr.collar_id = c.id and sr.status = 'processing'
      )
    )::text
    from api.collars c
    join private.device_credentials dc on dc.collar_id = c.id
    where c.id = ${sqlUuid(fixture.collarId)};
  `));
  assert.deepEqual(state, {
    collar_state: "revoked",
    collar_revoked: true,
    credential_state: "revoked",
    credential_revoked: true,
    processing_receipts: 0,
  });
}

const container = databaseContainer();
const raceFixtures = Array.from({ length: RACE_COUNT }, () => ({
  collarId: randomUUID(),
  deviceId: randomUUID(),
  credentialId: randomUUID(),
  secretDigest: randomBytes(32),
  capabilityHash: randomBytes(32),
}));
const restFixture = {
  collarId: randomUUID(),
  deviceId: randomUUID(),
  credentialId: randomUUID(),
};
const tokens = {
  owner: userToken(OWNER_ID),
  editor: userToken(EDITOR_ID),
  viewer: userToken(VIEWER_ID),
  outsider: userToken(OUTSIDER_ID),
};

try {
  psql(container, `
    insert into auth.users (
      instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
      raw_app_meta_data, raw_user_meta_data, created_at, updated_at,
      confirmation_token, email_change, email_change_token_new, recovery_token
    ) values
      (
        '00000000-0000-0000-0000-000000000000', '${EDITOR_ID}',
        'authenticated', 'authenticated', 'm112-matrix-editor@example.test',
        extensions.crypt('local-only', extensions.gen_salt('bf')), statement_timestamp(),
        '{"provider":"email","providers":["email"]}', '{}',
        statement_timestamp(), statement_timestamp(), '', '', '', ''
      ),
      (
        '00000000-0000-0000-0000-000000000000', '${VIEWER_ID}',
        'authenticated', 'authenticated', 'm112-matrix-viewer@example.test',
        extensions.crypt('local-only', extensions.gen_salt('bf')), statement_timestamp(),
        '{"provider":"email","providers":["email"]}', '{}',
        statement_timestamp(), statement_timestamp(), '', '', '', ''
      )
    on conflict (id) do nothing;

    insert into api.dog_memberships (dog_id, user_id, role) values
      ('${DOG_ID}', '${EDITOR_ID}', 'editor'),
      ('${DOG_ID}', '${VIEWER_ID}', 'viewer')
    on conflict (dog_id, user_id) do update set role = excluded.role;

    insert into api.collars (
      id, device_public_id, dog_id, display_name, state,
      protocol_version, hardware_revision, firmware_version,
      telemetry_schema, config_schema, capability_manifest, capability_hash,
      linked_at, last_sync_at, diagnostics_observed_at,
      outbox_chunks, outbox_points, outbox_used_bytes, outbox_capacity_bytes,
      oldest_unacknowledged_at, dropped_points_total, sync_error_present
    ) values
      ${raceFixtures.map((fixture, index) => `(
        '${fixture.collarId}', '${fixture.deviceId}', '${DOG_ID}',
        'M1.12 race ${index + 1}', 'active', 1, 'xiao-s3-r1', '2.0.0-cloud.1', 3, 7,
        '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7]}'::jsonb,
        decode('${fixture.capabilityHash.toString("hex")}', 'hex'),
        statement_timestamp(), null, null, null, null, null, null, null, null, null
      )`).join(",\n")},
      (
        '${restFixture.collarId}', '${restFixture.deviceId}', '${DOG_ID}',
        'M1.12 Data API fixture', 'active', 1, 'xiao-s3-r1', '2.0.0-cloud.1', 3, 7,
        '{"manifest_schema":1,"hardware_revision":"xiao-s3-r1","protocol_versions":[1],"telemetry":{"schemas":[3]},"config_schemas":[7]}'::jsonb,
        decode(repeat('66', 32), 'hex'), statement_timestamp(), statement_timestamp(),
        statement_timestamp(), 2, 7, 224, 4096,
        statement_timestamp() - interval '1 minute', 3, true
      );

    insert into private.device_credentials (credential_id, collar_id, secret_digest, state)
    values
      ${raceFixtures.map((fixture) => `(
        '${fixture.credentialId}', '${fixture.collarId}',
        decode('${fixture.secretDigest.toString("hex")}', 'hex'), 'active'
      )`).join(",\n")},
      ('${restFixture.credentialId}', '${restFixture.collarId}', decode(repeat('77', 32), 'hex'), 'active');
  `);

  for (const fixture of raceFixtures) {
    await raceSyncAndRevoke(container, fixture);
  }

  const select = [
    "id", "dog_id", "display_name", "state", "protocol_version",
    "last_sync_at", "diagnostics_observed_at", "outbox_chunks", "outbox_points",
    "outbox_used_bytes", "outbox_capacity_bytes", "oldest_unacknowledged_at",
    "dropped_points_total", "sync_error_present",
  ].join(",");
  const collarPath = `collars?id=eq.${restFixture.collarId}&select=${select}`;
  for (const role of ["owner", "editor", "viewer"]) {
    const result = await request(collarPath, { token: tokens[role] });
    assert.equal(result.status, 200, `${role} collar read must succeed`);
    assert.equal(result.body.length, 1, `${role} must see one collar`);
    assert.deepEqual(Object.keys(result.body[0]).sort(), select.split(",").sort());
    assert.equal(result.body[0].outbox_chunks, 2);
    assert.equal(result.body[0].sync_error_present, true);
  }
  assert.deepEqual((await request(collarPath, { token: tokens.outsider })).body, []);
  assertDenied(await request(collarPath), "anonymous collar read");

  for (const role of ["editor", "viewer", "outsider"]) {
    assertDenied(await request("rpc/revoke_collar_v1", {
      token: tokens[role],
      method: "POST",
      body: { p_collar_id: restFixture.collarId },
    }), `${role} revoke`);
  }
  assertDenied(await request("rpc/revoke_collar_v1", {
    method: "POST",
    body: { p_collar_id: restFixture.collarId },
  }), "anonymous revoke");

  const revoke = await request("rpc/revoke_collar_v1", {
    token: tokens.owner,
    method: "POST",
    body: { p_collar_id: restFixture.collarId },
  });
  assert.deepEqual(revoke, { status: 200, body: true });
  const replay = await request("rpc/revoke_collar_v1", {
    token: tokens.owner,
    method: "POST",
    body: { p_collar_id: restFixture.collarId },
  });
  assert.deepEqual(replay, revoke, "an exact owner retry must confirm terminal state");

  const after = await request(collarPath, { token: tokens.owner });
  assert.equal(after.body[0].state, "revoked");
  const privateState = JSON.parse(psql(container, `
    select jsonb_build_object(
      'state', state,
      'revoked', revoked_at is not null
    )::text
    from private.device_credentials where credential_id = '${restFixture.credentialId}';
  `));
  assert.deepEqual(privateState, { state: "revoked", revoked: true });

  console.log(
    `M1.12 local matrix passed: ${RACE_COUNT} sync/revoke races, exact replay, RLS reads, and owner-only Data API revocation.`,
  );
} finally {
  const allCollars = [...raceFixtures.map(({ collarId }) => collarId), restFixture.collarId];
  psql(container, `
    delete from api.collars where id in (${allCollars.map(sqlUuid).join(", ")});
    delete from api.dog_memberships
    where dog_id = '${DOG_ID}' and user_id in ('${EDITOR_ID}', '${VIEWER_ID}');
    delete from auth.users where id in ('${EDITOR_ID}', '${VIEWER_ID}');
  `);
}
