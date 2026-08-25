import { DEVICE_PROTOCOL } from "@dog-rgb/contracts";
import Link from "next/link";

import { getVerifiedIdentity } from "../lib/supabase/identity";
import { logoutAction } from "./auth/actions";

export const dynamic = "force-dynamic";

type HomePageProps = Readonly<{
  searchParams: Promise<Record<string, string | string[] | undefined>>;
}>;

export default async function Home({ searchParams }: HomePageProps) {
  const [identity, params] = await Promise.all([
    getVerifiedIdentity(),
    searchParams,
  ]);
  const confirmed = params.auth === "confirmed";

  return (
    <main className="home-page">
      <section className="home-hero" aria-labelledby="home-title">
        <p className="eyebrow">DOG-RGB_ CLOUD FOUNDATION</p>
        <h1 id="home-title">El collar sigue funcionando sin la nube.</h1>
        <p>
          La web es una extensión privada para sincronizar historial y unos pocos
          ajustes. El collar conserva GPS, luces y recuperación local aunque este
          portal no esté disponible.
        </p>
        {confirmed ? (
          <p className="page-notice" role="status">
            Correo confirmado y sesión verificada.
          </p>
        ) : null}
        <div className="home-actions">
          {identity ? (
            <>
              <Link className="button-link" href="/onboarding">
                ABRIR ÁREA PRIVADA
              </Link>
              <form action={logoutAction}>
                <button className="secondary-button" type="submit">
                  CERRAR ESTA SESIÓN
                </button>
              </form>
            </>
          ) : (
            <>
              <Link className="button-link" href="/login">
                INICIAR SESIÓN
              </Link>
              <Link className="text-link" href="/signup">
                Crear cuenta
              </Link>
            </>
          )}
        </div>
        <dl>
          <div>
            <dt>PROTOCOLO</dt>
            <dd>{DEVICE_PROTOCOL}</dd>
          </div>
          <div>
            <dt>CUENTA</dt>
            <dd>{identity ? "VERIFICADA" : "SIN SESIÓN"}</dd>
          </div>
          <div>
            <dt>ESTADO</dt>
            <dd>LOCAL / OPTIONAL</dd>
          </div>
        </dl>
      </section>
    </main>
  );
}
