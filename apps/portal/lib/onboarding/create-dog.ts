export const DOG_NAME_MAX_CODE_POINTS = 80;
export const DOG_TIMEZONE = "America/Bogota";

export type CreateDogActionState = Readonly<{
  status: "idle" | "error";
  message: string;
  fieldErrors?: Readonly<{ name?: string }>;
}>;

export const INITIAL_CREATE_DOG_ACTION_STATE: CreateDogActionState = {
  status: "idle",
  message: "",
};

export const CREATE_DOG_GENERIC_ERROR =
  "No pudimos crear el perfil. Inténtalo de nuevo.";
export const DOG_NAME_FIELD_ERROR =
  "Escribe un nombre de hasta 80 caracteres.";

type DogNameResult =
  | Readonly<{ ok: true; name: string }>
  | Readonly<{ ok: false; state: CreateDogActionState }>;

type DogCreationResult =
  | Readonly<{ ok: true; dogId: string }>
  | Readonly<{ ok: false; state: CreateDogActionState }>;

export type CreateDogMutationDependencies<Client> = Readonly<{
  createClient(): Promise<Client>;
  getFreshUserId(client: Client): Promise<string | null>;
  isCanonicalDogId(value: string): boolean;
  callCreateDogRpc(
    client: Client,
    input: Readonly<{ name: string; timezone: typeof DOG_TIMEZONE }>,
  ): Promise<unknown>;
}>;

function fieldErrorState(): CreateDogActionState {
  return {
    status: "error",
    message: "",
    fieldErrors: { name: DOG_NAME_FIELD_ERROR },
  };
}

function genericErrorResult(): DogCreationResult {
  return {
    ok: false,
    state: { status: "error", message: CREATE_DOG_GENERIC_ERROR },
  };
}

export function parseDogNameForm(formData: FormData): DogNameResult {
  const rawName = formData.get("name");
  if (typeof rawName !== "string") {
    return { ok: false, state: fieldErrorState() };
  }

  const name = rawName.trim();
  // A valid 80-code-point string uses at most 160 UTF-16 code units. The
  // cheap bound avoids materializing an untrusted, arbitrarily large array.
  if (
    name.length === 0 ||
    name.length > DOG_NAME_MAX_CODE_POINTS * 2 ||
    Array.from(name).length > DOG_NAME_MAX_CODE_POINTS
  ) {
    return { ok: false, state: fieldErrorState() };
  }

  return { ok: true, name };
}

export function createDogMutationHandler<Client>(
  dependencies: CreateDogMutationDependencies<Client>,
) {
  return async function createDogMutation(
    formData: FormData,
  ): Promise<DogCreationResult> {
    const parsed = parseDogNameForm(formData);
    if (!parsed.ok) {
      return parsed;
    }

    try {
      const client = await dependencies.createClient();
      const userId = await dependencies.getFreshUserId(client);
      if (!userId) {
        return genericErrorResult();
      }

      const dogId = await dependencies.callCreateDogRpc(client, {
        name: parsed.name,
        timezone: DOG_TIMEZONE,
      });

      if (
        typeof dogId !== "string" ||
        !dependencies.isCanonicalDogId(dogId)
      ) {
        return genericErrorResult();
      }

      return { ok: true, dogId };
    } catch {
      return genericErrorResult();
    }
  };
}
