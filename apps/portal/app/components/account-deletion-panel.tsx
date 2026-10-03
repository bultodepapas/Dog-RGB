"use client";

import { useActionState, useEffect, useRef, useState, type FormEvent } from "react";
import Link from "next/link";
import { ACCOUNT_CONFIRMATION, accountDeletionReceipt, INITIAL_ACCOUNT_ACTION, type AccountPreview, type AccountDeletion } from "../../lib/privacy/account";
import { accountDeletionAction } from "../account/deletion-actions";

const FAILURE_MESSAGE = "No pudimos completar la operación. Revisa tu contraseña y consulta el estado antes de reintentar.";
const RECOVERY_MESSAGE = "No pudimos confirmar la eliminación. Se mostrará como completada solo cuando haya un recibo válido.";

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

async function postFinalization(body: Record<string, string>): Promise<{ response: Response; payload: unknown }> {
  const response = await fetch("/account/finalize", {
    method: "POST",
    credentials: "same-origin",
    headers: { "content-type": "application/json", accept: "application/json" },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(30_000),
  });
  let payload: unknown = null;
  try { payload = await response.json(); } catch { /* Treat an incomplete response as unknown. */ }
  return { response, payload };
}

function parsedReceipt(payload: unknown, expectedRequestId: string) {
  return accountDeletionReceipt(payload, expectedRequestId);
}

function acknowledgeReceipt(requestId: string, receipt: string): void {
  void postFinalization({ action: "acknowledge", request_id: requestId, receipt_sha256: receipt }).catch(() => undefined);
}

export function AccountDeletionPanel({ preview, deletion }: Readonly<{ preview: AccountPreview | null; deletion: AccountDeletion }>) {
  const [state, action, pending] = useActionState(accountDeletionAction, INITIAL_ACCOUNT_ACTION);
  const [completion, setCompletion] = useState(INITIAL_ACCOUNT_ACTION);
  const [finalizing, setFinalizing] = useState(false);
  const result = useRef<HTMLDivElement>(null);
  useEffect(() => { if (state.status !== "idle" || completion.status !== "idle") result.current?.focus(); }, [state, completion]);

  async function finalize(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const form = event.currentTarget;
    const requestId = deletion.requestId;
    const password = new FormData(form).get("password");
    if (!requestId || typeof password !== "string" || password.length < 1 || password.length > 128) {
      setCompletion({ status: "error", message: "Escribe tu contraseña actual." });
      return;
    }
    setFinalizing(true);
    setCompletion(INITIAL_ACCOUNT_ACTION);
    try {
      const prepared = await postFinalization({ action: "prepare", request_id: requestId, password });
      if (!prepared.response.ok || !record(prepared.payload) || prepared.payload.status !== "prepared" ||
          prepared.payload.request_id !== requestId) throw new Error("preparation_failed");

      try {
        const finalized = await postFinalization({ action: "finalize", request_id: requestId });
        if (!finalized.response.ok) throw new Error("finalization_failed");
        const receipt = parsedReceipt(finalized.payload, requestId);
        setCompletion({ status: "completed", message: "Cuenta eliminada. Los datos activos asociados ya fueron purgados.", receipt: receipt.receipt, requestId, completedAt: receipt.completedAt });
        acknowledgeReceipt(requestId, receipt.receipt);
      } catch {
        try {
          const recovered = await postFinalization({ action: "receipt" });
          if (!recovered.response.ok) throw new Error("account_receipt_unavailable");
          const receipt = parsedReceipt(recovered.payload, requestId);
          setCompletion({ status: "completed", message: "Cuenta eliminada. Los datos activos asociados ya fueron purgados.", receipt: receipt.receipt, requestId, completedAt: receipt.completedAt });
          acknowledgeReceipt(requestId, receipt.receipt);
        } catch {
          setCompletion({ status: "error", message: RECOVERY_MESSAGE, requestId });
        }
      }
    } catch {
      setCompletion({ status: "error", message: FAILURE_MESSAGE });
    } finally {
      setFinalizing(false);
    }
  }

  if (completion.status === "completed" || state.status === "completed") {
    const completed = completion.status === "completed" ? completion : state;
    return <div ref={result} role="status" tabIndex={-1}>
    <h2>Cuenta eliminada</h2><p>{completed.message}</p>
    {completed.completedAt ? <p>Completada: <time dateTime={completed.completedAt}>{completed.completedAt}</time></p> : null}
    <p>Recibo: <code>{completed.receipt}</code></p><Link href="/">Volver al inicio</Link>
  </div>;
  }
  const active = deletion.status === "pending" || deletion.status === "ready";
  return <section aria-labelledby="account-delete-title">
    <h2 id="account-delete-title">Eliminar cuenta</h2>
    {active ? <>
      <p>Solicitud {deletion.requestId}: {deletion.status === "ready" ? "datos purgados; falta cerrar la cuenta" : "purga pendiente"}. Puedes cerrar sesión y volver aquí para continuar.</p>
      <p>Tu identidad se conserva hasta terminar todas las purgas. La cuenta ya no puede operar sobre perros.</p>
      {deletion.failedJobs > 0 ? <form action={action}><input name="action" value="retry" type="hidden" /><button type="submit" disabled={pending}>Reintentar purgas fallidas</button></form> : null}
      {deletion.status === "ready" ? <form onSubmit={finalize} className="data-form"><label htmlFor="finalize-password">Contraseña actual</label><input id="finalize-password" name="password" type="password" autoComplete="current-password" maxLength={128} required /><button className="button-danger" type="submit" disabled={finalizing}>{finalizing ? "Completando…" : "Completar eliminación de cuenta"}</button></form> : null}
    </> : preview ? <>
      <p>Se eliminarán tu cuenta y todos los perros que posees, aunque tengan otros propietarios o miembros. Se retirarán tus otras membresías sin borrar esos perros. Descarga tus datos antes de continuar.</p>
      <h3>Perros que se eliminarán</h3>
      {preview.ownedDogs.length ? <ul>{preview.ownedDogs.map(dog => <li key={dog.id}>{dog.name} · otros miembros afectados: {dog.otherMembers}</li>)}</ul> : <p>Ningún perro a tu nombre.</p>}
      <h3>Membresías que se retirarán</h3>
      {preview.detach.length ? <ul>{preview.detach.map(dog => <li key={dog.id}>{dog.name} ({dog.role === "editor" ? "editor" : "lector"})</li>)}</ul> : <p>No tienes otras membresías.</p>}
      {preview.blockers.length ? <p role="alert">No se puede iniciar el borrado. Hay referencias de propiedad sin resolver o el inventario supera el límite admitido. Resuelve la propiedad de los perros que creaste antes de reintentar; no se transferirá automáticamente.</p> : <form action={action} className="data-form" aria-busy={pending}>
        <input name="action" value="request" type="hidden" />
        <input name="requestId" value={preview.requestId} type="hidden" />
        <input name="scopeHash" value={preview.scopeHash} type="hidden" />
        <label htmlFor="account-password">Contraseña actual</label><input id="account-password" name="password" type="password" autoComplete="current-password" maxLength={128} required />
        <label htmlFor="account-confirmation">Escribe {ACCOUNT_CONFIRMATION}</label><input id="account-confirmation" name="confirmation" autoComplete="off" spellCheck={false} required pattern={ACCOUNT_CONFIRMATION} />
        <button type="submit" className="button-danger" disabled={pending}>{pending ? "Confirmando…" : "Solicitar eliminación de cuenta y perros"}</button>
      </form>}
    </> : <p>No hay un inventario disponible. Actualiza la página antes de continuar.</p>}
    {state.message || completion.message ? <div ref={result} role="alert" tabIndex={-1}><p>{completion.message || state.message}</p>
      {completion.status === "error" && completion.requestId ? <p><Link href="/account/deletion-receipt">Recuperar comprobante</Link></p> : null}
    </div> : null}
  </section>;
}
