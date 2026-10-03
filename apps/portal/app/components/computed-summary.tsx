import type { SummaryDto } from "../../lib/data-access/summary-core";

const LABELS = { pending: "Pendiente de cálculo", stale: "Resumen desactualizado", unavailable: "Datos insuficientes", available: "Resumen calculado" };
const REASONS: Record<string, string> = {
  insufficient_retained_data: "El detalle conservado no alcanza para reconstruir este período.",
  insufficient_time_evidence: "Faltan tiempos confiables para calcular actividad.",
  source_limit_exceeded: "El período supera el límite de cálculo de esta instalación.",
};

export function ComputedSummary({ summary, timezone }: Readonly<{ summary: SummaryDto; timezone: string }>) {
  const format = (value: number | null, unit: string) => value === null ? "No disponible" : `${new Intl.NumberFormat("es-CO", { maximumFractionDigits: 2 }).format(value)} ${unit}`;
  const date = (value: string) => new Intl.DateTimeFormat("es-CO", { timeZone: timezone, dateStyle: "short", timeStyle: "medium" }).format(new Date(value));
  const { metrics } = summary;
  return <div className="computed-summary">
    <strong>{LABELS[summary.status]}</strong>
    {summary.status !== "available" ? <p>{metrics ? "Cifras del intervalo indicado; el tiempo posterior todavía no forma parte de este cálculo." : REASONS[summary.reason ?? ""] ?? "El resumen no representa todavía los datos disponibles. El tiempo desconocido nunca cuenta como inactividad."}</p> : null}
    {metrics ? <dl className="today-facts">
      <div><dt>Tiempo observado</dt><dd>{format(metrics.observedSeconds, "s")}</dd></div>
      <div><dt>Movimiento estimado</dt><dd>{format(metrics.movingSeconds, "s")}</dd></div>
      <div><dt>Sin movimiento observado</dt><dd>{format(metrics.inactiveSeconds, "s")}</dd></div>
      <div><dt>Tiempo desconocido</dt><dd>{format(metrics.unknownSeconds, "s")}</dd></div>
      <div><dt>Cobertura del intervalo</dt><dd>{format(metrics.coverageRatio * 100, "%")}</dd></div>
      <div><dt>Distancia derivada</dt><dd>{format(metrics.distanceMeters, "m")}</dd></div>
      <div><dt>Velocidad media en movimiento</dt><dd>{format(metrics.averageMovingCmps === null ? null : metrics.averageMovingCmps / 100, "m/s")}</dd></div>
      <div><dt>Máxima filtrada</dt><dd>{format(metrics.filteredMaxCmps === null ? null : metrics.filteredMaxCmps / 100, "m/s")}</dd></div>
      <div><dt>Discontinuidades</dt><dd>{metrics.gapCount}</dd></div>
      <div><dt>Puntos perdidos declarados</dt><dd>{metrics.droppedPoints}</dd></div>
    </dl> : null}
    {summary.windowStart && summary.windowEnd ? <p>Intervalo: <time dateTime={summary.windowStart}>{date(summary.windowStart)}</time> – <time dateTime={summary.windowEnd}>{date(summary.windowEnd)}</time> ({timezone}). No incluye el tiempo posterior a ese corte.</p> : null}
    {summary.sourceReceivedAt ? <p>Datos recibidos hasta <time dateTime={summary.sourceReceivedAt}>{date(summary.sourceReceivedAt)}</time>. La recepción no demuestra observación reciente.</p> : null}
    {summary.computedAt && summary.algorithmVersion ? <p>Calculado <time dateTime={summary.computedAt}>{date(summary.computedAt)}</time> · algoritmo {summary.algorithmVersion}. Estimación derivada de telemetría.</p> : null}
  </div>;
}
