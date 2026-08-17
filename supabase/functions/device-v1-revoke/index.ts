import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import { validateContractRequest } from "../_shared/contract_validation.ts";
import {
  assertProtocolRequest, boundedJson, deviceDigest, HttpProblem,
  parseDeviceBearer, postgresBytea, problem, requestIdFrom, serviceRpc, sha256,
} from "../_shared/gateway.ts";

export default {
  fetch: withSupabase({ auth: "none" }, async (req, ctx) => {
    let requestId: string | null = null;
    try {
      const { raw, body } = await boundedJson(req, 4096);
      requestId = requestIdFrom(body);
      const auth = parseDeviceBearer(req);
      ({ requestId } = assertProtocolRequest(body));
      validateContractRequest("device-revoke", body);
      if (body.credential_id !== auth.credentialId || typeof body.device_id !== "string" ||
          (body.reason !== "local_unlink" && body.reason !== "factory_reset")) {
        throw new HttpProblem(400, "invalid_revoke_request", "Invalid revoke request", "The revoke identity or reason is not valid.");
      }
      const response = await serviceRpc(ctx.supabaseAdmin, "device_revoke_v1", {
        p_credential_id: auth.credentialId,
        p_secret_digest: postgresBytea(await deviceDigest(auth.secret)),
        p_request_id: requestId,
        p_request_sha256: postgresBytea(await sha256(raw)),
        p_device_id: body.device_id, p_reason: body.reason,
      });
      return Response.json(response, { headers: { "cache-control": "no-store" } });
    } catch (error) { return problem(error, requestId); }
  }),
};
