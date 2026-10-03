"use client";

import {
  type FormEvent,
  useActionState,
  useEffect,
  useRef,
  useState,
} from "react";

import { INITIAL_ISSUE_CLAIM_ACTION_STATE } from "../../../../lib/claim-code/issue-claim";
import { issueClaimAction } from "./actions";

type ClaimCodeFormProps = Readonly<{ dogId: string }>;

export function ClaimCodeForm({ dogId }: ClaimCodeFormProps) {
  const [state, action, pending] = useActionState(
    issueClaimAction,
    INITIAL_ISSUE_CLAIM_ACTION_STATE,
  );
  const [expired, setExpired] = useState(false);
  const submitLocked = useRef(false);
  const resultRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!pending) {
      submitLocked.current = false;
    }
    if (state.status === "success") {
      resultRef.current?.focus();
    }
  }, [pending, state.status]);

  useEffect(() => {
    if (state.status !== "success") return;
    const timer = window.setTimeout(() => setExpired(true), Math.max(0, Date.parse(state.expiresAt) - Date.now()));
    return () => window.clearTimeout(timer);
  }, [state]);

  function preventDuplicateSubmit(event: FormEvent<HTMLFormElement>) {
    if (pending || submitLocked.current) {
      event.preventDefault();
      return;
    }
    submitLocked.current = true;
  }

  if (state.status === "success") {
    const spokenCode = state.code.split("").join(" ");
    return (
      <div
        ref={resultRef}
        className="claim-result"
        role="status"
        aria-live="polite"
        tabIndex={-1}
      >
        <strong id="claim-code-label">{expired ? "CÓDIGO CADUCADO" : "CÓDIGO TEMPORAL"}</strong>
        {!expired ? <code className="claim-code" aria-label={`Código temporal: ${spokenCode}`}>
          {state.code}
        </code> : <p>Este código ya no puede vincular un collar. Genera otro cuando estés listo.</p>}
        <p>Vencimiento: <time dateTime={state.expiresAt}>{new Date(state.expiresAt).toLocaleTimeString("es-CO")}</time>.</p>
        <p>
          Anótalo ahora. Caduca en 15 minutos y desaparecerá si recargas o
          sales de esta pantalla. Si ya lo usaste, no vuelve a servir.
        </p>
        <a className="button-link" href={`/app/${dogId}/collars`}>Actualizar estado o generar otro código</a>
      </div>
    );
  }

  return (
    <form
      action={action}
      aria-busy={pending}
      className="claim-form"
      onSubmit={preventDuplicateSubmit}
    >
      <input name="dogId" type="hidden" value={dogId} />
      <p id="claim-code-help">
        Solo puede existir un código activo por perro. Genéralo cuando estés
        listo: no podremos volver a mostrarlo después de salir o recargar.
      </p>
      <label className="collar-confirmation-check">
        <input name="cloudConsent" type="checkbox" value="cloud-v1" required />
        <span>Autorizo sincronizar la ubicación y actividad de este collar con mi cuenta. He leído <a href="/privacy">el uso de datos</a>.</span>
      </label>
      {state.message ? (
        <p
          className="form-message form-message--error"
          role="alert"
          aria-live="polite"
        >
          {state.message}
        </p>
      ) : null}
      <button type="submit" disabled={pending} aria-describedby="claim-code-help">
        {pending ? "Generando…" : "Generar código"}
      </button>
    </form>
  );
}
