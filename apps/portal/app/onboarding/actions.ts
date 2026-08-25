"use server";

import { redirect } from "next/navigation";

import {
  dogAppPath,
  isCanonicalUuid,
} from "../../lib/auth/protected-route";
import {
  type CreateDogActionState,
  createDogMutationHandler,
} from "../../lib/onboarding/create-dog";
import { createServerSupabaseClient } from "../../lib/supabase/server";

type ServerSupabaseClient = Awaited<
  ReturnType<typeof createServerSupabaseClient>
>;

const createDogMutation = createDogMutationHandler<ServerSupabaseClient>({
  createClient: createServerSupabaseClient,
  isCanonicalDogId: isCanonicalUuid,
  async getFreshUserId(client) {
    const { data, error } = await client.auth.getUser();
    const userId = data.user?.id;
    return !error && typeof userId === "string" && userId.length > 0
      ? userId
      : null;
  },
  async callCreateDogRpc(client, input) {
    const { data, error } = await client.rpc("create_dog_v1", {
      p_name: input.name,
      p_timezone: input.timezone,
    });
    return error ? null : data;
  },
});

export async function createDogAction(
  _previousState: CreateDogActionState,
  formData: FormData,
): Promise<CreateDogActionState> {
  const result = await createDogMutation(formData);
  if (!result.ok) {
    return result.state;
  }

  redirect(dogAppPath(result.dogId, "today"));
}
