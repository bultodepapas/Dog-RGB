"use client";

import {
  type FormEvent,
  useActionState,
  useEffect,
  useRef,
} from "react";

import { INITIAL_CREATE_DOG_ACTION_STATE } from "../../lib/onboarding/create-dog";
import { createDogAction } from "./actions";

export function CreateDogForm() {
  const [state, action, pending] = useActionState(
    createDogAction,
    INITIAL_CREATE_DOG_ACTION_STATE,
  );
  const submitLocked = useRef(false);
  const nameInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!pending) {
      submitLocked.current = false;
    }
    if (state.fieldErrors?.name) {
      nameInput.current?.focus();
    }
  }, [pending, state]);

  function preventDuplicateSubmit(event: FormEvent<HTMLFormElement>) {
    if (pending || submitLocked.current) {
      event.preventDefault();
      return;
    }
    submitLocked.current = true;
  }

  const nameError = state.fieldErrors?.name;
  const describedBy = nameError
    ? "dog-name-help dog-name-error"
    : "dog-name-help";

  return (
    <form
      action={action}
      aria-busy={pending}
      className="auth-form onboarding-form"
      onSubmit={preventDuplicateSubmit}
    >
      <label htmlFor="dog-name">Nombre de tu perro</label>
      <input
        ref={nameInput}
        id="dog-name"
        name="name"
        type="text"
        autoComplete="off"
        aria-describedby={describedBy}
        aria-invalid={nameError ? true : undefined}
        required
      />
      <span className="field-help" id="dog-name-help">
        Entre 1 y 80 caracteres. Usaremos America/Bogota y unidades métricas
        por ahora.
      </span>
      {nameError ? (
        <span className="field-error" id="dog-name-error" role="alert">
          {nameError}
        </span>
      ) : null}
      {state.message ? (
        <p
          className="form-message form-message--error"
          role="alert"
          aria-live="polite"
        >
          {state.message}
        </p>
      ) : null}
      <button type="submit" disabled={pending}>
        {pending ? "Creando perfil…" : "Crear perfil"}
      </button>
    </form>
  );
}
