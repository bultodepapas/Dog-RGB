import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireDogPage } from "../../../../lib/auth/route-guard";
import { ProtectedDogPage } from "../../../components/protected-dog-page";

export const metadata: Metadata = { title: "Historial | Dog RGB" };
export const dynamic = "force-dynamic";

type HistoryPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function HistoryPage(
  props: HistoryPageProps,
) {
  const { dogId } = await props.params;
  const dog = await requireDogPage(dogId, dogAppPath(dogId, "history"));
  return <ProtectedDogPage dog={dog} section="history" />;
}
