"use server";

import { redirect } from "next/navigation";
import { revalidatePath } from "next/cache";
import { isCanonicalUuid } from "../../lib/auth/protected-route";
import { ACCOUNT_CONFIRMATION, accountDeletion, type AccountActionState } from "../../lib/privacy/account";
import { createServerSupabaseClient } from "../../lib/supabase/server";

export async function accountDeletionAction(_state: AccountActionState, form: FormData): Promise<AccountActionState> {
  const action = form.get("action");
  if (form.getAll("action").length !== 1 || !["request", "retry"].includes(String(action))) return { status: "error", message: "Solicitud no válida. Actualiza esta página." };
  try {
    const client = await createServerSupabaseClient();
    const identity = await client.auth.getUser();
    if (!identity.data.user?.email || identity.error) throw new Error("authentication_required");
    if (action === "request") {
      const password = form.get("password");
      if (form.getAll("password").length !== 1 || typeof password !== "string" || password.length < 1 || password.length > 128) throw new Error("reauthentication_required");
      const verified = await client.auth.signInWithPassword({ email: identity.data.user.email, password });
      if (verified.error || verified.data.user?.id !== identity.data.user.id) throw new Error("reauthentication_required");
    }
    if (action === "request") {
      const password = form.get("password"), requestId = form.get("requestId"), scopeHash = form.get("scopeHash");
      if (["password", "requestId", "scopeHash", "confirmation"].some(key => form.getAll(key).length !== 1) ||
          typeof password !== "string" || password.length < 1 || password.length > 128 ||
          typeof requestId !== "string" || !isCanonicalUuid(requestId) || typeof scopeHash !== "string" || scopeHash.length > 128 ||
          form.get("confirmation") !== ACCOUNT_CONFIRMATION) return { status: "error", message: "Escribe la frase completa y tu contraseña actual." };
      const result = await client.functions.invoke("user-v1-account-deletion", { body: {
        action, request_id: requestId, scope_sha256: scopeHash, confirmation_version: "account-delete-v1", confirmation_phrase: ACCOUNT_CONFIRMATION,
      } });
      if (result.error) throw new Error("request_failed");
      const state = accountDeletion(result.data);
      if (state.status !== "pending" && state.status !== "ready") throw new Error("request_blocked");
    } else if (action === "retry") {
      const { error } = await client.rpc("retry_my_account_deletion_v1");
      if (error) throw new Error("retry_failed");
    }
  } catch {
    return { status: "error", message: "No pudimos completar la operación. Revisa tu contraseña, actualiza el inventario y consulta el estado antes de reintentar. No se considera completada una purga pendiente." };
  }
  revalidatePath("/account");
  redirect("/account");
}
