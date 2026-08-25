import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireDogPage } from "../../../../lib/auth/route-guard";
import { DogPrivateShell } from "../../../components/private-shell";
import { ClaimCodeForm } from "./claim-code-form";

export const metadata: Metadata = { title: "Collares | Dog RGB" };
export const dynamic = "force-dynamic";

type CollarsPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function CollarsPage(
  props: CollarsPageProps,
) {
  const { dogId } = await props.params;
  const dog = await requireDogPage(dogId, dogAppPath(dogId, "collars"));
  const canIssueClaim = dog.role === "owner" || dog.role === "editor";

  return (
    <DogPrivateShell activeSection="collars" dog={dog}>
      <section className="workspace-state" aria-labelledby="workspace-title">
        <p className="eyebrow">COLLARES / VINCULACIÓN</p>
        <h1 id="workspace-title">Genera un código temporal.</h1>
        <p>
          Úsalo para vincular un collar con {dog.name}. El collar nunca recibe
          tu contraseña ni una clave de Supabase.
        </p>
        <div className="workspace-boundary claim-boundary">
          {canIssueClaim ? (
            <>
              <strong>UN SOLO USO · 15 MINUTOS</strong>
              <span>El código se mostrará una sola vez en esta pantalla.</span>
              <ClaimCodeForm dogId={dog.id} />
            </>
          ) : (
            <div className="claim-read-only" role="note">
              <strong>SOLO PROPIETARIO O EDITOR</strong>
              <span>
                Tu acceso es de lectura; no puedes generar códigos de
                vinculación.
              </span>
            </div>
          )}
        </div>
      </section>
    </DogPrivateShell>
  );
}
