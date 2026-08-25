import assert from "node:assert/strict";
import { test } from "node:test";

import { identityFromVerifiedClaims } from "./identity-claims.ts";

test("returns only the authenticated subject", () => {
  assert.deepEqual(
    identityFromVerifiedClaims({
      aud: "authenticated",
      role: "authenticated",
      sub: "48b38642-b623-453f-b42e-718ff4cb7e2b",
    }),
    { userId: "48b38642-b623-453f-b42e-718ff4cb7e2b" },
  );
});

test("accepts an authenticated audience array", () => {
  assert.deepEqual(
    identityFromVerifiedClaims({
      aud: ["authenticated"],
      role: "authenticated",
      sub: "48b38642-b623-453f-b42e-718ff4cb7e2b",
    }),
    { userId: "48b38642-b623-453f-b42e-718ff4cb7e2b" },
  );
});

test("rejects anonymous, wrong-audience, and missing-subject claims", () => {
  assert.equal(
    identityFromVerifiedClaims({
      aud: "authenticated",
      role: "anon",
      sub: "48b38642-b623-453f-b42e-718ff4cb7e2b",
    }),
    null,
  );
  assert.equal(
    identityFromVerifiedClaims({
      aud: "service",
      role: "authenticated",
      sub: "48b38642-b623-453f-b42e-718ff4cb7e2b",
    }),
    null,
  );
  assert.equal(
    identityFromVerifiedClaims({
      aud: "authenticated",
      role: "authenticated",
    }),
    null,
  );
});

test("never forwards user metadata into the identity DTO", () => {
  const claims = {
    aud: "authenticated",
    role: "authenticated",
    sub: "48b38642-b623-453f-b42e-718ff4cb7e2b",
    user_metadata: { role: "owner" },
  };

  assert.deepEqual(identityFromVerifiedClaims(claims), {
    userId: claims.sub,
  });
});
