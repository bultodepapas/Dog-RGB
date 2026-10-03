import Link from "next/link";
import type { Metadata } from "next";
import { requireDogPage } from "../../../../lib/auth/route-guard";
import { DogPrivateShell } from "../../../components/private-shell";
import { DogDeletionForm } from "../../../components/dog-deletion-form";

export const metadata: Metadata = { title: "Datos | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function DogDataPage({ params }: Readonly<{ params: Promise<{ dogId: string }> }>) {
  const { dogId } = await params;
  const dog = await requireDogPage(dogId, `/app/${dogId}/data`);
  return <DogPrivateShell dog={dog} activeSection="data">
    <h1>Datos de {dog.name}</h1>
    <p>Las ubicaciones y grabaciones son privadas. Revocar un collar conserva el historial; eliminar el perro lo borra.</p>
    <p><Link href="/privacy">Uso de datos, retención y copias de seguridad</Link></p>
    {dog.role === "owner" ? <>
      <section aria-labelledby="data-export"><h2 id="data-export">Exportar</h2>
        <p>Descarga una copia de los datos conservados al iniciar la consulta. Las cargas posteriores no se incluyen.</p>
        <p>Máximo por descarga: 25.000 puntos, 50.000 registros y 16 MiB. Si se supera un límite, no se entrega una copia parcial. Puedes descargar grabaciones individuales en GeoJSON desde su detalle, sujetas a los mismos límites aplicables.</p>
        <a className="button-link" href={`/app/${dogId}/data/export`}>Descargar datos JSON</a>
      </section>
      <section aria-labelledby="data-delete"><h2 id="data-delete">Eliminar perro y datos</h2>
        <DogDeletionForm dogId={dogId} requestId={crypto.randomUUID()} />
      </section>
    </> : <p>Solo un propietario puede exportar o eliminar los datos de este perro.</p>}
  </DogPrivateShell>;
}
