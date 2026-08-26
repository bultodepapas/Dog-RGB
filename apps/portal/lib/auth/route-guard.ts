import "server-only";

import { notFound, redirect } from "next/navigation";

import {
  DogDataAccessError,
  getDogSummary,
  getHistoryPage,
  getRecordingPage,
  getTodaySnapshot,
  type DogSummaryDto,
  type HistoryPageDto,
  type RecordingPageDto,
  type TodaySnapshotDto,
} from "../data-access/dogs";
import {
  getBrightnessConfiguration,
  type BrightnessConfigurationDto,
} from "../data-access/configuration";
import {
  getCollarPage,
  type CollarPageDto,
} from "../data-access/collars";
import {
  getFreshIdentity,
  type VerifiedIdentity,
} from "../supabase/identity";
import {
  classifyDogPageFailure,
  protectedLoginPath,
} from "./protected-route";

export async function requireFreshPageIdentity(
  returnTo: string,
): Promise<VerifiedIdentity> {
  const identity = await getFreshIdentity();

  if (!identity) {
    redirect(protectedLoginPath(returnTo));
  }

  return identity;
}

export async function requireDogPage(
  dogId: string,
  returnTo: string,
): Promise<DogSummaryDto> {
  return requireDogData(returnTo, () => getDogSummary(dogId, "read"));
}

export async function requireTodayPage(
  dogId: string,
  returnTo: string,
): Promise<TodaySnapshotDto> {
  return requireDogData(returnTo, () => getTodaySnapshot(dogId));
}

export async function requireConfigurationPage(
  dogId: string,
  returnTo: string,
): Promise<BrightnessConfigurationDto> {
  return requireDogData(returnTo, () => getBrightnessConfiguration(dogId));
}

export async function requireCollarsPage(
  dogId: string,
  returnTo: string,
): Promise<CollarPageDto> {
  return requireDogData(returnTo, () => getCollarPage(dogId));
}

export async function requireHistoryPage(
  dogId: string,
  cursor: unknown,
  returnTo: string,
): Promise<HistoryPageDto> {
  return requireDogData(returnTo, () => getHistoryPage(dogId, cursor));
}

export async function requireRecordingPage(
  dogId: string,
  recordingId: string,
  after: unknown,
  returnTo: string,
): Promise<RecordingPageDto> {
  return requireDogData(returnTo, () =>
    getRecordingPage(dogId, recordingId, after));
}

async function requireDogData<Result>(
  returnTo: string,
  read: () => Promise<Result>,
): Promise<Result> {
  try {
    return await read();
  } catch (error) {
    if (!(error instanceof DogDataAccessError)) {
      throw error;
    }

    const failure = classifyDogPageFailure(error.code);

    if (failure === "login") {
      redirect(protectedLoginPath(returnTo));
    }

    if (failure === "not-found") {
      notFound();
    }

    throw error;
  }
}
