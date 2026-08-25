import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  parseCredentialsForm,
  parseEmailForm,
  parseNewPasswordForm,
  parseSignupForm,
} from "./form.ts";
import {
  confirmationErrorRedirect,
  parseEmailOtpType,
  resolveConfirmationRedirect,
  resolveLocalAuthOrigin,
} from "./redirect.ts";

function form(values) {
  const data = new FormData();
  Object.entries(values).forEach(([name, value]) => data.set(name, value));
  return data;
}

test("email input is normalized and bounded", () => {
  assert.deepEqual(parseEmailForm(form({ email: " OWNER@Example.Test " })), {
    ok: true,
    value: { email: "owner@example.test" },
  });
  assert.equal(parseEmailForm(form({ email: "not-an-email" })).ok, false);
  assert.equal(
    parseEmailForm(form({ email: `${"a".repeat(245)}@test.example` })).ok,
    false,
  );
});

test("credential parsing rejects empty and oversized passwords", () => {
  assert.equal(
    parseCredentialsForm(form({ email: "owner@example.test", password: "" })).ok,
    false,
  );
  assert.equal(
    parseCredentialsForm(
      form({ email: "owner@example.test", password: "x".repeat(129) }),
    ).ok,
    false,
  );
});

test("new passwords meet the local auth bound and must match", () => {
  assert.equal(
    parseNewPasswordForm(
      form({ password: "short", passwordConfirmation: "short" }),
    ).ok,
    false,
  );
  assert.equal(
    parseNewPasswordForm(
      form({
        password: "long-enough-password",
        passwordConfirmation: "different-password",
      }),
    ).ok,
    false,
  );
  assert.deepEqual(
    parseNewPasswordForm(
      form({
        password: "long-enough-password",
        passwordConfirmation: "long-enough-password",
      }),
    ),
    { ok: true, value: { password: "long-enough-password" } },
  );
});

test("signup validates email and both password fields together", () => {
  const result = parseSignupForm(
    form({
      email: "invalid",
      password: "short",
      passwordConfirmation: "mismatch",
    }),
  );
  assert.equal(result.ok, false);
  assert.deepEqual(Object.keys(result.state.fieldErrors).sort(), [
    "email",
    "password",
    "passwordConfirmation",
  ]);
});

test("confirmation accepts only the two M1.2 email flow types", () => {
  assert.equal(parseEmailOtpType("email"), "email");
  assert.equal(parseEmailOtpType("recovery"), "recovery");
  assert.equal(parseEmailOtpType("magiclink"), null);
  assert.equal(parseEmailOtpType(null), null);
});

test("confirmation redirects are exact allowlist matches", () => {
  assert.equal(resolveConfirmationRedirect("email", "/login"), "/login");
  assert.equal(
    resolveConfirmationRedirect("recovery", "/forgot-password?mode=update"),
    "/forgot-password?mode=update",
  );

  for (const hostile of [
    "https://attacker.example",
    "//attacker.example",
    "\\\\attacker.example",
    "/login?next=https://attacker.example",
    "/%2f%2fattacker.example",
    "/forgot-password",
  ]) {
    assert.equal(
      resolveConfirmationRedirect("email", hostile),
      "/?auth=confirmed",
    );
  }
});

test("invalid or expired links return to the matching recovery surface", () => {
  assert.equal(
    confirmationErrorRedirect("recovery"),
    "/forgot-password?auth_error=invalid_or_expired",
  );
  assert.equal(
    confirmationErrorRedirect("email"),
    "/login?auth_error=invalid_or_expired",
  );
  assert.equal(
    confirmationErrorRedirect(null),
    "/login?auth_error=invalid_or_expired",
  );
});

test("local confirmation origin is exact and fails closed on Host injection", () => {
  assert.equal(
    resolveLocalAuthOrigin("127.0.0.1:3000"),
    "http://127.0.0.1:3000",
  );
  assert.equal(
    resolveLocalAuthOrigin("LOCALHOST:3000"),
    "http://localhost:3000",
  );
  assert.equal(
    resolveLocalAuthOrigin("attacker.example"),
    "http://127.0.0.1:3000",
  );
  assert.equal(resolveLocalAuthOrigin(null), "http://127.0.0.1:3000");
});

test("logout is explicitly local to the current session", async () => {
  const actions = await readFile(
    new URL("../../app/auth/actions.ts", import.meta.url),
    "utf8",
  );
  assert.match(actions, /signOut\(\{ scope: "local" \}\)/u);
  assert.doesNotMatch(actions, /user_metadata|service_role|sb_secret_/u);
});
