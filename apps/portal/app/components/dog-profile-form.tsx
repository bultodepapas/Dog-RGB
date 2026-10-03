"use client";

import { useActionState, useEffect, useRef } from "react";
import { INITIAL_RENAME_STATE } from "../../lib/profile/rename";
import { renameDogAction } from "../app/[dogId]/configuration/profile-actions";

export function DogProfileForm({ dogId, name }: Readonly<{ dogId: string; name: string }>) {
  const [state, action, pending] = useActionState(renameDogAction, INITIAL_RENAME_STATE);
  const resultRef = useRef<HTMLParagraphElement>(null);
  useEffect(() => { if (state.status !== "idle") resultRef.current?.focus(); }, [state]);
  return <form action={action} className="auth-form" aria-busy={pending}>
    <input name="dogId" type="hidden" value={dogId} />
    <label htmlFor="dog-profile-name">Nombre del perro</label>
    <input id="dog-profile-name" name="name" defaultValue={name} maxLength={160} required autoComplete="off" />
    <button type="submit" disabled={pending}>{pending ? "GUARDANDO…" : "GUARDAR NOMBRE"}</button>
    {state.message ? <p ref={resultRef} tabIndex={-1} role={state.status === "error" ? "alert" : "status"}>{state.message}</p> : null}
  </form>;
}
