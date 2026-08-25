export type AuthField = "email" | "password" | "passwordConfirmation";

export type AuthActionState = Readonly<{
  status: "idle" | "error" | "success";
  message: string;
  fieldErrors?: Partial<Record<AuthField, string>>;
}>;

export const INITIAL_AUTH_ACTION_STATE: AuthActionState = {
  status: "idle",
  message: "",
};

type ValidationResult<T> =
  | Readonly<{ ok: true; value: T }>
  | Readonly<{
      ok: false;
      state: AuthActionState;
    }>;

const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/u;
const EMAIL_MAX_LENGTH = 254;
export const PASSWORD_MIN_LENGTH = 10;
const PASSWORD_MAX_LENGTH = 128;

function formText(formData: FormData, name: string): string {
  const value = formData.get(name);
  return typeof value === "string" ? value : "";
}

function errorState(
  message: string,
  fieldErrors?: AuthActionState["fieldErrors"],
): ValidationResult<never> {
  return {
    ok: false,
    state: { status: "error", message, fieldErrors },
  };
}

export function parseEmailForm(
  formData: FormData,
): ValidationResult<Readonly<{ email: string }>> {
  const email = formText(formData, "email").trim().toLowerCase();

  if (
    email.length === 0 ||
    email.length > EMAIL_MAX_LENGTH ||
    !EMAIL_PATTERN.test(email)
  ) {
    return errorState("Revisa el correo e inténtalo de nuevo.", {
      email: "Escribe un correo válido.",
    });
  }

  return { ok: true, value: { email } };
}

export function parseCredentialsForm(
  formData: FormData,
): ValidationResult<Readonly<{ email: string; password: string }>> {
  const parsedEmail = parseEmailForm(formData);
  const password = formText(formData, "password");

  if (!parsedEmail.ok) {
    return parsedEmail;
  }

  if (password.length === 0 || password.length > PASSWORD_MAX_LENGTH) {
    return errorState("Revisa las credenciales e inténtalo de nuevo.", {
      password: "Escribe tu contraseña.",
    });
  }

  return {
    ok: true,
    value: { email: parsedEmail.value.email, password },
  };
}

export function parseNewPasswordForm(
  formData: FormData,
): ValidationResult<Readonly<{ password: string }>> {
  const password = formText(formData, "password");
  const passwordConfirmation = formText(formData, "passwordConfirmation");
  const fieldErrors: AuthActionState["fieldErrors"] = {};

  if (
    password.length < PASSWORD_MIN_LENGTH ||
    password.length > PASSWORD_MAX_LENGTH
  ) {
    fieldErrors.password = `Usa entre ${PASSWORD_MIN_LENGTH} y ${PASSWORD_MAX_LENGTH} caracteres.`;
  }

  if (passwordConfirmation !== password) {
    fieldErrors.passwordConfirmation = "Las contraseñas no coinciden.";
  }

  if (Object.keys(fieldErrors).length > 0) {
    return errorState("Revisa la contraseña e inténtalo de nuevo.", fieldErrors);
  }

  return { ok: true, value: { password } };
}

export function parseSignupForm(
  formData: FormData,
): ValidationResult<Readonly<{ email: string; password: string }>> {
  const parsedEmail = parseEmailForm(formData);
  const parsedPassword = parseNewPasswordForm(formData);

  if (!parsedEmail.ok && !parsedPassword.ok) {
    return errorState("Revisa los campos marcados e inténtalo de nuevo.", {
      ...parsedEmail.state.fieldErrors,
      ...parsedPassword.state.fieldErrors,
    });
  }

  if (!parsedEmail.ok) {
    return parsedEmail;
  }

  if (!parsedPassword.ok) {
    return parsedPassword;
  }

  return {
    ok: true,
    value: {
      email: parsedEmail.value.email,
      password: parsedPassword.value.password,
    },
  };
}
