export const CLAIM_TTL_SECONDS = 900;
export const CLAIM_CODE_PATTERN = /^[0-9A-HJKMNP-TV-Z]{16}$/u;
const UUID_V4_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;

export const ISSUE_CLAIM_GENERIC_ERROR =
  "No pudimos generar el código. Inténtalo de nuevo.";
export const ISSUE_CLAIM_ACTIVE_ERROR =
  "Ya existe un código activo. Úsalo o espera a que venza.";
export const ISSUE_CLAIM_EMAIL_ERROR =
  "Verifica el correo de tu cuenta antes de generar el código.";
export const ISSUE_CLAIM_RATE_ERROR =
  "Has generado demasiados códigos. Inténtalo más tarde.";

export type IssueClaimActionState =
  | Readonly<{ status: "idle"; message: "" }>
  | Readonly<{ status: "error"; message: string }>
  | Readonly<{ status: "success"; message: ""; code: string }>;

export const INITIAL_ISSUE_CLAIM_ACTION_STATE: IssueClaimActionState = {
  status: "idle",
  message: "",
};

type InvocationResult =
  | Readonly<{ ok: true; data: unknown }>
  | Readonly<{ ok: false; problemCode: string | null }>;

type IssueClaimResult =
  | Readonly<{ ok: true; state: Extract<IssueClaimActionState, { status: "success" }> }>
  | Readonly<{ ok: false; state: Extract<IssueClaimActionState, { status: "error" }> }>;

export type IssueClaimDependencies = Readonly<{
  isCanonicalUuid(value: string): boolean;
  createRequestId(): string;
  authorizeDogWrite(dogId: string): Promise<void>;
  invokeIssueClaim(input: Readonly<{
    protocol_version: 1;
    request_id: string;
    dog_id: string;
  }>): Promise<InvocationResult>;
}>;

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function hasExactKeys(
  value: Record<string, unknown>,
  expected: readonly string[],
): boolean {
  const actual = Object.keys(value).sort();
  const sortedExpected = [...expected].sort();
  return actual.length === sortedExpected.length &&
    actual.every((key, index) => key === sortedExpected[index]);
}

function canonicalTimestamp(value: unknown): number | null {
  if (typeof value !== "string") {
    return null;
  }

  const milliseconds = Date.parse(value);
  if (
    !Number.isFinite(milliseconds) ||
    new Date(milliseconds).toISOString() !== value
  ) {
    return null;
  }

  return milliseconds;
}

export function parseIssueClaimResponse(
  value: unknown,
  expected: Readonly<{ dogId: string; requestId: string }>,
): string | null {
  if (
    !isRecord(value) ||
    !hasExactKeys(value, ["claim", "protocol_version", "request_id", "server_time"]) ||
    value.protocol_version !== 1 ||
    value.request_id !== expected.requestId ||
    !isRecord(value.claim) ||
    !hasExactKeys(value.claim, [
      "claim_id",
      "code",
      "dog_id",
      "expires_at",
      "expires_in_seconds",
    ]) ||
    value.claim.dog_id !== expected.dogId ||
    typeof value.claim.claim_id !== "string" ||
    !UUID_V4_PATTERN.test(value.claim.claim_id) ||
    typeof value.claim.code !== "string" ||
    !CLAIM_CODE_PATTERN.test(value.claim.code) ||
    value.claim.expires_in_seconds !== CLAIM_TTL_SECONDS
  ) {
    return null;
  }

  const serverTime = canonicalTimestamp(value.server_time);
  const expiresAt = canonicalTimestamp(value.claim.expires_at);
  if (
    serverTime === null ||
    expiresAt === null ||
    expiresAt - serverTime !== CLAIM_TTL_SECONDS * 1_000
  ) {
    return null;
  }

  return value.claim.code;
}

function failure(message = ISSUE_CLAIM_GENERIC_ERROR): IssueClaimResult {
  return { ok: false, state: { status: "error", message } };
}

function problemMessage(problemCode: string | null): string {
  if (problemCode === "active_claim_exists") {
    return ISSUE_CLAIM_ACTIVE_ERROR;
  }
  if (problemCode === "email_not_verified") {
    return ISSUE_CLAIM_EMAIL_ERROR;
  }
  if (problemCode === "rate_limited") {
    return ISSUE_CLAIM_RATE_ERROR;
  }
  return ISSUE_CLAIM_GENERIC_ERROR;
}

export function issueClaimMutationHandler(
  dependencies: IssueClaimDependencies,
) {
  return async function issueClaimMutation(
    formData: FormData,
  ): Promise<IssueClaimResult> {
    const dogId = formData.get("dogId");
    if (typeof dogId !== "string" || !dependencies.isCanonicalUuid(dogId)) {
      return failure();
    }

    try {
      await dependencies.authorizeDogWrite(dogId);
      const requestId = dependencies.createRequestId();
      if (!UUID_V4_PATTERN.test(requestId)) {
        return failure();
      }

      const invocation = await dependencies.invokeIssueClaim({
        protocol_version: 1,
        request_id: requestId,
        dog_id: dogId,
      });
      if (!invocation.ok) {
        return failure(problemMessage(invocation.problemCode));
      }

      const code = parseIssueClaimResponse(
        invocation.data,
        { dogId, requestId },
      );
      if (!code) {
        return failure();
      }

      return { ok: true, state: { status: "success", message: "", code } };
    } catch {
      return failure();
    }
  };
}
