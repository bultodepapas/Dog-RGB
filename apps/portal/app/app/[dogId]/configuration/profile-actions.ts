"use server";

import { revalidatePath } from "next/cache";
import { renameDogHandler, type RenameState } from "../../../../lib/profile/rename";
import { getDogSummary } from "../../../../lib/data-access/dogs";
import { createServerSupabaseClient } from "../../../../lib/supabase/server";

const rename = renameDogHandler(async (dogId, name) => {
  await getDogSummary(dogId, "admin");
  const client = await createServerSupabaseClient();
  const { data, error } = await client.rpc("rename_dog_v1", { p_dog_id: dogId, p_name: name });
  if (error || data !== dogId) throw new Error("rename_failed");
  revalidatePath(`/app/${dogId}`, "layout");
  revalidatePath("/onboarding");
});
export async function renameDogAction(_previous: RenameState, form: FormData): Promise<RenameState> {
  return rename(form);
}
