export type PublicSupabaseEnvironment = Readonly<{
  publishableKey: string;
  url: string;
}>;

type EnvironmentInput = Readonly<{
  publishableKey: string | undefined;
  url: string | undefined;
}>;

const LOCAL_HOSTS = new Set(["127.0.0.1", "[::1]", "localhost"]);
const PUBLISHABLE_KEY_PATTERN = /^sb_publishable_[A-Za-z0-9_-]{16,}$/;

function requireValue(value: string | undefined, name: string): string {
  const normalized = value?.trim();

  if (!normalized) {
    throw new Error(
      `Missing ${name}. Copy apps/portal/.env.example to apps/portal/.env.local and replace its placeholders.`,
    );
  }

  return normalized;
}

function validateUrl(value: string): string {
  let parsed: URL;

  try {
    parsed = new URL(value);
  } catch {
    throw new Error("NEXT_PUBLIC_SUPABASE_URL must be an absolute URL.");
  }

  const isLocalHttp =
    parsed.protocol === "http:" && LOCAL_HOSTS.has(parsed.hostname);

  if (parsed.protocol !== "https:" && !isLocalHttp) {
    throw new Error(
      "NEXT_PUBLIC_SUPABASE_URL must use HTTPS, except for localhost development.",
    );
  }

  if (
    parsed.username ||
    parsed.password ||
    parsed.pathname !== "/" ||
    parsed.search ||
    parsed.hash
  ) {
    throw new Error(
      "NEXT_PUBLIC_SUPABASE_URL must contain only the Supabase project origin.",
    );
  }

  return parsed.origin;
}

function validatePublishableKey(value: string): string {
  if (
    !PUBLISHABLE_KEY_PATTERN.test(value) ||
    value.includes("REPLACE_WITH_LOCAL_VALUE")
  ) {
    throw new Error(
      "NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY must be an sb_publishable_ key; secret and legacy keys are not accepted.",
    );
  }

  return value;
}

export function validatePublicSupabaseEnvironment(
  input: EnvironmentInput,
): PublicSupabaseEnvironment {
  return Object.freeze({
    publishableKey: validatePublishableKey(
      requireValue(
        input.publishableKey,
        "NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY",
      ),
    ),
    url: validateUrl(requireValue(input.url, "NEXT_PUBLIC_SUPABASE_URL")),
  });
}

export function getPublicSupabaseEnvironment(): PublicSupabaseEnvironment {
  return validatePublicSupabaseEnvironment({
    publishableKey: process.env.NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY,
    url: process.env.NEXT_PUBLIC_SUPABASE_URL,
  });
}
