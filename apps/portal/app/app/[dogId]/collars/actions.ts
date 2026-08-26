"use server";

import { FunctionsHttpError } from "@supabase/supabase-js";

import { isCanonicalUuid } from "../../../../lib/auth/protected-route";
import {
  type RevokeCollarActionState,
  revokeCollarMutationHandler,
} from "../../../../lib/collars/revoke";
import {
  type IssueClaimActionState,
  issueClaimMutationHandler,
} from "../../../../lib/claim-code/issue-claim";
import { requireDogAccess } from "../../../../lib/data-access/dogs";
import { revokeCollar } from "../../../../lib/data-access/collars";
import { createServerSupabaseClient } from "../../../../lib/supabase/server";

const SAFE_PROBLEM_CODES = new Set([
  "active_claim_exists",
  "email_not_verified",
  "rate_limited",
]);

async function safeProblemCode(error: unknown): Promise<string | null> {
  if (!(error instanceof FunctionsHttpError)) {
    return null;
  }

  try {
    const body: unknown = await error.context.json();
    if (body === null || typeof body !== "object" || Array.isArray(body)) {
      return null;
    }
    const code = (body as Record<string, unknown>).code;
    return typeof code === "string" && SAFE_PROBLEM_CODES.has(code)
      ? code
      : null;
  } catch {
    return null;
  }
}

const issueClaimMutation = issueClaimMutationHandler({
  isCanonicalUuid,
  createRequestId: () => crypto.randomUUID(),
  async authorizeDogWrite(dogId) {
    await requireDogAccess(dogId, "write");
  },
  async invokeIssueClaim(input) {
    const client = await createServerSupabaseClient();
    const { data, error } = await client.functions.invoke(
      "user-v1-issue-claim",
      { body: input },
    );
    if (error) {
      return { ok: false as const, problemCode: await safeProblemCode(error) };
    }
    return { ok: true as const, data };
  },
});

const revokeMutation = revokeCollarMutationHandler({
  isCanonicalUuid,
  revoke: revokeCollar,
});

export async function issueClaimAction(
  _previousState: IssueClaimActionState,
  formData: FormData,
): Promise<IssueClaimActionState> {
  const result = await issueClaimMutation(formData);
  return result.state;
}

export async function revokeCollarAction(
  _previousState: RevokeCollarActionState,
  formData: FormData,
): Promise<RevokeCollarActionState> {
  return revokeMutation(formData);
}
