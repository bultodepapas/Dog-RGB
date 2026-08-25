import Link from "next/link";

import { dogAppPath } from "../../lib/auth/protected-route";
import type {
  HistoryPageDto,
  HistoryRecordingState,
} from "../../lib/data-access/dogs";
import { DogPrivateShell } from "./private-shell";

const RECORDING_STATE_LABELS = {
  open: "ABIERTA",
  closed: "CERRADA",
  legacy: "LEGADA",
  incomplete: "INCOMPLETA",
} as const satisfies Record<HistoryRecordingState, string>;

const TIME_QUALITY_LABELS = {
  trusted: "HORA VERIFICADA",
  approximate: "HORA APROXIMADA",
  unknown: "HORA NO DISPONIBLE",
} as const satisfies Record<
  Extract<HistoryPageDto, { status: "ready" }>["recordings"][number]["timeQuality"],
  string
>;

function formatTimestamp(value: string, timezone: string): string {
  return new Intl.DateTimeFormat("es-CO", {
    dateStyle: "medium",
    timeStyle: "medium",
    hour12: false,
    timeZone: timezone,
  }).format(new Date(value));
}

function formatInteger(value: number): string {
  return new Intl.NumberFormat("es-CO", {
    maximumFractionDigits: 0,
  }).format(value);
}

export function HistoryLedger({
  history,
}: Readonly<{ history: HistoryPageDto }>) {
  const firstPagePath = dogAppPath(history.dog.id, "history");

  if (history.status === "invalid_cursor") {
    return (
      <DogPrivateShell activeSection="history" dog={history.dog}>
        <section className="history-view" aria-labelledby="history-title">
          <header className="history-heading">
            <p className="eyebrow">HISTORIAL / ENLACE NO VÁLIDO</p>
            <h1 id="history-title">
              No pudimos abrir esta página del historial.
            </h1>
            <p>
              El enlace de paginación está incompleto o ya no es compatible.
              Vuelve al inicio del historial.
            </p>
            <Link className="button-link history-recovery" href={firstPagePath}>
              VOLVER AL INICIO DEL HISTORIAL
            </Link>
          </header>
        </section>
      </DogPrivateShell>
    );
  }

  const { dog, nextCursor, position, recordings } = history;

  return (
    <DogPrivateShell activeSection="history" dog={dog}>
      <section className="history-view" aria-labelledby="history-title">
        <header className="history-heading">
          <p className="eyebrow">HISTORIAL / GRABACIONES REGISTRADAS</p>
          <h1 id="history-title">Historial de grabaciones.</h1>
          <p>
            Grabaciones registradas por los collares de {dog.name}. Primero se
            muestran las que tienen hora de inicio, desde la más reciente;
            después aparecen las que no tienen una hora disponible.
          </p>
        </header>

        <section
          className="history-results"
          aria-labelledby="history-results-title"
        >
          <h2 id="history-results-title">Grabaciones registradas</h2>
          {recordings.length > 0 ? (
            <ol className="history-list" role="list">
              {recordings.map((recording) => (
                <li key={recording.id}>
                  <dl className="history-facts">
                    <div className="history-fact--start">
                      <dt>Inicio</dt>
                      <dd>
                        {recording.startedAt ? (
                          <time dateTime={recording.startedAt}>
                            {formatTimestamp(recording.startedAt, dog.timezone)} (
                            {dog.timezone})
                          </time>
                        ) : (
                          "HORA DE INICIO NO DISPONIBLE"
                        )}
                      </dd>
                    </div>
                    <div>
                      <dt>Calidad de la hora</dt>
                      <dd>{TIME_QUALITY_LABELS[recording.timeQuality]}</dd>
                    </div>
                    <div>
                      <dt>Collar</dt>
                      <dd>{recording.collarName}</dd>
                    </div>
                    <div>
                      <dt>Estado</dt>
                      <dd>{RECORDING_STATE_LABELS[recording.state]}</dd>
                    </div>
                    <div>
                      <dt>Puntos registrados</dt>
                      <dd>{formatInteger(recording.pointCount)}</dd>
                    </div>
                  </dl>
                </li>
              ))}
            </ol>
          ) : (
            <div className="history-empty">
              <strong>
                {position === "first"
                  ? "NO HAY GRABACIONES REGISTRADAS"
                  : "NO HAY MÁS GRABACIONES"}
              </strong>
              <p>
                {position === "first"
                  ? `Los collares de ${dog.name} todavía no tienen grabaciones disponibles.`
                  : "No quedan grabaciones disponibles después de este punto. Vuelve al inicio del historial."}
              </p>
              {position === "after_cursor" ? (
                <Link className="text-link" href={firstPagePath}>
                  VOLVER AL INICIO DEL HISTORIAL
                </Link>
              ) : null}
            </div>
          )}
        </section>

        {nextCursor ? (
          <nav className="history-pagination" aria-label="Paginación del historial">
            <Link
              className="button-link"
              href={`${firstPagePath}?cursor=${encodeURIComponent(nextCursor)}`}
              prefetch={false}
            >
              VER MÁS GRABACIONES
            </Link>
          </nav>
        ) : null}
      </section>
    </DogPrivateShell>
  );
}
