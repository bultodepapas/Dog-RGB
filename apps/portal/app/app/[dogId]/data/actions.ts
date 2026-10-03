"use server";

import { redirect } from "next/navigation";
import { getDogSummary } from "../../../../lib/data-access/dogs";
import { parseDeletionForm, type DeletionState } from "../../../../lib/privacy/deletion";
import { createServerSupabaseClient } from "../../../../lib/supabase/server";

export async function deleteDogAction(_state: DeletionState, form: FormData): Promise<DeletionState> {
  const input = parseDeletionForm(form);
  if (!input) return { status: "error", message: "Escribe ELIMINAR y tu contraseña actual para confirmar." };
  try {
    await getDogSummary(input.dogId, "admin");
    const client = await createServerSupabaseClient();
    const identity = await client.auth.getUser();
    if (!identity.data.user?.email || identity.error) throw new Error("identity_required");
    const verified = await client.auth.signInWithPassword({ email: identity.data.user.email, password: input.password });
    if (verified.error || verified.data.user?.id !== identity.data.user.id) throw new Error("reauthentication_required");
    const { error } = await client.rpc("request_dog_deletion_v1", {
      p_dog_id: input.dogId, p_request_id: input.requestId, p_confirmation_version: "dog-delete-v1",
    });
    if (error) throw new Error("deletion_unavailable");
  } catch {
    return { status: "error", message: "No pudimos confirmar el borrado. Comprueba tu contraseña y consulta Cuenta antes de reintentar." };
  }
  redirect("/account");
}
