import type { Metadata } from "next";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import { requireConfigurationPage } from "../../../../lib/auth/route-guard";
import { BrightnessConfiguration } from "../../../components/brightness-configuration";

export const metadata: Metadata = { title: "Configuración | Dog RGB" };
export const dynamic = "force-dynamic";

type ConfigurationPageProps = Readonly<{
  params: Promise<{ dogId: string }>;
}>;

export default async function ConfigurationPage(
  props: ConfigurationPageProps,
) {
  const { dogId } = await props.params;
  const snapshot = await requireConfigurationPage(
    dogId,
    dogAppPath(dogId, "configuration"),
  );
  const mutationId = snapshot.canEdit && snapshot.collar
    ? crypto.randomUUID()
    : null;
  return (
    <BrightnessConfiguration
      mutationId={mutationId}
      snapshot={snapshot}
    />
  );
}
