import assert from "node:assert/strict";
import { test } from "node:test";

import { validatePublicSupabaseEnvironment } from "./environment.ts";

const validPublishableKey = `sb_publishable_${"a".repeat(24)}`;

test("accepts and normalizes the local Supabase origin", () => {
  assert.deepEqual(
    validatePublicSupabaseEnvironment({
      publishableKey: validPublishableKey,
      url: "http://127.0.0.1:54321/",
    }),
    {
      publishableKey: validPublishableKey,
      url: "http://127.0.0.1:54321",
    },
  );
});

test("accepts an HTTPS project origin", () => {
  assert.equal(
    validatePublicSupabaseEnvironment({
      publishableKey: validPublishableKey,
      url: "https://project-ref.supabase.co",
    }).url,
    "https://project-ref.supabase.co",
  );
});

test("rejects missing configuration with an actionable message", () => {
  assert.throws(
    () =>
      validatePublicSupabaseEnvironment({
        publishableKey: undefined,
        url: undefined,
      }),
    /NEXT_PUBLIC_SUPABASE_PUBLISHABLE_KEY/,
  );
});

test("rejects non-publishable keys", () => {
  const secretKey = ["sb", "secret", "a".repeat(24)].join("_");

  assert.throws(
    () =>
      validatePublicSupabaseEnvironment({
        publishableKey: secretKey,
        url: "https://project-ref.supabase.co",
      }),
    /must be an sb_publishable_ key/,
  );

  assert.throws(
    () =>
      validatePublicSupabaseEnvironment({
        publishableKey: "sb_publishable_REPLACE_WITH_LOCAL_VALUE",
        url: "http://127.0.0.1:56321",
      }),
    /must be an sb_publishable_ key/,
  );
});

test("rejects insecure non-local and path-bearing URLs", () => {
  assert.throws(
    () =>
      validatePublicSupabaseEnvironment({
        publishableKey: validPublishableKey,
        url: "http://project-ref.supabase.co",
      }),
    /must use HTTPS/,
  );

  assert.throws(
    () =>
      validatePublicSupabaseEnvironment({
        publishableKey: validPublishableKey,
        url: "https://project-ref.supabase.co/auth/v1",
      }),
    /project origin/,
  );
});
