import type { DogSummaryDto } from "./dogs-core.ts";

export const RECORDING_POINT_PAGE_SIZE = 100;
export const RECORDING_POINT_QUERY_LIMIT = RECORDING_POINT_PAGE_SIZE + 1;
export const RECORDING_AFTER_MAX_LENGTH = 10;
export const PLAIN_PREVIEW_TIME_GAP_SECONDS = 65;

export const RECORDING_STATES = [
  "open",
  "closed",
  "legacy",
  "incomplete",
] as const;

export const RECORDING_TIME_QUALITIES = [
  "unknown",
  "approximate_persisted",
  "server_anchored",
  "sntp_synced",
  "gnss_trusted",
  "legacy_minute",
] as const;

export const TELEMETRY_FLAGS = Object.freeze({
  FIX_VALID: 0x01,
  MOVEMENT_EVIDENCE: 0x02,
  TIME_TRUSTED: 0x04,
  STATIONARY_HEARTBEAT: 0x08,
  LOW_QUALITY: 0x10,
  GAP: 0x20,
  LEGACY_V2: 0x40,
} as const);

export const TELEMETRY_FLAG_LABELS = Object.freeze([
  [TELEMETRY_FLAGS.FIX_VALID, "POSICIÓN VÁLIDA"],
  [TELEMETRY_FLAGS.MOVEMENT_EVIDENCE, "EVIDENCIA DE MOVIMIENTO"],
  [TELEMETRY_FLAGS.TIME_TRUSTED, "HORA UTC UTILIZABLE"],
  [TELEMETRY_FLAGS.STATIONARY_HEARTBEAT, "OBSERVACIÓN ESTACIONARIA"],
  [TELEMETRY_FLAGS.LOW_QUALITY, "CALIDAD BAJA"],
  [TELEMETRY_FLAGS.GAP, "BRECHA DE COBERTURA"],
  [TELEMETRY_FLAGS.LEGACY_V2, "CONVERSIÓN LEGADA V2"],
] as const);

export type RecordingState = (typeof RECORDING_STATES)[number];
export type RecordingTimeQuality = (typeof RECORDING_TIME_QUALITIES)[number];

export type RecordingDetailRecord = Readonly<{
  id: string;
  collar_id: string;
  joined_collar_id: string;
  dog_id: string;
  collar_display_name: string | null;
  boot_sequence: number;
  started_at: string | null;
  ended_at: string | null;
  timezone_at_start: string;
  state: string;
  first_point_sequence: number | null;
  last_point_sequence: number | null;
  point_count: number;
  clock_quality: string;
  telemetry_schema: number;
  firmware_version: string;
}>;

export type RecordingPointRecord = Readonly<{
  point_sequence: number;
  recorded_at: string | null;
  lat_e7: number | null;
  lon_e7: number | null;
  reported_speed_cmps: number | null;
  satellites: number | null;
  flags: number;
  time_quality: string;
}>;

export type RecordingMetadataDto = Readonly<{
  id: string;
  collarName: string;
  startedAt: string | null;
  endedAt: string | null;
  timezoneAtStart: string;
  state: RecordingState;
  bootSequence: number;
  firstPointSequence: number | null;
  lastPointSequence: number | null;
  pointCount: number;
  clockQuality: RecordingTimeQuality;
  telemetrySchema: number;
  firmwareVersion: string;
}>;

export type PointContinuity =
  | "page_boundary"
  | "continues"
  | "after_explicit_gap"
  | "after_invalid_fix"
  | "sequence_discontinuity"
  | "time_discontinuity"
  | "time_gap"
  | "explicit_gap"
  | "invalid_fix";

export type RecordingPointDto = Readonly<{
  sequence: number;
  recordedAt: string | null;
  latE7: number | null;
  lonE7: number | null;
  speedCmps: number | null;
  satellites: number | null;
  flags: number;
  flagLabels: readonly string[];
  timeQuality: RecordingTimeQuality;
  segmentNumber: number | null;
  continuity: PointContinuity;
}>;

export type RecordingPreviewDto = Readonly<{
  drawablePointCount: number;
  segments: readonly Readonly<{
    number: number;
    points: readonly Readonly<{
      sequence: number;
      latE7: number;
      lonE7: number;
    }>[];
  }>[];
}>;

export type RecordingDetailContext = Readonly<{
  dog: DogSummaryDto;
  recording: RecordingMetadataDto;
  pointQuery: Readonly<{
    collarId: string;
    bootSequence: number;
    firstPointSequence: number;
    lastPointSequence: number;
  }> | null;
}>;

export type RecordingPageDto =
  | Readonly<{
      status: "invalid_after";
      dog: DogSummaryDto;
      recording: RecordingMetadataDto;
    }>
  | Readonly<{
      status: "ready";
      dog: DogSummaryDto;
      recording: RecordingMetadataDto;
      position: "first" | "after";
      points: readonly RecordingPointDto[];
      nextAfter: string | null;
      preview: RecordingPreviewDto | null;
      previewUnavailableReason: "no_drawable_points" | "antimeridian_ambiguous" | null;
    }>;

export type RecordingAfterResult =
  | Readonly<{ status: "invalid" }>
  | Readonly<{ status: "valid"; after: number | null }>;

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const RFC3339_PATTERN =
  /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(?:Z|[+-](\d{2}):(\d{2}))$/u;
const AFTER_PATTERN = /^(?:0|[1-9][0-9]{0,9})$/u;
const UINT32_MAX = 4_294_967_295;

export class RecordingDataValidationError extends Error {
  constructor() {
    super("Recording data is unavailable.");
    this.name = "RecordingDataValidationError";
  }
}

function unavailable(): never {
  throw new RecordingDataValidationError();
}

function canonicalUuid(value: unknown): value is string {
  return typeof value === "string" && UUID_PATTERN.test(value) &&
    value === value.toLowerCase();
}

function uint32(value: unknown): value is number {
  return Number.isSafeInteger(value) && Number(value) >= 0 && Number(value) <= UINT32_MAX;
}

function timestamp(value: unknown, capturedAtMs: number): string {
  if (typeof value !== "string") unavailable();
  const parts = RFC3339_PATTERN.exec(value);
  if (!parts) unavailable();
  const year = Number(parts[1]);
  const month = Number(parts[2]);
  const day = Number(parts[3]);
  const hour = Number(parts[4]);
  const minute = Number(parts[5]);
  const second = Number(parts[6]);
  const offsetHour = parts[7] === undefined ? 0 : Number(parts[7]);
  const offsetMinute = parts[8] === undefined ? 0 : Number(parts[8]);
  const leapYear = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const daysInMonth = [31, leapYear ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  if (
    year === 0 || month < 1 || month > 12 || day < 1 || day > daysInMonth[month - 1] ||
    hour > 23 || minute > 59 || second > 59 || offsetHour > 23 || offsetMinute > 59
  ) unavailable();
  const milliseconds = Date.parse(value);
  if (!Number.isFinite(milliseconds) || milliseconds > capturedAtMs) unavailable();
  return new Date(milliseconds).toISOString();
}

function timezone(value: unknown): string {
  if (typeof value !== "string" || value.length === 0 || value.length > 64) unavailable();
  try {
    new Intl.DateTimeFormat("en", { timeZone: value }).format(new Date(0));
  } catch {
    unavailable();
  }
  return value;
}

function frozenDog(dog: DogSummaryDto): DogSummaryDto {
  if (
    !canonicalUuid(dog.id) || typeof dog.name !== "string" || dog.name.length === 0 ||
    typeof dog.timezone !== "string" || dog.timezone.length === 0 ||
    !["owner", "editor", "viewer"].includes(dog.role)
  ) unavailable();
  return Object.freeze({ ...dog });
}

function enumValue<const Values extends readonly string[]>(
  values: Values,
  value: unknown,
): Values[number] {
  const match = values.find((candidate) => candidate === value);
  return match ?? unavailable();
}

export function createRecordingDetailContext(input: Readonly<{
  dog: DogSummaryDto;
  capturedAt: Date;
  row: RecordingDetailRecord;
}>): RecordingDetailContext {
  const capturedAtMs = input.capturedAt.getTime();
  if (!Number.isFinite(capturedAtMs)) unavailable();
  const dog = frozenDog(input.dog);
  const row = input.row;
  if (
    !canonicalUuid(row.id) || !canonicalUuid(row.collar_id) ||
    !canonicalUuid(row.joined_collar_id) || row.joined_collar_id !== row.collar_id ||
    !canonicalUuid(row.dog_id) || row.dog_id !== dog.id ||
    !uint32(row.boot_sequence) || !Number.isSafeInteger(row.point_count) || row.point_count < 0 ||
    !Number.isSafeInteger(row.telemetry_schema) || row.telemetry_schema <= 0 ||
    typeof row.firmware_version !== "string" || row.firmware_version.length === 0 || row.firmware_version.length > 64
  ) unavailable();
  if (
    row.collar_display_name !== null &&
    (typeof row.collar_display_name !== "string" || row.collar_display_name.trim().length === 0 ||
      row.collar_display_name.length > 80)
  ) unavailable();

  const first = row.first_point_sequence;
  const last = row.last_point_sequence;
  if ((first === null) !== (last === null)) unavailable();
  if (first !== null && (!uint32(first) || !uint32(last) || first > last)) unavailable();

  const startedAt = row.started_at === null ? null : timestamp(row.started_at, capturedAtMs);
  const endedAt = row.ended_at === null ? null : timestamp(row.ended_at, capturedAtMs);
  if (startedAt !== null && endedAt !== null && endedAt < startedAt) unavailable();
  const clockQuality = enumValue(RECORDING_TIME_QUALITIES, row.clock_quality);
  if (clockQuality === "unknown" && (startedAt !== null || endedAt !== null)) unavailable();

  const recording = Object.freeze({
    id: row.id,
    collarName: row.collar_display_name ?? "Collar sin nombre",
    startedAt,
    endedAt,
    timezoneAtStart: timezone(row.timezone_at_start),
    state: enumValue(RECORDING_STATES, row.state),
    bootSequence: row.boot_sequence,
    firstPointSequence: first,
    lastPointSequence: last,
    pointCount: row.point_count,
    clockQuality,
    telemetrySchema: row.telemetry_schema,
    firmwareVersion: row.firmware_version,
  } satisfies RecordingMetadataDto);

  const pointQuery = first === null
    ? null
    : Object.freeze({
        collarId: row.collar_id,
        bootSequence: row.boot_sequence,
        firstPointSequence: first,
        lastPointSequence: last as number,
      });
  return Object.freeze({ dog, recording, pointQuery });
}

export function parseRecordingAfter(
  input: unknown,
  pointQuery: RecordingDetailContext["pointQuery"],
): RecordingAfterResult {
  if (input === undefined || input === null) {
    return Object.freeze({ status: "valid", after: null });
  }
  if (
    typeof input !== "string" || input.length === 0 ||
    input.length > RECORDING_AFTER_MAX_LENGTH || !AFTER_PATTERN.test(input)
  ) return Object.freeze({ status: "invalid" });
  const after = Number(input);
  if (!uint32(after) || String(after) !== input || pointQuery === null ||
    after < pointQuery.firstPointSequence || after >= pointQuery.lastPointSequence) {
    return Object.freeze({ status: "invalid" });
  }
  return Object.freeze({ status: "valid", after });
}

export function createInvalidRecordingPageDto(
  context: RecordingDetailContext,
): RecordingPageDto {
  return Object.freeze({
    status: "invalid_after",
    dog: context.dog,
    recording: context.recording,
  });
}

function pointDto(row: RecordingPointRecord, capturedAtMs: number): Omit<RecordingPointDto, "segmentNumber" | "continuity"> {
  if (
    !uint32(row.point_sequence) || !Number.isInteger(row.flags) || row.flags < 0 || row.flags > 127 ||
    (row.reported_speed_cmps !== null &&
      (!Number.isInteger(row.reported_speed_cmps) || row.reported_speed_cmps < 0 || row.reported_speed_cmps > 65534)) ||
    (row.satellites !== null &&
      (!Number.isInteger(row.satellites) || row.satellites < 0 || row.satellites > 255))
  ) unavailable();
  if ((row.lat_e7 === null) !== (row.lon_e7 === null)) unavailable();
  if (row.lat_e7 !== null &&
    (!Number.isInteger(row.lat_e7) || row.lat_e7 < -900_000_000 || row.lat_e7 > 900_000_000)) unavailable();
  if (row.lon_e7 !== null &&
    (!Number.isInteger(row.lon_e7) || row.lon_e7 < -1_800_000_000 || row.lon_e7 > 1_800_000_000)) unavailable();

  const quality = enumValue(RECORDING_TIME_QUALITIES, row.time_quality);
  const recordedAt = row.recorded_at === null ? null : timestamp(row.recorded_at, capturedAtMs);
  const hasTime = recordedAt !== null;
  const fix = (row.flags & TELEMETRY_FLAGS.FIX_VALID) !== 0;
  const moving = (row.flags & TELEMETRY_FLAGS.MOVEMENT_EVIDENCE) !== 0;
  const trustedTime = (row.flags & TELEMETRY_FLAGS.TIME_TRUSTED) !== 0;
  const stationary = (row.flags & TELEMETRY_FLAGS.STATIONARY_HEARTBEAT) !== 0;
  const gap = (row.flags & TELEMETRY_FLAGS.GAP) !== 0;
  const hasCoordinates = row.lat_e7 !== null;
  if (
    (quality === "unknown") !== !hasTime || trustedTime !== hasTime || fix !== hasCoordinates ||
    (moving && stationary) || (gap && (fix || moving || stationary)) ||
    (!fix && row.reported_speed_cmps !== null)
  ) unavailable();

  const flagLabels = Object.freeze(
    TELEMETRY_FLAG_LABELS
      .filter(([mask]) => (row.flags & mask) !== 0)
      .map(([, label]) => label),
  );
  return Object.freeze({
    sequence: row.point_sequence,
    recordedAt,
    latE7: row.lat_e7,
    lonE7: row.lon_e7,
    speedCmps: row.reported_speed_cmps,
    satellites: row.satellites,
    flags: row.flags,
    flagLabels,
    timeQuality: quality,
  });
}

function segmentedPoints(
  points: readonly Omit<RecordingPointDto, "segmentNumber" | "continuity">[],
): readonly RecordingPointDto[] {
  let segmentNumber = 0;
  let previousDrawable: (typeof points)[number] | null = null;
  let pendingBreak: "after_explicit_gap" | "after_invalid_fix" | null = null;
  const result = points.map((point) => {
    const explicitGap = (point.flags & TELEMETRY_FLAGS.GAP) !== 0;
    const drawable = point.latE7 !== null && !explicitGap;
    if (!drawable) {
      pendingBreak = explicitGap ? "after_explicit_gap" : pendingBreak ?? "after_invalid_fix";
      previousDrawable = null;
      return Object.freeze({
        ...point,
        segmentNumber: null,
        continuity: explicitGap ? "explicit_gap" : "invalid_fix",
      } satisfies RecordingPointDto);
    }

    let continuity: PointContinuity = "continues";
    if (previousDrawable === null) {
      continuity = pendingBreak ?? "page_boundary";
    } else if (point.sequence !== previousDrawable.sequence + 1) {
      continuity = "sequence_discontinuity";
    } else {
      const previousHasTime = previousDrawable.recordedAt !== null;
      const currentHasTime = point.recordedAt !== null;
      if (previousHasTime !== currentHasTime) {
        continuity = "time_discontinuity";
      } else if (previousHasTime && currentHasTime) {
        const deltaSeconds =
          (Date.parse(point.recordedAt as string) - Date.parse(previousDrawable.recordedAt as string)) / 1000;
        if (deltaSeconds > PLAIN_PREVIEW_TIME_GAP_SECONDS) continuity = "time_gap";
      }
    }
    if (continuity !== "continues") segmentNumber += 1;
    pendingBreak = null;
    previousDrawable = point;
    return Object.freeze({ ...point, segmentNumber, continuity });
  });
  return Object.freeze(result);
}

function preview(points: readonly RecordingPointDto[]): Readonly<{
  preview: RecordingPreviewDto | null;
  previewUnavailableReason: "no_drawable_points" | "antimeridian_ambiguous" | null;
}> {
  const drawable = points.filter((point) => point.segmentNumber !== null) as readonly (RecordingPointDto & {
    latE7: number;
    lonE7: number;
    segmentNumber: number;
  })[];
  if (drawable.length === 0) {
    return Object.freeze({ preview: null, previewUnavailableReason: "no_drawable_points" });
  }
  const longitudes = drawable.map((point) => point.lonE7);
  if (Math.max(...longitudes) - Math.min(...longitudes) > 1_800_000_000) {
    return Object.freeze({ preview: null, previewUnavailableReason: "antimeridian_ambiguous" });
  }
  const bySegment = new Map<number, typeof drawable>();
  drawable.forEach((point) => {
    const pointsForSegment = bySegment.get(point.segmentNumber) ?? [];
    bySegment.set(point.segmentNumber, [...pointsForSegment, point]);
  });
  const segments = Object.freeze(
    [...bySegment.entries()].map(([number, segmentPoints]) =>
      Object.freeze({
        number,
        points: Object.freeze(segmentPoints.map((point) => Object.freeze({
          sequence: point.sequence,
          latE7: point.latE7,
          lonE7: point.lonE7,
        }))),
      }) satisfies RecordingPreviewDto["segments"][number]),
  );
  return Object.freeze({
    preview: Object.freeze({ drawablePointCount: drawable.length, segments }),
    previewUnavailableReason: null,
  });
}

export function createRecordingPageDto(input: Readonly<{
  context: RecordingDetailContext;
  capturedAt: Date;
  after: number | null;
  rows: readonly RecordingPointRecord[];
}>): RecordingPageDto {
  const capturedAtMs = input.capturedAt.getTime();
  if (!Number.isFinite(capturedAtMs) || input.rows.length > RECORDING_POINT_QUERY_LIMIT) unavailable();
  const query = input.context.pointQuery;
  if (query === null && input.rows.length > 0) unavailable();
  const validated = input.rows.map((row) => pointDto(row, capturedAtMs));
  validated.forEach((point, index) => {
    const previous = index > 0 ? validated[index - 1] : null;
    if (
      query === null || point.sequence < query.firstPointSequence || point.sequence > query.lastPointSequence ||
      (input.after !== null && point.sequence <= input.after) ||
      (previous !== null && point.sequence <= previous.sequence)
    ) unavailable();
    if (
      previous?.recordedAt !== null && previous?.recordedAt !== undefined && point.recordedAt !== null &&
      point.recordedAt < previous.recordedAt
    ) unavailable();
  });
  const visible = segmentedPoints(validated.slice(0, RECORDING_POINT_PAGE_SIZE));
  const previewResult = preview(visible);
  return Object.freeze({
    status: "ready",
    dog: input.context.dog,
    recording: input.context.recording,
    position: input.after === null ? "first" : "after",
    points: visible,
    nextAfter: validated.length === RECORDING_POINT_QUERY_LIMIT
      ? String(visible[RECORDING_POINT_PAGE_SIZE - 1].sequence)
      : null,
    preview: previewResult.preview,
    previewUnavailableReason: previewResult.previewUnavailableReason,
  });
}
