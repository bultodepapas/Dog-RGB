import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import { validateContractRequest } from "../_shared/contract_validation.ts";
import {
  assertProtocolRequest, boundedJson, deviceDigest,
  parseDeviceBearer, postgresBytea, problem, serviceRpc, sha256,
  requestIdFrom, validateSyncSemantics,
} from "../_shared/gateway.ts";

export default {
  fetch: withSupabase({ auth: "none" }, async (req, ctx) => {
    let requestId: string | null = null;
    try {
      const { raw, body } = await boundedJson(req, 128 * 1024);
      requestId = requestIdFrom(body);
      const auth = parseDeviceBearer(req);
      ({ requestId } = assertProtocolRequest(body));
      validateContractRequest("device-sync", body);
      await validateSyncSemantics(body);
      const response = await serviceRpc(ctx.supabaseAdmin, "device_sync_gateway_v1", {
        p_credential_id: auth.credentialId,
        p_secret_digest: postgresBytea(await deviceDigest(auth.secret)),
        p_request_id: requestId,
        p_request_sha256: postgresBytea(await sha256(raw)),
        p_request: body,
      });
      return Response.json(response, { headers: { "cache-control": "no-store" } });
    } catch (error) { return problem(error, requestId); }
  }),
};
