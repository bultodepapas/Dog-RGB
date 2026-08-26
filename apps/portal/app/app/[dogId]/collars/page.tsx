import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireCollarsPage } from "../../../../lib/auth/route-guard";
import { CollarOverview } from "../../../components/collar-overview";

export const metadata: Metadata = { title: "Collares | Dog RGB" };
export const dynamic = "force-dynamic";

type CollarsPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function CollarsPage(
  props: CollarsPageProps,
) {
  const { dogId } = await props.params;
  const snapshot = await requireCollarsPage(
    dogId,
    dogAppPath(dogId, "collars"),
  );
  return <CollarOverview snapshot={snapshot} />;
}
