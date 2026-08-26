import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import { validateContractRequest } from "../_shared/contract_validation.ts";
import {
  assertProtocolRequest, base64UrlDecode, boundedJson, deviceDigest,
  claimAttemptKeys, hmacSha256, HttpProblem, postgresBytea, problem, requiredEnv, serviceRpc,
  requestIdFrom, sha256, validateCapabilityHash,
} from "../_shared/gateway.ts";

export default {
  fetch: withSupabase({ auth: "none" }, async (req, ctx) => {
    let requestId: string | null = null;
    try {
      const { raw, body } = await boundedJson(req, 32 * 1024);
      requestId = requestIdFrom(body);
      ({ requestId } = assertProtocolRequest(body));
      validateContractRequest("device-claim", body);
      const code = body.claim_code;
      const credentialId = body.credential_id;
      const device = body.device;
      const capabilities = body.capabilities;
      if (typeof code !== "string" || !/^[0-9A-HJKMNP-TV-Z]{16}$/.test(code) ||
          typeof credentialId !== "string" || !device || typeof device !== "object" ||
          !capabilities || typeof capabilities !== "object") {
        throw new HttpProblem(400, "invalid_claim_request", "Invalid claim request", "The claim request is not valid.");
      }
      const rawSecret = base64UrlDecode(String(body.credential_secret ?? ""));
      if (rawSecret.byteLength !== 32) throw new HttpProblem(400, "invalid_claim_request", "Invalid claim request", "The candidate secret must contain 32 bytes.");
      await validateCapabilityHash(device as Record<string, unknown>, capabilities);
      const deviceId = (device as Record<string, unknown>).device_id;
      if (typeof deviceId !== "string") {
        throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "A device identity is required.");
      }
      const attemptKeys = await claimAttemptKeys(req, deviceId);
      const result = await serviceRpc(ctx.supabaseAdmin, "consume_device_claim_gateway_v1", {
        p_source_attempt_key: postgresBytea(attemptKeys.source),
        p_device_attempt_key: postgresBytea(attemptKeys.device),
        p_code_digest: postgresBytea(await hmacSha256(requiredEnv("CLAIM_HMAC_PEPPER"), code)),
        p_request_id: requestId,
        p_request_sha256: postgresBytea(await sha256(raw)),
        p_device_public_id: deviceId,
        p_credential_id: credentialId,
        p_secret_digest: postgresBytea(await deviceDigest(rawSecret)),
        p_device: { ...(device as Record<string, unknown>), protocol_version: body.protocol_version },
        p_capabilities: capabilities,
      }) as Record<string, unknown>;
      if (result._problem === "claim_unavailable") {
        throw new HttpProblem(401, "claim_unavailable", "Claim unavailable", "The supplied claim is unavailable or expired.");
      }
      if (result._problem === "device_identity_conflict") {
        throw new HttpProblem(409, "device_identity_conflict", "Device identity conflict", "The supplied device identity conflicts with an existing link.");
      }
      if (result._problem === "rate_limited") {
        const retryAfter = Number.isInteger(result.retry_after_seconds) &&
            Number(result.retry_after_seconds) >= 1 && Number(result.retry_after_seconds) <= 86400
          ? Number(result.retry_after_seconds)
          : 900;
        throw new HttpProblem(429, "rate_limited", "Too many requests", "Too many failed claim attempts.", retryAfter);
      }
      if (result._problem !== undefined) {
        throw new HttpProblem(500, "internal_error", "Internal server error", "The claim could not be completed.");
      }
      return Response.json({
        protocol_version: 1, request_id: requestId, server_time: result.server_time,
        pairing: {
          device_id: result.device_id, credential_id: credentialId, collar_id: result.collar_id,
          dog_id: result.dog_id, state: "paired", accepted_capability_hash: result.accepted_capability_hash,
        },
        next_sync_after_seconds: 30,
      }, { headers: { "cache-control": "no-store" } });
    } catch (error) { return problem(error, requestId); }
  }),
};
