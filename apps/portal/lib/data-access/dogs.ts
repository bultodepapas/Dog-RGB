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

export {
  DOG_ROLES,
  DogDataAccessError,
  type DogAccessDto,
  type DogCapability,
  type DogDataAccessErrorCode,
  type DogRole,
  type DogSummaryDto,
} from "./dogs-core";

type MembershipRow = Pick<
  Database["api"]["Tables"]["dog_memberships"]["Row"],
  "dog_id" | "role"
>;

type DogRow = Pick<
  Database["api"]["Tables"]["dogs"]["Row"],
  "id" | "name" | "timezone"
>;

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

const dependencies: DogDataDependencies<ServerSupabaseClient> = {
  createClient: createServerSupabaseClient,
  getFreshUserId,
  findMembership,
  listMemberships,
  findDog,
  listDogs,
};

// The singleton holds dependency functions only. It never retains a Supabase
// client, user identity, session, or query result across requests.
const dogDataAccess = createDogDataAccess(dependencies);

export const requireDogAccess = dogDataAccess.requireDogAccess;
export const getDogSummary = dogDataAccess.getDogSummary;
export const listDogSummaries = dogDataAccess.listDogSummaries;
