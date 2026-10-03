"use client";

import { useActionState, useEffect, useRef } from "react";
import { INITIAL_DELETION_STATE } from "../../lib/privacy/deletion";
import { deleteDogAction } from "../app/[dogId]/data/actions";

export function DogDeletionForm({ dogId, requestId }: Readonly<{ dogId: string; requestId: string }>) {
  const [state, action, pending] = useActionState(deleteDogAction, INITIAL_DELETION_STATE);
  const result = useRef<HTMLParagraphElement>(null);
  useEffect(() => { if (state.status === "error") result.current?.focus(); }, [state]);
  return <form action={action} aria-busy={pending} className="data-form">
    <input type="hidden" name="dogId" value={dogId} />
    <input type="hidden" name="requestId" value={requestId} />
    <p id="delete-impact">Elimina este perro, sus collares y su historial para todos sus miembros. El acceso y las nuevas cargas se bloquean al aceptar la solicitud. El borrado de datos se procesa después y no se puede deshacer.</p>
    <label htmlFor="delete-password">Contraseña actual</label>
    <input id="delete-password" name="password" type="password" autoComplete="current-password" maxLength={128} required />
    <label htmlFor="delete-confirmation">Escribe ELIMINAR para confirmar</label>
    <input id="delete-confirmation" name="confirmation" autoComplete="off" spellCheck={false} required pattern="ELIMINAR" aria-describedby="delete-impact" />
    {state.message ? <p ref={result} role="alert" tabIndex={-1}>{state.message}</p> : null}
    <button className="button-danger" disabled={pending} type="submit">{pending ? "Confirmando…" : "Eliminar perro y datos"}</button>
  </form>;
}
