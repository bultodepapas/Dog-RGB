import type { Metadata } from "next";
import Link from "next/link";

import { SignupForm } from "../components/auth-forms";
import { AuthShell } from "../components/auth-shell";

export const metadata: Metadata = { title: "Crear cuenta | Dog RGB" };
export const dynamic = "force-dynamic";

export default function SignupPage() {
  return (
    <AuthShell
      eyebrow="CUENTA DE PROPIETARIO / LOCAL"
      title="Crea tu acceso privado."
      description="La cuenta extiende el collar; no reemplaza sus funciones locales. Confirma el correo antes de vincular un collar."
      footer={
        <p>
          ¿Ya tienes cuenta? <Link href="/login">Inicia sesión</Link>.
        </p>
      }
    >
      <SignupForm />
    </AuthShell>
  );
}
