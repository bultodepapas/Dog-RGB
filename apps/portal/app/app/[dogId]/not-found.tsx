import Link from "next/link";

export default function DogAppNotFound() {
  return (
    <main className="system-state">
      <p className="eyebrow">DOG-RGB_ / ACCESO CERRADO</p>
      <h1>Este espacio no está disponible.</h1>
      <p>
        La dirección no existe o esta sesión no tiene acceso. No se confirma
        cuál de las dos condiciones ocurrió.
      </p>
      <Link className="button-link" href="/onboarding">
        VOLVER AL ÁREA PRIVADA
      </Link>
    </main>
  );
}
