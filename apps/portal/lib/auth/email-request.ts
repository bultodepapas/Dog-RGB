import type { AuthActionState } from "./form";

export const EMAIL_REQUEST_NOTICE =
  "Si la solicitud puede completarse, enviaremos un correo. Si no llega, espera unos minutos e inténtalo de nuevo.";

export function resolveEmailRequestState(
  providerError: unknown,
): AuthActionState {
  // Auth may report different errors for delivery, rate limits, and ineligible
  // addresses. Return one recoverable response so those cases cannot identify
  // whether an account exists.
  void providerError;
  return { status: "success", message: EMAIL_REQUEST_NOTICE };
}

export async function runGenericEmailRequest(
  send: () => Promise<Readonly<{ error: unknown }>>,
): Promise<AuthActionState> {
  try {
    const { error } = await send();
    return resolveEmailRequestState(error);
  } catch (error) {
    return resolveEmailRequestState(error);
  }
}
