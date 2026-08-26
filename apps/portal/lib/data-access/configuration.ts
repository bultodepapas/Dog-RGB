import "server-only";

import type { Database } from "../database.generated";
import { createServerSupabaseClient } from "../supabase/server";
import {
  BRIGHTNESS_RESOURCE_KEY,
  BRIGHTNESS_RESOURCE_SCHEMA,
  createConfigurationDataAccess,
  type BrightnessHeadRecord,
  type BrightnessMutationInput,
  type BrightnessReportRecord,
  type BrightnessRpcResult,
  type ConfigurationDataDependencies,
} from "./configuration-core";
import {
  DogDataAccessError,
  type DogMembershipRecord,
  type DogRecord,
} from "./dogs-core";
import type { TodayCollarRecord } from "./today-core";

export {
  BRIGHTNESS_RESOURCE_KEY,
  BRIGHTNESS_RESOURCE_SCHEMA,
  CONFIG_REPORT_STATUSES,
  type BrightnessConfigurationDto,
  type ConfigReportStatus,
  type ConfigurationFreshness,
  type ConfigurationTruth,
} from "./configuration-core";

type ServerSupabaseClient = Awaited<
  ReturnType<typeof createServerSupabaseClient>
>;
type MembershipRow = Pick<
  Database["api"]["Tables"]["dog_memberships"]["Row"],
  "dog_id" | "role"
>;
type DogRow = Pick<
  Database["api"]["Tables"]["dogs"]["Row"],
  "id" | "name" | "timezone"
>;
type CollarRow = Pick<
  Database["api"]["Tables"]["collars"]["Row"],
  "id" | "dog_id" | "display_name" | "state" | "last_sync_at" | "linked_at"
>;
type HeadRow = Pick<
  Database["api"]["Tables"]["config_resource_heads"]["Row"],
  | "collar_id"
  | "resource_key"
  | "resource_schema"
  | "server_version"
  | "body"
  | "body_sha256"
  | "updated_at"
>;
type ReportRow = Pick<
  Database["api"]["Tables"]["config_reported"]["Row"],
  | "collar_id"
  | "resource_key"
  | "reported_server_version"
  | "reported_body_sha256"
  | "status"
  | "error_code"
  | "firmware_version"
  | "config_schema"
  | "device_applied_at"
  | "cloud_received_at"
>;

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

async function getFreshUserId(
  client: ServerSupabaseClient,
): Promise<string | null> {
  const { data, error } = await client.auth.getUser();
  return !error && typeof data.user?.id === "string" ? data.user.id : null;
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
  if (error) throw unavailable();
  return data satisfies MembershipRow | null;
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
  if (error) throw unavailable();
  return data satisfies DogRow | null;
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
  if (error) throw unavailable();
  return data satisfies CollarRow | null;
}

async function findBrightnessHead(
  client: ServerSupabaseClient,
  collarId: string,
): Promise<BrightnessHeadRecord | null> {
  const { data, error } = await client
    .from("config_resource_heads")
    .select(
      "collar_id, resource_key, resource_schema, server_version, body, body_sha256, updated_at",
    )
    .eq("collar_id", collarId)
    .eq("resource_key", BRIGHTNESS_RESOURCE_KEY)
    .limit(1)
    .maybeSingle();
  if (error) throw unavailable();
  return data satisfies HeadRow | null;
}

async function findBrightnessReport(
  client: ServerSupabaseClient,
  collarId: string,
): Promise<BrightnessReportRecord | null> {
  const { data, error } = await client
    .from("config_reported")
    .select(
      "collar_id, resource_key, reported_server_version, reported_body_sha256, status, error_code, firmware_version, config_schema, device_applied_at, cloud_received_at",
    )
    .eq("collar_id", collarId)
    .eq("resource_key", BRIGHTNESS_RESOURCE_KEY)
    .limit(1)
    .maybeSingle();
  if (error) throw unavailable();
  return data satisfies ReportRow | null;
}

function rpcFailure(error: unknown): BrightnessRpcResult {
  if (error !== null && typeof error === "object") {
    const candidate = error as { code?: unknown; message?: unknown };
    if (
      candidate.code === "PT409" &&
      candidate.message === "stale_base_server_version"
    ) return { ok: false, reason: "stale" };
    if (
      candidate.code === "23505" &&
      candidate.message === "mutation_id_conflict"
    ) return { ok: false, reason: "conflict" };
  }
  return { ok: false, reason: "ambiguous" };
}

async function invokeBrightnessMutation(
  client: ServerSupabaseClient,
  input: BrightnessMutationInput,
): Promise<BrightnessRpcResult> {
  const { data, error } = await client.rpc("mutate_config_resource_v1", {
    p_collar_id: input.collarId,
    p_resource_key: BRIGHTNESS_RESOURCE_KEY,
    p_resource_schema: BRIGHTNESS_RESOURCE_SCHEMA,
    p_mutation_id: input.mutationId,
    p_base_server_version: input.baseServerVersion,
    p_body: { brightness: input.brightness },
    p_body_sha256: input.bodySha256Hex,
  });
  if (error) return rpcFailure(error);
  return { ok: true, data };
}

const dependencies: ConfigurationDataDependencies<ServerSupabaseClient> = {
  createClient: createServerSupabaseClient,
  getFreshUserId,
  findMembership,
  findDog,
  findActiveCollar,
  findBrightnessHead,
  findBrightnessReport,
  invokeBrightnessMutation,
  now: () => new Date(),
};

const configurationDataAccess = createConfigurationDataAccess(dependencies);

export const getBrightnessConfiguration =
  configurationDataAccess.getBrightnessConfiguration;
export const mutateBrightness = configurationDataAccess.mutateBrightness;
