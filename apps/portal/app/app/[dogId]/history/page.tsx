import { getHistorySummaryStates } from "../../../../lib/data-access/summaries";
import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { parseHistoryRange } from "../../../../lib/data-access/history-range";
import { requireHistoryPage } from "../../../../lib/auth/route-guard";
import { HistoryLedger } from "../../../components/history-ledger";

export const metadata: Metadata = { title: "Historial | Dog RGB" };
export const dynamic = "force-dynamic";

type HistoryPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
  searchParams: Promise<{ cursor?: string | string[]; from?: string | string[]; to?: string | string[] }>;
}>;

export default async function HistoryPage(
  props: HistoryPageProps,
) {
  const [{ dogId }, searchParams] = await Promise.all([
    props.params,
    props.searchParams,
  ]);
  const range = parseHistoryRange(searchParams.from, searchParams.to);
  const history = await requireHistoryPage(
    dogId,
    searchParams.cursor,
    dogAppPath(dogId, "history"),
    range,
  );
  const summaries = history.status === "ready" ? await getHistorySummaryStates(dogId, history.recordings.map(row => row.id)) : {};
  return <HistoryLedger summaries={summaries} history={history} range={range === "invalid" ? null : range} invalidRange={range === "invalid"} />;
}
