import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import { validateContractRequest } from "../_shared/contract_validation.ts";
import {
  assertProtocolRequest, boundedJson, hmacSha256, HttpProblem, postgresBytea,
  isUuidV4, problem, requestIdFrom, requiredEnv, serviceRpc,
} from "../_shared/gateway.ts";

const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

function createClaimCode(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(10));
  let value = bytes.reduce((result, byte) => (result << 8n) | BigInt(byte), 0n);
  let code = "";
  for (let index = 0; index < 16; index += 1) {
    code = CROCKFORD[Number(value & 31n)] + code;
    value >>= 5n;
  }
  return code;
}

export default {
  fetch: withSupabase({ auth: "user" }, async (req, ctx) => {
    let requestId: string | null = null;
    try {
      const { body } = await boundedJson(req, 4096, false);
      requestId = requestIdFrom(body);
      ({ requestId } = assertProtocolRequest(body));
      validateContractRequest("issue-claim", body);
      if (typeof ctx.userClaims?.id !== "string") {
        throw new HttpProblem(401, "authentication_required", "Authentication required", "A verified user session is required.");
      }
      const { data: userResult, error: userError } = await ctx.supabase.auth.getUser();
      if (userError || !userResult.user) {
        throw new HttpProblem(401, "authentication_required", "Authentication required", "A current user session is required.");
      }
      if (!userResult.user.email_confirmed_at) {
        throw new HttpProblem(403, "email_not_verified", "Verified email required", "Verify the account email before issuing a claim.");
      }
      const userId = userResult.user.id;
      const dogId = body.dog_id;
      if (typeof dogId !== "string") {
        throw new HttpProblem(400, "invalid_claim_request", "Invalid claim request", "A dog_id is required.");
      }
      const code = createClaimCode();
      const issuedAt = new Date();
      const serverTime = issuedAt.toISOString();
      const expiresAt = new Date(issuedAt.getTime() + 900_000).toISOString();
      const digest = await hmacSha256(requiredEnv("CLAIM_HMAC_PEPPER"), code);
      const claimId = await serviceRpc(ctx.supabaseAdmin, "issue_device_claim_v1", {
        p_dog_id: dogId, p_requested_by: userId,
        p_code_digest: postgresBytea(digest), p_expires_at: expiresAt, p_max_attempts: 5,
      });
      if (!isUuidV4(claimId)) {
        throw new HttpProblem(500, "internal_error", "Internal error", "The claim could not be issued.");
      }
      return Response.json({
        protocol_version: 1, request_id: requestId, server_time: serverTime,
        claim: { claim_id: claimId, dog_id: dogId, code, expires_at: expiresAt, expires_in_seconds: 900 },
      }, { headers: { "cache-control": "no-store" } });
    } catch (error) { return problem(error, requestId); }
  }),
};
