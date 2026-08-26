"use client";

import {
  type FormEvent,
  type KeyboardEvent,
  useActionState,
  useEffect,
  useRef,
  useState,
} from "react";

import {
  INITIAL_REVOKE_COLLAR_ACTION_STATE,
  REVOKE_CONFIRMATION_VALUE,
} from "../../lib/collars/revoke";
import { revokeCollarAction } from "../app/[dogId]/collars/actions";

type CollarRevokeFormProps = Readonly<{
  dogId: string;
  collarId: string;
  collarName: string;
  refreshHref: string;
}>;

export function CollarRevokeForm({
  dogId,
  collarId,
  collarName,
  refreshHref,
}: CollarRevokeFormProps) {
  const [expanded, setExpanded] = useState(false);
  const [state, action, pending] = useActionState(
    revokeCollarAction,
    INITIAL_REVOKE_COLLAR_ACTION_STATE,
  );
  const submitLocked = useRef(false);
  const reviewRef = useRef<HTMLButtonElement>(null);
  const headingRef = useRef<HTMLHeadingElement>(null);
  const resultRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!pending) submitLocked.current = false;
    if (state.status !== "idle") resultRef.current?.focus();
  }, [pending, state.status]);

  function openConfirmation() {
    setExpanded(true);
    requestAnimationFrame(() => headingRef.current?.focus());
  }

  function closeConfirmation() {
    setExpanded(false);
    requestAnimationFrame(() => reviewRef.current?.focus());
  }

  function handleConfirmationKeyDown(event: KeyboardEvent<HTMLDivElement>) {
    if (event.key === "Escape" && !pending) {
      event.preventDefault();
      closeConfirmation();
    }
  }

  function preventDuplicateSubmit(event: FormEvent<HTMLFormElement>) {
    if (pending || submitLocked.current) {
      event.preventDefault();
      return;
    }
    submitLocked.current = true;
  }

  if (state.status === "revoked" || state.status === "already_revoked") {
    return (
      <div
        ref={resultRef}
        aria-atomic="true"
        aria-live="polite"
        className="collar-result collar-result--success"
        role="status"
        tabIndex={-1}
      >
        <strong>
          {state.status === "already_revoked"
            ? "EL COLLAR YA ESTABA REVOCADO"
            : "COLLAR REVOCADO EN LA NUBE"}
        </strong>
        <p>
          Ya no puede sincronizar ni descargar configuración. Sus grabaciones
          históricas siguen disponibles. Los datos marcados como estado al
          cargar corresponden a la captura anterior a esta revocación.
        </p>
        <a className="button-link collar-refresh" href={refreshHref}>
          RECARGAR ESTADO
        </a>
      </div>
    );
  }

  return (
    <div className="collar-revoke">
      <button
        ref={reviewRef}
        aria-controls="revoke-confirmation"
        aria-expanded={expanded}
        className="collar-review-button"
        onClick={openConfirmation}
        type="button"
      >
        REVISAR REVOCACIÓN
      </button>
      {expanded ? (
        <div
          id="revoke-confirmation"
          className="collar-confirmation"
          onKeyDown={handleConfirmationKeyDown}
        >
          <h3 ref={headingRef} tabIndex={-1}>
            Revocar acceso de {collarName}
          </h3>
          <p>
            Este cambio impide que este collar vuelva a sincronizar con la
            nube. Las grabaciones históricas permanecen. La web actual no puede
            reactivar ni volver a vincular este mismo dispositivo.
          </p>
          <form
            action={action}
            aria-busy={pending}
            className="collar-revoke-form"
            onSubmit={preventDuplicateSubmit}
          >
            <input name="dogId" type="hidden" value={dogId} />
            <input name="collarId" type="hidden" value={collarId} />
            <label className="collar-confirmation-check">
              <input
                name="confirmation"
                required
                type="checkbox"
                value={REVOKE_CONFIRMATION_VALUE}
              />
              <span>
                Entiendo que el collar perderá el acceso a la nube y que esta
                web todavía no ofrece reactivación.
              </span>
            </label>
            {state.status === "error" ? (
              <div
                ref={resultRef}
                aria-atomic="true"
                aria-live="polite"
                className="collar-result collar-result--error"
                role="status"
                tabIndex={-1}
              >
                <strong>NO PUDIMOS CONFIRMAR LA REVOCACIÓN</strong>
                <p>{state.message}</p>
              </div>
            ) : null}
            <div className="collar-confirmation-actions">
              <button className="button-danger" disabled={pending} type="submit">
                {pending ? "REVOCANDO…" : "REVOCAR ACCESO EN LA NUBE"}
              </button>
              <button
                className="button-secondary"
                disabled={pending}
                onClick={closeConfirmation}
                type="button"
              >
                CANCELAR
              </button>
            </div>
          </form>
        </div>
      ) : null}
    </div>
  );
}
