import {
  ExportDataError,
  getDogExport,
} from "../../../../../../lib/data-access/export";
import {
  createRecordingGeoJson,
  ExportDocumentError,
  serializeExport,
} from "../../../../../../lib/data-access/export-core";
import {
  exportErrorResponse,
  isCanonicalUuid,
  privateDownloadHeaders,
} from "../../../../../../lib/data-access/export-http";

export const dynamic = "force-dynamic";

type RecordingExportRouteContext = Readonly<{
  params: Promise<{ dogId: string; recordingId: string }>;
}>;

export async function GET(_request: Request, context: RecordingExportRouteContext) {
  const { dogId, recordingId } = await context.params;
  const validPath = isCanonicalUuid(dogId) && isCanonicalUuid(recordingId);
  const retryHref = validPath
    ? `/app/${dogId}/recordings/${recordingId}/geojson`
    : "/login";
  if (!validPath) {
    return exportErrorResponse("access_denied", dogId, retryHref);
  }

  try {
    const document = await getDogExport(dogId, recordingId);
    const geoJson = createRecordingGeoJson(document, recordingId);
    const body = serializeExport(geoJson);
    return new Response(body, {
      headers: privateDownloadHeaders(
        "application/geo+json; charset=utf-8",
        `recording-${recordingId}.geojson`,
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
