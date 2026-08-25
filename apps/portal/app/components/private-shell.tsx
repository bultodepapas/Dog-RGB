import Link from "next/link";

import type { DogSummaryDto } from "../../lib/data-access/dogs";
import {
  dogAppPath,
  type DogAppSection,
} from "../../lib/auth/protected-route";
import { logoutAction } from "../auth/actions";

const ROLE_LABELS = {
  owner: "PROPIETARIO",
  editor: "EDITOR",
  viewer: "LECTOR",
} as const satisfies Record<DogSummaryDto["role"], string>;

const NAV_ITEMS = [
  { section: "today", label: "Hoy" },
  { section: "history", label: "Historial" },
  { section: "collars", label: "Collares" },
  { section: "configuration", label: "Configuración" },
] as const satisfies readonly {
  section: DogAppSection;
  label: string;
}[];

type DogPrivateShellProps = Readonly<{
  dog: DogSummaryDto;
  activeSection: DogAppSection;
  children: React.ReactNode;
}>;

export function DogPrivateShell({
  dog,
  activeSection,
  children,
}: DogPrivateShellProps) {
  return (
    <div className="private-page">
      <a className="skip-link" href="#private-content">
        Saltar al contenido
      </a>
      <header className="private-header">
        <div className="private-header__inner">
          <Link className="brand-mark private-brand" href="/onboarding">
            DOG-RGB_
          </Link>
          <div className="private-context" aria-label="Contexto del perro">
            <span className="eyebrow">EXTENSIÓN WEB / PRIVADA</span>
            <strong>{dog.name}</strong>
            <span>{ROLE_LABELS[dog.role]}</span>
          </div>
          <form action={logoutAction}>
            <button className="quiet-button" type="submit">
              CERRAR SESIÓN
            </button>
          </form>
        </div>
      </header>
      <div className="private-workspace">
        <nav className="private-nav" aria-label="Secciones del perro">
          <p className="eyebrow">NAVEGACIÓN</p>
          <ul>
            {NAV_ITEMS.map((item) => (
              <li key={item.section}>
                <Link
                  aria-current={
                    item.section === activeSection ? "page" : undefined
                  }
                  href={dogAppPath(dog.id, item.section)}
                >
                  {item.label}
                </Link>
              </li>
            ))}
          </ul>
          <p className="private-nav__meta">
            Zona horaria
            <strong>{dog.timezone}</strong>
          </p>
        </nav>
        <main className="private-content" id="private-content" tabIndex={-1}>
          {children}
        </main>
      </div>
      <footer className="private-footer">
        <span>COLLAR LOCAL-FIRST</span>
        <span>LA NUBE ES OPCIONAL</span>
      </footer>
    </div>
  );
}

type OnboardingPrivateShellProps = Readonly<{
  children: React.ReactNode;
}>;

export function OnboardingPrivateShell({
  children,
}: OnboardingPrivateShellProps) {
  return (
    <div className="private-page private-page--onboarding">
      <a className="skip-link" href="#private-content">
        Saltar al contenido
      </a>
      <header className="private-header">
        <div className="private-header__inner">
          <Link className="brand-mark private-brand" href="/">
            DOG-RGB_
          </Link>
          <div className="private-context">
            <span className="eyebrow">ÁREA PRIVADA</span>
            <strong>Sesión verificada</strong>
            <span>ENTORNO LOCAL</span>
          </div>
          <form action={logoutAction}>
            <button className="quiet-button" type="submit">
              CERRAR SESIÓN
            </button>
          </form>
        </div>
      </header>
      <main className="onboarding-content" id="private-content" tabIndex={-1}>
        {children}
      </main>
      <footer className="private-footer">
        <span>COLLAR LOCAL-FIRST</span>
        <span>PERFIL MÍNIMO PRIMERO</span>
      </footer>
    </div>
  );
}
