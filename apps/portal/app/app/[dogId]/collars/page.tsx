import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireDogPage } from "../../../../lib/auth/route-guard";
import { ProtectedDogPage } from "../../../components/protected-dog-page";

export const metadata: Metadata = { title: "Collares | Dog RGB" };
export const dynamic = "force-dynamic";

type CollarsPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function CollarsPage(
  props: CollarsPageProps,
) {
  const { dogId } = await props.params;
  const dog = await requireDogPage(dogId, dogAppPath(dogId, "collars"));
  return <ProtectedDogPage dog={dog} section="collars" />;
}
