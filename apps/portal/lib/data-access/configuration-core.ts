import { createHash } from "node:crypto";

import {
  DogDataAccessError,
  type DogMembershipRecord,
  type DogRecord,
  type DogRole,
  type DogSummaryDto,
} from "./dogs-core.ts";
import type { TodayCollarRecord } from "./today-core.ts";

export const BRIGHTNESS_RESOURCE_KEY = "brightness";
export const BRIGHTNESS_RESOURCE_SCHEMA = 1;
export const CONFIG_REPORT_STATUSES = [
  "applied",
  "rejected_unsupported",
  "rejected_invalid",
  "storage_failed",
] as const;

export type ConfigReportStatus = (typeof CONFIG_REPORT_STATUSES)[number];
export type ConfigurationTruth = "unknown" | "pending" | ConfigReportStatus;
export type ConfigurationFreshness = "never" | "recent" | "stale";

export type BrightnessHeadRecord = Readonly<{
  collar_id: string;
  resource_key: string;
  resource_schema: number;
  server_version: number;
  body: unknown;
  body_sha256: string;
  updated_at: string;
}>;

export type BrightnessReportRecord = Readonly<{
  collar_id: string;
  resource_key: string;
  reported_server_version: number;
  reported_body_sha256: string;
  status: string;
  error_code: string | null;
  firmware_version: string;
  config_schema: number;
  device_applied_at: string | null;
  cloud_received_at: string;
}>;

export type BrightnessConfigurationDto = Readonly<{
  dog: DogSummaryDto;
  capturedAt: string;
  canEdit: boolean;
  collar: Readonly<{
    id: string;
    name: string;
    lastSyncAt: string | null;
    freshness: ConfigurationFreshness;
  }> | null;
  desired: Readonly<{
    brightness: number;
    serverVersion: number;
    updatedAt: string;
  }> | null;
  report: Readonly<{
    status: ConfigReportStatus;
    reportedServerVersion: number;
    cloudReceivedAt: string;
    deviceAppliedAt: string | null;
    firmwareVersion: string;
    configSchema: number;
  }> | null;
  truth: ConfigurationTruth;
}>;

export type BrightnessMutationInput = Readonly<{
  dogId: string;
  collarId: string;
  brightness: number;
  mutationId: string;
  baseServerVersion: number;
  canonicalBody: string;
  bodySha256Hex: string;
  bodySha256Base64Url: string;
}>;

export type BrightnessRpcResult =
  | Readonly<{ ok: true; data: unknown }>
  | Readonly<{ ok: false; reason: "stale" | "conflict" | "ambiguous" }>;

export type ConfigurationMutationResult =
  | BrightnessRpcResult
  | Readonly<{ ok: false; reason: "selection_changed" }>;

export type ConfigurationDataDependencies<Client> = Readonly<{
  createClient(): Promise<Client>;
  getFreshUserId(client: Client): Promise<string | null>;
  findMembership(
    client: Client,
    userId: string,
    dogId: string,
  ): Promise<DogMembershipRecord | null>;
  findDog(client: Client, dogId: string): Promise<DogRecord | null>;
  findActiveCollar(
    client: Client,
    dogId: string,
  ): Promise<TodayCollarRecord | null>;
  findBrightnessHead(
    client: Client,
    collarId: string,
  ): Promise<BrightnessHeadRecord | null>;
  findBrightnessReport(
    client: Client,
    collarId: string,
  ): Promise<BrightnessReportRecord | null>;
  invokeBrightnessMutation(
    client: Client,
    input: BrightnessMutationInput,
  ): Promise<BrightnessRpcResult>;
  now(): Date;
}>;

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const UUID_V4_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const BYTEA_SHA256_PATTERN = /^\\x[0-9a-f]{64}$/u;
const BASE64URL_SHA256_PATTERN = /^[A-Za-z0-9_-]{43}$/u;
const RFC3339_PATTERN =
  /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/u;
const RECENT_WINDOW_MS = 24 * 60 * 60 * 1000;
const EDIT_ROLES = new Set<DogRole>(["owner", "editor"]);
const READ_ROLES = new Set<DogRole>(["owner", "editor", "viewer"]);

export class ConfigurationDataValidationError extends Error {
  constructor() {
    super("Configuration data is unavailable.");
    this.name = "ConfigurationDataValidationError";
  }
}

function invalid(): never {
  throw new ConfigurationDataValidationError();
}

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

function assertUuid(value: string): void {
  if (!UUID_PATTERN.test(value)) invalid();
}

function timestamp(value: string | null, capturedAtMs: number): string | null {
  if (value === null) return null;
  if (typeof value !== "string" || !RFC3339_PATTERN.test(value)) invalid();
  const milliseconds = Date.parse(value);
  if (!Number.isFinite(milliseconds) || milliseconds > capturedAtMs) invalid();
  return new Date(milliseconds).toISOString();
}

function positiveSafeInteger(value: number): void {
  if (!Number.isSafeInteger(value) || value <= 0) invalid();
}

function asRole(value: string): DogRole {
  if (value === "owner" || value === "editor" || value === "viewer") {
    return value;
  }
  return invalid();
}

function dogDto(row: DogRecord, role: DogRole): DogSummaryDto {
  assertUuid(row.id);
  if (
    typeof row.name !== "string" ||
    row.name.length === 0 ||
    typeof row.timezone !== "string" ||
    row.timezone.length === 0
  ) invalid();
  try {
    new Intl.DateTimeFormat("en", { timeZone: row.timezone }).format();
  } catch {
    invalid();
  }
  return Object.freeze({
    id: row.id,
    name: row.name,
    timezone: row.timezone,
    role,
  });
}

function brightnessFromBody(body: unknown): number {
  if (body === null || typeof body !== "object" || Array.isArray(body)) {
    return invalid();
  }
  const record = body as Record<string, unknown>;
  if (Object.keys(record).length !== 1) invalid();
  const brightness = record.brightness;
  if (
    typeof brightness !== "number" ||
    !Number.isInteger(brightness) ||
    brightness < 1 ||
    brightness > 255
  ) {
    return invalid();
  }
  return brightness;
}

function reportStatus(value: string): ConfigReportStatus {
  return CONFIG_REPORT_STATUSES.find((status) => status === value) ?? invalid();
}

export function canonicalBrightnessBody(brightness: number): string {
  if (!Number.isInteger(brightness) || brightness < 1 || brightness > 255) {
    return invalid();
  }
  return `{"brightness":${brightness}}`;
}

export function brightnessSha256(brightness: number): Readonly<{
  hex: string;
  base64url: string;
}> {
  const digest = createHash("sha256")
    .update(canonicalBrightnessBody(brightness), "utf8")
    .digest();
  return Object.freeze({
    hex: `\\x${digest.toString("hex")}`,
    base64url: digest.toString("base64url"),
  });
}

export function createBrightnessConfigurationDto(input: Readonly<{
  dog: DogSummaryDto;
  capturedAt: Date;
  collar: TodayCollarRecord | null;
  head: BrightnessHeadRecord | null;
  report: BrightnessReportRecord | null;
}>): BrightnessConfigurationDto {
  const capturedAtMs = input.capturedAt.getTime();
  if (!Number.isFinite(capturedAtMs)) invalid();
  assertUuid(input.dog.id);

  if (!input.collar) {
    if (input.head || input.report) invalid();
    return Object.freeze({
      dog: input.dog,
      capturedAt: input.capturedAt.toISOString(),
      canEdit: EDIT_ROLES.has(input.dog.role),
      collar: null,
      desired: null,
      report: null,
      truth: "unknown",
    });
  }

  assertUuid(input.collar.id);
  assertUuid(input.collar.dog_id);
  if (input.collar.dog_id !== input.dog.id || input.collar.state !== "active") {
    invalid();
  }
  timestamp(input.collar.linked_at, capturedAtMs);
  const lastSyncAt = timestamp(input.collar.last_sync_at, capturedAtMs);
  if (
    input.collar.display_name !== null &&
    (typeof input.collar.display_name !== "string" ||
      input.collar.display_name.trim().length === 0 ||
      input.collar.display_name.length > 80)
  ) invalid();
  const freshness: ConfigurationFreshness = lastSyncAt === null
    ? "never"
    : capturedAtMs - Date.parse(lastSyncAt) <= RECENT_WINDOW_MS
      ? "recent"
      : "stale";
  const collar = Object.freeze({
    id: input.collar.id,
    name: input.collar.display_name ?? "Collar sin nombre",
    lastSyncAt,
    freshness,
  });

  let desired: BrightnessConfigurationDto["desired"] = null;
  let desiredHash: string | null = null;
  if (input.head) {
    assertUuid(input.head.collar_id);
    if (
      input.head.collar_id !== input.collar.id ||
      input.head.resource_key !== BRIGHTNESS_RESOURCE_KEY ||
      input.head.resource_schema !== BRIGHTNESS_RESOURCE_SCHEMA ||
      !BYTEA_SHA256_PATTERN.test(input.head.body_sha256)
    ) invalid();
    positiveSafeInteger(input.head.server_version);
    const brightness = brightnessFromBody(input.head.body);
    const expectedHash = brightnessSha256(brightness).hex;
    if (input.head.body_sha256 !== expectedHash) invalid();
    desiredHash = expectedHash;
    const updatedAt = timestamp(input.head.updated_at, capturedAtMs);
    if (updatedAt === null) invalid();
    desired = Object.freeze({
      brightness,
      serverVersion: input.head.server_version,
      updatedAt,
    });
  }

  let report: BrightnessConfigurationDto["report"] = null;
  let exactReport = false;
  if (input.report) {
    assertUuid(input.report.collar_id);
    if (
      input.report.collar_id !== input.collar.id ||
      input.report.resource_key !== BRIGHTNESS_RESOURCE_KEY ||
      !BYTEA_SHA256_PATTERN.test(input.report.reported_body_sha256)
    ) invalid();
    positiveSafeInteger(input.report.reported_server_version);
    positiveSafeInteger(input.report.config_schema);
    const status = reportStatus(input.report.status);
    if (
      typeof input.report.firmware_version !== "string" ||
      input.report.firmware_version.length < 1 ||
      input.report.firmware_version.length > 64 ||
      (status === "applied" && input.report.error_code !== null) ||
      (status !== "applied" &&
        (typeof input.report.error_code !== "string" ||
          input.report.error_code.length < 1 ||
          input.report.error_code.length > 64))
    ) invalid();
    const cloudReceivedAt = timestamp(
      input.report.cloud_received_at,
      capturedAtMs,
    );
    if (cloudReceivedAt === null) invalid();
    const deviceAppliedAt = timestamp(
      input.report.device_applied_at,
      capturedAtMs,
    );
    report = Object.freeze({
      status,
      reportedServerVersion: input.report.reported_server_version,
      cloudReceivedAt,
      deviceAppliedAt,
      firmwareVersion: input.report.firmware_version,
      configSchema: input.report.config_schema,
    });
    exactReport = desired !== null &&
      input.report.reported_server_version === desired.serverVersion &&
      input.report.reported_body_sha256 === desiredHash;
  }

  const truth: ConfigurationTruth = desired === null
    ? "unknown"
    : exactReport && report
      ? report.status
      : "pending";

  return Object.freeze({
    dog: input.dog,
    capturedAt: input.capturedAt.toISOString(),
    canEdit: EDIT_ROLES.has(input.dog.role),
    collar,
    desired,
    report,
    truth,
  });
}

export function createConfigurationDataAccess<Client>(
  dependencies: ConfigurationDataDependencies<Client>,
) {
  async function freshContext() {
    const client = await dependencies.createClient();
    const userId = await dependencies.getFreshUserId(client);
    if (!userId) throw new DogDataAccessError("authentication_required");
    return { client, userId };
  }

  async function authorize(
    client: Client,
    userId: string,
    dogId: string,
    capability: "read" | "write",
  ): Promise<DogRole> {
    const row = await dependencies.findMembership(client, userId, dogId);
    if (!row || row.dog_id !== dogId) {
      throw new DogDataAccessError("access_denied");
    }
    let role: DogRole;
    try {
      role = asRole(row.role);
    } catch (error) {
      if (error instanceof ConfigurationDataValidationError) throw unavailable();
      throw error;
    }
    if (!(capability === "read" ? READ_ROLES : EDIT_ROLES).has(role)) {
      throw new DogDataAccessError("access_denied");
    }
    return role;
  }

  function validateDogId(dogId: string): void {
    if (!UUID_PATTERN.test(dogId)) {
      throw new DogDataAccessError("invalid_dog_id");
    }
  }

  return Object.freeze({
    async getBrightnessConfiguration(
      dogId: string,
    ): Promise<BrightnessConfigurationDto> {
      validateDogId(dogId);
      const { client, userId } = await freshContext();
      const role = await authorize(client, userId, dogId, "read");
      const dogRow = await dependencies.findDog(client, dogId);
      if (!dogRow) throw new DogDataAccessError("access_denied");
      let dog: DogSummaryDto;
      try {
        dog = dogDto(dogRow, role);
      } catch (error) {
        if (error instanceof ConfigurationDataValidationError) throw unavailable();
        throw error;
      }
      const collar = await dependencies.findActiveCollar(client, dogId);
      if (!collar) {
        return createBrightnessConfigurationDto({
          dog,
          capturedAt: dependencies.now(),
          collar: null,
          head: null,
          report: null,
        });
      }
      const [head, report] = await Promise.all([
        dependencies.findBrightnessHead(client, collar.id),
        dependencies.findBrightnessReport(client, collar.id),
      ]);
      try {
        return createBrightnessConfigurationDto({
          dog,
          capturedAt: dependencies.now(),
          collar,
          head,
          report,
        });
      } catch (error) {
        if (error instanceof ConfigurationDataValidationError) throw unavailable();
        throw error;
      }
    },

    async mutateBrightness(
      input: BrightnessMutationInput,
    ): Promise<ConfigurationMutationResult> {
      validateDogId(input.dogId);
      if (
        !UUID_PATTERN.test(input.collarId) ||
        !UUID_V4_PATTERN.test(input.mutationId) ||
        !Number.isSafeInteger(input.baseServerVersion) ||
        input.baseServerVersion < 0 ||
        input.canonicalBody !== canonicalBrightnessBody(input.brightness)
      ) throw unavailable();
      const expectedHash = brightnessSha256(input.brightness);
      if (
        input.bodySha256Hex !== expectedHash.hex ||
        input.bodySha256Base64Url !== expectedHash.base64url ||
        !BASE64URL_SHA256_PATTERN.test(input.bodySha256Base64Url)
      ) throw unavailable();

      const { client, userId } = await freshContext();
      await authorize(client, userId, input.dogId, "write");
      const collar = await dependencies.findActiveCollar(client, input.dogId);
      if (!collar || collar.id !== input.collarId) {
        return { ok: false, reason: "selection_changed" };
      }
      return dependencies.invokeBrightnessMutation(client, input);
    },
  });
}
