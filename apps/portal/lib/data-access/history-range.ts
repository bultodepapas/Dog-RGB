export type HistoryRange = Readonly<{ from: string; to: string; until: string }>;

export function parseHistoryRange(from: unknown, to: unknown): HistoryRange | null | "invalid" {
  if ((from === undefined || from === "") && (to === undefined || to === "")) return null;
  if (typeof from !== "string" || typeof to !== "string") return "invalid";
  const parse = (value: string) => {
    if (!/^\d{4}-\d{2}-\d{2}$/u.test(value)) return NaN;
    const milliseconds = Date.parse(`${value}T00:00:00Z`);
    return Number.isFinite(milliseconds) && new Date(milliseconds).toISOString().slice(0, 10) === value ? milliseconds : NaN;
  };
  const start = parse(from), end = parse(to);
  const days = (end - start) / 86_400_000;
  if (!Number.isFinite(days) || days < 0 || days > 365) return "invalid";
  const until = new Date(end + 86_400_000).toISOString().slice(0, 10);
  if (!/^\d{4}-\d{2}-\d{2}$/u.test(until)) return "invalid";
  return { from, to, until };
}

export function historyRangeExpression(range: HistoryRange, timezone: string): string {
  // Dates are canonical and timezone comes from the authorized dog, never URL input.
  // PostgreSQL resolves both local midnights independently, including 23/25h days.
  const start = JSON.stringify(`${range.from} 00:00:00 ${timezone}`);
  const end = JSON.stringify(`${range.until} 00:00:00 ${timezone}`);
  return `or(started_at.is.null,and(started_at.gte.${start},started_at.lt.${end}))`;
}
