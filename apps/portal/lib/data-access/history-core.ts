import type { DogSummaryDto } from "./dogs-core.ts";

export const HISTORY_PAGE_SIZE = 20;
export const HISTORY_QUERY_LIMIT = HISTORY_PAGE_SIZE + 1;
export const HISTORY_CURSOR_MAX_LENGTH = 256;

export const HISTORY_RECORDING_STATES = [
  "open",
  "closed",
  "legacy",
  "incomplete",
] as const;

export const HISTORY_CLOCK_QUALITIES = [
  "unknown",
  "approximate_persisted",
  "server_anchored",
  "sntp_synced",
  "gnss_trusted",
  "legacy_minute",
] as const;

export type HistoryRecordingState =
  (typeof HISTORY_RECORDING_STATES)[number];
export type HistoryClockQuality =
  (typeof HISTORY_CLOCK_QUALITIES)[number];
export type HistoryRecordingTimeQuality =
  | "unknown"
  | "approximate"
  | "trusted";

export type HistoryRecordingRecord = Readonly<{
  id: string;
  collar_id: string;
  joined_collar_id: string;
  dog_id: string;
  collar_display_name: string | null;
  started_at: string | null;
  state: string;
  point_count: number;
  clock_quality: string;
}>;

export type HistoryCursor =
  | Readonly<{ bucket: "known"; startedAt: string; id: string }>
  | Readonly<{ bucket: "unknown"; id: string }>;

export type HistoryCursorResult =
  | Readonly<{ status: "valid"; cursor: HistoryCursor | null }>
  | Readonly<{ status: "invalid" }>;

export type HistoryPageDto =
  | Readonly<{
      status: "invalid_cursor";
      dog: DogSummaryDto;
    }>
  | Readonly<{
      status: "ready";
      dog: DogSummaryDto;
      position: "first" | "after_cursor";
      recordings: readonly Readonly<{
        id: string;
        startedAt: string | null;
        timeQuality: HistoryRecordingTimeQuality;
        state: HistoryRecordingState;
        pointCount: number;
        collarName: string;
      }>[];
      nextCursor: string | null;
    }>;

type HistoryPageInput = Readonly<{
  dog: DogSummaryDto;
  capturedAt: Date;
  cursor: HistoryCursor | null;
  rows: readonly HistoryRecordingRecord[];
}>;

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const RFC3339_PATTERN =
  /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(?:Z|[+-](\d{2}):(\d{2}))$/u;
const BASE64URL_PATTERN = /^[A-Za-z0-9_-]+$/u;
const TRUSTED_CLOCK_QUALITIES = new Set<HistoryClockQuality>([
  "server_anchored",
  "sntp_synced",
  "gnss_trusted",
]);
const APPROXIMATE_CLOCK_QUALITIES = new Set<HistoryClockQuality>([
  "approximate_persisted",
  "legacy_minute",
]);

export class HistoryDataValidationError extends Error {
  constructor() {
    super("History data is unavailable.");
    this.name = "HistoryDataValidationError";
  }
}

function unavailable(): never {
  throw new HistoryDataValidationError();
}

function capturedMilliseconds(capturedAt: Date): number {
  const value = capturedAt.getTime();
  if (!Number.isFinite(value)) {
    unavailable();
  }
  return value;
}

function canonicalUuid(value: unknown): value is string {
  return (
    typeof value === "string" &&
    UUID_PATTERN.test(value) &&
    value === value.toLowerCase()
  );
}

function timestamp(value: unknown, capturedAtMs: number): string {
  if (typeof value !== "string") {
    unavailable();
  }
  const parts = RFC3339_PATTERN.exec(value);
  if (!parts) {
    unavailable();
  }

  const year = Number(parts[1]);
  const month = Number(parts[2]);
  const day = Number(parts[3]);
  const hour = Number(parts[4]);
  const minute = Number(parts[5]);
  const second = Number(parts[6]);
  const offsetHour = parts[7] === undefined ? 0 : Number(parts[7]);
  const offsetMinute = parts[8] === undefined ? 0 : Number(parts[8]);
  const leapYear = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const daysInMonth = [
    31,
    leapYear ? 29 : 28,
    31,
    30,
    31,
    30,
    31,
    31,
    30,
    31,
    30,
    31,
  ];
  if (
    year === 0 ||
    month < 1 ||
    month > 12 ||
    day < 1 ||
    day > daysInMonth[month - 1] ||
    hour > 23 ||
    minute > 59 ||
    second > 59 ||
    offsetHour > 23 ||
    offsetMinute > 59
  ) {
    unavailable();
  }

  const milliseconds = Date.parse(value);
  if (!Number.isFinite(milliseconds) || milliseconds > capturedAtMs) {
    unavailable();
  }
  return new Date(milliseconds).toISOString();
}

function exactKeys(
  value: Record<string, unknown>,
  expected: readonly string[],
): boolean {
  const actual = Object.keys(value).sort();
  return (
    actual.length === expected.length &&
    expected.every((key, index) => key === actual[index])
  );
}

function parsedObject(value: unknown): value is Record<string, unknown> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    Object.getPrototypeOf(value) === Object.prototype
  );
}

function decodeCursor(value: string, capturedAtMs: number): HistoryCursor {
  if (
    value.length === 0 ||
    value.length > HISTORY_CURSOR_MAX_LENGTH ||
    !BASE64URL_PATTERN.test(value)
  ) {
    unavailable();
  }

  let bytes: Buffer;
  let parsed: unknown;
  try {
    bytes = Buffer.from(value, "base64url");
    if (bytes.toString("base64url") !== value) {
      unavailable();
    }
    const json = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    parsed = JSON.parse(json);
  } catch (error) {
    if (error instanceof HistoryDataValidationError) {
      throw error;
    }
    unavailable();
  }

  if (!parsedObject(parsed) || parsed.v !== 1) {
    unavailable();
  }

  if (parsed.bucket === "known") {
    if (
      !exactKeys(parsed, ["bucket", "id", "startedAt", "v"]) ||
      !canonicalUuid(parsed.id)
    ) {
      unavailable();
    }
    const cursor = Object.freeze({
      bucket: "known",
      startedAt: timestamp(parsed.startedAt, capturedAtMs),
      id: parsed.id,
    } as const);
    if (encodeHistoryCursor(cursor) !== value) {
      unavailable();
    }
    return cursor;
  }

  if (
    parsed.bucket !== "unknown" ||
    !exactKeys(parsed, ["bucket", "id", "v"]) ||
    !canonicalUuid(parsed.id)
  ) {
    unavailable();
  }
  const cursor = Object.freeze({ bucket: "unknown", id: parsed.id } as const);
  if (encodeHistoryCursor(cursor) !== value) {
    unavailable();
  }
  return cursor;
}

export function parseHistoryCursor(
  input: unknown,
  capturedAt: Date,
): HistoryCursorResult {
  const capturedAtMs = capturedMilliseconds(capturedAt);
  if (input === undefined || input === null) {
    return Object.freeze({ status: "valid", cursor: null });
  }
  if (typeof input !== "string") {
    return Object.freeze({ status: "invalid" });
  }

  try {
    return Object.freeze({
      status: "valid",
      cursor: decodeCursor(input, capturedAtMs),
    });
  } catch (error) {
    if (error instanceof HistoryDataValidationError) {
      return Object.freeze({ status: "invalid" });
    }
    throw error;
  }
}

function encodeHistoryCursor(cursor: HistoryCursor): string {
  if (!canonicalUuid(cursor.id)) {
    unavailable();
  }
  if (
    cursor.bucket === "known" &&
    (!RFC3339_PATTERN.test(cursor.startedAt) ||
      new Date(cursor.startedAt).toISOString() !== cursor.startedAt)
  ) {
    unavailable();
  }
  const payload = cursor.bucket === "known"
    ? { v: 1, bucket: "known", startedAt: cursor.startedAt, id: cursor.id }
    : { v: 1, bucket: "unknown", id: cursor.id };
  const value = Buffer.from(JSON.stringify(payload), "utf8").toString(
    "base64url",
  );
  if (value.length > HISTORY_CURSOR_MAX_LENGTH) {
    unavailable();
  }
  return value;
}

function frozenDog(dog: DogSummaryDto): DogSummaryDto {
  if (
    !canonicalUuid(dog.id) ||
    typeof dog.name !== "string" ||
    dog.name.length === 0 ||
    typeof dog.timezone !== "string" ||
    dog.timezone.length === 0 ||
    !["owner", "editor", "viewer"].includes(dog.role)
  ) {
    unavailable();
  }
  return Object.freeze({ ...dog });
}

export function createInvalidHistoryPageDto(
  dog: DogSummaryDto,
): HistoryPageDto {
  return Object.freeze({ status: "invalid_cursor", dog: frozenDog(dog) });
}

function recordingState(value: string): HistoryRecordingState {
  return HISTORY_RECORDING_STATES.find((candidate) => candidate === value) ??
    unavailable();
}

function clockQuality(value: string): HistoryClockQuality {
  return HISTORY_CLOCK_QUALITIES.find((candidate) => candidate === value) ??
    unavailable();
}

function rowDto(
  row: HistoryRecordingRecord,
  dogId: string,
  capturedAtMs: number,
): Extract<HistoryPageDto, { status: "ready" }>["recordings"][number] {
  if (
    !canonicalUuid(row.id) ||
    !canonicalUuid(row.collar_id) ||
    !canonicalUuid(row.joined_collar_id) ||
    row.joined_collar_id !== row.collar_id ||
    !canonicalUuid(row.dog_id) ||
    row.dog_id !== dogId ||
    !Number.isSafeInteger(row.point_count) ||
    row.point_count < 0
  ) {
    unavailable();
  }

  if (
    row.collar_display_name !== null &&
    (typeof row.collar_display_name !== "string" ||
      row.collar_display_name.trim().length === 0 ||
      row.collar_display_name.length > 80)
  ) {
    unavailable();
  }

  const startedAt = row.started_at === null
    ? null
    : timestamp(row.started_at, capturedAtMs);
  const quality = clockQuality(row.clock_quality);
  if (quality === "unknown" && startedAt !== null) {
    unavailable();
  }
  const timeQuality: HistoryRecordingTimeQuality = startedAt === null || quality === "unknown"
    ? "unknown"
    : TRUSTED_CLOCK_QUALITIES.has(quality)
      ? "trusted"
      : APPROXIMATE_CLOCK_QUALITIES.has(quality)
        ? "approximate"
        : unavailable();

  return Object.freeze({
    id: row.id,
    startedAt,
    timeQuality,
    state: recordingState(row.state),
    pointCount: row.point_count,
    collarName: row.collar_display_name ?? "Collar sin nombre",
  });
}

function cursorFromRecording(
  recording: Extract<HistoryPageDto, { status: "ready" }>["recordings"][number],
): HistoryCursor {
  return recording.startedAt === null
    ? Object.freeze({ bucket: "unknown", id: recording.id })
    : Object.freeze({
        bucket: "known",
        startedAt: recording.startedAt,
        id: recording.id,
      });
}

function isStrictlyAfter(
  previous: Extract<HistoryPageDto, { status: "ready" }>["recordings"][number],
  current: Extract<HistoryPageDto, { status: "ready" }>["recordings"][number],
): boolean {
  if (previous.startedAt === null) {
    return current.startedAt === null && previous.id > current.id;
  }
  if (current.startedAt === null) {
    return true;
  }
  return (
    previous.startedAt > current.startedAt ||
    (previous.startedAt === current.startedAt && previous.id > current.id)
  );
}

function followsCursor(
  cursor: HistoryCursor,
  recording: Extract<HistoryPageDto, { status: "ready" }>["recordings"][number],
): boolean {
  if (cursor.bucket === "unknown") {
    return recording.startedAt === null && recording.id < cursor.id;
  }
  return (
    recording.startedAt === null ||
    recording.startedAt < cursor.startedAt ||
    (recording.startedAt === cursor.startedAt && recording.id < cursor.id)
  );
}

export function createHistoryPageDto(input: HistoryPageInput): HistoryPageDto {
  const capturedAtMs = capturedMilliseconds(input.capturedAt);
  const dog = frozenDog(input.dog);
  if (input.rows.length > HISTORY_QUERY_LIMIT) {
    unavailable();
  }

  const validated = input.rows.map((row) =>
    rowDto(row, dog.id, capturedAtMs),
  );
  validated.forEach((recording, index) => {
    if (index > 0 && !isStrictlyAfter(validated[index - 1], recording)) {
      unavailable();
    }
    if (input.cursor && !followsCursor(input.cursor, recording)) {
      unavailable();
    }
  });

  const recordings = Object.freeze(validated.slice(0, HISTORY_PAGE_SIZE));
  const nextCursor = validated.length === HISTORY_QUERY_LIMIT
    ? encodeHistoryCursor(cursorFromRecording(recordings[HISTORY_PAGE_SIZE - 1]))
    : null;

  return Object.freeze({
    status: "ready",
    dog,
    position: input.cursor === null ? "first" : "after_cursor",
    recordings,
    nextCursor,
  });
}
