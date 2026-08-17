import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import { validateContractRequest } from "../_shared/contract_validation.ts";
import {
  assertProtocolRequest, base64UrlDecode, boundedJson, deviceDigest,
  hmacSha256, HttpProblem, postgresBytea, problem, requiredEnv, serviceRpc,
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
      const result = await serviceRpc(ctx.supabaseAdmin, "consume_device_claim_v1", {
        p_code_digest: postgresBytea(await hmacSha256(requiredEnv("CLAIM_HMAC_PEPPER"), code)),
        p_request_id: requestId,
        p_request_sha256: postgresBytea(await sha256(raw)),
        p_device_public_id: (device as Record<string, unknown>).device_id,
        p_credential_id: credentialId,
        p_secret_digest: postgresBytea(await deviceDigest(rawSecret)),
        p_device: device, p_capabilities: capabilities,
      }) as Record<string, unknown>;
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
