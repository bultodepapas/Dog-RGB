import type { DogDataAccessErrorCode } from "../data-access/dogs-core";

const UUID_SEGMENT =
  "[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}";

const DOG_SECTION_PATTERN = new RegExp(
  `^/app/${UUID_SEGMENT}/(?:today|history|collars|configuration)$`,
  "iu",
);
const RECORDING_PATTERN = new RegExp(
  `^/app/${UUID_SEGMENT}/recordings/${UUID_SEGMENT}$`,
  "iu",
);
const UUID_PATTERN = new RegExp(`^${UUID_SEGMENT}$`, "iu");

export const DEFAULT_PROTECTED_PATH = "/onboarding";

export const DOG_APP_SECTIONS = [
  "today",
  "history",
  "collars",
  "configuration",
] as const;

export type DogAppSection = (typeof DOG_APP_SECTIONS)[number];

export type DogPageFailure = "login" | "not-found" | "error";

export function isCanonicalUuid(value: string): boolean {
  return UUID_PATTERN.test(value);
}

export function resolveProtectedReturnPath(candidate: unknown): string {
  if (typeof candidate !== "string") {
    return DEFAULT_PROTECTED_PATH;
  }

  if (
    candidate === DEFAULT_PROTECTED_PATH ||
    DOG_SECTION_PATTERN.test(candidate) ||
    RECORDING_PATTERN.test(candidate)
  ) {
    return candidate;
  }

  return DEFAULT_PROTECTED_PATH;
}

export function isPrivatePortalPath(pathname: string): boolean {
  return pathname === DEFAULT_PROTECTED_PATH || pathname.startsWith("/app/");
}

export function protectedLoginPath(candidate: unknown): string {
  const returnPath = resolveProtectedReturnPath(candidate);
  return `/login?next=${encodeURIComponent(returnPath)}`;
}

export function dogAppPath(
  dogId: string,
  section: DogAppSection,
): string {
  return `/app/${dogId}/${section}`;
}

export function recordingAppPath(
  dogId: string,
  recordingId: string,
): string {
  return `/app/${dogId}/recordings/${recordingId}`;
}

export function classifyDogPageFailure(
  code: DogDataAccessErrorCode,
): DogPageFailure {
  if (code === "authentication_required") {
    return "login";
  }

  if (code === "invalid_dog_id" || code === "access_denied") {
    return "not-found";
  }

  return "error";
}
