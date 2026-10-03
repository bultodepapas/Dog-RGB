import assert from "node:assert/strict";
import test from "node:test";
import { recentPasswordAuthenticationTime } from "../_shared/account_deletion_auth.ts";

const now = 1_800_000_000;

test("accepts a fresh signed password AMR factor", () => {
  assert.equal(recentPasswordAuthenticationTime([
    { method: "otp", timestamp: now },
    { method: "password", timestamp: now - 12 },
  ], now), now - 12);
});

test("rejects password factors outside the five-minute window or in the future", () => {
  assert.equal(recentPasswordAuthenticationTime([{ method: "password", timestamp: now - 301 }], now), null);
  assert.equal(recentPasswordAuthenticationTime([{ method: "password", timestamp: now + 1 }], now), null);
});

test("does not treat OTP, recovery, malformed values, or user metadata as password reauthentication", () => {
  assert.equal(recentPasswordAuthenticationTime([
    { method: "otp", timestamp: now },
    { method: "recovery", timestamp: now },
  ], now), null);
  assert.equal(recentPasswordAuthenticationTime([{ method: "password", timestamp: "1800000000" }], now), null);
  assert.equal(recentPasswordAuthenticationTime({ method: "password", timestamp: now }, now), null);
});
