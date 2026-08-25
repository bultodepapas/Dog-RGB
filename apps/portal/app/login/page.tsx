import type { Metadata } from "next";
import Link from "next/link";

import { resolveProtectedReturnPath } from "../../lib/auth/protected-route";
import { getFreshIdentity } from "../../lib/supabase/identity";
import { logoutAction } from "../auth/actions";
import { LoginForm } from "../components/auth-forms";
import { AuthShell } from "../components/auth-shell";

export const metadata: Metadata = { title: "Iniciar sesión | Dog RGB" };
export const dynamic = "force-dynamic";

type LoginPageProps = Readonly<{
  searchParams: Promise<Record<string, string | string[] | undefined>>;
}>;

function first(value: string | string[] | undefined): string | undefined {
  return Array.isArray(value) ? value[0] : value;
}

export default async function LoginPage({ searchParams }: LoginPageProps) {
  const [params, identity] = await Promise.all([
    searchParams,
    getFreshIdentity(),
  ]);
  const authError = first(params.auth_error);
  const nextPath = resolveProtectedReturnPath(first(params.next));
  const notice = first(params.logged_out)
    ? "La sesión de este dispositivo se cerró correctamente."
    : first(params.password_updated)
      ? "La contraseña cambió. Inicia sesión con la nueva contraseña."
      : first(params.confirmed)
        ? "El correo quedó confirmado."
        : null;

  return (
    <AuthShell
      eyebrow="SESIÓN DE PROPIETARIO"
      title={identity ? "La sesión está verificada." : "Vuelve a tu collar."}
      description={
        identity
          ? "Este navegador conserva una sesión válida. Puedes continuar al área privada protegida."
          : "Usa la cuenta confirmada para acceder a la extensión web opcional de Dog RGB."
      }
      footer={
        identity ? (
          <p>
            También puedes volver al <Link href="/">estado local</Link>.
          </p>
        ) : (
          <p>
            ¿No tienes cuenta? <Link href="/signup">Créala aquí</Link>.
          </p>
        )
      }
    >
      {notice ? (
        <p className="page-notice" role="status">
          {notice}
        </p>
      ) : null}
      {authError ? (
        <p className="page-notice page-notice--error" role="alert">
          El enlace no es válido, ya fue utilizado o expiró. Solicita uno nuevo.
        </p>
      ) : null}
      {identity ? (
        <div className="signed-in-actions">
          <Link className="button-link" href={nextPath}>
            CONTINUAR AL ÁREA PRIVADA
          </Link>
          <form action={logoutAction}>
            <button className="secondary-button" type="submit">
              CERRAR ESTA SESIÓN
            </button>
          </form>
        </div>
      ) : (
        <>
          <LoginForm nextPath={nextPath} />
          <p className="form-aside">
            <Link href="/forgot-password">Olvidé mi contraseña</Link>
          </p>
        </>
      )}
    </AuthShell>
  );
}
