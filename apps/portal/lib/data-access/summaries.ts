import "server-only";
import { getDogSummary, DogDataAccessError } from "./dogs";
import { createServerSupabaseClient } from "../supabase/server";
import { summaryDto } from "./summary-core";

const COLUMNS = "observed_s,moving_s,inactive_s,unknown_s,distance_m,average_moving_cmps,filtered_max_speed_cmps,coverage_ratio,gap_count,dropped_points,algorithm_version,computed_at,source_received_at,window_start,window_end,source_schema_min,source_schema_max,summary_status" as const;

export async function getComputedSummary(dogId: string, scope: Readonly<{ localDate: string }> | Readonly<{ recordingId: string }>) {
  const dog = await getDogSummary(dogId, "read");
  const client = await createServerSupabaseClient();
  const daily = "localDate" in scope;
  const freshness = await client.rpc("summary_freshness_v1", {
    p_dog_id: dogId, ...(daily ? { p_local_date: scope.localDate } : { p_recording_id: scope.recordingId }),
  });
  if (freshness.error) throw new DogDataAccessError("data_unavailable");
  if (!freshness.data) throw new DogDataAccessError("access_denied");
  const result = daily
    ? await client.from("daily_summaries").select(COLUMNS).eq("dog_id", dogId).eq("local_date", scope.localDate).eq("timezone", dog.timezone).order("algorithm_version", { ascending: false }).limit(1).maybeSingle()
    : await client.from("recording_summaries").select(COLUMNS).eq("recording_id", scope.recordingId).order("algorithm_version", { ascending: false }).limit(1).maybeSingle();
  if (result.error) throw new DogDataAccessError("data_unavailable");
  return summaryDto(result.data, freshness.data);
}

export async function getHistorySummaryStates(dogId: string, ids: readonly string[]) {
  if (ids.length > 20) throw new DogDataAccessError("data_unavailable");
  if (!ids.length) return {};
  await getDogSummary(dogId, "read");
  const client = await createServerSupabaseClient();
  const recordings = await client.from("recordings").select("id,updated_at,collar:collars!inner(dog_id)")
    .in("id", [...ids]).eq("collar.dog_id", dogId).limit(20);
  if (recordings.error) throw new DogDataAccessError("data_unavailable");
  // Read the newest row for each visible recording. If a newer algorithm is
  // unknown, summaryDto must surface it as unavailable rather than falling
  // back to stale v1 metrics.
  const latestSummaries = await Promise.all((recordings.data ?? []).map(recording =>
    client.from("recording_summaries").select(`recording_id,${COLUMNS}`)
      .eq("recording_id", recording.id).order("algorithm_version", { ascending: false }).limit(1).maybeSingle(),
  ));
  if (latestSummaries.some(result => result.error)) throw new DogDataAccessError("data_unavailable");
  return Object.fromEntries((recordings.data ?? []).map(recording => {
    const row = latestSummaries.find(result => result.data?.recording_id === recording.id)?.data ?? null;
    const pending = !row || !row.source_received_at || Date.parse(recording.updated_at) > Date.parse(row.source_received_at);
    const state = summaryDto(row, { ...row, status: pending ? "pending" : row?.summary_status, pending });
    return [recording.id, state];
  }));
}
