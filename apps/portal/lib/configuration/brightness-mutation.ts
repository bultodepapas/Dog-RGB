import {
  brightnessSha256,
  canonicalBrightnessBody,
  type BrightnessMutationInput,
  type ConfigurationMutationResult,
} from "../data-access/configuration-core.ts";
import {
  BRIGHTNESS_GENERIC_MESSAGE,
  BRIGHTNESS_STALE_MESSAGE,
  BRIGHTNESS_VALIDATION_MESSAGE,
  type BrightnessActionState,
} from "./brightness-state.ts";

export {
  BRIGHTNESS_GENERIC_MESSAGE,
  BRIGHTNESS_STALE_MESSAGE,
  BRIGHTNESS_VALIDATION_MESSAGE,
  INITIAL_BRIGHTNESS_ACTION_STATE,
  type BrightnessActionState,
} from "./brightness-state.ts";

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const UUID_V4_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const BRIGHTNESS_PATTERN =
  /^(?:[1-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$/u;
const VERSION_PATTERN = /^(?:0|[1-9][0-9]{0,15})$/u;

export type BrightnessMutationDependencies = Readonly<{
  mutate(input: BrightnessMutationInput): Promise<ConfigurationMutationResult>;
}>;

function exactString(formData: FormData, name: string): string | null {
  const values = formData.getAll(name);
  return values.length === 1 && typeof values[0] === "string"
    ? values[0]
    : null;
}

function boundedAttempt(value: string | null): string {
  return (value ?? "").slice(0, 32);
}

function exactKeys(
  value: Record<string, unknown>,
  expected: readonly string[],
): boolean {
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  return actual.length === wanted.length &&
    actual.every((key, index) => key === wanted[index]);
}

function parseRpcResponse(
  value: unknown,
  expected: Readonly<{
    mutationId: string;
    baseServerVersion: number;
    hash: string;
  }>,
): Readonly<{
  disposition: "winning" | "unchanged";
  serverVersion: number;
}> | null {
  if (
    value === null ||
    typeof value !== "object" ||
    Array.isArray(value) ||
    !exactKeys(value as Record<string, unknown>, [
      "body_sha256",
      "disposition",
      "mutation_id",
      "server_version",
    ])
  ) return null;
  const response = value as Record<string, unknown>;
  if (
    response.mutation_id !== expected.mutationId ||
    response.body_sha256 !== expected.hash ||
    (response.disposition !== "winning" &&
      response.disposition !== "unchanged") ||
    !Number.isSafeInteger(response.server_version)
  ) return null;
  const serverVersion = response.server_version as number;
  if (
    (response.disposition === "winning" &&
      serverVersion !== expected.baseServerVersion + 1) ||
    (response.disposition === "unchanged" &&
      serverVersion !== expected.baseServerVersion)
  ) return null;
  return Object.freeze({
    disposition: response.disposition,
    serverVersion,
  });
}

function validation(attemptedBrightness: string): BrightnessActionState {
  return {
    status: "validation",
    message: BRIGHTNESS_VALIDATION_MESSAGE,
    attemptedBrightness,
  };
}

export function brightnessMutationHandler(
  dependencies: BrightnessMutationDependencies,
) {
  return async function submitBrightness(
    formData: FormData,
  ): Promise<BrightnessActionState> {
    const dogId = exactString(formData, "dogId");
    const collarId = exactString(formData, "collarId");
    const brightnessValue = exactString(formData, "brightness");
    const mutationId = exactString(formData, "mutationId");
    const baseValue = exactString(formData, "baseServerVersion");
    const attemptedBrightness = boundedAttempt(brightnessValue);

    if (
      dogId === null ||
      collarId === null ||
      brightnessValue === null ||
      mutationId === null ||
      baseValue === null ||
      !UUID_PATTERN.test(dogId) ||
      !UUID_PATTERN.test(collarId) ||
      !UUID_V4_PATTERN.test(mutationId) ||
      !BRIGHTNESS_PATTERN.test(brightnessValue) ||
      !VERSION_PATTERN.test(baseValue)
    ) return validation(attemptedBrightness);

    const brightness = Number(brightnessValue);
    const baseServerVersion = Number(baseValue);
    if (!Number.isSafeInteger(baseServerVersion)) {
      return validation(attemptedBrightness);
    }
    const canonicalBody = canonicalBrightnessBody(brightness);
    const digest = brightnessSha256(brightness);
    const input = Object.freeze({
      dogId,
      collarId,
      brightness,
      mutationId,
      baseServerVersion,
      canonicalBody,
      bodySha256Hex: digest.hex,
      bodySha256Base64Url: digest.base64url,
    });

    let result: ConfigurationMutationResult;
    try {
      result = await dependencies.mutate(input);
    } catch {
      return {
        status: "ambiguous",
        message: BRIGHTNESS_GENERIC_MESSAGE,
        attemptedBrightness,
        retry: Object.freeze({
          dogId,
          collarId,
          brightness: brightnessValue,
          mutationId,
          baseServerVersion: baseValue,
        }),
      };
    }

    if (!result.ok) {
      if (
        result.reason === "stale" ||
        result.reason === "selection_changed" ||
        result.reason === "conflict"
      ) {
        return {
          status: "stale",
          message: BRIGHTNESS_STALE_MESSAGE,
          attemptedBrightness,
        };
      }
      return {
        status: "ambiguous",
        message: BRIGHTNESS_GENERIC_MESSAGE,
        attemptedBrightness,
        retry: Object.freeze({
          dogId,
          collarId,
          brightness: brightnessValue,
          mutationId,
          baseServerVersion: baseValue,
        }),
      };
    }

    const parsed = parseRpcResponse(result.data, {
      mutationId,
      baseServerVersion,
      hash: digest.base64url,
    });
    if (!parsed) {
      return {
        status: "ambiguous",
        message: BRIGHTNESS_GENERIC_MESSAGE,
        attemptedBrightness,
        retry: Object.freeze({
          dogId,
          collarId,
          brightness: brightnessValue,
          mutationId,
          baseServerVersion: baseValue,
        }),
      };
    }
    return {
      status: parsed.disposition === "winning" ? "saved" : "unchanged",
      message: "",
      attemptedBrightness,
      brightness,
      serverVersion: parsed.serverVersion,
    };
  };
}
