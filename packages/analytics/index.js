export function coverageRatio(observedSeconds, windowSeconds) {
  if (!Number.isFinite(observedSeconds) || !Number.isFinite(windowSeconds) || windowSeconds <= 0) return 0;
  return Math.max(0, Math.min(1, observedSeconds / windowSeconds));
}
