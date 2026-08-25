"use client";

import { useActionState } from "react";

import {
  loginAction,
  requestPasswordResetAction,
  signupAction,
  updatePasswordAction,
} from "../auth/actions";
import {
  type AuthActionState,
  INITIAL_AUTH_ACTION_STATE,
  PASSWORD_MIN_LENGTH,
} from "../../lib/auth/form";

function FormMessage({ state }: Readonly<{ state: AuthActionState }>) {
  if (!state.message) {
    return null;
  }

  return (
    <p
      className={`form-message form-message--${state.status}`}
      role={state.status === "error" ? "alert" : "status"}
      aria-live="polite"
    >
      {state.message}
    </p>
  );
}

function FieldError({ message, id }: Readonly<{ message?: string; id: string }>) {
  return message ? (
    <span className="field-error" id={id}>
      {message}
    </span>
  ) : null;
}

export function LoginForm({ nextPath }: Readonly<{ nextPath: string }>) {
  const [state, action, pending] = useActionState(
    loginAction,
    INITIAL_AUTH_ACTION_STATE,
  );

  return (
    <form className="auth-form" action={action}>
      <input name="next" type="hidden" value={nextPath} />
      <label htmlFor="login-email">Correo</label>
      <input
        id="login-email"
        name="email"
        type="email"
        autoComplete="email"
        inputMode="email"
        maxLength={254}
        aria-describedby={state.fieldErrors?.email ? "login-email-error" : undefined}
        required
      />
      <FieldError id="login-email-error" message={state.fieldErrors?.email} />

      <label htmlFor="login-password">Contraseña</label>
      <input
        id="login-password"
        name="password"
        type="password"
        autoComplete="current-password"
        maxLength={128}
        aria-describedby={
          state.fieldErrors?.password ? "login-password-error" : undefined
        }
        required
      />
      <FieldError
        id="login-password-error"
        message={state.fieldErrors?.password}
      />
      <FormMessage state={state} />
      <button type="submit" disabled={pending}>
        {pending ? "VERIFICANDO…" : "INICIAR SESIÓN"}
      </button>
    </form>
  );
}

export function SignupForm() {
  const [state, action, pending] = useActionState(
    signupAction,
    INITIAL_AUTH_ACTION_STATE,
  );

  return (
    <form className="auth-form" action={action}>
      <label htmlFor="signup-email">Correo</label>
      <input
        id="signup-email"
        name="email"
        type="email"
        autoComplete="email"
        inputMode="email"
        maxLength={254}
        aria-describedby={
          state.fieldErrors?.email ? "signup-email-error" : undefined
        }
        required
      />
      <FieldError id="signup-email-error" message={state.fieldErrors?.email} />

      <label htmlFor="signup-password">Contraseña</label>
      <input
        id="signup-password"
        name="password"
        type="password"
        autoComplete="new-password"
        minLength={PASSWORD_MIN_LENGTH}
        maxLength={128}
        aria-describedby="signup-password-help signup-password-error"
        required
      />
      <span className="field-help" id="signup-password-help">
        Entre {PASSWORD_MIN_LENGTH} y 128 caracteres.
      </span>
      <FieldError
        id="signup-password-error"
        message={state.fieldErrors?.password}
      />

      <label htmlFor="signup-password-confirmation">Repite la contraseña</label>
      <input
        id="signup-password-confirmation"
        name="passwordConfirmation"
        type="password"
        autoComplete="new-password"
        minLength={PASSWORD_MIN_LENGTH}
        maxLength={128}
        aria-describedby={
          state.fieldErrors?.passwordConfirmation
            ? "signup-password-confirmation-error"
            : undefined
        }
        required
      />
      <FieldError
        id="signup-password-confirmation-error"
        message={state.fieldErrors?.passwordConfirmation}
      />
      <FormMessage state={state} />
      <button type="submit" disabled={pending}>
        {pending ? "CREANDO…" : "CREAR CUENTA"}
      </button>
    </form>
  );
}

export function PasswordResetRequestForm() {
  const [state, action, pending] = useActionState(
    requestPasswordResetAction,
    INITIAL_AUTH_ACTION_STATE,
  );

  return (
    <form className="auth-form" action={action}>
      <label htmlFor="recovery-email">Correo de la cuenta</label>
      <input
        id="recovery-email"
        name="email"
        type="email"
        autoComplete="email"
        inputMode="email"
        maxLength={254}
        aria-describedby={
          state.fieldErrors?.email ? "recovery-email-error" : undefined
        }
        required
      />
      <FieldError
        id="recovery-email-error"
        message={state.fieldErrors?.email}
      />
      <FormMessage state={state} />
      <button type="submit" disabled={pending}>
        {pending ? "ENVIANDO…" : "ENVIAR ENLACE"}
      </button>
    </form>
  );
}

export function PasswordUpdateForm() {
  const [state, action, pending] = useActionState(
    updatePasswordAction,
    INITIAL_AUTH_ACTION_STATE,
  );

  return (
    <form className="auth-form" action={action}>
      <label htmlFor="new-password">Contraseña nueva</label>
      <input
        id="new-password"
        name="password"
        type="password"
        autoComplete="new-password"
        minLength={PASSWORD_MIN_LENGTH}
        maxLength={128}
        aria-describedby="new-password-help new-password-error"
        required
      />
      <span className="field-help" id="new-password-help">
        Entre {PASSWORD_MIN_LENGTH} y 128 caracteres.
      </span>
      <FieldError id="new-password-error" message={state.fieldErrors?.password} />

      <label htmlFor="new-password-confirmation">Repite la contraseña</label>
      <input
        id="new-password-confirmation"
        name="passwordConfirmation"
        type="password"
        autoComplete="new-password"
        minLength={PASSWORD_MIN_LENGTH}
        maxLength={128}
        aria-describedby={
          state.fieldErrors?.passwordConfirmation
            ? "new-password-confirmation-error"
            : undefined
        }
        required
      />
      <FieldError
        id="new-password-confirmation-error"
        message={state.fieldErrors?.passwordConfirmation}
      />
      <FormMessage state={state} />
      <button type="submit" disabled={pending}>
        {pending ? "GUARDANDO…" : "CAMBIAR CONTRASEÑA"}
      </button>
    </form>
  );
}
