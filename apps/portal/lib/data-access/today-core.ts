import type { DogSummaryDto } from "./dogs-core.ts";

export const TODAY_RECORDING_STATES = [
  "open",
  "closed",
  "legacy",
  "incomplete",
] as const;

export const TODAY_CLOCK_QUALITIES = [
  "unknown",
  "approximate_persisted",
  "server_anchored",
  "sntp_synced",
  "gnss_trusted",
  "legacy_minute",
] as const;

export type TodayRecordingState = (typeof TODAY_RECORDING_STATES)[number];
export type TodayClockQuality = (typeof TODAY_CLOCK_QUALITIES)[number];
export type TodayFreshness = "never" | "recent" | "stale";
export type TodayRecordingTimeQuality = "unknown" | "approximate" | "trusted";

export type TodayCollarRecord = Readonly<{
  id: string;
  dog_id: string;
  display_name: string | null;
  state: string;
  last_sync_at: string | null;
  linked_at: string | null;
}>;

export type TodayDailySummaryRecord = Readonly<{
  dog_id: string;
  local_date: string;
  timezone: string;
  coverage_ratio: number;
  unknown_s: number;
  algorithm_version: number;
  computed_at: string;
}>;

export type TodayRecordingRecord = Readonly<{
  id: string;
  collar_id: string;
  started_at: string | null;
  ended_at: string | null;
  created_at: string;
  state: string;
  point_count: number;
  clock_quality: string;
}>;

export type TodayRecordingSummaryRecord = Readonly<{
  recording_id: string;
  coverage_ratio: number;
  algorithm_version: number;
  computed_at: string;
}>;

export type TodaySnapshotDto = Readonly<{
  dog: DogSummaryDto;
  localDate: string;
  collar: Readonly<{
    name: string;
    lastSyncAt: string | null;
    freshness: TodayFreshness;
  }> | null;
  dailySummary: Readonly<{
    coverageRatio: number;
    unknownSeconds: number;
  }> | null;
  latestRecording: Readonly<{
    startedAt: string | null;
    timeQuality: TodayRecordingTimeQuality;
    state: TodayRecordingState;
    pointCount: number;
    coverageRatio: number | null;
  }> | null;
}>;

type TodaySnapshotInput = Readonly<{
  dog: DogSummaryDto;
  capturedAt: Date;
  localDate: string;
  collar: TodayCollarRecord | null;
  dailySummary: TodayDailySummaryRecord | null;
  latestRecording: TodayRecordingRecord | null;
  recordingSummary: TodayRecordingSummaryRecord | null;
}>;

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const DATE_PATTERN = /^\d{4}-\d{2}-\d{2}$/u;
const RFC3339_PATTERN =
  /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(?:Z|[+-](\d{2}):(\d{2}))$/u;
const RECENT_WINDOW_MS = 24 * 60 * 60 * 1000;
const TRUSTED_CLOCK_QUALITIES = new Set<TodayClockQuality>([
  "server_anchored",
  "sntp_synced",
  "gnss_trusted",
]);
const APPROXIMATE_CLOCK_QUALITIES = new Set<TodayClockQuality>([
  "approximate_persisted",
  "legacy_minute",
]);

export class TodayDataValidationError extends Error {
  constructor() {
    super("Today data is unavailable.");
    this.name = "TodayDataValidationError";
  }
}

function unavailable(): never {
  throw new TodayDataValidationError();
}

function capturedMilliseconds(capturedAt: Date): number {
  const value = capturedAt.getTime();
  if (!Number.isFinite(value)) {
    unavailable();
  }
  return value;
}

function timestamp(
  value: string | null,
  capturedAtMs: number,
): string | null {
  if (value === null) {
    return null;
  }
  const parts = typeof value === "string" ? RFC3339_PATTERN.exec(value) : null;
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

function uuid(value: string): void {
  if (!UUID_PATTERN.test(value)) {
    unavailable();
  }
}

function nonNegativeSafeInteger(value: number): void {
  if (!Number.isSafeInteger(value) || value < 0) {
    unavailable();
  }
}

function coverage(value: number): void {
  if (!Number.isFinite(value) || value < 0 || value > 1) {
    unavailable();
  }
}

function positiveSafeInteger(value: number): void {
  if (!Number.isSafeInteger(value) || value <= 0) {
    unavailable();
  }
}

function asRecordingState(value: string): TodayRecordingState {
  const state = TODAY_RECORDING_STATES.find((candidate) => candidate === value);
  return state ?? unavailable();
}

function asClockQuality(value: string): TodayClockQuality {
  const quality = TODAY_CLOCK_QUALITIES.find((candidate) => candidate === value);
  return quality ?? unavailable();
}

export function dogLocalDate(capturedAt: Date, timezone: string): string {
  capturedMilliseconds(capturedAt);
  if (typeof timezone !== "string" || timezone.length === 0) {
    unavailable();
  }

  let parts: Intl.DateTimeFormatPart[];
  try {
    parts = new Intl.DateTimeFormat("en-CA-u-ca-iso8601-nu-latn", {
      timeZone: timezone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
    }).formatToParts(capturedAt);
  } catch {
    unavailable();
  }

  const part = (type: Intl.DateTimeFormatPartTypes) =>
    parts.find((candidate) => candidate.type === type)?.value;
  const result = `${part("year")}-${part("month")}-${part("day")}`;
  if (!DATE_PATTERN.test(result)) {
    unavailable();
  }
  return result;
}

export function createTodaySnapshotDto(
  input: TodaySnapshotInput,
): TodaySnapshotDto {
  const capturedAtMs = capturedMilliseconds(input.capturedAt);
  uuid(input.dog.id);
  const expectedLocalDate = dogLocalDate(input.capturedAt, input.dog.timezone);
  if (input.localDate !== expectedLocalDate) {
    unavailable();
  }

  let collar: TodaySnapshotDto["collar"] = null;
  if (input.collar) {
    uuid(input.collar.id);
    uuid(input.collar.dog_id);
    if (input.collar.dog_id !== input.dog.id || input.collar.state !== "active") {
      unavailable();
    }
    timestamp(input.collar.linked_at, capturedAtMs);
    const lastSyncAt = timestamp(input.collar.last_sync_at, capturedAtMs);
    const displayName = input.collar.display_name;
    if (
      displayName !== null &&
      (typeof displayName !== "string" ||
        displayName.trim().length === 0 ||
        displayName.length > 80)
    ) {
      unavailable();
    }
    const freshness: TodayFreshness = lastSyncAt === null
      ? "never"
      : capturedAtMs - Date.parse(lastSyncAt) <= RECENT_WINDOW_MS
        ? "recent"
        : "stale";
    collar = Object.freeze({
      name: displayName ?? "Collar sin nombre",
      lastSyncAt,
      freshness,
    });
  }

  let dailySummary: TodaySnapshotDto["dailySummary"] = null;
  if (input.dailySummary) {
    uuid(input.dailySummary.dog_id);
    if (
      input.dailySummary.dog_id !== input.dog.id ||
      input.dailySummary.local_date !== input.localDate ||
      input.dailySummary.timezone !== input.dog.timezone
    ) {
      unavailable();
    }
    coverage(input.dailySummary.coverage_ratio);
    nonNegativeSafeInteger(input.dailySummary.unknown_s);
    positiveSafeInteger(input.dailySummary.algorithm_version);
    timestamp(input.dailySummary.computed_at, capturedAtMs);
    dailySummary = Object.freeze({
      coverageRatio: input.dailySummary.coverage_ratio,
      unknownSeconds: input.dailySummary.unknown_s,
    });
  }

  if (!input.collar && (input.latestRecording || input.recordingSummary)) {
    unavailable();
  }

  let latestRecording: TodaySnapshotDto["latestRecording"] = null;
  if (input.latestRecording && input.collar) {
    uuid(input.latestRecording.id);
    uuid(input.latestRecording.collar_id);
    if (input.latestRecording.collar_id !== input.collar.id) {
      unavailable();
    }
    const createdAt = timestamp(input.latestRecording.created_at, capturedAtMs);
    if (createdAt === null) {
      unavailable();
    }
    const startedAt = timestamp(input.latestRecording.started_at, capturedAtMs);
    const endedAt = timestamp(input.latestRecording.ended_at, capturedAtMs);
    if (
      startedAt !== null &&
      endedAt !== null &&
      Date.parse(endedAt) < Date.parse(startedAt)
    ) {
      unavailable();
    }
    const clockQuality = asClockQuality(input.latestRecording.clock_quality);
    if (
      clockQuality === "unknown" &&
      (startedAt !== null || endedAt !== null)
    ) {
      unavailable();
    }
    const timeQuality: TodayRecordingTimeQuality = startedAt === null || clockQuality === "unknown"
      ? "unknown"
      : TRUSTED_CLOCK_QUALITIES.has(clockQuality)
        ? "trusted"
        : APPROXIMATE_CLOCK_QUALITIES.has(clockQuality)
          ? "approximate"
          : unavailable();
    nonNegativeSafeInteger(input.latestRecording.point_count);

    let recordingCoverage: number | null = null;
    if (input.recordingSummary) {
      uuid(input.recordingSummary.recording_id);
      if (input.recordingSummary.recording_id !== input.latestRecording.id) {
        unavailable();
      }
      coverage(input.recordingSummary.coverage_ratio);
      positiveSafeInteger(input.recordingSummary.algorithm_version);
      timestamp(input.recordingSummary.computed_at, capturedAtMs);
      recordingCoverage = input.recordingSummary.coverage_ratio;
    }

    latestRecording = Object.freeze({
      startedAt,
      timeQuality,
      state: asRecordingState(input.latestRecording.state),
      pointCount: input.latestRecording.point_count,
      coverageRatio: recordingCoverage,
    });
  } else if (input.recordingSummary) {
    unavailable();
  }

  return Object.freeze({
    dog: input.dog,
    localDate: input.localDate,
    collar,
    dailySummary,
    latestRecording,
  });
}
