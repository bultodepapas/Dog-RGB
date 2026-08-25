import "server-only";

import type { Database } from "../database.generated";
import { createServerSupabaseClient } from "../supabase/server";
import {
  createDogDataAccess,
  DogDataAccessError,
  type DogDataDependencies,
  type DogMembershipRecord,
  type DogRecord,
} from "./dogs-core";
import type {
  TodayCollarRecord,
  TodayDailySummaryRecord,
  TodayRecordingRecord,
  TodayRecordingSummaryRecord,
} from "./today-core";
import {
  HISTORY_QUERY_LIMIT,
  type HistoryCursor,
  type HistoryRecordingRecord,
} from "./history-core";

export {
  DOG_ROLES,
  DogDataAccessError,
  type DogAccessDto,
  type DogCapability,
  type DogDataAccessErrorCode,
  type DogRole,
  type DogSummaryDto,
} from "./dogs-core";
export {
  TODAY_CLOCK_QUALITIES,
  TODAY_RECORDING_STATES,
  type TodayFreshness,
  type TodayRecordingState,
  type TodayRecordingTimeQuality,
  type TodaySnapshotDto,
} from "./today-core";
export {
  HISTORY_CLOCK_QUALITIES,
  HISTORY_CURSOR_MAX_LENGTH,
  HISTORY_PAGE_SIZE,
  HISTORY_QUERY_LIMIT,
  HISTORY_RECORDING_STATES,
  type HistoryPageDto,
  type HistoryRecordingState,
  type HistoryRecordingTimeQuality,
} from "./history-core";

type MembershipRow = Pick<
  Database["api"]["Tables"]["dog_memberships"]["Row"],
  "dog_id" | "role"
>;

type DogRow = Pick<
  Database["api"]["Tables"]["dogs"]["Row"],
  "id" | "name" | "timezone"
>;

type TodayCollarRow = Pick<
  Database["api"]["Tables"]["collars"]["Row"],
  "id" | "dog_id" | "display_name" | "state" | "last_sync_at" | "linked_at"
>;

type TodayDailySummaryRow = Pick<
  Database["api"]["Tables"]["daily_summaries"]["Row"],
  | "dog_id"
  | "local_date"
  | "timezone"
  | "coverage_ratio"
  | "unknown_s"
  | "algorithm_version"
  | "computed_at"
>;

type TodayRecordingRow = Pick<
  Database["api"]["Tables"]["recordings"]["Row"],
  | "id"
  | "collar_id"
  | "started_at"
  | "ended_at"
  | "created_at"
  | "state"
  | "point_count"
  | "clock_quality"
>;

type TodayRecordingSummaryRow = Pick<
  Database["api"]["Tables"]["recording_summaries"]["Row"],
  | "recording_id"
  | "coverage_ratio"
  | "algorithm_version"
  | "computed_at"
>;

type HistoryRecordingRow = Pick<
  Database["api"]["Tables"]["recordings"]["Row"],
  "id" | "collar_id" | "started_at" | "state" | "point_count" | "clock_quality"
> & Readonly<{
  collar: Pick<
    Database["api"]["Tables"]["collars"]["Row"],
    "id" | "dog_id" | "display_name"
  > | null;
}>;

type ServerSupabaseClient = Awaited<
  ReturnType<typeof createServerSupabaseClient>
>;

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

async function getFreshUserId(
  client: ServerSupabaseClient,
): Promise<string | null> {
  // Unlike getClaims(), getUser() checks the session with the Auth server. DAL
  // entry points require that freshness so a revoked/stale session fails closed.
  const { data, error } = await client.auth.getUser();
  const userId = data.user?.id;

  return !error && typeof userId === "string" ? userId : null;
}

async function findMembership(
  client: ServerSupabaseClient,
  userId: string,
  dogId: string,
): Promise<DogMembershipRecord | null> {
  const { data, error } = await client
    .from("dog_memberships")
    .select("dog_id, role")
    .eq("dog_id", dogId)
    .eq("user_id", userId)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies MembershipRow | null;
}

async function listMemberships(
  client: ServerSupabaseClient,
  userId: string,
): Promise<DogMembershipRecord[]> {
  const { data, error } = await client
    .from("dog_memberships")
    .select("dog_id, role")
    .eq("user_id", userId)
    .order("dog_id", { ascending: true });

  if (error) {
    throw unavailable();
  }

  return (data ?? []) satisfies MembershipRow[];
}

async function findDog(
  client: ServerSupabaseClient,
  dogId: string,
): Promise<DogRecord | null> {
  const { data, error } = await client
    .from("dogs")
    .select("id, name, timezone")
    .eq("id", dogId)
    .is("deleted_at", null)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies DogRow | null;
}

async function listDogs(
  client: ServerSupabaseClient,
  dogIds: readonly string[],
): Promise<DogRecord[]> {
  if (dogIds.length === 0) {
    return [];
  }

  const { data, error } = await client
    .from("dogs")
    .select("id, name, timezone")
    .in("id", [...dogIds])
    .is("deleted_at", null)
    .order("name", { ascending: true })
    .order("id", { ascending: true });

  if (error) {
    throw unavailable();
  }

  return (data ?? []) satisfies DogRow[];
}

async function findActiveCollar(
  client: ServerSupabaseClient,
  dogId: string,
): Promise<TodayCollarRecord | null> {
  const { data, error } = await client
    .from("collars")
    .select("id, dog_id, display_name, state, last_sync_at, linked_at")
    .eq("dog_id", dogId)
    .eq("state", "active")
    .order("last_sync_at", { ascending: false, nullsFirst: false })
    .order("linked_at", { ascending: false, nullsFirst: false })
    .order("id", { ascending: true })
    .limit(1)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies TodayCollarRow | null;
}

async function findDailySummary(
  client: ServerSupabaseClient,
  dogId: string,
  localDate: string,
): Promise<TodayDailySummaryRecord | null> {
  const { data, error } = await client
    .from("daily_summaries")
    .select(
      "dog_id, local_date, timezone, coverage_ratio, unknown_s, algorithm_version, computed_at",
    )
    .eq("dog_id", dogId)
    .eq("local_date", localDate)
    .order("algorithm_version", { ascending: false })
    .limit(1)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies TodayDailySummaryRow | null;
}

async function findLatestRecording(
  client: ServerSupabaseClient,
  collarId: string,
): Promise<TodayRecordingRecord | null> {
  const { data, error } = await client
    .from("recordings")
    .select(
      "id, collar_id, started_at, ended_at, created_at, state, point_count, clock_quality",
    )
    .eq("collar_id", collarId)
    .order("started_at", { ascending: false, nullsFirst: false })
    .order("created_at", { ascending: false })
    .order("id", { ascending: false })
    .limit(1)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies TodayRecordingRow | null;
}

async function findRecordingSummary(
  client: ServerSupabaseClient,
  recordingId: string,
): Promise<TodayRecordingSummaryRecord | null> {
  const { data, error } = await client
    .from("recording_summaries")
    .select("recording_id, coverage_ratio, algorithm_version, computed_at")
    .eq("recording_id", recordingId)
    .order("algorithm_version", { ascending: false })
    .limit(1)
    .maybeSingle();

  if (error) {
    throw unavailable();
  }

  return data satisfies TodayRecordingSummaryRow | null;
}

async function listHistoryRecordings(
  client: ServerSupabaseClient,
  dogId: string,
  cursor: HistoryCursor | null,
): Promise<HistoryRecordingRecord[]> {
  let query = client
    .from("recordings")
    .select(
      "id, collar_id, started_at, state, point_count, clock_quality, collar:collars!recordings_collar_id_fkey!inner(id, dog_id, display_name)",
    )
    .eq("collar.dog_id", dogId);

  if (cursor?.bucket === "known") {
    // The raw PostgREST expression is assembled only from the strictly decoded,
    // normalized timestamp and canonical UUID; raw search input never reaches it.
    query = query.or(
      `started_at.lt.${cursor.startedAt},and(started_at.eq.${cursor.startedAt},id.lt.${cursor.id}),started_at.is.null`,
    );
  } else if (cursor?.bucket === "unknown") {
    query = query.is("started_at", null).lt("id", cursor.id);
  }

  const { data, error } = await query
    .order("started_at", { ascending: false, nullsFirst: false })
    .order("id", { ascending: false })
    .limit(HISTORY_QUERY_LIMIT);

  if (error) {
    throw unavailable();
  }

  const rows = (data ?? []) as unknown as HistoryRecordingRow[];
  return rows.map((row) => ({
    id: row.id,
    collar_id: row.collar_id,
    joined_collar_id: row.collar?.id,
    dog_id: row.collar?.dog_id,
    collar_display_name: row.collar?.display_name,
    started_at: row.started_at,
    state: row.state,
    point_count: row.point_count,
    clock_quality: row.clock_quality,
  })) as HistoryRecordingRecord[];
}

const dependencies: DogDataDependencies<ServerSupabaseClient> = {
  createClient: createServerSupabaseClient,
  getFreshUserId,
  findMembership,
  listMemberships,
  findDog,
  listDogs,
  now: () => new Date(),
  findActiveCollar,
  findDailySummary,
  findLatestRecording,
  findRecordingSummary,
  listHistoryRecordings,
};

// The singleton holds dependency functions only. It never retains a Supabase
// client, user identity, session, or query result across requests.
const dogDataAccess = createDogDataAccess(dependencies);

export const requireDogAccess = dogDataAccess.requireDogAccess;
export const getDogSummary = dogDataAccess.getDogSummary;
export const getTodaySnapshot = dogDataAccess.getTodaySnapshot;
export const getHistoryPage = dogDataAccess.getHistoryPage;
export const listDogSummaries = dogDataAccess.listDogSummaries;
