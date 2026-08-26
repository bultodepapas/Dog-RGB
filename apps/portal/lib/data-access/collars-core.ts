import {
  DogDataAccessError,
  type DogMembershipRecord,
  type DogRecord,
  type DogRole,
  type DogSummaryDto,
} from "./dogs-core.ts";

export type CollarLifecycleState = "active" | "revoked";
export type CollarFreshness = "never" | "recent" | "stale";

export type CollarPageRecord = Readonly<{
  id: string;
  dog_id: string;
  display_name: string | null;
  state: string;
  hardware_revision: string | null;
  firmware_version: string | null;
  protocol_version: number | null;
  telemetry_schema: number | null;
  config_schema: number | null;
  capability_manifest: unknown;
  linked_at: string | null;
  last_sync_at: string | null;
  revoked_at: string | null;
  diagnostics_observed_at: string | null;
  outbox_chunks: number | null;
  outbox_points: number | null;
  outbox_used_bytes: number | null;
  outbox_capacity_bytes: number | null;
  oldest_unacknowledged_at: string | null;
  dropped_points_total: number | null;
  sync_error_present: boolean | null;
}>;

export type CollarIdentityRecord = Readonly<{
  id: string;
  dog_id: string;
  state: string;
  revoked_at: string | null;
}>;

export type CollarPageDto = Readonly<{
  dog: DogSummaryDto;
  capturedAt: string;
  canIssueClaim: boolean;
  canRevoke: boolean;
  collar: Readonly<{
    id: string;
    name: string;
    state: "active";
    linkedAt: string;
    lastSyncAt: string | null;
    revokedAt: string | null;
    freshness: CollarFreshness;
    compatibility: Readonly<{
      hardwareRevision: string;
      firmwareVersion: string;
      protocolVersion: number;
      telemetrySchema: number;
      configSchema: number;
      brightnessBidirectional: boolean;
      configurationReporting: boolean;
      telemetryLossMarkers: boolean;
      legacyV2Upload: boolean;
      resourceCount: number;
      effectCount: number;
      paletteCount: number;
      maxChunksPerSync: number;
      maxPointsPerSync: number;
    }>;
    diagnostics: Readonly<{
      observedAt: string;
      outboxChunks: number;
      outboxPoints: number;
      usedBytes: number;
      capacityBytes: number;
      oldestUnacknowledgedAt: string | null;
      droppedPointsTotal: number;
      errorReported: boolean;
      state: "empty" | "pending";
    }> | null;
  }> | null;
}>;

export type CollarRevokeInput = Readonly<{
  dogId: string;
  collarId: string;
}>;

export type CollarRevokeResult =
  | Readonly<{ ok: true; previousState: CollarLifecycleState }>
  | Readonly<{ ok: false; reason: "selection_changed" | "ambiguous" }>;

export type CollarDataDependencies<Client> = Readonly<{
  createClient(): Promise<Client>;
  getFreshUserId(client: Client): Promise<string | null>;
  findMembership(
    client: Client,
    userId: string,
    dogId: string,
  ): Promise<DogMembershipRecord | null>;
  findDog(client: Client, dogId: string): Promise<DogRecord | null>;
  findActiveCollar(client: Client, dogId: string): Promise<CollarPageRecord | null>;
  findCollarIdentity(
    client: Client,
    dogId: string,
    collarId: string,
  ): Promise<CollarIdentityRecord | null>;
  invokeRevoke(
    client: Client,
    collarId: string,
  ): Promise<Readonly<{ ok: true; data: unknown }> | Readonly<{ ok: false }>>;
  now(): Date;
}>;

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const RFC3339_PATTERN =
  /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/u;
const RESOURCE_KEY_PATTERN = /^[a-z][a-z0-9_]{0,31}$/u;
const CAPABILITY_KEY_PATTERN = /^[a-z][a-z0-9_]{0,31}$/u;
const UINT32_MAX = 4_294_967_295;
const RECENT_WINDOW_MS = 24 * 60 * 60 * 1_000;
const READ_ROLES = new Set<DogRole>(["owner", "editor", "viewer"]);

export class CollarDataValidationError extends Error {
  constructor() {
    super("Collar data is unavailable.");
    this.name = "CollarDataValidationError";
  }
}

function invalid(): never {
  throw new CollarDataValidationError();
}

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) invalid();
  return value as Record<string, unknown>;
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

function boundedString(value: unknown, maximum: number): string {
  if (typeof value !== "string" || value.length < 1 || value.length > maximum) invalid();
  return value;
}

function positiveInteger(value: unknown): number {
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value <= 0) invalid();
  return value;
}

function uint32(value: unknown): number {
  if (
    typeof value !== "number" ||
    !Number.isSafeInteger(value) ||
    value < 0 ||
    value > UINT32_MAX
  ) invalid();
  return value;
}

function unique(values: unknown[]): void {
  if (new Set(values).size !== values.length) invalid();
}

function includesInteger(values: unknown, expected: number): void {
  if (!Array.isArray(values) || values.length < 1 ||
      values.some((value) => typeof value !== "number" || !Number.isInteger(value)) ||
      !values.includes(expected)) invalid();
}

function compatibility(
  row: CollarPageRecord,
): NonNullable<NonNullable<CollarPageDto["collar"]>["compatibility"]> {
  const hardwareRevision = boundedString(row.hardware_revision, 64);
  const firmwareVersion = boundedString(row.firmware_version, 64);
  const protocolVersion = positiveInteger(row.protocol_version);
  const telemetrySchema = positiveInteger(row.telemetry_schema);
  const configSchema = positiveInteger(row.config_schema);
  const manifest = record(row.capability_manifest);
  if (manifest.manifest_schema !== 1 || manifest.hardware_revision !== hardwareRevision) invalid();
  includesInteger(manifest.protocol_versions, protocolVersion);
  includesInteger(manifest.config_schemas, configSchema);

  const telemetry = record(manifest.telemetry);
  includesInteger(telemetry.schemas, telemetrySchema);
  const resources = manifest.config_resources;
  if (!Array.isArray(resources) || resources.length < 1 || resources.length > 16) invalid();
  const resourceKeys: string[] = [];
  let brightnessBidirectional = false;
  for (const candidate of resources) {
    const resource = record(candidate);
    const key = boundedString(resource.resource_key, 32);
    if (!RESOURCE_KEY_PATTERN.test(key)) invalid();
    const schema = positiveInteger(resource.resource_schema);
    if (resource.sync_mode !== "bidirectional") invalid();
    resourceKeys.push(key);
    brightnessBidirectional ||= key === "brightness" && schema === 1;
  }
  unique(resourceKeys);

  const led = record(manifest.led);
  const effects = led.effects;
  const palettes = led.palettes;
  if (!Array.isArray(effects) || effects.length < 1 || effects.length > 64 ||
      !Array.isArray(palettes) || palettes.length > 64) invalid();
  const effectIds: number[] = [];
  const effectKeys: string[] = [];
  for (const candidate of effects) {
    const effect = record(candidate);
    const id = uint32(effect.id);
    if (id > 255) invalid();
    const key = boundedString(effect.key, 32);
    if (!CAPABILITY_KEY_PATTERN.test(key)) invalid();
    if (!(["calm", "active", "advanced"] as unknown[]).includes(effect.safety)) invalid();
    effectIds.push(id);
    effectKeys.push(key);
  }
  unique(effectIds);
  unique(effectKeys);
  const paletteIds: number[] = [];
  const paletteKeys: string[] = [];
  for (const candidate of palettes) {
    const palette = record(candidate);
    const id = uint32(palette.id);
    if (id > 255) invalid();
    const key = boundedString(palette.key, 32);
    if (!CAPABILITY_KEY_PATTERN.test(key)) invalid();
    paletteIds.push(id);
    paletteKeys.push(key);
  }
  unique(paletteIds);
  unique(paletteKeys);

  const limits = record(manifest.limits);
  const maxChunksPerSync = uint32(limits.max_chunks_per_sync);
  const maxPointsPerSync = uint32(limits.max_points_per_sync);
  if (maxChunksPerSync < 1 || maxChunksPerSync > 8 ||
      maxPointsPerSync < 1 || maxPointsPerSync > 384) invalid();
  const features = record(manifest.features);
  if (
    typeof features.configuration_reporting !== "boolean" ||
    typeof features.telemetry_loss_markers !== "boolean" ||
    typeof features.legacy_v2_upload !== "boolean"
  ) invalid();

  return Object.freeze({
    hardwareRevision,
    firmwareVersion,
    protocolVersion,
    telemetrySchema,
    configSchema,
    brightnessBidirectional,
    configurationReporting: features.configuration_reporting,
    telemetryLossMarkers: features.telemetry_loss_markers,
    legacyV2Upload: features.legacy_v2_upload,
    resourceCount: resources.length,
    effectCount: effects.length,
    paletteCount: palettes.length,
    maxChunksPerSync,
    maxPointsPerSync,
  });
}

function diagnostics(
  row: CollarPageRecord,
  capturedAtMs: number,
): NonNullable<NonNullable<CollarPageDto["collar"]>["diagnostics"]> | null {
  const values = [
    row.diagnostics_observed_at,
    row.outbox_chunks,
    row.outbox_points,
    row.outbox_used_bytes,
    row.outbox_capacity_bytes,
    row.oldest_unacknowledged_at,
    row.dropped_points_total,
    row.sync_error_present,
  ];
  if (values.every((value) => value === null)) return null;
  if (
    row.diagnostics_observed_at === null ||
    row.outbox_chunks === null ||
    row.outbox_points === null ||
    row.outbox_used_bytes === null ||
    row.outbox_capacity_bytes === null ||
    row.dropped_points_total === null ||
    typeof row.sync_error_present !== "boolean"
  ) invalid();
  const observedAt = timestamp(row.diagnostics_observed_at, capturedAtMs);
  const oldestUnacknowledgedAt = timestamp(row.oldest_unacknowledged_at, capturedAtMs);
  const outboxChunks = uint32(row.outbox_chunks);
  const outboxPoints = uint32(row.outbox_points);
  const usedBytes = uint32(row.outbox_used_bytes);
  const capacityBytes = uint32(row.outbox_capacity_bytes);
  const droppedPointsTotal = uint32(row.dropped_points_total);
  if (observedAt === null || usedBytes > capacityBytes) invalid();
  return Object.freeze({
    observedAt,
    outboxChunks,
    outboxPoints,
    usedBytes,
    capacityBytes,
    oldestUnacknowledgedAt,
    droppedPointsTotal,
    errorReported: row.sync_error_present,
    state: outboxChunks === 0 && outboxPoints === 0 && usedBytes === 0
      ? "empty"
      : "pending",
  });
}

function asRole(value: string): DogRole {
  if (value === "owner" || value === "editor" || value === "viewer") return value;
  return invalid();
}

function dogDto(row: DogRecord, role: DogRole): DogSummaryDto {
  assertUuid(row.id);
  const name = boundedString(row.name, 80);
  const timezone = boundedString(row.timezone, 64);
  try {
    new Intl.DateTimeFormat("en", { timeZone: timezone }).format();
  } catch {
    invalid();
  }
  return Object.freeze({ id: row.id, name, timezone, role });
}

export function createCollarPageDto(input: Readonly<{
  dog: DogSummaryDto;
  capturedAt: Date;
  collar: CollarPageRecord | null;
}>): CollarPageDto {
  const capturedAtMs = input.capturedAt.getTime();
  if (!Number.isFinite(capturedAtMs)) invalid();
  assertUuid(input.dog.id);
  const canIssueClaim = input.dog.role === "owner" || input.dog.role === "editor";
  if (!input.collar) {
    return Object.freeze({
      dog: input.dog,
      capturedAt: input.capturedAt.toISOString(),
      canIssueClaim,
      canRevoke: false,
      collar: null,
    });
  }

  const row = input.collar;
  assertUuid(row.id);
  assertUuid(row.dog_id);
  if (row.dog_id !== input.dog.id || row.state !== "active") invalid();
  const state = "active" as const;
  const linkedAt = timestamp(row.linked_at, capturedAtMs);
  const lastSyncAt = timestamp(row.last_sync_at, capturedAtMs);
  const revokedAt = timestamp(row.revoked_at, capturedAtMs);
  if (linkedAt === null || revokedAt !== null) invalid();
  if (row.display_name !== null &&
      (row.display_name.trim().length === 0 || row.display_name.length > 80)) invalid();
  const freshness: CollarFreshness = lastSyncAt === null
    ? "never"
    : capturedAtMs - Date.parse(lastSyncAt) <= RECENT_WINDOW_MS
      ? "recent"
      : "stale";
  const diagnosticSnapshot = diagnostics(row, capturedAtMs);
  if (diagnosticSnapshot && diagnosticSnapshot.observedAt !== lastSyncAt) invalid();

  const collar = Object.freeze({
    id: row.id,
    name: row.display_name ?? "Collar sin nombre",
    state,
    linkedAt,
    lastSyncAt,
    revokedAt,
    freshness,
    compatibility: compatibility(row),
    diagnostics: diagnosticSnapshot,
  });
  return Object.freeze({
    dog: input.dog,
    capturedAt: input.capturedAt.toISOString(),
    canIssueClaim,
    canRevoke: input.dog.role === "owner",
    collar,
  });
}

export function createCollarDataAccess<Client>(
  dependencies: CollarDataDependencies<Client>,
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
    capability: "read" | "admin",
  ): Promise<DogRole> {
    const membership = await dependencies.findMembership(client, userId, dogId);
    if (!membership || membership.dog_id !== dogId) {
      throw new DogDataAccessError("access_denied");
    }
    let role: DogRole;
    try {
      role = asRole(membership.role);
    } catch (error) {
      if (error instanceof CollarDataValidationError) throw unavailable();
      throw error;
    }
    if (capability === "admin" ? role !== "owner" : !READ_ROLES.has(role)) {
      throw new DogDataAccessError("access_denied");
    }
    return role;
  }

  function validateId(value: string): void {
    if (!UUID_PATTERN.test(value)) throw new DogDataAccessError("invalid_dog_id");
  }

  return Object.freeze({
    async getCollarPage(dogId: string): Promise<CollarPageDto> {
      validateId(dogId);
      const { client, userId } = await freshContext();
      const role = await authorize(client, userId, dogId, "read");
      const dogRow = await dependencies.findDog(client, dogId);
      if (!dogRow) throw new DogDataAccessError("access_denied");
      let dog: DogSummaryDto;
      try {
        dog = dogDto(dogRow, role);
      } catch (error) {
        if (error instanceof CollarDataValidationError) throw unavailable();
        throw error;
      }
      const selected = await dependencies.findActiveCollar(client, dogId);
      try {
        return createCollarPageDto({
          dog,
          capturedAt: dependencies.now(),
          collar: selected,
        });
      } catch (error) {
        if (error instanceof CollarDataValidationError) throw unavailable();
        throw error;
      }
    },

    async revokeCollar(input: CollarRevokeInput): Promise<CollarRevokeResult> {
      validateId(input.dogId);
      if (!UUID_PATTERN.test(input.collarId)) throw unavailable();
      const { client, userId } = await freshContext();
      await authorize(client, userId, input.dogId, "admin");
      const active = await dependencies.findActiveCollar(client, input.dogId);
      let previousState: CollarLifecycleState;
      if (active?.id === input.collarId) {
        previousState = "active";
      } else {
        const exact = await dependencies.findCollarIdentity(
          client,
          input.dogId,
          input.collarId,
        );
        if (!exact || exact.dog_id !== input.dogId || exact.state !== "revoked") {
          return { ok: false, reason: "selection_changed" };
        }
        previousState = "revoked";
      }
      const invoked = await dependencies.invokeRevoke(client, input.collarId);
      if (!invoked.ok || invoked.data !== true) return { ok: false, reason: "ambiguous" };
      const confirmed = await dependencies.findCollarIdentity(
        client,
        input.dogId,
        input.collarId,
      );
      if (!confirmed || confirmed.dog_id !== input.dogId ||
          confirmed.state !== "revoked" || confirmed.revoked_at === null) {
        return { ok: false, reason: "ambiguous" };
      }
      return { ok: true, previousState };
    },
  });
}
