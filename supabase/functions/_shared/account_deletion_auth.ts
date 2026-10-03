export function recentPasswordAuthenticationTime(
  amr: unknown,
  nowSeconds = Math.floor(Date.now() / 1000),
): number | null {
  if (!Array.isArray(amr)) return null;
  let newest: number | null = null;
  for (const factor of amr) {
    if (!factor || typeof factor !== "object" || Array.isArray(factor)) continue;
    const record = factor as Record<string, unknown>;
    if (record.method !== "password" || typeof record.timestamp !== "number") continue;
    const timestamp = record.timestamp;
    if (!Number.isSafeInteger(timestamp) || timestamp > nowSeconds || timestamp < nowSeconds - 300) continue;
    if (newest === null || timestamp > newest) newest = timestamp;
  }
  return newest;
}
