import { cookies } from "next/headers";
import { isCanonicalUuid } from "../../../lib/auth/protected-route";
import { accountDeletion, accountDeletionReceipt } from "../../../lib/privacy/account";
import { createServerSupabaseClient } from "../../../lib/supabase/server";
import { resolveAuthOrigin } from "../../../lib/auth/redirect";

export const dynamic = "force-dynamic";

const MAX_BODY_BYTES = 1024;
const RECEIPT_PATTERN = /^[A-Za-z0-9_-]{43}$/u;
const RECOVERY_COOKIE = "dog_rgb_account_deletion_request";
const FAILURE_MESSAGE = "No pudimos completar la operación. Revisa tu contraseña y consulta el estado antes de reintentar.";
const RECOVERY_MESSAGE = "No pudimos confirmar la eliminación. Se mostrará como completada solo cuando haya un recibo válido.";

function json(body: unknown, status: number): Response {
  return Response.json(body, {
    status,
    headers: {
      "Cache-Control": "private, no-store",
      Pragma: "no-cache",
      Vary: "Cookie, Origin",
    },
  });
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function exactKeys(value: Record<string, unknown>, keys: readonly string[]): boolean {
  return Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}

function requestIdentifier(value: unknown): value is string {
  return typeof value === "string" && isCanonicalUuid(value) &&
    /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu.test(value);
}

async function boundedBody(request: Request): Promise<Record<string, unknown> | null> {
  if (request.headers.get("content-type")?.split(";", 1)[0].trim().toLowerCase() !== "application/json") return null;
  const declaredLength = request.headers.get("content-length");
  if (declaredLength && (!/^\d+$/u.test(declaredLength) || Number(declaredLength) > MAX_BODY_BYTES)) return null;
  const reader = request.body?.getReader();
  if (!reader) return null;
  const chunks: Uint8Array[] = [];
  let bytes = 0;
  try {
    for (;;) {
      const next = await reader.read();
      if (next.done) break;
      bytes += next.value.byteLength;
      if (bytes > MAX_BODY_BYTES) {
        await reader.cancel();
        return null;
      }
      chunks.push(next.value);
    }
  } finally {
    reader.releaseLock();
  }
  const raw = new Uint8Array(bytes);
  let offset = 0;
  for (const chunk of chunks) { raw.set(chunk, offset); offset += chunk.byteLength; }
  try {
    const value: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(raw));
    return record(value) ? value : null;
  } catch {
    return null;
  }
}

function receiptJson(receipt: ReturnType<typeof accountDeletionReceipt>) {
  return {
    schema_version: "account-deletion-receipt-v1",
    request_id: receipt.requestId,
    status: "completed",
    completed_at: receipt.completedAt,
    receipt_sha256: receipt.receipt,
  };
}

export async function POST(request: Request): Promise<Response> {
  const trustedOrigin = resolveAuthOrigin(
    process.env.PORTAL_SITE_ORIGIN,
    request.headers.get("host"),
  );
  if (!trustedOrigin || request.headers.get("origin") !== trustedOrigin) {
    return json({ status: "error", message: FAILURE_MESSAGE }, 403);
  }

  const body = await boundedBody(request);
  if (!body || typeof body.action !== "string") return json({ status: "error", message: FAILURE_MESSAGE }, 400);

  try {
    if (body.action === "prepare") {
      if (!exactKeys(body, ["action", "request_id", "password"]) || !requestIdentifier(body.request_id) ||
          typeof body.password !== "string" || body.password.length < 1 || body.password.length > 128) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 400);
      }

      const client = await createServerSupabaseClient();
      const identity = await client.auth.getUser();
      if (identity.error || !identity.data.user?.email) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 401);
      }

      const verified = await client.auth.signInWithPassword({
        email: identity.data.user.email,
        password: body.password,
      });
      if (verified.error || verified.data.user?.id !== identity.data.user.id || !verified.data.session) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 403);
      }

      const current = await client.rpc("get_my_account_deletion_v1");
      if (current.error) return json({ status: "error", message: FAILURE_MESSAGE }, 409);
      const deletion = accountDeletion(current.data);
      if (deletion.status !== "ready" || deletion.requestId !== body.request_id) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 409);
      }

      const expiresAt = verified.data.session.expires_at;
      const maxAge = typeof expiresAt === "number" ? Math.min(3600, expiresAt - Math.floor(Date.now() / 1000)) : 0;
      if (maxAge < 1) return json({ status: "error", message: FAILURE_MESSAGE }, 401);
      const cookieStore = await cookies();
      cookieStore.set(RECOVERY_COOKIE, body.request_id, {
        httpOnly: true,
        sameSite: "strict",
        secure: trustedOrigin.startsWith("https://"),
        path: "/account",
        maxAge,
      });
      return json({ status: "prepared", request_id: body.request_id }, 200);
    }

    if (body.action === "finalize") {
      if (!exactKeys(body, ["action", "request_id"]) || !requestIdentifier(body.request_id)) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 400);
      }
      const cookieStore = await cookies();
      const recoveryRequestId = cookieStore.get(RECOVERY_COOKIE)?.value;
      if (recoveryRequestId !== body.request_id) return json({ status: "error", message: FAILURE_MESSAGE }, 409);

      const client = await createServerSupabaseClient();
      const identity = await client.auth.getUser();
      if (identity.error || !identity.data.user) return json({ status: "error", message: FAILURE_MESSAGE }, 401);
      const result = await client.functions.invoke("user-v1-account-deletion", {
        body: { action: "finalize", request_id: body.request_id },
      });
      if (result.error) return json({ status: "error", message: FAILURE_MESSAGE }, 409);
      const receipt = accountDeletionReceipt(result.data, body.request_id);
      return json(receiptJson(receipt), 200);
    }

    if (body.action === "receipt") {
      if (!exactKeys(body, ["action"])) return json({ status: "error", message: RECOVERY_MESSAGE }, 400);
      const cookieStore = await cookies();
      const recoveryRequestId = cookieStore.get(RECOVERY_COOKIE)?.value;
      if (!requestIdentifier(recoveryRequestId)) return json({ status: "error", message: RECOVERY_MESSAGE }, 401);

      // This read intentionally uses the signed session without a live Auth lookup.
      // The Edge function verifies the JWT and limits it to this completed receipt.
      const client = await createServerSupabaseClient();
      const result = await client.functions.invoke("user-v1-account-deletion", {
        body: { action: "receipt", request_id: recoveryRequestId },
      });
      if (result.error) return json({ status: "error", message: RECOVERY_MESSAGE }, 409);
      const receipt = accountDeletionReceipt(result.data, recoveryRequestId);
      return json(receiptJson(receipt), 200);
    }

    if (body.action === "acknowledge") {
      if (!exactKeys(body, ["action", "request_id", "receipt_sha256"]) || !requestIdentifier(body.request_id) ||
          typeof body.receipt_sha256 !== "string" || !RECEIPT_PATTERN.test(body.receipt_sha256)) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 400);
      }
      const cookieStore = await cookies();
      if (cookieStore.get(RECOVERY_COOKIE)?.value !== body.request_id) {
        return json({ status: "error", message: FAILURE_MESSAGE }, 409);
      }

      // The browser sends this only after it has received and validated the receipt.
      const client = await createServerSupabaseClient();
      await client.auth.signOut({ scope: "local" }).catch(() => undefined);
      cookieStore.set(RECOVERY_COOKIE, "", {
        httpOnly: true,
        sameSite: "strict",
        secure: trustedOrigin.startsWith("https://"),
        path: "/account",
        maxAge: 0,
      });
      return json({ status: "acknowledged" }, 200);
    }

    return json({ status: "error", message: FAILURE_MESSAGE }, 400);
  } catch {
    return json({ status: "error", message: FAILURE_MESSAGE }, 500);
  }
}
