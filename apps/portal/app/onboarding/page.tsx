import type { Metadata } from "next";

import { requireFreshPageIdentity } from "../../lib/auth/route-guard";
import { OnboardingPrivateShell } from "../components/private-shell";
import { CreateDogForm } from "./create-dog-form";

export const metadata: Metadata = { title: "Preparar collar | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function OnboardingPage() {
  await requireFreshPageIdentity("/onboarding");

  return (
    <OnboardingPrivateShell>
      <section className="onboarding-state" aria-labelledby="onboarding-title">
        <p className="eyebrow">M1.5 / PERFIL DEL PERRO</p>
        <h1 id="onboarding-title">¿Cómo se llama tu perro?</h1>
        <p>
          Crearemos el perfil mínimo para abrir su espacio privado. El collar y
          la vinculación vienen después.
        </p>
        <CreateDogForm />
      </section>
    </OnboardingPrivateShell>
  );
}
