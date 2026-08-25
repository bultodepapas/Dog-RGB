"use client";

import Link from "next/link";

export default function DogAppError({ reset }: { reset: () => void }) {
  return (
    <main className="system-state">
      <p className="eyebrow">DOG-RGB_ / ERROR SEGURO</p>
      <h1>No pudimos cargar el área privada.</h1>
      <p>No se mostró información parcial. Puedes volver a comprobar el acceso.</p>
      <div className="system-state__actions">
        <button type="button" onClick={reset}>
          REINTENTAR
        </button>
        <Link className="text-link" href="/onboarding">
          Volver al área privada
        </Link>
      </div>
    </main>
  );
}
