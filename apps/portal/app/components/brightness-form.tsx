"use client";

import { type FormEvent, useActionState, useEffect, useRef } from "react";

import {
  INITIAL_BRIGHTNESS_ACTION_STATE,
} from "../../lib/configuration/brightness-state";
import { saveBrightnessAction } from "../app/[dogId]/configuration/actions";

type BrightnessFormProps = Readonly<{
  dogId: string;
  collarId: string;
  mutationId: string;
  baseServerVersion: number;
  desiredBrightness: number | null;
  refreshHref: string;
}>;

export function BrightnessForm({
  dogId,
  collarId,
  mutationId,
  baseServerVersion,
  desiredBrightness,
  refreshHref,
}: BrightnessFormProps) {
  const [state, action, pending] = useActionState(
    saveBrightnessAction,
    INITIAL_BRIGHTNESS_ACTION_STATE,
  );
  const submitLocked = useRef(false);
  const inputRef = useRef<HTMLInputElement>(null);
  const resultRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!pending) submitLocked.current = false;
    if (state.status === "validation") inputRef.current?.focus();
    if (
      state.status === "saved" ||
      state.status === "unchanged" ||
      state.status === "stale" ||
      state.status === "ambiguous"
    ) resultRef.current?.focus();
  }, [pending, state.status]);

  function preventDuplicateSubmit(event: FormEvent<HTMLFormElement>) {
    if (pending || submitLocked.current) {
      event.preventDefault();
      return;
    }
    submitLocked.current = true;
  }

  const result = state.status === "idle" || state.status === "validation"
    ? null
    : (
      <div
        ref={resultRef}
        aria-atomic="true"
        aria-live="polite"
        className={`configuration-result configuration-result--${state.status}`}
        role="status"
        tabIndex={-1}
      >
        <strong>
          {state.status === "saved"
            ? "GUARDADO EN LA NUBE · ESPERANDO AL COLLAR"
            : state.status === "unchanged"
              ? "SIN CAMBIOS EN LA NUBE"
              : state.status === "stale"
                ? "CAMBIO SUPERADO · RECARGA NECESARIA"
                : "NO PUDIMOS CONFIRMAR EL RESULTADO"}
        </strong>
        <p>
          {state.status === "saved"
            ? `El brillo ${state.brightness} quedó guardado como versión ${state.serverVersion}. Esto todavía no confirma que esté aplicado.`
            : state.status === "unchanged"
              ? `El brillo ${state.brightness} ya era el valor deseado. No se creó una versión ganadora nueva.`
              : state.status === "stale"
                ? `${state.message} Tu intento fue ${state.attemptedBrightness || "un valor no disponible"}.`
                : `${state.message} El intento fue ${state.attemptedBrightness}.`}
        </p>
      </div>
    );

  if (state.status === "stale") {
    return (
      <div className="brightness-action-stack">
        {result}
        <label htmlFor="brightness-stale">Brillo intentado</label>
        <input
          id="brightness-stale"
          readOnly
          type="number"
          value={state.attemptedBrightness}
        />
        <a className="button-link configuration-refresh" href={refreshHref}>
          RECARGAR ESTADO
        </a>
      </div>
    );
  }

  if (state.status === "ambiguous") {
    return (
      <div className="brightness-action-stack">
        {result}
        <form
          action={action}
          aria-busy={pending}
          className="brightness-form"
          onSubmit={preventDuplicateSubmit}
        >
          <input name="dogId" type="hidden" value={state.retry.dogId} />
          <input name="collarId" type="hidden" value={state.retry.collarId} />
          <input name="mutationId" type="hidden" value={state.retry.mutationId} />
          <input
            name="baseServerVersion"
            type="hidden"
            value={state.retry.baseServerVersion}
          />
          <label htmlFor="brightness-retry">Brillo sin confirmar</label>
          <input
            id="brightness-retry"
            inputMode="numeric"
            max={255}
            min={1}
            name="brightness"
            readOnly
            required
            step={1}
            type="number"
            value={state.retry.brightness}
          />
          <button disabled={pending} type="submit">
            {pending ? "REINTENTANDO…" : "REINTENTAR EL MISMO VALOR"}
          </button>
        </form>
        <a className="button-link configuration-refresh" href={refreshHref}>
          RECARGAR ESTADO
        </a>
      </div>
    );
  }

  const inputValue = state.status === "validation"
    ? state.attemptedBrightness
    : desiredBrightness?.toString() ?? "";
  const errorId = state.status === "validation" ? "brightness-error" : undefined;
  const describedBy = ["brightness-help", errorId].filter(Boolean).join(" ");

  return (
    <div className="brightness-action-stack">
      {result}
      <form
        key={mutationId}
        action={action}
        aria-busy={pending}
        className="brightness-form"
        onSubmit={preventDuplicateSubmit}
      >
        <input name="dogId" type="hidden" value={dogId} />
        <input name="collarId" type="hidden" value={collarId} />
        <input name="mutationId" type="hidden" value={mutationId} />
        <input
          name="baseServerVersion"
          type="hidden"
          value={baseServerVersion}
        />
        <label htmlFor="brightness">Brillo deseado</label>
        <input
          ref={inputRef}
          aria-describedby={describedBy}
          aria-invalid={state.status === "validation" ? true : undefined}
          autoComplete="off"
          defaultValue={inputValue}
          id="brightness"
          inputMode="numeric"
          max={255}
          min={1}
          name="brightness"
          required
          step={1}
          type="number"
        />
        <p className="field-help" id="brightness-help">
          Escribe un número entero del 1 al 255, sin decimales ni ceros
          iniciales. 1 es el mínimo y 255 el máximo.
        </p>
        {state.status === "validation" ? (
          <p className="field-error" id="brightness-error" role="alert">
            {state.message}
          </p>
        ) : null}
        <button disabled={pending} type="submit">
          {pending ? "GUARDANDO…" : "GUARDAR BRILLO"}
        </button>
      </form>
    </div>
  );
}
