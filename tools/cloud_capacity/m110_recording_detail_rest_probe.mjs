import assert from "node:assert/strict";
import { createClient } from "@supabase/supabase-js";

const supabaseUrl = process.env.SUPABASE_URL;
const anonKey = process.env.SUPABASE_ANON_KEY;
if (!supabaseUrl || !anonKey) throw new Error("SUPABASE_URL and SUPABASE_ANON_KEY are required");
const origin = new URL(supabaseUrl);
if (!new Set(["127.0.0.1", "localhost", "::1"]).has(origin.hostname)) {
  throw new Error("M1.10 fixture credentials may be used only against loopback Supabase");
}

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const COLLAR_ID = "84000000-0000-4000-8000-000000000002";
const RECORDING_ID = "84000000-0000-4000-8000-000000000004";
const RECORDING_SELECT = "id,collar_id,boot_sequence,started_at,ended_at,timezone_at_start,state,first_point_sequence,last_point_sequence,point_count,clock_quality,telemetry_schema,firmware_version,collar:collars!recordings_collar_id_fkey!inner(id,dog_id,display_name)";
const POINT_SELECT = "point_sequence,recorded_at,lat_e7,lon_e7,reported_speed_cmps,satellites,flags,time_quality";

function restUrl(table, parameters) {
  const url = new URL(`/rest/v1/${table}`, supabaseUrl);
  Object.entries(parameters).forEach(([key, value]) => url.searchParams.set(key, value));
  return url;
}

async function token(email, password) {
  const client = createClient(supabaseUrl, anonKey, {
    auth: { autoRefreshToken: false, persistSession: false },
  });
  const { data, error } = await client.auth.signInWithPassword({ email, password });
  if (error || !data.session?.access_token) throw error ?? new Error(`No session for ${email}`);
  return data.session.access_token;
}

async function rawRows(url, accessToken) {
  const headers = { apikey: anonKey, Accept: "application/json" };
  if (accessToken) headers.Authorization = `Bearer ${accessToken}`;
  const response = await fetch(url, { headers });
  const body = await response.json();
  return { body, status: response.status };
}

const recordingUrl = restUrl("recordings", {
  select: RECORDING_SELECT,
  id: `eq.${RECORDING_ID}`,
  "collar.dog_id": `eq.${DOG_ID}`,
  limit: "1",
});
const firstPointsUrl = restUrl("telemetry_points", {
  select: POINT_SELECT,
  collar_id: `eq.${COLLAR_ID}`,
  boot_sequence: "eq.10",
  point_sequence: "gte.1",
  and: "(point_sequence.lte.106)",
  order: "point_sequence.asc",
  limit: "101",
});
const deepPointsUrl = new URL(firstPointsUrl);
deepPointsUrl.searchParams.set("and", "(point_sequence.lte.106,point_sequence.gt.101)");

const identities = [
  ["owner", "owner@example.test", "local-owner-password", 1, 101, 5],
  ["viewer", "other@example.test", "local-other-password", 1, 101, 5],
  ["outsider", "recording-outsider@example.test", "local-recording-outsider-password", 0, 0, 0],
];

for (const [name, email, password, recordingCount, firstCount, deepCount] of identities) {
  const accessToken = await token(email, password);
  const [recording, first, deep] = await Promise.all([
    rawRows(recordingUrl, accessToken),
    rawRows(firstPointsUrl, accessToken),
    rawRows(deepPointsUrl, accessToken),
  ]);
  assert.equal(recording.status, 200, `${name} recording status`);
  assert.equal(first.status, 200, `${name} first points status`);
  assert.equal(deep.status, 200, `${name} deep points status`);
  assert.equal(recording.body.length, recordingCount, `${name} recording rows`);
  assert.equal(first.body.length, firstCount, `${name} first point rows`);
  assert.equal(deep.body.length, deepCount, `${name} deep point rows`);
  if (firstCount > 0) {
    assert.equal(first.body[0].point_sequence, 1);
    assert.equal(first.body.at(-1).point_sequence, 102);
    assert.equal(deep.body[0].point_sequence, 102);
    assert.equal(deep.body.at(-1).point_sequence, 106);
  }
  console.log(`${name}: recording=${recording.body.length} first=${first.body.length} deep=${deep.body.length}`);
}

const anonymous = await rawRows(firstPointsUrl, null);
assert.ok(new Set([401, 403]).has(anonymous.status));
assert.equal(Array.isArray(anonymous.body), false);
console.log(`anonymous: status=${anonymous.status} rows=denied`);
