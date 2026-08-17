import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Dog RGB",
  description: "Optional local-first cloud portal for the Dog RGB collar.",
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="es">
      <body>{children}</body>
    </html>
  );
}
