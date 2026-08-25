export type SupportedEmailOtpType = "email" | "recovery";

const REDIRECTS_BY_TYPE: Readonly<
  Record<SupportedEmailOtpType, ReadonlySet<string>>
> = {
  email: new Set(["/", "/login"]),
  recovery: new Set(["/forgot-password?mode=update"]),
};

const DEFAULT_REDIRECT: Readonly<Record<SupportedEmailOtpType, string>> = {
  email: "/?auth=confirmed",
  recovery: "/forgot-password?mode=update",
};

const LOCAL_AUTH_ORIGINS = new Map([
  ["127.0.0.1:3000", "http://127.0.0.1:3000"],
  ["localhost:3000", "http://localhost:3000"],
]);

export function parseEmailOtpType(
  candidate: string | null,
): SupportedEmailOtpType | null {
  return candidate === "email" || candidate === "recovery" ? candidate : null;
}

export function resolveConfirmationRedirect(
  type: SupportedEmailOtpType,
  candidate: string | null,
): string {
  if (candidate && REDIRECTS_BY_TYPE[type].has(candidate)) {
    return candidate;
  }

  return DEFAULT_REDIRECT[type];
}

export function confirmationErrorRedirect(
  type: SupportedEmailOtpType | null,
): string {
  return type === "recovery"
    ? "/forgot-password?auth_error=invalid_or_expired"
    : "/login?auth_error=invalid_or_expired";
}

export function resolveLocalAuthOrigin(hostHeader: string | null): string {
  const host = hostHeader?.trim().toLowerCase() ?? "";

  // M1.2 is intentionally local-only. An unknown or injected Host must never
  // become a redirect origin. M3 will replace this with the reviewed hosted
  // site origin when Vercel Preview is authorized.
  return LOCAL_AUTH_ORIGINS.get(host) ?? "http://127.0.0.1:3000";
}
