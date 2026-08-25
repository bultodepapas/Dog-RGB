import type { Metadata } from "next";

import { requireFreshPageIdentity } from "../../lib/auth/route-guard";
import { OnboardingPrivateShell } from "../components/private-shell";

export const metadata: Metadata = { title: "Preparar collar | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function OnboardingPage() {
  await requireFreshPageIdentity("/onboarding");

  return (
    <OnboardingPrivateShell>
      <section className="onboarding-state" aria-labelledby="onboarding-title">
        <p className="eyebrow">M1.4 / ACCESO PROTEGIDO</p>
        <h1 id="onboarding-title">El área privada está lista.</h1>
        <p>
          La sesión se comprobó directamente con el servidor de autenticación.
          Crear un perro y vincular un collar pertenecen al siguiente paso y
          siguen deshabilitados.
        </p>
        <dl className="onboarding-checks">
          <div>
            <dt>SESIÓN</dt>
            <dd>VERIFICADA</dd>
          </div>
          <div>
            <dt>DATOS</dt>
            <dd>SIN CAMBIOS</dd>
          </div>
          <div>
            <dt>SIGUIENTE</dt>
            <dd>CREAR PERRO</dd>
          </div>
        </dl>
        <p className="page-notice" role="status">
          No hay ninguna acción de configuración disponible en esta fase.
        </p>
      </section>
    </OnboardingPrivateShell>
  );
}
