import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireDogPage } from "../../../../lib/auth/route-guard";
import { ProtectedDogPage } from "../../../components/protected-dog-page";

export const metadata: Metadata = { title: "Configuración | Dog RGB" };
export const dynamic = "force-dynamic";

export default async function ConfigurationPage(
  props: PageProps<"/app/[dogId]/configuration">,
) {
  const { dogId } = await props.params;
  const dog = await requireDogPage(
    dogId,
    dogAppPath(dogId, "configuration"),
  );
  return <ProtectedDogPage dog={dog} section="configuration" />;
}
