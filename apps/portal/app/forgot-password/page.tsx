import type { Metadata } from "next";
import Link from "next/link";

import { getVerifiedIdentity } from "../../lib/supabase/identity";
import {
  PasswordResetRequestForm,
  PasswordUpdateForm,
} from "../components/auth-forms";
import { AuthShell } from "../components/auth-shell";

export const metadata: Metadata = { title: "Recuperar acceso | Dog RGB" };
export const dynamic = "force-dynamic";

type ForgotPasswordPageProps = Readonly<{
  searchParams: Promise<Record<string, string | string[] | undefined>>;
}>;

function first(value: string | string[] | undefined): string | undefined {
  return Array.isArray(value) ? value[0] : value;
}

export default async function ForgotPasswordPage({
  searchParams,
}: ForgotPasswordPageProps) {
  const params = await searchParams;
  const wantsUpdate = first(params.mode) === "update";
  const identity = wantsUpdate ? await getVerifiedIdentity() : null;
  const canUpdate = wantsUpdate && identity !== null;
  const linkError = first(params.auth_error) || (wantsUpdate && !identity);

  return (
    <AuthShell
      eyebrow="RECUPERACIÓN DE CUENTA"
      title={canUpdate ? "Define una contraseña nueva." : "Recupera tu acceso."}
      description={
        canUpdate
          ? "El enlace fue verificado para esta sesión. Al guardar, volverás al inicio de sesión."
          : "Enviaremos un enlace de un solo uso si existe una cuenta para ese correo."
      }
      footer={
        <p>
          <Link href="/login">Volver a iniciar sesión</Link>
        </p>
      }
    >
      {linkError ? (
        <p className="page-notice page-notice--error" role="alert">
          El enlace no es válido, ya fue utilizado o expiró. Solicita uno nuevo.
        </p>
      ) : null}
      {canUpdate ? <PasswordUpdateForm /> : <PasswordResetRequestForm />}
    </AuthShell>
  );
}
