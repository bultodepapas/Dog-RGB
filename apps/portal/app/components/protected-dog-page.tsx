import type { DogAppSection } from "../../lib/auth/protected-route";
import type { DogSummaryDto } from "../../lib/data-access/dogs";
import { DogPrivateShell } from "./private-shell";

const SECTION_CONTENT = {
  today: {
    eyebrow: "HOY / ESTRUCTURA PREPARADA",
    title: "Todavía no hay resumen para mostrar.",
    description:
      "Esta fase protege y organiza la ruta. La sincronización, cobertura y grabación más reciente se incorporan después sin inventar actividad.",
  },
  history: {
    eyebrow: "HISTORIAL / ESTRUCTURA PREPARADA",
    title: "El historial aún no consulta grabaciones.",
    description:
      "La lista paginada se habilitará cuando su lectura y sus estados de cobertura tengan evidencia completa.",
  },
  configuration: {
    eyebrow: "CONFIGURACIÓN / ESTRUCTURA PREPARADA",
    title: "Los ajustes remotos siguen bloqueados.",
    description:
      "Brillo será el primer ajuste web. Permanecerá pendiente hasta que el collar reporte la versión exacta aplicada.",
  },
} as const satisfies Record<
  Exclude<DogAppSection, "collars">,
  Readonly<{ eyebrow: string; title: string; description: string }>
>;

type ProtectedDogPageProps = Readonly<{
  dog: DogSummaryDto;
  section: Exclude<DogAppSection, "collars">;
}>;

export function ProtectedDogPage({
  dog,
  section,
}: ProtectedDogPageProps) {
  const content = SECTION_CONTENT[section];

  return (
    <DogPrivateShell activeSection={section} dog={dog}>
      <section className="workspace-state" aria-labelledby="workspace-title">
        <p className="eyebrow">{content.eyebrow}</p>
        <h1 id="workspace-title">{content.title}</h1>
        <p>{content.description}</p>
        <div className="workspace-boundary" role="status">
          <strong>SIN DATOS DE PRODUCTO EN M1.4</strong>
          <span>La autorización ya está activa; la función permanece cerrada.</span>
        </div>
      </section>
    </DogPrivateShell>
  );
}

type ProtectedRecordingPageProps = Readonly<{
  dog: DogSummaryDto;
}>;

export function ProtectedRecordingPage({ dog }: ProtectedRecordingPageProps) {
  return (
    <DogPrivateShell activeSection="history" dog={dog}>
      <section className="workspace-state" aria-labelledby="workspace-title">
        <p className="eyebrow">GRABACIÓN / ESTRUCTURA PREPARADA</p>
        <h1 id="workspace-title">El detalle aún no carga observaciones.</h1>
        <p>
          Metadatos, segmentos, brechas y la alternativa tabular se incorporan
          después. Esta ruta no consulta puntos GPS en M1.4.
        </p>
        <div className="workspace-boundary" role="status">
          <strong>SIN RUTA NI COORDENADAS</strong>
          <span>La autorización del perro ya está activa.</span>
        </div>
      </section>
    </DogPrivateShell>
  );
}
