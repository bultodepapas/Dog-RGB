import "@supabase/functions-js/edge-runtime.d.ts";
import { withSupabase } from "@supabase/server";
import {
  boundedJson, HttpProblem, isUuidV4, problem, requestIdFrom,
} from "../_shared/gateway.ts";
import { recentPasswordAuthenticationTime } from "../_shared/account_deletion_auth.ts";

const SCOPE_SHA256 = /^[A-Za-z0-9_-]{43}$/;

type AccountDeletionContext = {
  supabase: {
    auth: {
      getClaims(jwt: string): Promise<{ data: { claims?: unknown } | null; error: unknown }>;
      getUser(jwt: string): Promise<{ data: { user: { id: string } | null }; error: unknown }>;
    };
  };
  jwtClaims?: Record<string, unknown> | null;
  supabaseAdmin: {
    schema(name: string): {
      rpc(name: string, args: Record<string, unknown>): Promise<{ data: unknown; error: { message?: string } | null }>;
    };
    auth: { admin: { deleteUser(id: string, shouldSoftDelete?: boolean): Promise<{ error: unknown }> } };
  };
};

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function apiProblem(message: string | undefined): HttpProblem {
  if (message === "account_deletion_scope_changed") {
    return new HttpProblem(409, "account_deletion_scope_changed", "Inventory changed", "Refresh the account deletion preview before confirming.");
  }
  if (message === "request_id_reused") {
    return new HttpProblem(409, "request_id_reused", "Request identifier reused", "The request identifier was already used with different content.");
  }
  if (message === "account_deletion_pending") {
    return new HttpProblem(409, "account_deletion_pending", "Deletion already pending", "This account already has a deletion request in progress.");
  }
  if (message === "reauthentication_required") {
    return new HttpProblem(403, "reauthentication_required", "Reauthentication required", "Sign in with your password again before continuing.");
  }
  if (message === "account_deletion_not_ready") {
    return new HttpProblem(409, "account_deletion_not_ready", "Deletion is still processing", "The account deletion cannot be finalized until every dog purge completes.");
  }
  if (message === "account_deletion_not_completed") {
    return new HttpProblem(409, "account_deletion_receipt_unavailable", "Receipt unavailable", "No completed receipt is available for this request.");
  }
  return new HttpProblem(503, "server_busy", "Service temporarily unavailable", "The account deletion request could not be completed. Try again later.", 30);
}

async function apiRpc(ctx: AccountDeletionContext, name: string, args: Record<string, unknown>): Promise<unknown> {
  const { data, error } = await ctx.supabaseAdmin.schema("api").rpc(name, args);
  if (error) throw apiProblem(error.message);
  return data;
}

async function requireRecentPassword(ctx: AccountDeletionContext, request: Request): Promise<{ userId: string; authenticatedAt: number }> {
  const authorization = request.headers.get("authorization");
  const token = authorization?.match(/^Bearer\s+([^\s]+)$/iu)?.[1];
  if (!token) {
    throw new HttpProblem(401, "authentication_required", "Authentication required", "A verified user session is required.");
  }
  const { data: verified, error: claimsError } = await ctx.supabase.auth.getClaims(token);
  if (claimsError || !verified || !isRecord(verified.claims)) {
    throw new HttpProblem(401, "authentication_required", "Authentication required", "A verified user session is required.");
  }
  const claims = verified.claims;
  const userId = claims.sub;
  if (typeof userId !== "string" || !isUuidV4(userId)) {
    throw new HttpProblem(401, "authentication_required", "Authentication required", "A verified user session is required.");
  }
  const { data, error } = await ctx.supabase.auth.getUser(token);
  if (error || !data.user || data.user.id !== userId) {
    throw new HttpProblem(401, "authentication_required", "Authentication required", "A current user session is required.");
  }
  const authenticatedAt = recentPasswordAuthenticationTime(claims.amr);
  if (authenticatedAt === null) {
    throw new HttpProblem(403, "reauthentication_required", "Reauthentication required", "Sign in with your password again before continuing.");
  }
  return { userId, authenticatedAt };
}

function completedReceipt(value: unknown, requestId: string): Record<string, unknown> {
  if (!isRecord(value) || value.schema_version !== "account-deletion-v1" || value.status !== "completed" ||
      value.request_id !== requestId || typeof value.completed_at !== "string" ||
      !Number.isFinite(Date.parse(value.completed_at)) || typeof value.receipt_sha256 !== "string" ||
      !/^[A-Za-z0-9_-]{43}$/u.test(value.receipt_sha256) || !Array.isArray(value.dog_jobs) ||
      value.dog_jobs.some(job => !isRecord(job) || job.status !== "completed")) {
    throw new HttpProblem(409, "account_deletion_receipt_unavailable", "Receipt unavailable", "No completed receipt is available for this request.");
  }
  return {
    schema_version: "account-deletion-receipt-v1",
    request_id: requestId,
    status: "completed",
    completed_at: value.completed_at,
    receipt_sha256: value.receipt_sha256,
  };
}

export default {
  fetch: withSupabase({ auth: "user" }, async (req, rawContext) => {
    let requestId: string | null = null;
    try {
      const { body } = await boundedJson(req, 4096, false);
      requestId = requestIdFrom(body);
      const ctx = rawContext as unknown as AccountDeletionContext;
      if (body.action === "request") {
        if (Object.keys(body).length !== 5 || !isUuidV4(body.request_id) ||
            body.confirmation_version !== "account-delete-v1" ||
            typeof body.scope_sha256 !== "string" || !SCOPE_SHA256.test(body.scope_sha256) ||
            body.confirmation_phrase !== "ELIMINAR CUENTA Y TODOS LOS PERROS") {
          throw new HttpProblem(400, "invalid_account_deletion_request", "Invalid account deletion request", "Refresh the preview and provide the exact confirmation.");
        }
        const identity = await requireRecentPassword(ctx, req);
        const result = await apiRpc(ctx, "request_account_deletion_v1", {
          p_user_id: identity.userId,
          p_request_id: body.request_id,
          p_confirmation_version: body.confirmation_version,
          p_scope_sha256: body.scope_sha256,
          p_confirmation_phrase: body.confirmation_phrase,
          p_recent_password_at: identity.authenticatedAt,
        });
        return Response.json(result, { headers: { "cache-control": "no-store" } });
      }

      if (body.action === "finalize") {
        if (Object.keys(body).length !== 2 || !isUuidV4(body.request_id)) {
          throw new HttpProblem(400, "invalid_account_deletion_request", "Invalid account deletion request", "The finalize action requires the exact request identifier.");
        }
        const identity = await requireRecentPassword(ctx, req);
        const prepared = await apiRpc(ctx, "prepare_account_deletion_finalization_v1", {
          p_user_id: identity.userId,
        });
        if (!isRecord(prepared) || prepared.status !== "ready" || typeof prepared.request_id !== "string") {
          throw new HttpProblem(409, "account_deletion_not_ready", "Deletion is still processing", "The account deletion cannot be finalized until every dog purge completes.");
        }
        if (prepared.request_id !== body.request_id) {
          throw new HttpProblem(409, "account_deletion_not_ready", "Deletion request changed", "Refresh the account deletion status before continuing.");
        }
        const { error } = await ctx.supabaseAdmin.auth.admin.deleteUser(identity.userId, false);
        if (error) {
          const { data: receipt, error: receiptError } = await ctx.supabaseAdmin.schema("api").rpc(
            "get_account_deletion_receipt_v1",
            { p_user_id: identity.userId, p_request_id: prepared.request_id },
          );
          if (!receiptError && isRecord(receipt) && receipt.status === "completed") {
            return Response.json(completedReceipt(receipt, prepared.request_id), { headers: { "cache-control": "no-store" } });
          }
          throw new HttpProblem(503, "server_busy", "Service temporarily unavailable", "The account could not be finalized. Its deletion status is still available after sign-in.", 30);
        }
        const result = await apiRpc(ctx, "get_account_deletion_receipt_v1", {
          p_user_id: identity.userId,
          p_request_id: prepared.request_id,
        });
        return Response.json(completedReceipt(result, prepared.request_id), { headers: { "cache-control": "no-store" } });
      }

      if (body.action === "receipt") {
        if (Object.keys(body).length !== 2 || !isUuidV4(body.request_id)) {
          throw new HttpProblem(400, "invalid_account_deletion_request", "Invalid account deletion request", "The receipt request is invalid.");
        }
        const userId = ctx.jwtClaims?.sub;
        if (typeof userId !== "string" || !isUuidV4(userId)) {
          throw new HttpProblem(401, "authentication_required", "Authentication required", "A verified user session is required.");
        }
        const stored = await apiRpc(ctx, "get_account_deletion_receipt_v1", {
          p_user_id: userId,
          p_request_id: body.request_id,
        });
        return Response.json(completedReceipt(stored, body.request_id), { headers: { "cache-control": "no-store" } });
      }

      throw new HttpProblem(400, "invalid_account_deletion_request", "Invalid account deletion request", "Choose a supported account deletion action.");
    } catch (error) {
      return problem(error, requestId);
    }
  }),
};
