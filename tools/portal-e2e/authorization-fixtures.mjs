import { createHash, randomUUID } from "node:crypto";
import { execFileSync } from "node:child_process";

import { createPairOnlySimulator } from "../device-simulator/pair-only.mjs";

export const AUTHORIZATION_TABLES = Object.freeze([
  "profiles",
  "dogs",
  "dog_memberships",
  "collars",
  "recordings",
  "telemetry_points",
  "daily_summaries",
  "recording_summaries",
  "config_revisions",
  "config_resource_heads",
  "config_reported",
]);

export const AUTHORIZATION_RPCS = Object.freeze([
  "create_dog_v1",
  "mutate_config_resource_v1",
  "revoke_collar_v1",
  "request_dog_deletion_v1",
  "get_deletion_job_v1",
]);

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const EMAIL_PATTERN = /^[a-z0-9.+_-]+@example\.test$/;
const SAFE_TEXT_PATTERN = /^[A-Za-z0-9 ._-]+$/;

function requireUuid(value, label) {
  if (!UUID_PATTERN.test(value)) {
    throw new Error(`${label} must be a version-4 UUID.`);
  }
  return value;
}

function requireEmail(value, label) {
  if (!EMAIL_PATTERN.test(value)) {
    throw new Error(`${label} is outside the local test email boundary.`);
  }
  return value;
}

function requireSafeText(value, label) {
  if (!SAFE_TEXT_PATTERN.test(value)) {
    throw new Error(`${label} contains unsupported fixture characters.`);
  }
  return value;
}

function sqlLiteral(value) {
  return `'${String(value).replaceAll("'", "''")}'`;
}

function databaseContainer() {
  const result = execFileSync("docker", [
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ], { encoding: "utf8" }).trim().split(/\r?\n/u).filter(Boolean);
  if (result.length !== 1) {
    throw new Error("M1.14 could not locate the repository-owned local database.");
  }
  return result[0];
}

function psql(sql, { tuplesOnly = false } = {}) {
  const args = [
    "exec",
    "-i",
    databaseContainer(),
    "psql",
    "-v",
    "ON_ERROR_STOP=1",
    "-U",
    "supabase_admin",
    "-d",
    "postgres",
  ];

  if (tuplesOnly) {
    args.push("-qAt");
  }

  args.push("-c", sql);

  return execFileSync("docker", args, {
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"],
  }).trim();
}

function queryJson(sql) {
  const output = psql(
    `select coalesce(jsonb_agg(to_jsonb(q)), '[]'::jsonb)::text from (${sql}) q;`,
    { tuplesOnly: true },
  );
  return output ? JSON.parse(output) : [];
}

function fixtureDefinition(cycle) {
  if (cycle !== 1 && cycle !== 2) {
    throw new Error(`M1.14 cycle must be 1 or 2; received ${cycle}.`);
  }

  const prefix = cycle === 1 ? "a14" : "b14";
  const uuid = (slot) => `${prefix}${slot}0000-0000-4000-8000-00000000000${slot}`;
  const account = (slot, role, label) => ({
    id: uuid(slot),
    email: `m114-${label}-${cycle}@example.test`,
    role,
  });

  return {
    cycle,
    password: `M114-local-authorization-${cycle}-24!`,
    accounts: {
      ownerA: account(1, "OWNER", "owner-a"),
      ownerB: account(2, "OWNER", "owner-b"),
      editor: account(3, "EDITOR", "editor"),
      viewer: account(4, "VIEWER", "viewer"),
    },
    dogNames: {
      dogA: `M114 Alpha ${cycle}`,
      dogB: `M114 Beta ${cycle}`,
    },
    brightness: 140 + cycle,
    missing: {
      dogId: `${prefix}90000-0000-4000-8000-000000000009`,
      collarId: `${prefix}a0000-0000-4000-8000-00000000000a`,
      recordingId: `${prefix}b0000-0000-4000-8000-00000000000b`,
      jobId: `${prefix}c0000-0000-4000-8000-00000000000c`,
    },
  };
}

export function authorizationPassword(cycle) {
  return fixtureDefinition(cycle).password;
}

function insertConfirmedAuthUsers(definition) {
  const statements = Object.values(definition.accounts).map((account) => {
    requireUuid(account.id, `${account.role} account id`);
    requireEmail(account.email, `${account.role} account email`);
    return `
      insert into auth.users (
        instance_id,
        id,
        aud,
        role,
        email,
        encrypted_password,
        email_confirmed_at,
        raw_app_meta_data,
        raw_user_meta_data,
        created_at,
        updated_at,
        confirmation_token,
        email_change,
        email_change_token_new,
        recovery_token
      ) values (
        '00000000-0000-0000-0000-000000000000',
        ${sqlLiteral(account.id)}::uuid,
        'authenticated',
        'authenticated',
        ${sqlLiteral(account.email)},
        extensions.crypt(${sqlLiteral(definition.password)}, extensions.gen_salt('bf')),
        timezone('utc'::text, now()),
        '{"provider":"email","providers":["email"]}'::jsonb,
        '{}'::jsonb,
        timezone('utc'::text, now()),
        timezone('utc'::text, now()),
        '',
        '',
        '',
        ''
      );`;
  });

  psql(`begin; ${statements.join("\n")} commit;`);

  const rows = queryJson(`
    select id::text, email
    from auth.users
    where id = any(array[${Object.values(definition.accounts)
      .map((account) => `${sqlLiteral(account.id)}::uuid`)
      .join(", ")}])
    order by id
  `);
  if (rows.length !== 4) {
    throw new Error(`Expected four confirmed M1.14 Auth users, found ${rows.length}.`);
  }
}

async function requestJson(url, { method = "GET", publishableKey, accessToken, body } = {}) {
  const headers = {
    apikey: publishableKey,
    Accept: "application/json",
  };
  if (accessToken) {
    headers.Authorization = `Bearer ${accessToken}`;
  }
  if (body !== undefined) {
    headers["Content-Type"] = "application/json";
  }

  const response = await fetch(url, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  let payload = null;
  if (text) {
    try {
      payload = JSON.parse(text);
    } catch {
      payload = text;
    }
  }
  return { status: response.status, payload };
}

async function passwordLogin(apiUrl, publishableKey, account, password) {
  const result = await requestJson(`${apiUrl}/auth/v1/token?grant_type=password`, {
    method: "POST",
    publishableKey,
    body: { email: account.email, password },
  });
  const accessToken = result.payload?.access_token;
  if (result.status !== 200 || typeof accessToken !== "string" || accessToken.length < 32) {
    throw new Error(`Real password login failed for ${account.role} fixture (${result.status}).`);
  }
  return accessToken;
}

async function invokeRpc(apiUrl, publishableKey, accessToken, name, body) {
  return requestJson(`${apiUrl}/rest/v1/rpc/${name}`, {
    method: "POST",
    publishableKey,
    accessToken,
    body,
  });
}

async function createDog(apiUrl, publishableKey, accessToken, dogName) {
  requireSafeText(dogName, "dog name");
  const result = await invokeRpc(apiUrl, publishableKey, accessToken, "create_dog_v1", {
    p_name: dogName,
    p_timezone: "America/Bogota",
  });
  if (result.status !== 200 || !UUID_PATTERN.test(result.payload)) {
    throw new Error(`create_dog_v1 did not return a dog UUID (${result.status}).`);
  }
  return result.payload;
}

async function issueClaim(apiUrl, publishableKey, accessToken, dogId) {
  const result = await requestJson(`${apiUrl}/functions/v1/user-v1-issue-claim`, {
    method: "POST",
    publishableKey,
    accessToken,
    body: { protocol_version: 1, request_id: randomUUID(), dog_id: dogId },
  });
  const claimCode = result.payload?.claim?.code;
  if (result.status !== 200 || typeof claimCode !== "string" || claimCode.length < 12) {
    throw new Error(`user-v1-issue-claim failed during fixture setup (${result.status}).`);
  }
  return claimCode;
}

function canonicalBrightness(value) {
  return JSON.stringify({ brightness: value });
}

async function seedBrightness(apiUrl, publishableKey, accessToken, collarId, brightness) {
  const bodyJson = canonicalBrightness(brightness);
  const digest = createHash("sha256").update(bodyJson).digest("hex");
  const result = await invokeRpc(
    apiUrl,
    publishableKey,
    accessToken,
    "mutate_config_resource_v1",
    {
      p_collar_id: collarId,
      p_resource_key: "brightness",
      p_resource_schema: 1,
      p_mutation_id: randomUUID(),
      p_base_server_version: 0,
      p_body: { brightness },
      p_body_sha256: `\\x${digest}`,
    },
  );
  const row = Array.isArray(result.payload) ? result.payload[0] : result.payload;
  if (result.status !== 200 || Number(row?.server_version) !== 1) {
    throw new Error(`Initial brightness mutation failed (${result.status}).`);
  }
}

function currentBogotaDate() {
  return new Intl.DateTimeFormat("en-CA", {
    timeZone: "America/Bogota",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  }).format(new Date());
}

function recordingFor(collarId, bootSequence) {
  const rows = queryJson(`
    select id::text
    from api.recordings
    where collar_id = ${sqlLiteral(requireUuid(collarId, "collar id"))}::uuid
      and boot_sequence = ${Number(bootSequence)}
    order by started_at desc, id
    limit 1
  `);
  if (rows.length !== 1) {
    throw new Error(`Expected one uploaded recording for collar ${collarId}.`);
  }
  return rows[0].id;
}

function addMembershipsAndSummaries(definition, dogAId, recordingAId) {
  const { editor, viewer } = definition.accounts;
  const localDate = currentBogotaDate();
  psql(`
    begin;
    insert into api.dog_memberships (dog_id, user_id, role)
    values
      (${sqlLiteral(dogAId)}::uuid, ${sqlLiteral(editor.id)}::uuid, 'editor'),
      (${sqlLiteral(dogAId)}::uuid, ${sqlLiteral(viewer.id)}::uuid, 'viewer');

    insert into api.daily_summaries (
      dog_id, local_date, timezone, observed_s, moving_s, inactive_s, unknown_s,
      distance_m, average_observed_cmps, average_moving_cmps,
      filtered_max_speed_cmps, valid_points, warning_points, gap_count,
      dropped_points, coverage_ratio, algorithm_version, source_revision, computed_at
    ) values (
      ${sqlLiteral(dogAId)}::uuid, ${sqlLiteral(localDate)}::date, 'America/Bogota',
      60, 20, 20, 20, 15, 25, 50, 75, 3, 0, 0, 0, 0.666667, 1, 1,
      timezone('utc'::text, now())
    );

    insert into api.recording_summaries (
      recording_id, observed_s, moving_s, inactive_s, unknown_s, distance_m,
      average_observed_cmps, average_moving_cmps, filtered_max_speed_cmps,
      valid_points, warning_points, gap_count, dropped_points, coverage_ratio,
      algorithm_version, computed_at
    ) values (
      ${sqlLiteral(recordingAId)}::uuid, 60, 20, 20, 20, 15, 25, 50, 75,
      3, 0, 0, 0, 0.666667, 1, timezone('utc'::text, now())
    );
    commit;
  `);
}

export async function prepareAuthorizationFixture({ apiUrl, publishableKey, cycle }) {
  const definition = fixtureDefinition(cycle);
  insertConfirmedAuthUsers(definition);

  const ownerAToken = await passwordLogin(
    apiUrl,
    publishableKey,
    definition.accounts.ownerA,
    definition.password,
  );
  const ownerBToken = await passwordLogin(
    apiUrl,
    publishableKey,
    definition.accounts.ownerB,
    definition.password,
  );

  const dogAId = await createDog(apiUrl, publishableKey, ownerAToken, definition.dogNames.dogA);
  const dogBId = await createDog(apiUrl, publishableKey, ownerBToken, definition.dogNames.dogB);
  const claimA = await issueClaim(apiUrl, publishableKey, ownerAToken, dogAId);
  const claimB = await issueClaim(apiUrl, publishableKey, ownerBToken, dogBId);

  const simulatorA = await createPairOnlySimulator({
    apiUrl,
    claimCode: claimA,
    expectedDogId: dogAId,
  });
  const simulatorB = await createPairOnlySimulator({
    apiUrl,
    claimCode: claimB,
    expectedDogId: dogBId,
  });
  const pairingA = await simulatorA.attempt();
  const pairingB = await simulatorB.attempt();
  if (!pairingA.ok || !pairingB.ok) {
    throw new Error("M1.14 fixture pairing did not complete for both owners.");
  }

  const uploadA = await simulatorA.uploadJourneyRecording();
  const uploadB = await simulatorB.uploadJourneyRecording();
  await seedBrightness(
    apiUrl,
    publishableKey,
    ownerAToken,
    pairingA.pairing.collarId,
    definition.brightness,
  );
  await simulatorA.convergeBrightness(definition.brightness);

  const recordingAId = recordingFor(pairingA.pairing.collarId, uploadA.bootSequence);
  const recordingBId = recordingFor(pairingB.pairing.collarId, uploadB.bootSequence);
  addMembershipsAndSummaries(definition, dogAId, recordingAId);

  const manifest = Object.freeze({
    cycle,
    accounts: definition.accounts,
    dogA: {
      id: dogAId,
      name: definition.dogNames.dogA,
      collarId: pairingA.pairing.collarId,
      recordingId: recordingAId,
    },
    dogB: {
      id: dogBId,
      name: definition.dogNames.dogB,
      collarId: pairingB.pairing.collarId,
      recordingId: recordingBId,
    },
    brightness: definition.brightness,
    missing: definition.missing,
  });

  const privateValues = [definition.password, ownerAToken, ownerBToken, claimA, claimB];
  const artifactContainsPrivateMaterial = (value) => {
    const text = Buffer.isBuffer(value) ? value.toString("utf8") : String(value ?? "");
    return (
      privateValues.some((privateValue) => text.includes(privateValue)) ||
      simulatorA.artifactContainsPrivateMaterial(value) ||
      simulatorB.artifactContainsPrivateMaterial(value)
    );
  };

  return { manifest, artifactContainsPrivateMaterial };
}

export function authorizationSurfaceInventory() {
  const tables = queryJson(`
    select table_name as name
    from information_schema.role_table_grants
    where grantee = 'authenticated'
      and table_schema = 'api'
      and privilege_type = 'SELECT'
    order by table_name
  `).map((row) => row.name);

  const rpcs = queryJson(`
    select p.proname as name
    from pg_proc p
    join pg_namespace n on n.oid = p.pronamespace
    where n.nspname = 'api'
      and has_function_privilege('authenticated', p.oid, 'EXECUTE')
    order by p.proname
  `).map((row) => row.name);

  const anonRpcs = queryJson(`
    select p.proname as name
    from pg_proc p
    join pg_namespace n on n.oid = p.pronamespace
    where n.nspname = 'api'
      and p.proname = any(array[${AUTHORIZATION_RPCS.map(sqlLiteral).join(", ")}])
      and has_function_privilege('anon', p.oid, 'EXECUTE')
    order by p.proname
  `).map((row) => row.name);

  return { tables, rpcs, anonRpcs };
}

function aggregateSql(table, predicate, orderBy) {
  return `(select coalesce(jsonb_agg(to_jsonb(t) order by ${orderBy}), '[]'::jsonb)
    from ${table} t where ${predicate})`;
}

export function authorizationGraphSnapshot(manifest) {
  const dogIds = [manifest.dogA.id, manifest.dogB.id].map((id) => sqlLiteral(requireUuid(id, "dog id")));
  const collarIds = [manifest.dogA.collarId, manifest.dogB.collarId].map((id) =>
    sqlLiteral(requireUuid(id, "collar id")),
  );
  const recordingIds = [manifest.dogA.recordingId, manifest.dogB.recordingId].map((id) =>
    sqlLiteral(requireUuid(id, "recording id")),
  );
  const accountIds = Object.values(manifest.accounts).map((account) =>
    sqlLiteral(requireUuid(account.id, "account id")),
  );
  const dogs = `dog_id = any(array[${dogIds.join(", ")}]::uuid[])`;
  const collars = `collar_id = any(array[${collarIds.join(", ")}]::uuid[])`;
  const recordings = `recording_id = any(array[${recordingIds.join(", ")}]::uuid[])`;

  const objectSql = `jsonb_build_object(
    'profiles', ${aggregateSql("api.profiles", `user_id = any(array[${accountIds.join(", ")}]::uuid[])`, "t.user_id")},
    'dogs', ${aggregateSql("api.dogs", `id = any(array[${dogIds.join(", ")}]::uuid[])`, "t.id")},
    'memberships', ${aggregateSql("api.dog_memberships", dogs, "t.dog_id, t.user_id")},
    'collars', ${aggregateSql("api.collars", dogs, "t.id")},
    'recordings', ${aggregateSql("api.recordings", collars, "t.id")},
    'telemetry_points', ${aggregateSql("api.telemetry_points", collars, "t.collar_id, t.boot_sequence, t.point_sequence")},
    'daily_summaries', ${aggregateSql("api.daily_summaries", dogs, "t.dog_id, t.local_date")},
    'recording_summaries', ${aggregateSql("api.recording_summaries", recordings, "t.recording_id")},
    'config_revisions', ${aggregateSql("api.config_revisions", collars, "t.collar_id, t.resource_key, t.server_version")},
    'config_heads', ${aggregateSql("api.config_resource_heads", collars, "t.collar_id, t.resource_key")},
    'config_reported', ${aggregateSql("api.config_reported", collars, "t.collar_id, t.resource_key")},
    'claims', ${aggregateSql("private.device_claims", dogs, "t.id")},
    'credentials', ${aggregateSql("private.device_credentials", collars, "t.credential_id")},
    'sync_requests', ${aggregateSql("private.sync_requests", collars, "t.collar_id, t.request_id")},
    'chunks', ${aggregateSql("private.telemetry_chunks", collars, "t.collar_id, t.boot_sequence, t.chunk_sequence")},
    'deletion_tombstones', ${aggregateSql("private.deletion_tombstones", "true", "t.id")},
    'deletion_jobs', ${aggregateSql("private.deletion_jobs", "true", "t.id")},
    'deletion_receipts', ${aggregateSql("private.deletion_receipts", "true", "t.job_id")}
  )`;
  const rows = queryJson(`
    select
      encode(extensions.digest(convert_to(snapshot::text, 'UTF8'), 'sha256'), 'hex') as digest,
      jsonb_build_object(
        'profiles', jsonb_array_length(snapshot->'profiles'),
        'dogs', jsonb_array_length(snapshot->'dogs'),
        'memberships', jsonb_array_length(snapshot->'memberships'),
        'collars', jsonb_array_length(snapshot->'collars'),
        'recordings', jsonb_array_length(snapshot->'recordings'),
        'telemetry_points', jsonb_array_length(snapshot->'telemetry_points'),
        'daily_summaries', jsonb_array_length(snapshot->'daily_summaries'),
        'recording_summaries', jsonb_array_length(snapshot->'recording_summaries'),
        'config_revisions', jsonb_array_length(snapshot->'config_revisions'),
        'config_heads', jsonb_array_length(snapshot->'config_heads'),
        'config_reported', jsonb_array_length(snapshot->'config_reported'),
        'claims', jsonb_array_length(snapshot->'claims'),
        'credentials', jsonb_array_length(snapshot->'credentials'),
        'sync_requests', jsonb_array_length(snapshot->'sync_requests'),
        'chunks', jsonb_array_length(snapshot->'chunks'),
        'deletion_tombstones', jsonb_array_length(snapshot->'deletion_tombstones'),
        'deletion_jobs', jsonb_array_length(snapshot->'deletion_jobs'),
        'deletion_receipts', jsonb_array_length(snapshot->'deletion_receipts')
      ) as counts
    from (select ${objectSql} as snapshot) s
  `);
  if (rows.length !== 1) {
    throw new Error("Could not capture the M1.14 target graph snapshot.");
  }
  return rows[0];
}

export function deleteAuthorizationViewer(manifest) {
  const viewerId = requireUuid(manifest.accounts.viewer.id, "viewer id");
  const dogAId = requireUuid(manifest.dogA.id, "dog A id");
  const preflight = queryJson(`
    select
      exists(select 1 from auth.users where id = ${sqlLiteral(viewerId)}::uuid) as auth_user,
      exists(select 1 from api.profiles where user_id = ${sqlLiteral(viewerId)}::uuid) as profile,
      exists(
        select 1 from api.dog_memberships
        where user_id = ${sqlLiteral(viewerId)}::uuid
          and dog_id = ${sqlLiteral(dogAId)}::uuid
          and role = 'viewer'
      ) as viewer_membership,
      exists(select 1 from api.dogs where created_by = ${sqlLiteral(viewerId)}::uuid) as created_dog
  `)[0];
  if (!preflight?.auth_user || !preflight.profile || !preflight.viewer_membership || preflight.created_dog) {
    throw new Error("Deleted-user preflight did not identify exactly the isolated VIEWER fixture.");
  }

  psql(`delete from auth.users where id = ${sqlLiteral(viewerId)}::uuid;`);
  const remaining = queryJson(`
    select
      (select count(*)::int from auth.users where id = ${sqlLiteral(viewerId)}::uuid) as auth_users,
      (select count(*)::int from auth.sessions where user_id = ${sqlLiteral(viewerId)}::uuid) as sessions,
      (select count(*)::int from auth.refresh_tokens where user_id = ${sqlLiteral(viewerId)}) as refresh_tokens,
      (select count(*)::int from api.profiles where user_id = ${sqlLiteral(viewerId)}::uuid) as profiles,
      (select count(*)::int from api.dog_memberships where user_id = ${sqlLiteral(viewerId)}::uuid) as memberships
  `)[0];
  if (!remaining || Object.values(remaining).some((count) => Number(count) !== 0)) {
    throw new Error("Deleting the isolated viewer did not remove its Auth and membership state.");
  }
  return { deletedUser: true, authStateRows: 0, applicationRows: 0 };
}

// These bounded helpers let the Playwright process exercise the public local
// Auth/Data API with real password sessions. They never expose the database
// administrator connection or a service-role key.
export const authorizationRequestJson = requestJson;
export const authorizationPasswordLogin = passwordLogin;
export const authorizationInvokeRpc = invokeRpc;
