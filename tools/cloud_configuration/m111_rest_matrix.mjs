import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash, createHmac, randomUUID } from "node:crypto";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const OWNER_ID = "10000000-0000-4000-8000-000000000001";
const OUTSIDER_ID = "20000000-0000-4000-8000-000000000002";
const EDITOR_ID = "19100000-0000-4000-8000-000000000001";
const VIEWER_ID = "19100000-0000-4000-8000-000000000002";

const requiredEnvironment = [
  "SUPABASE_URL",
  "SUPABASE_PUBLISHABLE_KEY",
  "SUPABASE_JWT_SECRET",
];
for (const name of requiredEnvironment) {
  if (!process.env[name]) throw new Error(`${name} is required.`);
}
const apiOrigin = new URL(process.env.SUPABASE_URL);
if (
  apiOrigin.protocol !== "http:" ||
  (apiOrigin.hostname !== "127.0.0.1" && apiOrigin.hostname !== "localhost")
) {
  throw new Error("Refusing to run the M1.11 fixture against a non-local Data API.");
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

function base64url(value) {
  return Buffer.from(value).toString("base64url");
}

function userToken(userId) {
  const now = Math.floor(Date.now() / 1000);
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

function brightnessDigest(brightness) {
  return createHash("sha256")
    .update(JSON.stringify({ brightness }), "utf8")
    .digest();
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

function rpcBody(collarId, brightness, baseServerVersion, mutationId) {
  return {
    p_collar_id: collarId,
    p_resource_key: "brightness",
    p_resource_schema: 1,
    p_mutation_id: mutationId,
    p_base_server_version: baseServerVersion,
    p_body: { brightness },
    p_body_sha256: `\\x${brightnessDigest(brightness).toString("hex")}`,
  };
}

function assertDenied(result, label) {
  assert.equal([401, 403].includes(result.status), true, `${label} must be denied`);
  assert.notEqual(result.body, null, `${label} must return a bounded error body`);
}

const container = databaseContainer();
const collarId = randomUUID();
const deviceId = randomUUID();
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
        'authenticated', 'authenticated', 'm111-rest-editor@example.test',
        extensions.crypt('local-only', extensions.gen_salt('bf')), statement_timestamp(),
        '{"provider":"email","providers":["email"]}', '{}',
        statement_timestamp(), statement_timestamp(), '', '', '', ''
      ),
      (
        '00000000-0000-0000-0000-000000000000', '${VIEWER_ID}',
        'authenticated', 'authenticated', 'm111-rest-viewer@example.test',
        extensions.crypt('local-only', extensions.gen_salt('bf')), statement_timestamp(),
        '{"provider":"email","providers":["email"]}', '{}',
        statement_timestamp(), statement_timestamp(), '', '', '', ''
      )
    on conflict (id) do nothing;

    insert into api.dog_memberships (dog_id, user_id, role) values
      ('${DOG_ID}', '${EDITOR_ID}', 'editor'),
      ('${DOG_ID}', '${VIEWER_ID}', 'viewer')
    on conflict (dog_id, user_id) do update set role = excluded.role;

    insert into api.collars (id, device_public_id, dog_id, display_name, state, linked_at)
    values ('${collarId}', '${deviceId}', '${DOG_ID}', 'M1.11 raw matrix', 'active', statement_timestamp());
  `);

  const mutationId = randomUUID();
  const firstBody = rpcBody(collarId, 91, 0, mutationId);
  const first = await request("rpc/mutate_config_resource_v1", {
    token: tokens.owner, method: "POST", body: firstBody,
  });
  assert.equal(first.status, 200);
  assert.equal(first.body.disposition, "winning");
  assert.equal(first.body.server_version, 1);

  const replay = await request("rpc/mutate_config_resource_v1", {
    token: tokens.owner, method: "POST", body: firstBody,
  });
  assert.deepEqual(replay, first, "raw exact replay must return the original receipt");

  const firstDigestHex = brightnessDigest(91).toString("hex");
  psql(container, `
    insert into api.config_reported (
      collar_id, resource_key, reported_server_version, reported_body_sha256,
      status, firmware_version, config_schema, device_applied_at, cloud_received_at
    ) values (
      '${collarId}', 'brightness', 1, decode('${firstDigestHex}', 'hex'),
      'applied', 'm111-local', 1, statement_timestamp(), statement_timestamp()
    );
  `);

  const headPath = `config_resource_heads?collar_id=eq.${collarId}&resource_key=eq.brightness&select=collar_id,resource_key,server_version,body`;
  const reportPath = `config_reported?collar_id=eq.${collarId}&resource_key=eq.brightness&select=collar_id,resource_key,reported_server_version,status`;
  for (const role of ["owner", "editor", "viewer"]) {
    const head = await request(headPath, { token: tokens[role] });
    const report = await request(reportPath, { token: tokens[role] });
    assert.equal(head.status, 200, `${role} head read must succeed`);
    assert.equal(report.status, 200, `${role} report read must succeed`);
    assert.equal(head.body.length, 1, `${role} must see one desired head`);
    assert.equal(report.body.length, 1, `${role} must see one report`);
  }

  const outsiderHead = await request(headPath, { token: tokens.outsider });
  const outsiderReport = await request(reportPath, { token: tokens.outsider });
  assert.deepEqual(outsiderHead.body, []);
  assert.deepEqual(outsiderReport.body, []);

  assertDenied(await request(headPath), "anonymous head read");
  assertDenied(await request(reportPath), "anonymous report read");

  const editorWrite = await request("rpc/mutate_config_resource_v1", {
    token: tokens.editor,
    method: "POST",
    body: rpcBody(collarId, 92, 1, randomUUID()),
  });
  assert.equal(editorWrite.status, 200);
  assert.equal(editorWrite.body.server_version, 2);

  const viewerWrite = await request("rpc/mutate_config_resource_v1", {
    token: tokens.viewer,
    method: "POST",
    body: rpcBody(collarId, 93, 2, randomUUID()),
  });
  assertDenied(viewerWrite, "viewer mutation");

  const outsiderWrite = await request("rpc/mutate_config_resource_v1", {
    token: tokens.outsider,
    method: "POST",
    body: rpcBody(collarId, 93, 2, randomUUID()),
  });
  assertDenied(outsiderWrite, "non-member mutation");

  const anonymousWrite = await request("rpc/mutate_config_resource_v1", {
    method: "POST",
    body: rpcBody(collarId, 93, 2, randomUUID()),
  });
  assertDenied(anonymousWrite, "anonymous mutation");

  const stale = await request("rpc/mutate_config_resource_v1", {
    token: tokens.owner,
    method: "POST",
    body: rpcBody(collarId, 94, 1, randomUUID()),
  });
  assert.equal(stale.status, 409);
  assert.equal(stale.body.code, "PT409");
  assert.equal(stale.body.message, "stale_base_server_version");

  console.log(
    "M1.11 raw Data API passed: owner/editor mutation, viewer/non-member denial, RLS reads, replay, and HTTP 409 stale.",
  );
} finally {
  psql(container, `
    delete from api.collars where id = '${collarId}';
    delete from api.dog_memberships where dog_id = '${DOG_ID}' and user_id in ('${EDITOR_ID}', '${VIEWER_ID}');
    delete from auth.users where id in ('${EDITOR_ID}', '${VIEWER_ID}');
  `);
}
