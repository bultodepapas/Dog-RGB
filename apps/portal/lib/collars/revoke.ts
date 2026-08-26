export const REVOKE_CONFIRMATION_VALUE = "confirmed";
export const REVOKE_GENERIC_ERROR =
  "No pudimos confirmar la revocación. Recarga para comprobar el estado antes de intentarlo otra vez.";
export const REVOKE_SELECTION_ERROR =
  "El collar seleccionado cambió. Recarga antes de continuar.";

export type RevokeCollarActionState =
  | Readonly<{ status: "idle"; message: "" }>
  | Readonly<{ status: "error"; message: string }>
  | Readonly<{ status: "revoked" | "already_revoked"; message: "" }>;

export const INITIAL_REVOKE_COLLAR_ACTION_STATE: RevokeCollarActionState = {
  status: "idle",
  message: "",
};

type RevokeResult =
  | Readonly<{ ok: true; previousState: "active" | "revoked" }>
  | Readonly<{ ok: false; reason: "selection_changed" | "ambiguous" }>;

export type RevokeCollarDependencies = Readonly<{
  isCanonicalUuid(value: string): boolean;
  revoke(input: Readonly<{ dogId: string; collarId: string }>): Promise<RevokeResult>;
}>;

function failure(message = REVOKE_GENERIC_ERROR): RevokeCollarActionState {
  return { status: "error", message };
}

function exactText(formData: FormData, name: string): string | null {
  const values = formData.getAll(name);
  return values.length === 1 && typeof values[0] === "string" ? values[0] : null;
}

export function revokeCollarMutationHandler(
  dependencies: RevokeCollarDependencies,
) {
  return async function revokeCollarMutation(
    formData: FormData,
  ): Promise<RevokeCollarActionState> {
    const dogId = exactText(formData, "dogId");
    const collarId = exactText(formData, "collarId");
    const confirmation = exactText(formData, "confirmation");
    if (
      typeof dogId !== "string" ||
      typeof collarId !== "string" ||
      !dependencies.isCanonicalUuid(dogId) ||
      !dependencies.isCanonicalUuid(collarId) ||
      confirmation !== REVOKE_CONFIRMATION_VALUE
    ) return failure();

    try {
      const result = await dependencies.revoke({ dogId, collarId });
      if (!result.ok) {
        return failure(
          result.reason === "selection_changed"
            ? REVOKE_SELECTION_ERROR
            : REVOKE_GENERIC_ERROR,
        );
      }
      return {
        status: result.previousState === "revoked" ? "already_revoked" : "revoked",
        message: "",
      };
    } catch {
      return failure();
    }
  };
}
