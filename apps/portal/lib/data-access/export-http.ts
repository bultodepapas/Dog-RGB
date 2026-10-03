import type { ExportDataErrorCode } from "./export";

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;

const ERROR_CONTENT = {
  authentication_required: {
    status: 401,
    title: "Inicia sesión para descargar estos datos.",
    detail: "Tu sesión ya no está disponible. Inicia sesión y vuelve a intentarlo.",
    primaryLabel: "Iniciar sesión",
  },
  access_denied: {
    status: 404,
    title: "No se pudo abrir esta descarga.",
    detail: "Comprueba que el perro exista y que tu cuenta tenga acceso.",
    primaryLabel: "Abrir el portal",
  },
  limit_exceeded: {
    status: 413,
    title: "La descarga supera el límite disponible.",
    detail: "No se entregó un archivo parcial. Descarga las grabaciones GeoJSON por separado desde su detalle.",
    primaryLabel: "Volver a los datos del perro",
  },
  data_unavailable: {
    status: 503,
    title: "La descarga no está disponible ahora.",
    detail: "No se entregó un archivo parcial. Inténtalo de nuevo en unos minutos.",
    primaryLabel: "Reintentar descarga",
  },
} as const satisfies Record<ExportDataErrorCode, Readonly<{
  status: number;
  title: string;
  detail: string;
  primaryLabel: string;
}>>;

export function exportErrorResponse(
  code: ExportDataErrorCode,
  dogId: string,
  retryHref: string,
): Response {
  const content = ERROR_CONTENT[code];
  const validDogId = UUID_PATTERN.test(dogId);
  const dataHref = validDogId ? `/app/${dogId}/data` : "/onboarding";
  const primaryHref = code === "authentication_required"
    ? "/login"
    : code === "access_denied"
      ? "/onboarding"
      : code === "limit_exceeded"
        ? dataHref
        : retryHref;
  const html = `<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Descarga | Dog RGB</title></head><body><main><h1>${content.title}</h1><p>${content.detail}</p><p><a href="${primaryHref}">${content.primaryLabel}</a></p>${code === "authentication_required" ? `<p><a href="${dataHref}">Volver a los datos del perro</a></p>` : ""}</main></body></html>`;

  return new Response(html, {
    status: content.status,
    headers: {
      "Cache-Control": "private, no-store",
      "Content-Type": "text/html; charset=utf-8",
      "Content-Security-Policy": "default-src 'none'; base-uri 'none'; frame-ancestors 'none'",
      "Referrer-Policy": "no-referrer",
      "X-Content-Type-Options": "nosniff",
    },
  });
}

export function isCanonicalUuid(value: string): boolean {
  return UUID_PATTERN.test(value);
}

export function privateDownloadHeaders(
  contentType: "application/json; charset=utf-8" | "application/geo+json; charset=utf-8",
  filename: string,
): Headers {
  const headers = new Headers({
    "Cache-Control": "private, no-store",
    "Content-Disposition": `attachment; filename="${filename}"`,
    "Content-Type": contentType,
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
  });
  return headers;
}
