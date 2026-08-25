import Link from "next/link";

type AuthShellProps = Readonly<{
  eyebrow: string;
  title: string;
  description: string;
  children: React.ReactNode;
  footer?: React.ReactNode;
}>;

export function AuthShell({
  eyebrow,
  title,
  description,
  children,
  footer,
}: AuthShellProps) {
  return (
    <main className="auth-page">
      <section className="auth-shell" aria-labelledby="auth-title">
        <header className="auth-header">
          <Link className="brand-mark" href="/" aria-label="Dog RGB, inicio">
            DOG-RGB_
          </Link>
          <p className="eyebrow">{eyebrow}</p>
          <h1 id="auth-title">{title}</h1>
          <p className="auth-description">{description}</p>
        </header>
        {children}
        {footer ? <footer className="auth-footer">{footer}</footer> : null}
      </section>
    </main>
  );
}
