import "server-only";

import { DogDataAccessError, getDogSummary } from "./dogs";
import {
  parseDogExportDocument,
  type DogExportDocument,
} from "./export-core";
import { createServerSupabaseClient } from "../supabase/server";

export type ExportDataErrorCode =
  | "authentication_required"
  | "access_denied"
  | "limit_exceeded"
  | "data_unavailable";

export class ExportDataError extends Error {
  readonly code: ExportDataErrorCode;

  constructor(code: ExportDataErrorCode) {
    super(code);
    this.name = "ExportDataError";
    this.code = code;
  }
}

function mapDogError(error: unknown): never {
  if (error instanceof DogDataAccessError) {
    if (error.code === "authentication_required") {
      throw new ExportDataError("authentication_required");
    }
    if (error.code === "access_denied" || error.code === "invalid_dog_id") {
      throw new ExportDataError("access_denied");
    }
  }
  throw new ExportDataError("data_unavailable");
}

export async function getDogExport(
  dogId: string,
  recordingId?: string,
): Promise<DogExportDocument> {
  try {
    // This uses a fresh Auth lookup and requires the owner capability. The RPC
    // repeats that check in the same snapshot as the exported rows.
    await getDogSummary(dogId, "admin");
  } catch (error) {
    mapDogError(error);
  }

  const client = await createServerSupabaseClient();
  const { data, error } = await client.rpc("export_dog_data_v1", {
    p_dog_id: dogId,
    ...(recordingId ? { p_recording_id: recordingId } : {}),
  }).abortSignal(AbortSignal.timeout(15_000));

  if (error) {
    if (error.code === "28000") throw new ExportDataError("authentication_required");
    if (error.code === "42501") throw new ExportDataError("access_denied");
    if (error.code === "54000" || error.message.includes("export_limit_exceeded")) {
      throw new ExportDataError("limit_exceeded");
    }
    throw new ExportDataError("data_unavailable");
  }

  try {
    return parseDogExportDocument(data, dogId, recordingId);
  } catch {
    throw new ExportDataError("data_unavailable");
  }
}
