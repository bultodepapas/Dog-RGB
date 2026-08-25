import type { Metadata } from "next";

import {
  recordingAppPath,
} from "../../../../../lib/auth/protected-route";
import { requireRecordingPage } from "../../../../../lib/auth/route-guard";
import { RecordingDetail } from "../../../../components/recording-detail";

export const metadata: Metadata = { title: "Grabación | Dog RGB" };
export const dynamic = "force-dynamic";

type RecordingPageProps = Readonly<{
  params: Promise<{ dogId: string; recordingId: string }>;
  searchParams: Promise<{ after?: string | string[] }>;
}>;

export default async function RecordingPage(
  props: RecordingPageProps,
) {
  const [{ dogId, recordingId }, searchParams] = await Promise.all([
    props.params,
    props.searchParams,
  ]);
  const recording = await requireRecordingPage(
    dogId,
    recordingId,
    searchParams.after,
    recordingAppPath(dogId, recordingId),
  );
  return <RecordingDetail page={recording} />;
}
