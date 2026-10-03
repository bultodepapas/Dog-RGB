export const DOG_EXPORT_MAX_BYTES = 16 * 1024 * 1024;
export const DOG_EXPORT_MAX_POINTS = 25_000;
export const DOG_EXPORT_MAX_ROWS = 50_000;

export type ExportPoint = Readonly<{
  recording_id: string | null;
  collar_id: string;
  boot_sequence: number;
  chunk_sequence: number;
  point_sequence: number;
  recorded_at: string | null;
  received_at: string;
  lat_e7: number | null;
  lon_e7: number | null;
  latitude: number | null;
  longitude: number | null;
  reported_speed_cmps: number | null;
  satellites: number | null;
  flags: number;
  time_quality: string;
  telemetry_schema: number;
  firmware_version: string;
}>;

export type ExportRecording = Readonly<{
  id: string;
  collar_id: string;
  boot_sequence: number;
  timezone_at_start: string;
  clock_quality: string;
}>;

export type DogExportDocument = Readonly<{
  schema_version: 1;
  complete: true;
  export_type: "dog_data" | "recording_geojson_source";
  snapshot_at: string;
  timezone: string;
  units: Readonly<Record<string, string>>;
  dog: Readonly<{ id: string; name: string; timezone: string }>;
  collars: readonly Readonly<Record<string, unknown>>[];
  recordings: readonly ExportRecording[];
  daily_summaries: readonly Readonly<Record<string, unknown>>[];
  recording_summaries: readonly Readonly<Record<string, unknown>>[];
  configuration: Readonly<{
    resource_heads: readonly Readonly<Record<string, unknown>>[];
    device_reported: readonly Readonly<Record<string, unknown>>[];
    revisions: readonly Readonly<Record<string, unknown>>[];
  }>;
  telemetry_points: readonly ExportPoint[];
  loss_markers: readonly Readonly<{
    recording_id: string | null;
    collar_id: string;
    boot_sequence: number;
    id: string;
    first_missing_point_sequence: number;
    last_missing_point_sequence: number;
    dropped_points: number;
    reason: string;
    recorded_at: string;
    recorded_utc_ms: number | null;
  }>[];
}>;

export class ExportDocumentError extends Error {
  constructor() {
    super("Export data is unavailable.");
    this.name = "ExportDocumentError";
  }
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function parseDogExportDocument(
  value: unknown,
  dogId: string,
  recordingId?: string,
): DogExportDocument {
  if (
    !record(value) || value.schema_version !== 1 || value.complete !== true ||
    value.export_type !== (recordingId ? "recording_geojson_source" : "dog_data") ||
    typeof value.snapshot_at !== "string" || typeof value.timezone !== "string" ||
    !record(value.dog) || value.dog.id !== dogId ||
    typeof value.dog.name !== "string" || value.dog.timezone !== value.timezone ||
    !record(value.units) || !Array.isArray(value.collars) || !Array.isArray(value.recordings) ||
    !Array.isArray(value.daily_summaries) || !Array.isArray(value.recording_summaries) ||
    !record(value.configuration) || !Array.isArray(value.configuration.resource_heads) ||
    !Array.isArray(value.configuration.device_reported) || !Array.isArray(value.configuration.revisions) ||
    !Array.isArray(value.telemetry_points) || !Array.isArray(value.loss_markers)
  ) {
    throw new ExportDocumentError();
  }

  const configuration = value.configuration as Readonly<{
    resource_heads: readonly unknown[];
    device_reported: readonly unknown[];
    revisions: readonly unknown[];
  }>;
  const rowCount = value.collars.length + value.recordings.length +
    value.daily_summaries.length + value.recording_summaries.length +
    configuration.resource_heads.length + configuration.device_reported.length +
    configuration.revisions.length + value.telemetry_points.length + value.loss_markers.length;
  if (value.telemetry_points.length > DOG_EXPORT_MAX_POINTS || rowCount > DOG_EXPORT_MAX_ROWS) {
    throw new ExportDocumentError();
  }

  if (recordingId && (
    value.recordings.length !== 1 || !record(value.recordings[0]) ||
    value.recordings[0].id !== recordingId ||
    value.telemetry_points.some((point) => !record(point) || point.recording_id !== recordingId) ||
    value.loss_markers.some((loss) => !record(loss) || loss.recording_id !== recordingId)
  )) {
    throw new ExportDocumentError();
  }

  return value as unknown as DogExportDocument;
}

export type GeoJsonFeature = Readonly<{
  type: "Feature";
  id?: string;
  geometry: Readonly<{
    type: "Point";
    coordinates: readonly [number, number];
  }> | Readonly<{
    type: "LineString";
    coordinates: readonly (readonly [number, number])[];
  }> | null;
  properties: Readonly<Record<string, unknown>>;
}>;

export type RecordingGeoJson = Readonly<{
  type: "FeatureCollection";
  complete: true;
  metadata: Readonly<Record<string, unknown>>;
  features: readonly GeoJsonFeature[];
}>;

function validFix(point: ExportPoint): boolean {
  return (point.flags & 0x01) !== 0 && (point.flags & 0x20) === 0 &&
    point.lat_e7 !== null && point.lon_e7 !== null &&
    point.latitude !== null && point.longitude !== null;
}

function assertPoint(point: ExportPoint): void {
  if (
    !Number.isInteger(point.point_sequence) || point.point_sequence < 0 ||
    !Number.isInteger(point.flags) || point.flags < 0 || point.flags > 127 ||
    ((point.lat_e7 === null) !== (point.lon_e7 === null)) ||
    ((point.latitude === null) !== (point.longitude === null)) ||
    (point.lat_e7 !== null && (!Number.isInteger(point.lat_e7) || point.lat_e7 < -900_000_000 || point.lat_e7 > 900_000_000)) ||
    (point.lon_e7 !== null && (!Number.isInteger(point.lon_e7) || point.lon_e7 < -1_800_000_000 || point.lon_e7 > 1_800_000_000)) ||
    (point.recorded_at !== null && !Number.isFinite(Date.parse(point.recorded_at)))
  ) {
    throw new ExportDocumentError();
  }
}

export function createRecordingGeoJson(
  document: DogExportDocument,
  recordingId: string,
): RecordingGeoJson {
  const recording = document.recordings.find((row) => row.id === recordingId);
  if (!recording || document.export_type !== "recording_geojson_source") {
    throw new ExportDocumentError();
  }

  const points = [...document.telemetry_points].sort((a, b) => a.point_sequence - b.point_sequence);
  const features: GeoJsonFeature[] = [];
  let segment: ExportPoint[] = [];
  let previous: ExportPoint | null = null;
  let segmentIndex = 0;

  const flushSegment = () => {
    if (segment.length > 1) {
      features.push({
        type: "Feature",
        id: `segment-${segmentIndex}`,
        geometry: {
          type: "LineString",
          coordinates: segment.map((point) => [point.longitude as number, point.latitude as number] as const),
        },
        properties: {
          kind: "track_segment",
          recording_id: recordingId,
          segment_index: segmentIndex,
          first_sequence: segment[0].point_sequence,
          last_sequence: segment[segment.length - 1].point_sequence,
          point_count: segment.length,
        },
      });
      segmentIndex += 1;
    }
    segment = [];
  };

  for (const point of points) {
    assertPoint(point);
    const drawable = validFix(point);
    const timeGap = previous !== null && (
      previous.recorded_at === null || point.recorded_at === null ||
      previous.time_quality !== point.time_quality ||
      previous.telemetry_schema !== point.telemetry_schema ||
      (previous.recorded_at !== null && point.recorded_at !== null && (
        (Date.parse(point.recorded_at) - Date.parse(previous.recorded_at)) / 1000 <= 0 ||
        (Date.parse(point.recorded_at) - Date.parse(previous.recorded_at)) / 1000 > 65
      ))
    );
    const breakBefore = previous !== null && (
      point.point_sequence !== previous.point_sequence + 1 || timeGap
    );
    if (!drawable || breakBefore) flushSegment();

    const gap = (point.flags & 0x20) !== 0;
    features.push({
      type: "Feature",
      id: `point-${point.point_sequence}`,
      geometry: drawable
        ? { type: "Point", coordinates: [point.longitude as number, point.latitude as number] }
        : null,
      properties: {
        kind: gap ? "explicit_gap" : drawable ? "telemetry_point" : "invalid_fix",
        collar_id: point.collar_id,
        boot_sequence: point.boot_sequence,
        chunk_sequence: point.chunk_sequence,
        point_sequence: point.point_sequence,
        recorded_at: point.recorded_at,
        received_at: point.received_at,
        lat_e7: point.lat_e7,
        lon_e7: point.lon_e7,
        reported_speed_cmps: point.reported_speed_cmps,
        satellites: point.satellites,
        flags: point.flags,
        time_quality: point.time_quality,
        telemetry_schema: point.telemetry_schema,
        firmware_version: point.firmware_version,
      },
    });

    if (drawable) {
      if (!breakBefore && segment.length > 0) segment.push(point);
      else segment = [point];
    }
    previous = drawable ? point : null;
  }
  flushSegment();

  for (const loss of document.loss_markers) {
    features.push({
      type: "Feature",
      id: `loss-${loss.id}`,
      geometry: null,
      properties: {
        kind: "telemetry_loss",
        collar_id: loss.collar_id,
        boot_sequence: loss.boot_sequence,
        first_missing_point_sequence: loss.first_missing_point_sequence,
        last_missing_point_sequence: loss.last_missing_point_sequence,
        dropped_points: loss.dropped_points,
        reason: loss.reason,
        recorded_at: loss.recorded_at,
        recorded_utc_ms: loss.recorded_utc_ms,
      },
    });
  }

  return {
    type: "FeatureCollection",
    complete: true,
    metadata: {
      schema_version: document.schema_version,
      snapshot_at: document.snapshot_at,
      dog_id: document.dog.id,
      dog_name: document.dog.name,
      timezone: recording.timezone_at_start,
      units: document.units,
      recording,
    },
    features,
  };
}

export function serializeExport(value: unknown): string {
  const serialized = JSON.stringify(value);
  if (new TextEncoder().encode(serialized).byteLength > DOG_EXPORT_MAX_BYTES) {
    throw new ExportDocumentError();
  }
  return serialized;
}
