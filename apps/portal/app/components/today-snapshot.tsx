import type {
  TodayRecordingState,
  TodaySnapshotDto,
} from "../../lib/data-access/dogs";
import { DogPrivateShell } from "./private-shell";

const FRESHNESS_LABELS = {
  never: "NUNCA SINCRONIZADO",
  recent: "ACTUALIZADO EN LAS ÚLTIMAS 24 H",
  stale: "SIN CONEXIÓN RECIENTE",
} as const satisfies Record<
  NonNullable<TodaySnapshotDto["collar"]>["freshness"],
  string
>;

const RECORDING_STATE_LABELS = {
  open: "ABIERTA",
  closed: "CERRADA",
  legacy: "LEGADA",
  incomplete: "INCOMPLETA",
} as const satisfies Record<TodayRecordingState, string>;

function formatLocalDate(value: string): string {
  const [year, month, day] = value.split("-").map(Number);
  return new Intl.DateTimeFormat("es-CO", {
    dateStyle: "long",
    timeZone: "UTC",
  }).format(new Date(Date.UTC(year, month - 1, day, 12)));
}

function formatTimestamp(value: string, timezone: string): string {
  return new Intl.DateTimeFormat("es-CO", {
    dateStyle: "medium",
    timeStyle: "medium",
    hour12: false,
    timeZone: timezone,
  }).format(new Date(value));
}

function formatCoverage(value: number): string {
  return new Intl.NumberFormat("es-CO", {
    style: "percent",
    maximumFractionDigits: 1,
  }).format(value);
}

function formatInteger(value: number): string {
  return new Intl.NumberFormat("es-CO", {
    maximumFractionDigits: 0,
  }).format(value);
}

function ExactTime({
  value,
  timezone,
}: Readonly<{ value: string; timezone: string }>) {
  return (
    <time dateTime={value}>
      {formatTimestamp(value, timezone)} ({timezone})
    </time>
  );
}

export function TodaySnapshot({
  snapshot,
}: Readonly<{ snapshot: TodaySnapshotDto }>) {
  const { collar, dailySummary, dog, latestRecording } = snapshot;

  return (
    <DogPrivateShell activeSection="today" dog={dog}>
      <section className="today-view" aria-labelledby="today-title">
        <header className="today-heading">
          <p className="eyebrow">HOY / RESUMEN REGISTRADO</p>
          <h1 id="today-title">Resumen de hoy.</h1>
          <p>
            Estado registrado para {dog.name} el{" "}
            <time dateTime={snapshot.localDate}>
              {formatLocalDate(snapshot.localDate)}
            </time>
            , según su zona horaria.
          </p>
        </header>

        <div className="today-sections">
          <section className="today-section today-section--collar" aria-labelledby="collar-title">
            <p className="eyebrow">COLLAR</p>
            <h2 id="collar-title">Estado del collar</h2>
            {collar ? (
              <dl className="today-facts">
                <div>
                  <dt>Nombre</dt>
                  <dd>{collar.name}</dd>
                </div>
                <div>
                  <dt>Estado de sincronización</dt>
                  <dd className={`today-signal today-signal--${collar.freshness}`}>
                    {FRESHNESS_LABELS[collar.freshness]}
                  </dd>
                </div>
                <div>
                  <dt>Última sincronización</dt>
                  <dd>
                    {collar.lastSyncAt ? (
                      <ExactTime value={collar.lastSyncAt} timezone={dog.timezone} />
                    ) : (
                      "No disponible"
                    )}
                  </dd>
                </div>
              </dl>
            ) : (
              <div className="today-empty">
                <strong>SIN COLLAR ACTIVO</strong>
                <p>No hay un collar activo vinculado a {dog.name}.</p>
              </div>
            )}
          </section>

          <section className="today-section" aria-labelledby="coverage-title">
            <p className="eyebrow">FECHA LOCAL</p>
            <h2 id="coverage-title">Cobertura de hoy</h2>
            {dailySummary ? (
              <dl className="today-facts">
                <div>
                  <dt>Cobertura registrada</dt>
                  <dd>{formatCoverage(dailySummary.coverageRatio)}</dd>
                </div>
                <div>
                  <dt>Tiempo sin datos</dt>
                  <dd>{formatInteger(dailySummary.unknownSeconds)} s</dd>
                </div>
              </dl>
            ) : (
              <div className="today-empty">
                <strong>PROCESANDO O DATOS INSUFICIENTES</strong>
                <p>
                  Todavía no hay un resumen validado para la fecha local de hoy.
                  El tiempo sin observaciones no se interpreta como inactividad.
                </p>
              </div>
            )}
          </section>

          <section className="today-section" aria-labelledby="recording-title">
            <p className="eyebrow">REGISTRO DEL COLLAR</p>
            <h2 id="recording-title">Grabación más reciente</h2>
            {latestRecording ? (
              <dl className="today-facts">
                <div>
                  <dt>Inicio</dt>
                  <dd>
                    {latestRecording.startedAt ? (
                      <ExactTime value={latestRecording.startedAt} timezone={dog.timezone} />
                    ) : (
                      "HORA DE INICIO NO DISPONIBLE"
                    )}
                  </dd>
                </div>
                <div>
                  <dt>Calidad de la hora</dt>
                  <dd>
                    {latestRecording.timeQuality === "trusted"
                      ? "HORA VERIFICADA"
                      : latestRecording.timeQuality === "approximate"
                        ? "HORA APROXIMADA"
                        : "El collar no registró una hora de inicio confiable."}
                  </dd>
                </div>
                <div>
                  <dt>Estado</dt>
                  <dd>{RECORDING_STATE_LABELS[latestRecording.state]}</dd>
                </div>
                <div>
                  <dt>Puntos registrados</dt>
                  <dd>{formatInteger(latestRecording.pointCount)}</dd>
                </div>
                {latestRecording.coverageRatio !== null ? (
                  <div>
                    <dt>Cobertura de la grabación</dt>
                    <dd>{formatCoverage(latestRecording.coverageRatio)}</dd>
                  </div>
                ) : null}
              </dl>
            ) : (
              <div className="today-empty">
                <strong>SIN GRABACIÓN DISPONIBLE</strong>
                <p>
                  {collar
                    ? "Este collar todavía no tiene una grabación disponible."
                    : "No hay un collar activo con una grabación disponible."}
                </p>
              </div>
            )}
          </section>
        </div>
      </section>
    </DogPrivateShell>
  );
}
