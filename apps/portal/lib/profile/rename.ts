import { parseDogNameForm } from "../onboarding/create-dog.ts";

export type RenameState = Readonly<{ status: "idle" | "error" | "saved"; message: string }>;
export const INITIAL_RENAME_STATE: RenameState = { status: "idle", message: "" };

export function renameDogHandler(save: (dogId: string, name: string) => Promise<void>) {
  return async (form: FormData): Promise<RenameState> => {
    const dogId = form.get("dogId");
    const parsed = parseDogNameForm(form);
    if (form.getAll("dogId").length !== 1 || form.getAll("name").length !== 1 ||
        typeof dogId !== "string" || !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu.test(dogId) || !parsed.ok) {
      return { status: "error", message: "Escribe un nombre de entre 1 y 80 caracteres." };
    }
    try {
      await save(dogId, parsed.name);
      return { status: "saved", message: "Nombre actualizado." };
    } catch {
      return { status: "error", message: "No pudimos guardar el nombre. Recarga e inténtalo otra vez." };
    }
  };
}
