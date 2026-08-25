import type { Metadata } from "next";
import { notFound } from "next/navigation";

import {
  isCanonicalUuid,
  recordingAppPath,
} from "../../../../../lib/auth/protected-route";
import { requireDogPage } from "../../../../../lib/auth/route-guard";
import { ProtectedRecordingPage } from "../../../../components/protected-dog-page";

export const metadata: Metadata = { title: "Grabación | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function RecordingPage(
  props: PageProps<"/app/[dogId]/recordings/[recordingId]">,
) {
  const { dogId, recordingId } = await props.params;
  if (!isCanonicalUuid(recordingId)) {
    notFound();
  }

  const dog = await requireDogPage(
    dogId,
    recordingAppPath(dogId, recordingId),
  );
  return <ProtectedRecordingPage dog={dog} />;
}
