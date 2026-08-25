import type { VerifiedIdentity } from "./identity";

type VerifiedClaims = Readonly<{
  aud?: unknown;
  role?: unknown;
  sub?: unknown;
}>;

export function identityFromVerifiedClaims(
  claims: VerifiedClaims | null | undefined,
): VerifiedIdentity | null {
  const audience = claims?.aud;
  const hasAuthenticatedAudience =
    audience === "authenticated" ||
    (Array.isArray(audience) && audience.includes("authenticated"));

  if (
    typeof claims?.sub !== "string" ||
    claims.sub.length === 0 ||
    claims.role !== "authenticated" ||
    !hasAuthenticatedAudience
  ) {
    return null;
  }

  return Object.freeze({ userId: claims.sub });
}
