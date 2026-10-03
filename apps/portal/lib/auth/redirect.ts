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

function configuredOrigin(candidate: string): string | null {
  try {
    const url = new URL(candidate);
    const isLoopback = ["127.0.0.1", "localhost", "[::1]"].includes(
      url.hostname,
    );

    if (
      (url.protocol !== "https:" && !(url.protocol === "http:" && isLoopback)) ||
      !url.hostname ||
      url.username ||
      url.password ||
      (url.pathname !== "" && url.pathname !== "/") ||
      url.search ||
      url.hash
    ) {
      return null;
    }

    return url.origin;
  } catch {
    return null;
  }
}

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

export function resolveAuthOrigin(
  configuredSiteOrigin: string | null | undefined,
  hostHeader: string | null,
): string | null {
  const configured = configuredSiteOrigin?.trim();
  if (configured) return configuredOrigin(configured);

  const host = hostHeader?.trim().toLowerCase() ?? "";
  return LOCAL_AUTH_ORIGINS.get(host) ?? null;
}
