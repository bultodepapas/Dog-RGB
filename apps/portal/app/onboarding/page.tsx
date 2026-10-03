import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { createServerSupabaseClient } from "../../lib/supabase/server";
import { accountDeletion } from "../../lib/privacy/account";
import { resolveDogEntry } from "../../lib/auth/entry";
import { requireFreshPageIdentity } from "../../lib/auth/route-guard";
import { listDogSummaries } from "../../lib/data-access/dogs";
import { dogAppPath } from "../../lib/auth/protected-route";
import { OnboardingPrivateShell } from "../components/private-shell";
import { CreateDogForm } from "./create-dog-form";
import { MembershipSelector } from "./membership-selector";

export const metadata: Metadata = { title: "Área privada | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function OnboardingPage() {
  await requireFreshPageIdentity("/onboarding");
  const client = await createServerSupabaseClient();
  const accountResult = await client.rpc("get_my_account_deletion_v1");
  if (accountResult.error) throw new Error("No pudimos consultar el estado de tu cuenta.");
  const deletion = accountDeletion(accountResult.data);
  if (deletion.status === "pending" || deletion.status === "ready") redirect("/account");
  const dogs = await listDogSummaries();
  const entry = resolveDogEntry(dogs);

  if (entry.kind === "open") {
    redirect(dogAppPath(entry.dogId, "today"));
  }

  if (entry.kind === "select") {
    return (
      <OnboardingPrivateShell>
        <section className="onboarding-state" aria-labelledby="onboarding-title">
          <p className="eyebrow">ÁREA PRIVADA</p>
          <h1 id="onboarding-title">Elige un perro</h1>
          <p>Abre el espacio privado que quieres consultar.</p>
          <MembershipSelector dogs={dogs} />
        </section>
      </OnboardingPrivateShell>
    );
  }

  return (
    <OnboardingPrivateShell>
      <section className="onboarding-state" aria-labelledby="onboarding-title">
        <p className="eyebrow">ÁREA PRIVADA</p>
        <h1 id="onboarding-title">Registra tu perro</h1>
        <p>
          Crearemos el perfil mínimo para abrir su espacio privado. El collar y
          la vinculación vienen después.
        </p>
        <CreateDogForm />
      </section>
    </OnboardingPrivateShell>
  );
}
