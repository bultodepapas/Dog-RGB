"use client";

import Link from "next/link";

export default function AccountError({ reset }: { reset: () => void }) {
  return (
    <main className="system-state">
      <p className="eyebrow">DOG-RGB_ / ERROR SEGURO</p>
      <h1>No pudimos consultar tu cuenta.</h1>
      <p>Actualiza el estado antes de repetir una solicitud de borrado. Un fallo de conexión no confirma ni cancela una operación.</p>
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
