import Link from "next/link";

import {
  dogAppPath,
  recordingAppPath,
} from "../../lib/auth/protected-route";
import type {
  PointContinuity,
  RecordingPageDto,
  RecordingState,
  RecordingTimeQuality,
} from "../../lib/data-access/dogs";
import { DogPrivateShell } from "./private-shell";

const RECORDING_STATE_LABELS = {
  open: "ABIERTA",
  closed: "CERRADA",
  legacy: "LEGADA",
  incomplete: "INCOMPLETA",
} as const satisfies Record<RecordingState, string>;

const TIME_QUALITY_LABELS = {
  unknown: "HORA NO DISPONIBLE",
  approximate_persisted: "APROXIMADA / ANCLA PERSISTIDA",
  server_anchored: "ANCLADA AL SERVIDOR",
  sntp_synced: "SINCRONIZADA POR SNTP",
  gnss_trusted: "ACEPTADA DESDE GNSS",
  legacy_minute: "LEGADA / PRECISIÓN DE MINUTO",
} as const satisfies Record<RecordingTimeQuality, string>;

const CONTINUITY_LABELS = {
  page_boundary: "INICIO DE PÁGINA",
  continues: "CONTINÚA",
  after_explicit_gap: "DESPUÉS DE BRECHA EXPLÍCITA",
  after_invalid_fix: "DESPUÉS DE POSICIÓN NO UTILIZABLE",
  sequence_discontinuity: "SECUENCIA NO CONSECUTIVA",
  time_discontinuity: "CAMBIO DE CONFIANZA HORARIA",
  time_gap: "INTERVALO MAYOR A 65 S",
  explicit_gap: "BRECHA EXPLÍCITA · SIN TRAZO",
  invalid_fix: "POSICIÓN NO UTILIZABLE · SIN TRAZO",
} as const satisfies Record<PointContinuity, string>;

function formatTimestamp(value: string, timezone: string): string {
  return new Intl.DateTimeFormat("es-CO", {
    dateStyle: "medium",
    timeStyle: "long",
    hour12: false,
    timeZone: timezone,
  }).format(new Date(value));
}

function formatInteger(value: number): string {
  return new Intl.NumberFormat("es-CO", { maximumFractionDigits: 0 }).format(value);
}

function formatCoordinate(value: number | null): string {
  if (value === null) return "NO DISPONIBLE";
  const sign = value < 0 ? "-" : "";
  const absolute = Math.abs(value);
  return `${sign}${Math.floor(absolute / 10_000_000)}.${String(absolute % 10_000_000).padStart(7, "0")}`;
}

function formatSpeed(value: number | null): string {
  if (value === null) return "NO DISPONIBLE";
  return `${Math.floor(value / 100)},${String(value % 100).padStart(2, "0")} m/s`;
}

function formatFlags(flags: number, labels: readonly string[]): string {
  const storedValue = `0x${flags.toString(16).padStart(2, "0").toUpperCase()}`;
  return `${storedValue} · ${labels.length === 0 ? "SIN INDICADORES" : labels.join(" · ")}`;
}

function continuityLabel(
  continuity: PointContinuity,
  segmentNumber: number | null,
  position: "first" | "after",
): string {
  if (continuity === "explicit_gap" || continuity === "invalid_fix") {
    return CONTINUITY_LABELS[continuity];
  }
  const label = continuity === "page_boundary" && position === "after"
    ? "LÍMITE DE PÁGINA"
    : CONTINUITY_LABELS[continuity];
  return `TRAMO ${segmentNumber} DE ESTA PÁGINA · ${label}`;
}

function RecordingFacts({
  page,
}: Readonly<{ page: RecordingPageDto }>) {
  const recording = page.recording;
  const facts = [
    ["Collar", recording.collarName],
    ["Estado", RECORDING_STATE_LABELS[recording.state]],
    ["Arranque", formatInteger(recording.bootSequence)],
    ["Primera secuencia", recording.firstPointSequence === null ? "NO DISPONIBLE" : formatInteger(recording.firstPointSequence)],
    ["Última secuencia", recording.lastPointSequence === null ? "NO DISPONIBLE" : formatInteger(recording.lastPointSequence)],
    ["Puntos registrados", formatInteger(recording.pointCount)],
    ["Calidad horaria almacenada", TIME_QUALITY_LABELS[recording.clockQuality]],
    ["Esquema de telemetría", formatInteger(recording.telemetrySchema)],
    ["Versión de firmware", recording.firmwareVersion],
  ] as const;

  return (
    <section className="recording-section" aria-labelledby="recording-facts-title">
      <h2 id="recording-facts-title">Datos de la grabación</h2>
      <dl className="recording-facts">
        <div className="recording-fact--time">
          <dt>Inicio registrado</dt>
          <dd>
            {recording.startedAt ? (
              <time dateTime={recording.startedAt}>
                {formatTimestamp(recording.startedAt, recording.timezoneAtStart)} ({recording.timezoneAtStart})
              </time>
            ) : "NO DISPONIBLE"}
          </dd>
        </div>
        <div className="recording-fact--time">
          <dt>Fin registrado</dt>
          <dd>
            {recording.endedAt ? (
              <time dateTime={recording.endedAt}>
                {formatTimestamp(recording.endedAt, recording.timezoneAtStart)} ({recording.timezoneAtStart})
              </time>
            ) : "NO DISPONIBLE"}
          </dd>
        </div>
        <div>
          <dt>Zona horaria almacenada</dt>
          <dd>{recording.timezoneAtStart}</dd>
        </div>
        {facts.map(([term, value]) => (
          <div key={term}>
            <dt>{term}</dt>
            <dd>{value}</dd>
          </div>
        ))}
      </dl>
      <p className="recording-truth-note">
        El conteo y la calidad horaria son metadatos almacenados. La tabla muestra
        las observaciones que siguen disponibles y su calidad horaria individual.
      </p>
    </section>
  );
}

function RoutePreview({ page }: Readonly<{
  page: Extract<RecordingPageDto, { status: "ready" }>;
}>) {
  if (!page.preview) {
    return (
      <section className="recording-section" aria-labelledby="preview-heading">
        <h2 id="preview-heading">Vista previa simple</h2>
        <div className="recording-empty">
          <strong>VISTA PREVIA NO DISPONIBLE</strong>
          <p>
            {page.previewUnavailableReason === "antimeridian_ambiguous"
              ? "Las longitudes de esta página cruzan una referencia geográfica ambigua; no dibujamos un trazo que pueda resultar engañoso. La tabla conserva la evidencia disponible."
              : "Esta página no contiene posiciones válidas para representar. La tabla conserva la evidencia disponible."}
          </p>
        </div>
      </section>
    );
  }

  const allPoints = page.preview.segments.flatMap((segment) => segment.points);
  const latitudes = allPoints.map((point) => point.latE7);
  const longitudes = allPoints.map((point) => point.lonE7);
  const minLat = Math.min(...latitudes);
  const maxLat = Math.max(...latitudes);
  const minLon = Math.min(...longitudes);
  const maxLon = Math.max(...longitudes);
  const width = 640;
  const height = 360;
  const padding = 28;
  const x = (longitude: number) => minLon === maxLon
    ? width / 2
    : padding + ((longitude - minLon) / (maxLon - minLon)) * (width - 2 * padding);
  const y = (latitude: number) => minLat === maxLat
    ? height / 2
    : height - padding - ((latitude - minLat) / (maxLat - minLat)) * (height - 2 * padding);

  return (
    <section className="recording-section" aria-labelledby="preview-heading">
      <h2 id="preview-heading">Vista previa simple</h2>
      <figure className="route-preview">
        <svg
          aria-describedby="preview-description"
          aria-labelledby="preview-title"
          focusable="false"
          preserveAspectRatio="xMidYMid meet"
          role="img"
          viewBox={`0 0 ${width} ${height}`}
        >
          <title id="preview-title">Vista previa simple de las posiciones de esta página</title>
          <desc id="preview-description">
            Ayuda de orientación, no un mapa. Muestra {page.preview.drawablePointCount} {page.preview.drawablePointCount === 1 ? "posición válida" : "posiciones válidas"} en {page.preview.segments.length} {page.preview.segments.length === 1 ? "tramo" : "tramos"}. La tabla contiene los valores exactos.
          </desc>
          <rect className="route-preview__field" height={height} width={width} x="0" y="0" />
          {page.preview.segments.map((segment) => {
            const first = segment.points[0];
            const last = segment.points[segment.points.length - 1];
            const polylinePoints = segment.points.map((point) =>
              `${x(point.lonE7).toFixed(2)},${y(point.latE7).toFixed(2)}`).join(" ");
            return (
              <g aria-hidden="true" key={segment.number}>
                {segment.points.length > 1 ? (
                  <polyline className="route-preview__line" points={polylinePoints} />
                ) : null}
                <circle className="route-preview__start" cx={x(first.lonE7)} cy={y(first.latE7)} r="7" />
                <rect
                  className="route-preview__end"
                  height="14"
                  width="14"
                  x={x(last.lonE7) - 7}
                  y={y(last.latE7) - 7}
                />
              </g>
            );
          })}
        </svg>
        <figcaption>
          <strong>VISTA PREVIA NORMALIZADA · NO ES UN MAPA NI REPRESENTA DISTANCIAS</strong>
          <span><i className="preview-key preview-key--start" aria-hidden="true" /> INICIO DE TRAMO</span>
          <span><i className="preview-key preview-key--end" aria-hidden="true" /> FIN DE TRAMO</span>
        </figcaption>
      </figure>
    </section>
  );
}

function PointTable({ page }: Readonly<{
  page: Extract<RecordingPageDto, { status: "ready" }>;
}>) {
  return (
    <section className="recording-section" aria-labelledby="points-title">
      <h2 id="points-title">Observaciones de esta página</h2>
      <p id="points-help">
        La tabla contiene los valores almacenados. En pantallas estrechas,
        desplázala horizontalmente para consultar todas las columnas.
      </p>
      <div
        aria-describedby="points-help"
        aria-labelledby="points-title"
        className="point-table-scroll"
        role="region"
        tabIndex={0}
      >
        <table className="point-table">
          <caption>Hasta 100 observaciones de esta página, ordenadas por secuencia.</caption>
          <thead>
            <tr>
              <th scope="col">Secuencia</th>
              <th scope="col">Hora registrada</th>
              <th scope="col">Tramo / continuidad</th>
              <th scope="col">Latitud</th>
              <th scope="col">Longitud</th>
              <th scope="col">Velocidad reportada</th>
              <th scope="col">Satélites</th>
              <th scope="col">Indicadores</th>
              <th scope="col">Calidad de la hora</th>
            </tr>
          </thead>
          <tbody>
            {page.points.length === 0 ? (
              <tr><td colSpan={9}>No hay observaciones registradas para esta página.</td></tr>
            ) : page.points.map((point) => (
              <tr key={point.sequence}>
                <th scope="row">{formatInteger(point.sequence)}</th>
                <td>
                  {point.recordedAt ? (
                    <time dateTime={point.recordedAt}>
                      {formatTimestamp(point.recordedAt, page.recording.timezoneAtStart)}
                    </time>
                  ) : "NO DISPONIBLE"}
                </td>
                <td>{continuityLabel(point.continuity, point.segmentNumber, page.position)}</td>
                <td>{formatCoordinate(point.latE7)}</td>
                <td>{formatCoordinate(point.lonE7)}</td>
                <td>{formatSpeed(point.speedCmps)}</td>
                <td>{point.satellites === null ? "NO DISPONIBLE" : formatInteger(point.satellites)}</td>
                <td>{formatFlags(point.flags, point.flagLabels)}</td>
                <td>{TIME_QUALITY_LABELS[point.timeQuality]}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

export function RecordingDetail({ page }: Readonly<{ page: RecordingPageDto }>) {
  const canonicalPath = recordingAppPath(page.dog.id, page.recording.id);
  return (
    <DogPrivateShell activeSection="history" dog={page.dog}>
      <article className="recording-view" aria-labelledby="recording-title">
        <header className="recording-heading">
          <p className="eyebrow">
            {page.status === "invalid_after"
              ? "GRABACIÓN / ENLACE NO VÁLIDO"
              : "GRABACIÓN / DETALLE REGISTRADO"}
          </p>
          <h1 id="recording-title">
            {page.status === "invalid_after"
              ? "No pudimos abrir esta página de observaciones."
              : "Detalle de la grabación."}
          </h1>
          <p>
            {page.status === "invalid_after"
              ? "El enlace de paginación está incompleto o ya no es compatible. Vuelve al inicio de la grabación."
              : `Datos almacenados para ${page.recording.collarName}. La tabla de observaciones es la fuente autoritativa; la vista previa solo ayuda a orientarse y no es un mapa.`}
          </p>
          <Link className="text-link recording-back-link" href={dogAppPath(page.dog.id, "history")} prefetch={false}>
            VOLVER AL HISTORIAL
          </Link>
        </header>

        <RecordingFacts page={page} />

        {page.status === "invalid_after" ? (
          <Link className="button-link recording-recovery" href={canonicalPath} prefetch={false}>
            VOLVER AL INICIO DE LA GRABACIÓN
          </Link>
        ) : (
          <>
            <RoutePreview page={page} />
            <PointTable page={page} />
            {page.nextAfter ? (
              <nav className="recording-pagination" aria-label="Paginación de observaciones">
                <Link className="button-link" href={`${canonicalPath}?after=${page.nextAfter}`} prefetch={false}>
                  VER LAS SIGUIENTES OBSERVACIONES
                </Link>
              </nav>
            ) : null}
          </>
        )}
      </article>
    </DogPrivateShell>
  );
}
