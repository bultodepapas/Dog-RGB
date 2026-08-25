import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireTodayPage } from "../../../../lib/auth/route-guard";
import { TodaySnapshot } from "../../../components/today-snapshot";

export const metadata: Metadata = { title: "Hoy | Dog RGB" };
export const dynamic = "force-dynamic";

type TodayPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function TodayPage(
  props: TodayPageProps,
) {
  const { dogId } = await props.params;
  const snapshot = await requireTodayPage(dogId, dogAppPath(dogId, "today"));
  return <TodaySnapshot snapshot={snapshot} />;
}
