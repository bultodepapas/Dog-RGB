"use client";

import { useEffect, useRef, useState } from "react";
import Link from "next/link";
import { accountDeletionReceipt } from "../../lib/privacy/account";

type RecoveryState = Readonly<{
  status: "checking" | "error" | "completed";
  message: string;
  requestId?: string;
  receipt?: string;
  completedAt?: string;
}>;

const INITIAL: RecoveryState = { status: "checking", message: "Buscando un comprobante confirmado…" };

async function post(body: Record<string, string>): Promise<{ response: Response; payload: unknown }> {
  const response = await fetch("/account/finalize", {
    method: "POST",
    credentials: "same-origin",
    headers: { "content-type": "application/json", accept: "application/json" },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(30_000),
  });
  let payload: unknown = null;
  try { payload = await response.json(); } catch { /* Handle incomplete responses as unknown. */ }
  return { response, payload };
}

export function AccountDeletionReceiptRecovery() {
  const [state, setState] = useState<RecoveryState>(INITIAL);
  const [checking, setChecking] = useState(false);
  const started = useRef(false);
  const result = useRef<HTMLDivElement>(null);

  async function recover() {
    setChecking(true);
    setState({ status: "checking", message: "Buscando un comprobante confirmado…" });
    try {
      const recovered = await post({ action: "receipt" });
      if (!recovered.response.ok) throw new Error("account_receipt_unavailable");
      const receipt = accountDeletionReceipt(recovered.payload);
      setState({ status: "completed", message: "La eliminación quedó confirmada.", requestId: receipt.requestId, receipt: receipt.receipt, completedAt: receipt.completedAt });
      void post({ action: "acknowledge", request_id: receipt.requestId, receipt_sha256: receipt.receipt }).catch(() => undefined);
    } catch {
      setState({ status: "error", message: "No pudimos confirmar la eliminación con la sesión disponible. El ID de solicitud por sí solo no permite recuperar el recibo." });
    } finally {
      setChecking(false);
    }
  }

  useEffect(() => {
    if (started.current) return;
    started.current = true;
    void recover();
  }, []);

  useEffect(() => { if (state.status !== "checking") result.current?.focus(); }, [state]);

  return <section aria-labelledby="receipt-recovery-title">
    <h1 id="receipt-recovery-title">Comprobante de eliminación</h1>
    <p>La consulta usa la sesión firmada que se conservó para esta solicitud. Solo funciona mientras esa sesión siga vigente; después de su vencimiento, esta página no puede confirmar el resultado.</p>
    <div ref={result} aria-live="polite" tabIndex={-1}>
      <p>{state.message}</p>
      {state.status === "completed" ? <>
        <p>Solicitud: <code>{state.requestId}</code></p>
        {state.completedAt ? <p>Completada: <time dateTime={state.completedAt}>{state.completedAt}</time></p> : null}
        <p>Recibo: <code>{state.receipt}</code></p>
      </> : null}
    </div>
    {state.status !== "completed" ? <button type="button" onClick={() => void recover()} disabled={checking}>{checking ? "Consultando…" : "Volver a consultar"}</button> : null}
    <p><Link href="/">Volver al inicio</Link></p>
  </section>;
}
