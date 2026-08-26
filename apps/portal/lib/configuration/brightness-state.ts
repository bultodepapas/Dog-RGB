export const BRIGHTNESS_VALIDATION_MESSAGE =
  "Escribe un número entero del 1 al 255.";
export const BRIGHTNESS_GENERIC_MESSAGE =
  "No pudimos confirmar el resultado. No cambies el valor: reintenta exactamente el mismo brillo o recarga el estado.";
export const BRIGHTNESS_STALE_MESSAGE =
  "Otro cambio ganó antes. No sobrescribimos el valor nuevo. Recarga para revisar el estado actual.";

type RetryInput = Readonly<{
  dogId: string;
  collarId: string;
  brightness: string;
  mutationId: string;
  baseServerVersion: string;
}>;

export type BrightnessActionState =
  | Readonly<{ status: "idle"; message: ""; attemptedBrightness: "" }>
  | Readonly<{
    status: "validation";
    message: typeof BRIGHTNESS_VALIDATION_MESSAGE;
    attemptedBrightness: string;
  }>
  | Readonly<{
    status: "saved";
    message: "";
    attemptedBrightness: string;
    brightness: number;
    serverVersion: number;
  }>
  | Readonly<{
    status: "unchanged";
    message: "";
    attemptedBrightness: string;
    brightness: number;
    serverVersion: number;
  }>
  | Readonly<{
    status: "stale";
    message: typeof BRIGHTNESS_STALE_MESSAGE;
    attemptedBrightness: string;
  }>
  | Readonly<{
    status: "ambiguous";
    message: typeof BRIGHTNESS_GENERIC_MESSAGE;
    attemptedBrightness: string;
    retry: RetryInput;
  }>;

export const INITIAL_BRIGHTNESS_ACTION_STATE: BrightnessActionState = {
  status: "idle",
  message: "",
  attemptedBrightness: "",
};
