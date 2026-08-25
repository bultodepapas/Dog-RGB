"use server";

import { redirect } from "next/navigation";

import {
  type AuthActionState,
  parseCredentialsForm,
  parseEmailForm,
  parseNewPasswordForm,
  parseSignupForm,
} from "../../lib/auth/form";
import { getVerifiedIdentity } from "../../lib/supabase/identity";
import { createServerSupabaseClient } from "../../lib/supabase/server";

const LOGIN_ERROR =
  "No pudimos iniciar sesión. Revisa las credenciales y confirma tu correo.";
const SIGNUP_SUCCESS =
  "Si la dirección puede registrarse, recibirás un enlace de confirmación. Revísalo en Mailpit durante el desarrollo local.";
const RECOVERY_SUCCESS =
  "Si existe una cuenta para esa dirección, recibirás un enlace para cambiar la contraseña.";

export async function loginAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const parsed = parseCredentialsForm(formData);
  if (!parsed.ok) {
    return parsed.state;
  }

  const supabase = await createServerSupabaseClient();
  const { error } = await supabase.auth.signInWithPassword(parsed.value);

  if (error) {
    return { status: "error", message: LOGIN_ERROR };
  }

  redirect("/");
}

export async function signupAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const parsed = parseSignupForm(formData);
  if (!parsed.ok) {
    return parsed.state;
  }

  const supabase = await createServerSupabaseClient();
  const { error } = await supabase.auth.signUp(parsed.value);

  if (error) {
    return {
      status: "error",
      message:
        "No pudimos procesar el registro ahora. Espera un momento e inténtalo de nuevo.",
    };
  }

  return { status: "success", message: SIGNUP_SUCCESS };
}

export async function requestPasswordResetAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const parsed = parseEmailForm(formData);
  if (!parsed.ok) {
    return parsed.state;
  }

  const supabase = await createServerSupabaseClient();
  await supabase.auth.resetPasswordForEmail(parsed.value.email);

  // Deliberately return the same response whether the account exists or the
  // provider rejects the request. This avoids an email-enumeration oracle.
  return { status: "success", message: RECOVERY_SUCCESS };
}

export async function updatePasswordAction(
  _previousState: AuthActionState,
  formData: FormData,
): Promise<AuthActionState> {
  const parsed = parseNewPasswordForm(formData);
  if (!parsed.ok) {
    return parsed.state;
  }

  const identity = await getVerifiedIdentity();
  if (!identity) {
    return {
      status: "error",
      message: "El enlace de recuperación no es válido o ya expiró.",
    };
  }

  const supabase = await createServerSupabaseClient();
  const { error } = await supabase.auth.updateUser({
    password: parsed.value.password,
  });

  if (error) {
    return {
      status: "error",
      message:
        "No pudimos cambiar la contraseña. Solicita un enlace nuevo e inténtalo otra vez.",
    };
  }

  await supabase.auth.signOut({ scope: "local" });
  redirect("/login?password_updated=1");
}

export async function logoutAction(): Promise<never> {
  const supabase = await createServerSupabaseClient();
  await supabase.auth.signOut({ scope: "local" });
  redirect("/login?logged_out=1");
}
