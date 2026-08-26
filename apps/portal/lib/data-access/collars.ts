import "server-only";

import type { Database } from "../database.generated";
import { createServerSupabaseClient } from "../supabase/server";
import {
  createCollarDataAccess,
  type CollarDataDependencies,
  type CollarIdentityRecord,
  type CollarPageRecord,
} from "./collars-core";
import {
  DogDataAccessError,
  type DogMembershipRecord,
  type DogRecord,
} from "./dogs-core";

export {
  type CollarFreshness,
  type CollarLifecycleState,
  type CollarPageDto,
  type CollarRevokeResult,
} from "./collars-core";

type ServerSupabaseClient = Awaited<ReturnType<typeof createServerSupabaseClient>>;
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
  | "id"
  | "dog_id"
  | "display_name"
  | "state"
  | "hardware_revision"
  | "firmware_version"
  | "protocol_version"
  | "telemetry_schema"
  | "config_schema"
  | "capability_manifest"
  | "linked_at"
  | "last_sync_at"
  | "revoked_at"
  | "diagnostics_observed_at"
  | "outbox_chunks"
  | "outbox_points"
  | "outbox_used_bytes"
  | "outbox_capacity_bytes"
  | "oldest_unacknowledged_at"
  | "dropped_points_total"
  | "sync_error_present"
>;
type CollarIdentityRow = Pick<
  Database["api"]["Tables"]["collars"]["Row"],
  "id" | "dog_id" | "state" | "revoked_at"
>;

const COLLAR_COLUMNS =
  "id, dog_id, display_name, state, hardware_revision, firmware_version, protocol_version, telemetry_schema, config_schema, capability_manifest, linked_at, last_sync_at, revoked_at, diagnostics_observed_at, outbox_chunks, outbox_points, outbox_used_bytes, outbox_capacity_bytes, oldest_unacknowledged_at, dropped_points_total, sync_error_present";

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

async function getFreshUserId(client: ServerSupabaseClient): Promise<string | null> {
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
): Promise<CollarPageRecord | null> {
  const { data, error } = await client
    .from("collars")
    .select(COLLAR_COLUMNS)
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

async function findCollarIdentity(
  client: ServerSupabaseClient,
  dogId: string,
  collarId: string,
): Promise<CollarIdentityRecord | null> {
  const { data, error } = await client
    .from("collars")
    .select("id, dog_id, state, revoked_at")
    .eq("id", collarId)
    .eq("dog_id", dogId)
    .limit(1)
    .maybeSingle();
  if (error) throw unavailable();
  return data satisfies CollarIdentityRow | null;
}

async function invokeRevoke(
  client: ServerSupabaseClient,
  collarId: string,
) {
  const { data, error } = await client.rpc("revoke_collar_v1", {
    p_collar_id: collarId,
  });
  return error ? { ok: false as const } : { ok: true as const, data };
}

const dependencies: CollarDataDependencies<ServerSupabaseClient> = {
  createClient: createServerSupabaseClient,
  getFreshUserId,
  findMembership,
  findDog,
  findActiveCollar,
  findCollarIdentity,
  invokeRevoke,
  now: () => new Date(),
};

const collarDataAccess = createCollarDataAccess(dependencies);

export const getCollarPage = collarDataAccess.getCollarPage;
export const revokeCollar = collarDataAccess.revokeCollar;
