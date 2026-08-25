"use client";

import Link from "next/link";

export default function OnboardingError({ reset }: { reset: () => void }) {
  return (
    <main className="system-state">
      <p className="eyebrow">DOG-RGB_ / ERROR SEGURO</p>
      <h1>No pudimos preparar el área privada.</h1>
      <p>No se mostró ni se modificó información. Puedes intentarlo de nuevo.</p>
      <div className="system-state__actions">
        <button type="button" onClick={reset}>
          REINTENTAR
        </button>
        <Link className="text-link" href="/">
          Volver al inicio
        </Link>
      </div>
    </main>
  );
}
