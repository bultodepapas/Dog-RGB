import {
  ExportDataError,
  getDogExport,
} from "../../../../../lib/data-access/export";
import {
  ExportDocumentError,
  serializeExport,
} from "../../../../../lib/data-access/export-core";
import {
  exportErrorResponse,
  isCanonicalUuid,
  privateDownloadHeaders,
} from "../../../../../lib/data-access/export-http";

export const dynamic = "force-dynamic";

type ExportRouteContext = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export async function GET(_request: Request, context: ExportRouteContext) {
  const { dogId } = await context.params;
  const retryHref = isCanonicalUuid(dogId) ? `/app/${dogId}/data/export` : "/login";
  if (!isCanonicalUuid(dogId)) {
    return exportErrorResponse("access_denied", dogId, retryHref);
  }

  try {
    const document = await getDogExport(dogId);
    const body = serializeExport(document);
    return new Response(body, {
      headers: privateDownloadHeaders(
        "application/json; charset=utf-8",
        `dog-rgb-${dogId}.json`,
      ),
    });
  } catch (error) {
    const code = error instanceof ExportDataError
      ? error.code
      : error instanceof ExportDocumentError
        ? "limit_exceeded"
        : "data_unavailable";
    return exportErrorResponse(code, dogId, retryHref);
  }
}
