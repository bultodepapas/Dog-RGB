"use client";

import {
  type FormEvent,
  useActionState,
  useEffect,
  useRef,
} from "react";

import { INITIAL_ISSUE_CLAIM_ACTION_STATE } from "../../../../lib/claim-code/issue-claim";
import { issueClaimAction } from "./actions";

type ClaimCodeFormProps = Readonly<{ dogId: string }>;

export function ClaimCodeForm({ dogId }: ClaimCodeFormProps) {
  const [state, action, pending] = useActionState(
    issueClaimAction,
    INITIAL_ISSUE_CLAIM_ACTION_STATE,
  );
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
        <strong id="claim-code-label">CÓDIGO TEMPORAL</strong>
        <code className="claim-code" aria-label={`Código temporal: ${spokenCode}`}>
          {state.code}
        </code>
        <p>
          Anótalo ahora. Caduca en 15 minutos y desaparecerá si recargas o
          sales de esta pantalla.
        </p>
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
