import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const OWNER_ID = "10000000-0000-4000-8000-000000000001";

function invoke(command, args, options = {}) {
  const result = spawnSync(command, args, {
    encoding: "utf8",
    ...options,
  });
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

function sqlUuid(value) {
  if (!/^[0-9a-f-]{36}$/u.test(value)) throw new Error("Unsafe UUID fixture.");
  return `'${value}'::uuid`;
}

function brightnessDigest(brightness) {
  return `\\x${createHash("sha256")
    .update(JSON.stringify({ brightness }), "utf8")
    .digest("hex")}`;
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

async function mutate(container, input) {
  const digest = brightnessDigest(input.brightness).slice(2);
  const result = await asyncPsql(container, `
    begin;
    set local role authenticated;
    set local "request.jwt.claim.sub" = '${OWNER_ID}';
    select api.mutate_config_resource_v1(
      ${sqlUuid(input.collarId)}, 'brightness', 1,
      ${sqlUuid(input.mutationId)}, ${input.baseServerVersion},
      '{"brightness":${input.brightness}}'::jsonb,
      decode('${digest}', 'hex')
    )::text;
    commit;
  `);
  if (result.status === 0) {
    return { status: 200, body: JSON.parse(result.stdout) };
  }
  const message = result.stderr.includes("stale_base_server_version")
    ? "stale_base_server_version"
    : "bounded_failure";
  return { status: 409, body: { code: message === "stale_base_server_version" ? "PT409" : "XXXXX", message } };
}

function assertOneWinnerOneStale(results, expectedVersion) {
  const winners = results.filter(({ status, body }) =>
    status === 200 && body.disposition === "winning" &&
    body.server_version === expectedVersion);
  const stale = results.filter(({ body }) =>
    body?.code === "PT409" && body?.message === "stale_base_server_version");
  assert.equal(winners.length, 1, "concurrent writes must produce one winner");
  assert.equal(stale.length, 1, "the other concurrent write must be bounded stale");
}

function state(container, collarId) {
  const output = psql(container, `
    select jsonb_build_object(
      'head_version', h.server_version,
      'head_brightness', h.body ->> 'brightness',
      'head_revision', h.winning_revision_id,
      'head_updated_at', h.updated_at,
      'revisions', (select count(*) from api.config_revisions r where r.collar_id = h.collar_id),
      'winning', (select count(*) from api.config_revisions r where r.collar_id = h.collar_id and r.disposition = 'winning'),
      'superseded', (select count(*) from api.config_revisions r where r.collar_id = h.collar_id and r.disposition = 'superseded'),
      'hlc_physical', s.physical_ms,
      'hlc_logical', s.logical
    )::text
    from api.config_resource_heads h
    join private.config_hlc_state s on s.collar_id = h.collar_id
    where h.collar_id = ${sqlUuid(collarId)} and h.resource_key = 'brightness';
  `);
  return JSON.parse(output);
}

const container = databaseContainer();
const collars = Array.from({ length: 3 }, () => randomUUID());
const deviceIds = Array.from({ length: 3 }, () => randomUUID());

try {
  psql(container, `
    insert into api.collars (id, device_public_id, dog_id, display_name, state, linked_at)
    values
      (${sqlUuid(collars[0])}, ${sqlUuid(deviceIds[0])}, ${sqlUuid(DOG_ID)}, 'M1.11 concurrent distinct', 'active', statement_timestamp()),
      (${sqlUuid(collars[1])}, ${sqlUuid(deviceIds[1])}, ${sqlUuid(DOG_ID)}, 'M1.11 concurrent replay', 'active', statement_timestamp()),
      (${sqlUuid(collars[2])}, ${sqlUuid(deviceIds[2])}, ${sqlUuid(DOG_ID)}, 'M1.11 concurrent no-op', 'active', statement_timestamp());
  `);

  const firstIds = [randomUUID(), randomUUID()];
  const first = await Promise.all([
    mutate(container, {
      collarId: collars[0], mutationId: firstIds[0],
      baseServerVersion: 0, brightness: 40,
    }),
    mutate(container, {
      collarId: collars[0], mutationId: firstIds[1],
      baseServerVersion: 0, brightness: 41,
    }),
  ]);
  assertOneWinnerOneStale(first, 1);
  assert.deepEqual(
    { version: state(container, collars[0]).head_version, revisions: state(container, collars[0]).revisions },
    { version: 1, revisions: 1 },
  );

  const existing = await Promise.all([
    mutate(container, {
      collarId: collars[0], mutationId: randomUUID(),
      baseServerVersion: 1, brightness: 50,
    }),
    mutate(container, {
      collarId: collars[0], mutationId: randomUUID(),
      baseServerVersion: 1, brightness: 51,
    }),
  ]);
  assertOneWinnerOneStale(existing, 2);
  assert.equal(state(container, collars[0]).head_version, 2);
  assert.equal(state(container, collars[0]).revisions, 2);

  const replayId = randomUUID();
  const exactReplay = await Promise.all([
    mutate(container, {
      collarId: collars[1], mutationId: replayId,
      baseServerVersion: 0, brightness: 72,
    }),
    mutate(container, {
      collarId: collars[1], mutationId: replayId,
      baseServerVersion: 0, brightness: 72,
    }),
  ]);
  assert.equal(exactReplay.every(({ status }) => status === 200), true);
  assert.deepEqual(exactReplay[0].body, exactReplay[1].body);
  assert.equal(state(container, collars[1]).revisions, 1);

  const seed = await mutate(container, {
    collarId: collars[2], mutationId: randomUUID(),
    baseServerVersion: 0, brightness: 88,
  });
  assert.equal(seed.status, 200);
  const beforeNoop = state(container, collars[2]);
  const noops = await Promise.all([
    mutate(container, {
      collarId: collars[2], mutationId: randomUUID(),
      baseServerVersion: 1, brightness: 88,
    }),
    mutate(container, {
      collarId: collars[2], mutationId: randomUUID(),
      baseServerVersion: 1, brightness: 88,
    }),
  ]);
  assert.equal(noops.every(({ status, body }) =>
    status === 200 && body.disposition === "unchanged" && body.server_version === 1), true);
  const afterNoop = state(container, collars[2]);
  assert.deepEqual(
    {
      head_version: afterNoop.head_version,
      head_revision: afterNoop.head_revision,
      head_updated_at: afterNoop.head_updated_at,
      hlc_physical: afterNoop.hlc_physical,
      hlc_logical: afterNoop.hlc_logical,
      winning: afterNoop.winning,
      superseded: afterNoop.superseded,
      revisions: afterNoop.revisions,
    },
    {
      head_version: beforeNoop.head_version,
      head_revision: beforeNoop.head_revision,
      head_updated_at: beforeNoop.head_updated_at,
      hlc_physical: beforeNoop.hlc_physical,
      hlc_logical: beforeNoop.hlc_logical,
      winning: 1,
      superseded: 2,
      revisions: 3,
    },
  );

  console.log(
    "M1.11 RPC concurrency passed: first/existing one-winner, exact replay, and no-op receipts.",
  );
} finally {
  psql(container, `delete from api.collars where id in (${collars.map(sqlUuid).join(", ")});`);
}
