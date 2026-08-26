import {
  type BrightnessConfigurationDto,
  type ConfigurationTruth,
} from "../../lib/data-access/configuration";
import { dogAppPath } from "../../lib/auth/protected-route";
import { BrightnessForm } from "./brightness-form";
import { DogPrivateShell } from "./private-shell";

const TRUTH_COPY = {
  unknown: {
    label: "SIN BRILLO GUARDADO EN LA NUBE",
    description:
      "Todavía no existe un brillo deseado en la nube. El valor local del collar no se conoce desde esta pantalla.",
  },
  pending: {
    label: "GUARDADO EN LA NUBE · ESPERANDO AL COLLAR",
    description:
      "Aún no hay evidencia exacta de que el collar haya aplicado el valor deseado actual.",
  },
  applied: {
    label: "APLICADO EN EL COLLAR",
    description:
      "El collar reportó aplicada la versión y el valor exactos que están guardados en la nube.",
  },
  rejected_unsupported: {
    label: "RECHAZADO POR EL COLLAR",
    description: "El collar reportó que no admite esta configuración.",
  },
  rejected_invalid: {
    label: "RECHAZADO POR EL COLLAR",
    description: "El collar reportó que el valor no era válido.",
  },
  storage_failed: {
    label: "RECHAZADO POR EL COLLAR",
    description: "El collar no pudo guardar el valor.",
  },
} as const satisfies Record<
  ConfigurationTruth,
  Readonly<{ label: string; description: string }>
>;

function formatTimestamp(value: string, timezone: string): string {
  return new Intl.DateTimeFormat("es-CO", {
    timeZone: timezone,
    dateStyle: "medium",
    timeStyle: "short",
  }).format(new Date(value));
}

type BrightnessConfigurationProps = Readonly<{
  snapshot: BrightnessConfigurationDto;
  mutationId: string | null;
}>;

export function BrightnessConfiguration({
  snapshot,
  mutationId,
}: BrightnessConfigurationProps) {
  const { dog, collar, desired } = snapshot;
  const truthCopy = TRUTH_COPY[snapshot.truth];
  const refreshHref = dogAppPath(dog.id, "configuration");

  return (
    <DogPrivateShell activeSection="configuration" dog={dog}>
      <section className="configuration-view" aria-labelledby="configuration-title">
        <header className="configuration-heading">
          <p className="eyebrow">CONFIGURACIÓN / BRILLO</p>
          <h1 id="configuration-title">Brillo del collar.</h1>
          <p>
            Guarda el brillo deseado en la nube. El cambio solo se considera
            aplicado cuando el collar reporta la misma versión exacta.
          </p>
        </header>

        {!collar ? (
          <section className="configuration-section" aria-labelledby="collar-title">
            <h2 id="collar-title">Collar seleccionado</h2>
            <div className="configuration-empty">
              <strong>SIN COLLAR ACTIVO</strong>
              <p>
                No hay un collar activo vinculado a {dog.name}. Vincula o
                reactiva uno antes de configurar el brillo.
              </p>
            </div>
          </section>
        ) : (
          <>
            <section className="configuration-section" aria-labelledby="collar-title">
              <h2 id="collar-title">Collar seleccionado</h2>
              <dl className="configuration-facts">
                <div>
                  <dt>Nombre</dt>
                  <dd>{collar.name}</dd>
                </div>
                <div>
                  <dt>Última sincronización</dt>
                  <dd>
                    {collar.lastSyncAt ? (
                      <time dateTime={collar.lastSyncAt}>
                        {formatTimestamp(collar.lastSyncAt, dog.timezone)}
                      </time>
                    ) : (
                      "NUNCA REGISTRADA"
                    )}
                  </dd>
                </div>
              </dl>
              {collar.freshness !== "recent" ? (
                <div className="configuration-warning" role="note">
                  <strong>COLLAR SIN SINCRONIZACIÓN RECIENTE</strong>
                  <p>
                    La información puede estar desactualizada. Recarga después
                    de que el collar vuelva a sincronizar.
                  </p>
                </div>
              ) : null}
            </section>

            <section className="configuration-section" aria-labelledby="truth-title">
              <h2 id="truth-title">Estado del brillo</h2>
              <div className={`configuration-truth configuration-truth--${snapshot.truth}`}>
                <strong>{truthCopy.label}</strong>
                <p>{truthCopy.description}</p>
              </div>
              {desired ? (
                <dl className="configuration-facts configuration-facts--desired">
                  <div>
                    <dt>Brillo deseado</dt>
                    <dd>{desired.brightness} de 255</dd>
                  </div>
                  <div>
                    <dt>Versión en la nube</dt>
                    <dd>{desired.serverVersion}</dd>
                  </div>
                </dl>
              ) : null}
            </section>

            <section className="configuration-section" aria-labelledby="edit-title">
              <h2 id="edit-title">Cambiar brillo</h2>
              {snapshot.canEdit && mutationId ? (
                <BrightnessForm
                  baseServerVersion={desired?.serverVersion ?? 0}
                  collarId={collar.id}
                  desiredBrightness={desired?.brightness ?? null}
                  dogId={dog.id}
                  mutationId={mutationId}
                  refreshHref={refreshHref}
                />
              ) : (
                <div className="configuration-read-only" role="note">
                  <strong>SOLO LECTURA</strong>
                  <p>
                    Puedes revisar el valor deseado y lo último reportado por el
                    collar, pero no puedes cambiarlo.
                  </p>
                </div>
              )}
            </section>
          </>
        )}
      </section>
    </DogPrivateShell>
  );
}
