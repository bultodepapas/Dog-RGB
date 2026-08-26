"use server";

import { revalidatePath } from "next/cache";

import { dogAppPath } from "../../../../lib/auth/protected-route";
import {
  type BrightnessActionState,
  brightnessMutationHandler,
} from "../../../../lib/configuration/brightness-mutation";
import { mutateBrightness } from "../../../../lib/data-access/configuration";

const submitBrightness = brightnessMutationHandler({
  mutate: mutateBrightness,
});

export async function saveBrightnessAction(
  _previousState: BrightnessActionState,
  formData: FormData,
): Promise<BrightnessActionState> {
  const result = await submitBrightness(formData);
  const dogId = formData.get("dogId");
  if (
    (result.status === "saved" || result.status === "unchanged") &&
    typeof dogId === "string"
  ) {
    revalidatePath(dogAppPath(dogId, "configuration"));
  }
  return result;
}
