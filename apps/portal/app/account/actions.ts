"use server";

import { revalidatePath } from "next/cache";
import { isCanonicalUuid } from "../../lib/auth/protected-route";
import { createServerSupabaseClient } from "../../lib/supabase/server";

export async function retryDeletionAction(form: FormData) {
  const id = form.get("jobId");
  if (typeof id !== "string" || !isCanonicalUuid(id) || form.getAll("jobId").length !== 1) return;
  const client = await createServerSupabaseClient();
  if (!(await client.auth.getUser()).data.user) return;
  const { error } = await client.rpc("retry_my_deletion_job_v1", { p_job_id: id });
  if (error) throw new Error("No pudimos reprogramar el borrado. Recarga e inténtalo otra vez.");
  revalidatePath("/account");
}
