import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireHistoryPage } from "../../../../lib/auth/route-guard";
import { HistoryLedger } from "../../../components/history-ledger";

export const metadata: Metadata = { title: "Historial | Dog RGB" };
export const dynamic = "force-dynamic";

type HistoryPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
  searchParams: Promise<{ cursor?: string | string[] }>;
}>;

export default async function HistoryPage(
  props: HistoryPageProps,
) {
  const [{ dogId }, searchParams] = await Promise.all([
    props.params,
    props.searchParams,
  ]);
  const history = await requireHistoryPage(
    dogId,
    searchParams.cursor,
    dogAppPath(dogId, "history"),
  );
  return <HistoryLedger history={history} />;
}
