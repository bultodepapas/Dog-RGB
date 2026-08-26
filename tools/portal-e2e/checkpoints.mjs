import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const EMAIL_PATTERN = /^m113-owner-[12]@example\.test$/u;
const DOG_NAME_PATTERN = /^M113 Dog [12]$/u;
const HEX_SHA256_PATTERN = /^[0-9a-f]{64}$/u;

function safeUuid(value, label) {
  if (typeof value !== "string" || !UUID_PATTERN.test(value)) {
    throw new Error(`M1.13 checkpoint rejected ${label}.`);
  }
  return `'${value}'::uuid`;
}

function safeEmail(value) {
  if (!EMAIL_PATTERN.test(value)) throw new Error("M1.13 checkpoint rejected fixture email.");
  return `'${value}'`;
}

function safeDogName(value) {
  if (!DOG_NAME_PATTERN.test(value)) throw new Error("M1.13 checkpoint rejected fixture dog name.");
  return `'${value}'`;
}

function safeInteger(value, label) {
  if (!Number.isInteger(value) || value < 0 || value > 4_294_967_295) {
    throw new Error(`M1.13 checkpoint rejected ${label}.`);
  }
  return String(value);
}

function safeHash(value) {
  if (typeof value !== "string" || !HEX_SHA256_PATTERN.test(value)) {
    throw new Error("M1.13 checkpoint rejected configuration hash.");
  }
  return `'${value}'`;
}

function databaseContainer() {
  const result = spawnSync("docker", [
    "ps",
    "--filter", "label=com.supabase.cli.project=Dog-RGB-1",
    "--filter", "name=^/supabase_db_Dog-RGB-1$",
    "--format", "{{.ID}}",
  ], { encoding: "utf8", maxBuffer: 1024 * 1024 });
  const ids = result.status === 0
    ? result.stdout.split(/\r?\n/u).filter(Boolean)
    : [];
  if (ids.length !== 1) throw new Error("M1.13 could not locate its local database.");
  return ids[0];
}

function queryJson(sql) {
  const result = spawnSync("docker", [
    "exec", "-i", databaseContainer(),
    "psql", "-X", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
    "-U", "supabase_admin", "-d", "postgres",
  ], {
    input: sql,
    encoding: "utf8",
    maxBuffer: 2 * 1024 * 1024,
    timeout: 10_000,
  });
  if (result.status !== 0) throw new Error("M1.13 persisted checkpoint query failed.");
  try {
    return JSON.parse(result.stdout.trim());
  } catch {
    throw new Error("M1.13 persisted checkpoint returned invalid evidence.");
  }
}

function exact(actual, expected, label) {
  try {
    assert.deepEqual(actual, expected);
  } catch {
    throw new Error(`M1.13 ${label} checkpoint did not match.`);
  }
}

export function checkpointSignup(email) {
  const row = queryJson(`
    select json_build_object(
      'user_count', count(*),
      'unconfirmed_count', count(*) filter (where u.email_confirmed_at is null),
      'profile_count', count(p.user_id),
      'dog_count', count(d.id)
    )::text
    from auth.users u
    left join api.profiles p on p.user_id = u.id
    left join api.dogs d on d.created_by = u.id
    where u.email = ${safeEmail(email)};
  `);
  exact(row, {
    user_count: 1,
    unconfirmed_count: 1,
    profile_count: 1,
    dog_count: 0,
  }, "signup");
}

export function checkpointConfirmed(email) {
  const row = queryJson(`
    select json_build_object(
      'user_count', count(*),
      'confirmed_count', count(*) filter (where email_confirmed_at is not null)
    )::text
    from auth.users where email = ${safeEmail(email)};
  `);
  exact(row, { user_count: 1, confirmed_count: 1 }, "confirmation");
}

export function checkpointDog({ email, dogId, dogName }) {
  const row = queryJson(`
    select json_build_object(
      'dog_count', count(*),
      'owner_count', count(*) filter (where dm.role = 'owner'),
      'timezone_count', count(*) filter (where d.timezone = 'America/Bogota'),
      'creator_matches', count(*) filter (where d.created_by = u.id)
    )::text
    from auth.users u
    join api.dogs d on d.created_by = u.id
    join api.dog_memberships dm on dm.dog_id = d.id and dm.user_id = u.id
    where u.email = ${safeEmail(email)}
      and d.id = ${safeUuid(dogId, "dog identity")}
      and d.name = ${safeDogName(dogName)};
  `);
  exact(row, {
    dog_count: 1,
    owner_count: 1,
    timezone_count: 1,
    creator_matches: 1,
  }, "dog creation");
}

export function checkpointIssuedClaim(dogId) {
  const row = queryJson(`
    select json_build_object(
      'claim_count', count(*),
      'issued_count', count(*) filter (where state = 'issued'),
      'unused_count', count(*) filter (where attempt_count = 0 and consumed_at is null),
      'digest_count', count(*) filter (where octet_length(code_digest) = 32),
      'future_count', count(*) filter (where expires_at > statement_timestamp()),
      'empty_receipt_count', count(*) filter (
        where request_id is null and request_sha256 is null and response_json is null
      )
    )::text
    from private.device_claims where dog_id = ${safeUuid(dogId, "dog identity")};
  `);
  exact(row, {
    claim_count: 1,
    issued_count: 1,
    unused_count: 1,
    digest_count: 1,
    future_count: 1,
    empty_receipt_count: 1,
  }, "claim issuance");
}

export function checkpointPairing({ dogId, collarId, deviceId }) {
  const row = queryJson(`
    select json_build_object(
      'consumed_claims', (select count(*) from private.device_claims
        where dog_id = ${safeUuid(dogId, "dog identity")}
          and state = 'consumed' and attempt_count = 1
          and consumed_by_device_id = ${safeUuid(deviceId, "device identity")}
          and octet_length(request_sha256) = 32),
      'active_collars', (select count(*) from api.collars
        where id = ${safeUuid(collarId, "collar identity")}
          and dog_id = ${safeUuid(dogId, "dog identity")}
          and device_public_id = ${safeUuid(deviceId, "device identity")}
          and state = 'active'),
      'active_credentials', (select count(*) from private.device_credentials
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and state = 'active' and octet_length(secret_digest) = 32),
      'sync_rows', (select count(*) from private.sync_requests
        where collar_id = ${safeUuid(collarId, "collar identity")}),
      'recording_rows', (select count(*) from api.recordings
        where collar_id = ${safeUuid(collarId, "collar identity")})
    )::text;
  `);
  exact(row, {
    consumed_claims: 1,
    active_collars: 1,
    active_credentials: 1,
    sync_rows: 0,
    recording_rows: 0,
  }, "pairing");
}

export function checkpointUpload({
  collarId,
  requestId,
  summaryId,
  bootSequence,
  chunkSequence,
}) {
  const row = queryJson(`
    select json_build_object(
      'sync_count', (select count(*) from private.sync_requests
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and request_id = ${safeUuid(requestId, "sync request identity")}
          and status = 'committed'),
      'chunk_count', (select count(*) from private.telemetry_chunks
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and boot_sequence = ${safeInteger(bootSequence, "boot sequence")}
          and chunk_sequence = ${safeInteger(chunkSequence, "chunk sequence")}
          and point_count = 3),
      'point_count', (select count(*) from api.telemetry_points
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and boot_sequence = ${safeInteger(bootSequence, "boot sequence")}
          and point_sequence in (0, 1, 2)),
      'summary_count', (select count(*) from private.device_daily_summaries
        where summary_id = ${safeUuid(summaryId, "summary identity")}
          and collar_id = ${safeUuid(collarId, "collar identity")}),
      'recording_count', (select count(*) from api.recordings
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and boot_sequence = ${safeInteger(bootSequence, "boot sequence")}
          and point_count = 3),
      'recording_id', (select id::text from api.recordings
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and boot_sequence = ${safeInteger(bootSequence, "boot sequence")}),
      'diagnostic_count', (select count(*) from api.collars
        where id = ${safeUuid(collarId, "collar identity")}
          and last_sync_at is not null and diagnostics_observed_at is not null
          and outbox_chunks = 1 and outbox_points = 3 and outbox_used_bytes = 96)
    )::text;
  `);
  const { recording_id: recordingId, ...counts } = row;
  exact(counts, {
    sync_count: 1,
    chunk_count: 1,
    point_count: 3,
    summary_count: 1,
    recording_count: 1,
    diagnostic_count: 1,
  }, "recording upload");
  safeUuid(recordingId, "recording identity");
  return Object.freeze({ recordingId });
}

export function checkpointDesiredBrightness({ collarId, email, brightness }) {
  const row = queryJson(`
    select json_build_object(
      'head_count', count(*),
      'brightness', max((h.body ->> 'brightness')::integer),
      'server_version', max(h.server_version),
      'body_sha256_hex', max(encode(h.body_sha256, 'hex')),
      'winning_web_revisions', (
        select count(*) from api.config_revisions r
        join auth.users u on u.id = r.actor_user_id
        where r.collar_id = ${safeUuid(collarId, "collar identity")}
          and r.resource_key = 'brightness' and r.origin = 'web'
          and r.disposition = 'winning' and u.email = ${safeEmail(email)}
      ),
      'reported_count', (select count(*) from api.config_reported p
        where p.collar_id = ${safeUuid(collarId, "collar identity")}
          and p.resource_key = 'brightness')
    )::text
    from api.config_resource_heads h
    where h.collar_id = ${safeUuid(collarId, "collar identity")}
      and h.resource_key = 'brightness'
      and h.body = jsonb_build_object('brightness', ${safeInteger(brightness, "brightness")});
  `);
  if (
    row.head_count !== 1 ||
    row.brightness !== brightness ||
    !Number.isInteger(row.server_version) ||
    row.server_version < 1 ||
    !HEX_SHA256_PATTERN.test(String(row.body_sha256_hex)) ||
    row.winning_web_revisions !== 1 ||
    row.reported_count !== 0
  ) {
    throw new Error("M1.13 desired brightness checkpoint did not match.");
  }
  return Object.freeze({
    brightness,
    serverVersion: row.server_version,
    bodySha256Hex: row.body_sha256_hex,
  });
}

export function checkpointAppliedBrightness({
  collarId,
  brightness,
  serverVersion,
  bodySha256Hex,
}) {
  const row = queryJson(`
    select json_build_object(
      'exact_head', (select count(*) from api.config_resource_heads h
        where h.collar_id = ${safeUuid(collarId, "collar identity")}
          and h.resource_key = 'brightness'
          and h.server_version = ${safeInteger(serverVersion, "server version")}
          and encode(h.body_sha256, 'hex') = ${safeHash(bodySha256Hex)}
          and h.body = jsonb_build_object('brightness', ${safeInteger(brightness, "brightness")})),
      'exact_report', (select count(*) from api.config_reported p
        where p.collar_id = ${safeUuid(collarId, "collar identity")}
          and p.resource_key = 'brightness'
          and p.reported_server_version = ${safeInteger(serverVersion, "server version")}
          and encode(p.reported_body_sha256, 'hex') = ${safeHash(bodySha256Hex)}
          and p.status = 'applied' and p.error_code is null),
      'sync_count', (select count(*) from private.sync_requests
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and status = 'committed'),
      'empty_diagnostics', (select count(*) from api.collars
        where id = ${safeUuid(collarId, "collar identity")}
          and outbox_chunks = 0 and outbox_points = 0
          and outbox_used_bytes = 0 and oldest_unacknowledged_at is null)
    )::text;
  `);
  exact(row, {
    exact_head: 1,
    exact_report: 1,
    sync_count: 3,
    empty_diagnostics: 1,
  }, "applied brightness");
}

export function checkpointRevoked({ collarId, recordingId }) {
  const row = queryJson(`
    select json_build_object(
      'revoked_collar', (select count(*) from api.collars
        where id = ${safeUuid(collarId, "collar identity")}
          and state = 'revoked' and revoked_at is not null),
      'active_credentials', (select count(*) from private.device_credentials
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and state = 'active'),
      'revoked_credentials', (select count(*) from private.device_credentials
        where collar_id = ${safeUuid(collarId, "collar identity")}
          and state = 'revoked' and revoked_at is not null),
      'matching_terminal_time', (select count(*) from private.device_credentials dc
        join api.collars c on c.id = dc.collar_id
        where c.id = ${safeUuid(collarId, "collar identity")}
          and dc.revoked_at = c.revoked_at),
      'retained_recording', (select count(*) from api.recordings
        where id = ${safeUuid(recordingId, "recording identity")}
          and collar_id = ${safeUuid(collarId, "collar identity")}),
      'retained_points', (select count(*) from api.telemetry_points
        where collar_id = ${safeUuid(collarId, "collar identity")})
    )::text;
  `);
  exact(row, {
    revoked_collar: 1,
    active_credentials: 0,
    revoked_credentials: 1,
    matching_terminal_time: 1,
    retained_recording: 1,
    retained_points: 3,
  }, "revocation");
}
