import "server-only";

import { notFound, redirect } from "next/navigation";

import {
  DogDataAccessError,
  getDogSummary,
  getTodaySnapshot,
  type DogSummaryDto,
  type TodaySnapshotDto,
} from "../data-access/dogs";
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
