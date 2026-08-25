import { createClient } from "@supabase/supabase-js";

const supabaseUrl = process.env.SUPABASE_URL;
const supabaseAnonKey = process.env.SUPABASE_ANON_KEY;
const fixtureDogId =
  process.env.M19_HISTORY_DOG_ID ?? "31900000-0000-4000-8000-000000000001";
const email = process.env.M19_HISTORY_EMAIL ?? "owner@example.test";
const password = process.env.M19_HISTORY_PASSWORD ?? "local-owner-password";
const maximumExecutionMs = process.env.M19_HISTORY_MAX_EXECUTION_MS
  ? Number(process.env.M19_HISTORY_MAX_EXECUTION_MS)
  : null;

if (!supabaseUrl || !supabaseAnonKey) {
  throw new Error("SUPABASE_URL and SUPABASE_ANON_KEY are required");
}

if (
  maximumExecutionMs !== null &&
  (!Number.isFinite(maximumExecutionMs) || maximumExecutionMs <= 0)
) {
  throw new Error("M19_HISTORY_MAX_EXECUTION_MS must be a positive number");
}

const client = createClient(supabaseUrl, supabaseAnonKey, {
  auth: { autoRefreshToken: false, persistSession: false },
  db: { schema: "api" },
});

const { error: signInError } = await client.auth.signInWithPassword({
  email,
  password,
});

if (signInError) {
  throw signInError;
}

function baseQuery() {
  return client
    .from("recordings")
    .select(
      "id, collar_id, started_at, state, point_count, clock_quality, collar:collars!recordings_collar_id_fkey!inner(id, dog_id, display_name)",
    )
    .eq("collar.dog_id", fixtureDogId);
}

const cases = [
  ["first", baseQuery()],
  [
    "known",
    baseQuery().or(
      "started_at.lt.2026-01-01T01:06:40.000Z,and(started_at.eq.2026-01-01T01:06:40.000Z,id.lt.71000000-0000-4000-8000-000000004000),started_at.is.null",
    ),
  ],
  [
    "null",
    baseQuery()
      .is("started_at", null)
      .lt("id", "71000000-0000-4000-8000-000000009001"),
  ],
];

let failed = false;

for (const [name, query] of cases) {
  const { data, error } = await query
    .order("started_at", { ascending: false, nullsFirst: false })
    .order("id", { ascending: false })
    .limit(21)
    .explain({ analyze: true, buffers: true, settings: true });

  if (error) {
    throw error;
  }

  const plan = String(data);
  const executionMatch = plan.match(/Execution Time: ([0-9.]+) ms/u);
  const executionMs = executionMatch ? Number(executionMatch[1]) : null;
  console.log(`=== ${name.toUpperCase()} ===`);
  console.log(plan);

  if (
    maximumExecutionMs !== null &&
    (executionMs === null || executionMs > maximumExecutionMs)
  ) {
    failed = true;
    console.error(
      `History ${name} page exceeded ${maximumExecutionMs} ms: ${executionMs ?? "unparsed"}`,
    );
  }
}

await client.auth.signOut();

if (failed) {
  process.exitCode = 1;
}
