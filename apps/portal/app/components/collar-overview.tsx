import { dogAppPath } from "../../lib/auth/protected-route";
import type { CollarPageDto } from "../../lib/data-access/collars";
import { ClaimCodeForm } from "../app/[dogId]/collars/claim-code-form";
import { CollarRevokeForm } from "./collar-revoke-form";
import { DogPrivateShell } from "./private-shell";

const FRESHNESS_LABELS = {
  never: "NUNCA SINCRONIZADO",
  recent: "ACTUALIZADO EN LAS ÚLTIMAS 24 H",
  stale: "SIN CONEXIÓN RECIENTE",
} as const;

function formatDateTime(value: string, timezone: string): string {
  return new Intl.DateTimeFormat("es-CO", {
    dateStyle: "medium",
    timeStyle: "medium",
    timeZone: timezone,
  }).format(new Date(value));
}

function formatBytes(value: number): string {
  const exact = new Intl.NumberFormat("es-CO").format(value);
  if (value < 1_024) return `${exact} bytes`;
  const unitValue = value < 1_048_576 ? value / 1_024 : value / 1_048_576;
  const unit = value < 1_048_576 ? "KiB" : "MiB";
  return `${exact} bytes (${new Intl.NumberFormat("es-CO", {
    maximumFractionDigits: 2,
  }).format(unitValue)} ${unit})`;
}

function yesNo(value: boolean): string {
  return value ? "SÍ" : "NO";
}

export function CollarOverview({ snapshot }: Readonly<{ snapshot: CollarPageDto }>) {
  const { dog, collar } = snapshot;
  const refreshHref = dogAppPath(dog.id, "collars");
  return (
    <DogPrivateShell activeSection="collars" dog={dog}>
      <div className="collar-view">
        <header className="collar-heading">
          <p className="eyebrow">COLLARES / ESTADO Y ACCESO</p>
          <h1 id="collar-page-title">Estado del collar.</h1>
          <p>
            Esta página muestra la última información que llegó a la nube. No
            es un diagnóstico en vivo.
          </p>
        </header>

        {!collar ? (
          <section className="collar-section" aria-labelledby="selected-collar-title">
            <h2 id="selected-collar-title">Collar seleccionado</h2>
            <div className="collar-empty" role="note">
              <strong>SIN COLLAR VINCULADO</strong>
              <p>Todavía no hay un collar disponible para {dog.name}.</p>
            </div>
          </section>
        ) : (
          <>
            <section className="collar-section" aria-labelledby="selected-collar-title">
              <h2 id="selected-collar-title">Collar seleccionado</h2>
              <dl className="collar-facts">
                <div><dt>Nombre</dt><dd>{collar.name}</dd></div>
                <div>
                  <dt>Estado al cargar</dt>
                  <dd>ACTIVO</dd>
                </div>
                <div>
                  <dt>Vinculado</dt>
                  <dd><time dateTime={collar.linkedAt}>{formatDateTime(collar.linkedAt, dog.timezone)}</time></dd>
                </div>
                <div>
                  <dt>Última sincronización</dt>
                  <dd>
                    {collar.lastSyncAt ? (
                      <time dateTime={collar.lastSyncAt}>{formatDateTime(collar.lastSyncAt, dog.timezone)}</time>
                    ) : "NO DISPONIBLE"}
                  </dd>
                </div>
                <div><dt>Actualidad</dt><dd>{FRESHNESS_LABELS[collar.freshness]}</dd></div>
              </dl>
              {collar.freshness !== "recent" ? (
                <div className="collar-warning" role="note">
                  <strong>COLLAR SIN SINCRONIZACIÓN RECIENTE</strong>
                  <p>
                    La nube no ha recibido una sincronización reciente. Esto no
                    demuestra que el collar físico esté apagado.
                  </p>
                </div>
              ) : null}
            </section>

            <section className="collar-section" aria-labelledby="compatibility-title">
              <h2 id="compatibility-title">Compatibilidad registrada</h2>
              <p className="collar-section-copy">
                CAPACIDADES ACEPTADAS POR LA NUBE. Se actualizan solo cuando el
                collar envía un manifiesto completo y válido.
              </p>
              <dl className="collar-facts">
                <div><dt>Firmware</dt><dd>{collar.compatibility.firmwareVersion}</dd></div>
                <div><dt>Hardware</dt><dd>{collar.compatibility.hardwareRevision}</dd></div>
                <div><dt>Protocolo aceptado</dt><dd>device-v{collar.compatibility.protocolVersion}</dd></div>
                <div><dt>Esquema de telemetría</dt><dd>{collar.compatibility.telemetrySchema}</dd></div>
                <div><dt>Esquema de configuración</dt><dd>{collar.compatibility.configSchema}</dd></div>
                <div><dt>Brillo bidireccional</dt><dd>{yesNo(collar.compatibility.brightnessBidirectional)}</dd></div>
                <div><dt>Reporte de configuración</dt><dd>{yesNo(collar.compatibility.configurationReporting)}</dd></div>
                <div><dt>Marcadores de pérdida</dt><dd>{yesNo(collar.compatibility.telemetryLossMarkers)}</dd></div>
                <div><dt>Carga heredada v2</dt><dd>{yesNo(collar.compatibility.legacyV2Upload)}</dd></div>
                <div><dt>Recursos declarados</dt><dd>{collar.compatibility.resourceCount}</dd></div>
                <div><dt>Efectos / paletas</dt><dd>{collar.compatibility.effectCount} / {collar.compatibility.paletteCount}</dd></div>
                <div>
                  <dt>Máximo por sincronización</dt>
                  <dd>{collar.compatibility.maxChunksPerSync} fragmentos · {collar.compatibility.maxPointsPerSync} puntos</dd>
                </div>
              </dl>
            </section>

            <section className="collar-section" aria-labelledby="queue-title">
              <h2 id="queue-title">Cola reportada en la última sincronización</h2>
              <p className="collar-section-copy">
                Es una captura enviada antes de que el collar procesara la
                respuesta de la nube. La cola física actual puede haber cambiado.
              </p>
              {!collar.diagnostics ? (
                <div className="collar-empty" role="note">
                  <strong>SIN DIAGNÓSTICO DE COLA</strong>
                  <p>El collar todavía no ha enviado una captura compatible.</p>
                </div>
              ) : (
                <>
                  <div className="collar-truth">
                    <strong>
                      {collar.diagnostics.state === "empty"
                        ? "COLA VACÍA AL REPORTAR"
                        : "DATOS PENDIENTES AL REPORTAR"}
                    </strong>
                    <span>
                      Captura recibida el{" "}
                      <time dateTime={collar.diagnostics.observedAt}>
                        {formatDateTime(collar.diagnostics.observedAt, dog.timezone)}
                      </time>.
                    </span>
                  </div>
                  <dl className="collar-facts collar-facts--queue">
                    <div><dt>Fragmentos pendientes al reportar</dt><dd>{collar.diagnostics.outboxChunks}</dd></div>
                    <div><dt>Puntos pendientes al reportar</dt><dd>{collar.diagnostics.outboxPoints}</dd></div>
                    <div><dt>Espacio usado</dt><dd>{formatBytes(collar.diagnostics.usedBytes)}</dd></div>
                    <div><dt>Capacidad</dt><dd>{formatBytes(collar.diagnostics.capacityBytes)}</dd></div>
                    <div>
                      <dt>Observación pendiente más antigua</dt>
                      <dd>
                        {collar.diagnostics.oldestUnacknowledgedAt ? (
                          <time dateTime={collar.diagnostics.oldestUnacknowledgedAt}>
                            {formatDateTime(collar.diagnostics.oldestUnacknowledgedAt, dog.timezone)}
                          </time>
                        ) : "NO DISPONIBLE"}
                      </dd>
                    </div>
                    <div><dt>Puntos descartados acumulados</dt><dd>{collar.diagnostics.droppedPointsTotal}</dd></div>
                    <div><dt>Error local reportado</dt><dd>{yesNo(collar.diagnostics.errorReported)}</dd></div>
                  </dl>
                  {collar.diagnostics.droppedPointsTotal > 0 ? (
                    <div className="collar-warning" role="note">
                      <strong>PUNTOS DESCARTADOS REPORTADOS</strong>
                      <p>El contador es acumulado; esta pantalla no atribuye una causa.</p>
                    </div>
                  ) : null}
                </>
              )}
            </section>

            <section className="collar-section" aria-labelledby="cloud-access-title">
              <h2 id="cloud-access-title">Acceso a la nube</h2>
              <p className="collar-section-copy">
                Revocar bloquea nuevas sincronizaciones y cambios remotos. No
                borra las grabaciones ya guardadas ni modifica las funciones
                locales del collar.
              </p>
              {snapshot.canRevoke ? (
                <CollarRevokeForm
                  collarId={collar.id}
                  collarName={collar.name}
                  dogId={dog.id}
                  refreshHref={refreshHref}
                />
              ) : (
                <div className="collar-read-only" role="note">
                  <strong>SOLO EL PROPIETARIO PUEDE REVOCAR</strong>
                  <p>Tu acceso permite revisar el estado, pero no cambiar la autoridad del collar.</p>
                </div>
              )}
            </section>
          </>
        )}

        <section className="collar-section" aria-labelledby="pairing-title">
          <h2 id="pairing-title">Vincular otro collar</h2>
          <div className="workspace-boundary claim-boundary">
            {snapshot.canIssueClaim ? (
              <>
                <strong>UN SOLO USO · 15 MINUTOS</strong>
                <span>El código se mostrará una sola vez en esta pantalla.</span>
                <ClaimCodeForm dogId={dog.id} />
              </>
            ) : (
              <div className="claim-read-only" role="note">
                <strong>SOLO PROPIETARIO O EDITOR</strong>
                <span>Tu acceso es de lectura; no puedes generar códigos de vinculación.</span>
              </div>
            )}
          </div>
        </section>
      </div>
    </DogPrivateShell>
  );
}
