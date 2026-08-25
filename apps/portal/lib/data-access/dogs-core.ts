import {
  createTodaySnapshotDto,
  dogLocalDate,
  TodayDataValidationError,
  type TodayCollarRecord,
  type TodayDailySummaryRecord,
  type TodayRecordingRecord,
  type TodayRecordingSummaryRecord,
  type TodaySnapshotDto,
} from "./today-core.ts";
import {
  createHistoryPageDto,
  createInvalidHistoryPageDto,
  HistoryDataValidationError,
  parseHistoryCursor,
  type HistoryCursor,
  type HistoryPageDto,
  type HistoryRecordingRecord,
} from "./history-core.ts";
import {
  createInvalidRecordingPageDto,
  createRecordingDetailContext,
  createRecordingPageDto,
  parseRecordingAfter,
  RecordingDataValidationError,
  type RecordingAfterResult,
  type RecordingDetailContext,
  type RecordingDetailRecord,
  type RecordingPageDto,
  type RecordingPointRecord,
} from "./recording-detail-core.ts";

// Pure, dependency-injected policy kernel for direct tests. This module never
// authenticates or queries data and is not an authorization boundary. Product
// code must import the server-only adapter in ./dogs.ts instead.
export const DOG_ROLES = ["owner", "editor", "viewer"] as const;

export type DogRole = (typeof DOG_ROLES)[number];
export type DogCapability = "read" | "write" | "admin";

export type DogAccessDto = Readonly<{
  dogId: string;
  role: DogRole;
}>;

export type DogSummaryDto = Readonly<{
  id: string;
  name: string;
  timezone: string;
  role: DogRole;
}>;

export type DogDataAccessErrorCode =
  | "authentication_required"
  | "access_denied"
  | "invalid_dog_id"
  | "data_unavailable";

export type DogMembershipRecord = Readonly<{
  dog_id: string;
  role: string;
}>;

export type DogRecord = Readonly<{
  id: string;
  name: string;
  timezone: string;
}>;

export type DogDataDependencies<Client> = Readonly<{
  createClient: () => Promise<Client>;
  getFreshUserId: (client: Client) => Promise<string | null>;
  findMembership: (
    client: Client,
    userId: string,
    dogId: string,
  ) => Promise<DogMembershipRecord | null>;
  listMemberships: (
    client: Client,
    userId: string,
  ) => Promise<DogMembershipRecord[]>;
  findDog: (client: Client, dogId: string) => Promise<DogRecord | null>;
  listDogs: (
    client: Client,
    dogIds: readonly string[],
  ) => Promise<DogRecord[]>;
  now: () => Date;
  findActiveCollar: (
    client: Client,
    dogId: string,
  ) => Promise<TodayCollarRecord | null>;
  findDailySummary: (
    client: Client,
    dogId: string,
    localDate: string,
  ) => Promise<TodayDailySummaryRecord | null>;
  findLatestRecording: (
    client: Client,
    collarId: string,
  ) => Promise<TodayRecordingRecord | null>;
  findRecordingSummary: (
    client: Client,
    recordingId: string,
  ) => Promise<TodayRecordingSummaryRecord | null>;
  listHistoryRecordings: (
    client: Client,
    dogId: string,
    cursor: HistoryCursor | null,
  ) => Promise<HistoryRecordingRecord[]>;
  findRecordingDetail: (
    client: Client,
    dogId: string,
    recordingId: string,
  ) => Promise<RecordingDetailRecord | null>;
  listRecordingPoints: (
    client: Client,
    query: NonNullable<RecordingDetailContext["pointQuery"]>,
    after: number | null,
  ) => Promise<RecordingPointRecord[]>;
}>;

const ERROR_MESSAGES = {
  authentication_required: "Authentication required.",
  access_denied: "Dog access denied.",
  invalid_dog_id: "Invalid dog identifier.",
  data_unavailable: "Dog data is unavailable.",
} as const satisfies Record<DogDataAccessErrorCode, string>;

export class DogDataAccessError extends Error {
  readonly code: DogDataAccessErrorCode;

  constructor(code: DogDataAccessErrorCode) {
    super(ERROR_MESSAGES[code]);
    this.name = "DogDataAccessError";
    this.code = code;
  }
}

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;

const CAPABILITY_ROLES: Readonly<
  Record<DogCapability, ReadonlySet<DogRole>>
> = {
  read: new Set(DOG_ROLES),
  write: new Set(["owner", "editor"]),
  admin: new Set(["owner"]),
};

function unavailable(): DogDataAccessError {
  return new DogDataAccessError("data_unavailable");
}

function assertDogId(dogId: string): void {
  if (!UUID_PATTERN.test(dogId)) {
    throw new DogDataAccessError("invalid_dog_id");
  }
}

function asDogRole(value: string): DogRole | null {
  return DOG_ROLES.find((role) => role === value) ?? null;
}

function membershipDto(row: DogMembershipRecord): DogAccessDto {
  const role = asDogRole(row.role);

  if (!UUID_PATTERN.test(row.dog_id) || !role) {
    throw unavailable();
  }

  return Object.freeze({ dogId: row.dog_id, role });
}

function dogSummaryDto(row: DogRecord, role: DogRole): DogSummaryDto {
  if (
    !UUID_PATTERN.test(row.id) ||
    typeof row.name !== "string" ||
    row.name.length === 0 ||
    typeof row.timezone !== "string" ||
    row.timezone.length === 0
  ) {
    throw unavailable();
  }

  return Object.freeze({
    id: row.id,
    name: row.name,
    timezone: row.timezone,
    role,
  });
}

function todaySnapshotDto(
  input: Parameters<typeof createTodaySnapshotDto>[0],
): TodaySnapshotDto {
  try {
    return createTodaySnapshotDto(input);
  } catch (error) {
    if (error instanceof TodayDataValidationError) {
      throw unavailable();
    }
    throw error;
  }
}

function historyPageDto(
  input: Parameters<typeof createHistoryPageDto>[0],
): HistoryPageDto {
  try {
    return createHistoryPageDto(input);
  } catch (error) {
    if (error instanceof HistoryDataValidationError) {
      throw unavailable();
    }
    throw error;
  }
}

function recordingContextDto(
  input: Parameters<typeof createRecordingDetailContext>[0],
): RecordingDetailContext {
  try {
    return createRecordingDetailContext(input);
  } catch (error) {
    if (error instanceof RecordingDataValidationError) throw unavailable();
    throw error;
  }
}

function recordingPageDto(
  input: Parameters<typeof createRecordingPageDto>[0],
): RecordingPageDto {
  try {
    return createRecordingPageDto(input);
  } catch (error) {
    if (error instanceof RecordingDataValidationError) throw unavailable();
    throw error;
  }
}

export function createDogDataAccess<Client>(
  dependencies: DogDataDependencies<Client>,
) {
  async function freshContext() {
    // The DAL object may live at module scope, but clients and identity do not:
    // every public call creates a request-scoped client and verifies freshness.
    const client = await dependencies.createClient();
    const userId = await dependencies.getFreshUserId(client);

    if (!userId) {
      throw new DogDataAccessError("authentication_required");
    }

    return { client, userId };
  }

  async function authorize(
    client: Client,
    userId: string,
    dogId: string,
    capability: DogCapability,
  ): Promise<DogAccessDto> {
    const row = await dependencies.findMembership(client, userId, dogId);

    if (!row) {
      throw new DogDataAccessError("access_denied");
    }

    const access = membershipDto(row);
    const allowedRoles = CAPABILITY_ROLES[capability];
    if (!allowedRoles || !allowedRoles.has(access.role)) {
      throw new DogDataAccessError("access_denied");
    }

    return access;
  }

  return Object.freeze({
    async requireDogAccess(
      dogId: string,
      capability: DogCapability,
    ): Promise<DogAccessDto> {
      assertDogId(dogId);
      const { client, userId } = await freshContext();
      return authorize(client, userId, dogId, capability);
    },

    async getDogSummary(
      dogId: string,
      capability: DogCapability = "read",
    ): Promise<DogSummaryDto> {
      assertDogId(dogId);
      const { client, userId } = await freshContext();
      const access = await authorize(client, userId, dogId, capability);
      const dog = await dependencies.findDog(client, dogId);

      if (!dog) {
        throw new DogDataAccessError("access_denied");
      }

      return dogSummaryDto(dog, access.role);
    },

    async getTodaySnapshot(dogId: string): Promise<TodaySnapshotDto> {
      assertDogId(dogId);
      const { client, userId } = await freshContext();
      const access = await authorize(client, userId, dogId, "read");
      const dogRow = await dependencies.findDog(client, dogId);

      if (!dogRow) {
        throw new DogDataAccessError("access_denied");
      }

      const dog = dogSummaryDto(dogRow, access.role);
      const capturedAt = dependencies.now();
      let localDate: string;
      try {
        localDate = dogLocalDate(capturedAt, dog.timezone);
      } catch (error) {
        if (error instanceof TodayDataValidationError) {
          throw unavailable();
        }
        throw error;
      }

      const [collar, dailySummary] = await Promise.all([
        dependencies.findActiveCollar(client, dogId),
        dependencies.findDailySummary(client, dogId, localDate),
      ]);
      const baseInput = {
        dog,
        capturedAt,
        localDate,
        collar,
        dailySummary,
        latestRecording: null,
        recordingSummary: null,
      } as const;
      const baseSnapshot = todaySnapshotDto(baseInput);

      if (!collar) {
        return baseSnapshot;
      }

      const latestRecording = await dependencies.findLatestRecording(
        client,
        collar.id,
      );
      if (!latestRecording) {
        return baseSnapshot;
      }

      const recordingSummary = await dependencies.findRecordingSummary(
        client,
        latestRecording.id,
      );
      return todaySnapshotDto({
        ...baseInput,
        latestRecording,
        recordingSummary,
      });
    },

    async getHistoryPage(
      dogId: string,
      cursorInput: unknown,
    ): Promise<HistoryPageDto> {
      assertDogId(dogId);
      const { client, userId } = await freshContext();
      const access = await authorize(client, userId, dogId, "read");
      const dogRow = await dependencies.findDog(client, dogId);

      if (!dogRow) {
        throw new DogDataAccessError("access_denied");
      }

      const dog = dogSummaryDto(dogRow, access.role);
      const capturedAt = dependencies.now();
      const parsedCursor = parseHistoryCursor(cursorInput, capturedAt);
      if (parsedCursor.status === "invalid") {
        return createInvalidHistoryPageDto(dog);
      }

      const rows = await dependencies.listHistoryRecordings(
        client,
        dogId,
        parsedCursor.cursor,
      );
      return historyPageDto({
        dog,
        capturedAt,
        cursor: parsedCursor.cursor,
        rows,
      });
    },

    async getRecordingPage(
      dogId: string,
      recordingId: string,
      afterInput: unknown,
    ): Promise<RecordingPageDto> {
      assertDogId(dogId);
      assertDogId(recordingId);
      const { client, userId } = await freshContext();
      const access = await authorize(client, userId, dogId, "read");
      const dogRow = await dependencies.findDog(client, dogId);
      if (!dogRow) throw new DogDataAccessError("access_denied");

      const dog = dogSummaryDto(dogRow, access.role);
      const capturedAt = dependencies.now();
      const row = await dependencies.findRecordingDetail(
        client,
        dogId,
        recordingId,
      );
      if (!row) throw new DogDataAccessError("access_denied");

      const context = recordingContextDto({ dog, capturedAt, row });
      const parsed = parseRecordingAfter(
        afterInput,
        context.pointQuery,
      ) satisfies RecordingAfterResult;
      if (parsed.status === "invalid") {
        return createInvalidRecordingPageDto(context);
      }
      const rows = context.pointQuery === null
        ? []
        : await dependencies.listRecordingPoints(
            client,
            context.pointQuery,
            parsed.after,
          );
      return recordingPageDto({
        context,
        capturedAt,
        after: parsed.after,
        rows,
      });
    },

    async listDogSummaries(): Promise<readonly DogSummaryDto[]> {
      const { client, userId } = await freshContext();
      const membershipRows = await dependencies.listMemberships(client, userId);
      const membershipByDog = new Map<string, DogRole>();

      membershipRows.forEach((row) => {
        const access = membershipDto(row);
        membershipByDog.set(access.dogId, access.role);
      });

      const dogs = await dependencies.listDogs(
        client,
        [...membershipByDog.keys()],
      );

      return Object.freeze(
        dogs.map((dog) => {
          const role = membershipByDog.get(dog.id);
          if (!role) {
            throw unavailable();
          }
          return dogSummaryDto(dog, role);
        }),
      );
    },
  });
}
