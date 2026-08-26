import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  CLAIM_CODE_PATTERN,
  CLAIM_TTL_SECONDS,
  ISSUE_CLAIM_ACTIVE_ERROR,
  ISSUE_CLAIM_EMAIL_ERROR,
  ISSUE_CLAIM_GENERIC_ERROR,
  ISSUE_CLAIM_RATE_ERROR,
  issueClaimMutationHandler,
  parseIssueClaimResponse,
} from "./issue-claim.ts";

const DOG_ID = "30000000-0000-4000-8000-000000000003";
const REQUEST_ID = "41000000-0000-4000-8000-000000000004";
const CLAIM_ID = "42000000-0000-4000-8000-000000000004";
const CODE = "0123ABCD4567EFGH";
const SERVER_TIME = "2026-08-25T17:00:00.000Z";
const EXPIRES_AT = "2026-08-25T17:15:00.000Z";

function form(dogId = DOG_ID) {
  const data = new FormData();
  if (dogId !== undefined) {
    data.set("dogId", dogId);
  }
  return data;
}

function successResponse(overrides = {}) {
  const response = {
    protocol_version: 1,
    request_id: REQUEST_ID,
    server_time: SERVER_TIME,
    claim: {
      claim_id: CLAIM_ID,
      dog_id: DOG_ID,
      code: CODE,
      expires_at: EXPIRES_AT,
      expires_in_seconds: CLAIM_TTL_SECONDS,
    },
  };
  return { ...response, ...overrides };
}

function harness({ invocation, authorizeError, requestId = REQUEST_ID } = {}) {
  const calls = [];
  const mutate = issueClaimMutationHandler({
    isCanonicalUuid(value) {
      calls.push(["isCanonicalUuid", value]);
      return value === DOG_ID;
    },
    createRequestId() {
      calls.push(["createRequestId"]);
      return requestId;
    },
    async authorizeDogWrite(dogId) {
      calls.push(["authorizeDogWrite", dogId]);
      if (authorizeError) {
        throw authorizeError;
      }
    },
    async invokeIssueClaim(input) {
      calls.push(["invokeIssueClaim", input]);
      if (invocation instanceof Error) {
        throw invocation;
      }
      return invocation ?? { ok: true, data: successResponse() };
    },
  });
  return { calls, mutate };
}

test("the frozen claim code is 16 unambiguous Crockford characters", () => {
  assert.equal(CLAIM_CODE_PATTERN.test(CODE), true);
  assert.equal(CLAIM_CODE_PATTERN.test("0123ABCD4567EFGO"), false);
  assert.equal(CLAIM_CODE_PATTERN.test("0123ABCD4567EFG"), false);
  assert.equal(CLAIM_TTL_SECONDS, 900);
});

test("invalid or missing dog identifiers fail before authorization and Edge", async () => {
  const missing = new FormData();
  const blob = new FormData();
  blob.set("dogId", new Blob());
  for (const data of [missing, form(""), form("not-a-uuid"), blob]) {
    const { calls, mutate } = harness();
    assert.deepEqual(await mutate(data), {
      ok: false,
      state: { status: "error", message: ISSUE_CLAIM_GENERIC_ERROR },
    });
    assert.equal(calls.some(([name]) => name === "authorizeDogWrite"), false);
    assert.equal(calls.some(([name]) => name === "invokeIssueClaim"), false);
  }
});

test("write authorization precedes one exact Edge invocation", async () => {
  const { calls, mutate } = harness();

  assert.deepEqual(await mutate(form()), {
    ok: true,
    state: { status: "success", message: "", code: CODE },
  });
  assert.deepEqual(calls, [
    ["isCanonicalUuid", DOG_ID],
    ["authorizeDogWrite", DOG_ID],
    ["createRequestId"],
    [
      "invokeIssueClaim",
      { protocol_version: 1, request_id: REQUEST_ID, dog_id: DOG_ID },
    ],
  ]);
});

test("the response parser returns only the raw code from the exact contract", () => {
  assert.equal(
    parseIssueClaimResponse(successResponse(), {
      dogId: DOG_ID,
      requestId: REQUEST_ID,
    }),
    CODE,
  );
});

test("the response parser rejects shape, identity, code, and expiry drift", () => {
  const invalid = [
    null,
    { ...successResponse(), extra: true },
    successResponse({ protocol_version: 2 }),
    successResponse({ request_id: "43000000-0000-4000-8000-000000000004" }),
    successResponse({ server_time: "not-a-time" }),
    successResponse({
      claim: { ...successResponse().claim, claim_id: "42000000-0000-1000-8000-000000000004" },
    }),
    successResponse({
      claim: { ...successResponse().claim, dog_id: "44000000-0000-4000-8000-000000000004" },
    }),
    successResponse({
      claim: { ...successResponse().claim, code: "0123ABCD4567EFGO" },
    }),
    successResponse({
      claim: { ...successResponse().claim, expires_in_seconds: 899 },
    }),
    successResponse({
      claim: { ...successResponse().claim, expires_at: "2026-08-25T17:14:59.999Z" },
    }),
    successResponse({
      claim: { ...successResponse().claim, extra: true },
    }),
  ];

  invalid.forEach((response) => {
    assert.equal(
      parseIssueClaimResponse(response, {
        dogId: DOG_ID,
        requestId: REQUEST_ID,
      }),
      null,
    );
  });
});

test("only allowlisted problem codes become bounded Spanish guidance", async () => {
  const cases = [
    ["active_claim_exists", ISSUE_CLAIM_ACTIVE_ERROR],
    ["email_not_verified", ISSUE_CLAIM_EMAIL_ERROR],
    ["rate_limited", ISSUE_CLAIM_RATE_ERROR],
    ["dog_access_denied", ISSUE_CLAIM_GENERIC_ERROR],
    ["database_internal_detail", ISSUE_CLAIM_GENERIC_ERROR],
    [null, ISSUE_CLAIM_GENERIC_ERROR],
  ];

  for (const [problemCode, message] of cases) {
    const { mutate } = harness({
      invocation: { ok: false, problemCode },
    });
    assert.deepEqual(await mutate(form()), {
      ok: false,
      state: { status: "error", message },
    });
  }
});

test("authorization, invocation, request-id, and malformed-success failures converge safely", async () => {
  const cases = [
    { authorizeError: new Error("membership detail") },
    { invocation: new Error("network detail") },
    { requestId: "not-v4" },
    { invocation: { ok: true, data: { database: "detail" } } },
  ];

  for (const options of cases) {
    const { mutate } = harness(options);
    assert.deepEqual(await mutate(form()), {
      ok: false,
      state: { status: "error", message: ISSUE_CLAIM_GENERIC_ERROR },
    });
  }
});

test("the production action keeps the DAL and authenticated Edge boundaries", async () => {
  const action = await readFile(
    new URL("../../app/app/[dogId]/collars/actions.ts", import.meta.url),
    "utf8",
  );
  const core = await readFile(new URL("./issue-claim.ts", import.meta.url), "utf8");
  const authorizeIndex = action.indexOf('requireDogAccess(dogId, "write")');
  const invokeIndex = action.indexOf('"user-v1-issue-claim"');

  assert.match(action, /^"use server";/u);
  assert.ok(authorizeIndex >= 0);
  assert.ok(invokeIndex > authorizeIndex);
  assert.match(action, /createRequestId: \(\) => crypto\.randomUUID\(\)/u);
  assert.match(action, /client\.functions\.invoke/u);
  assert.doesNotMatch(action, /\.rpc\(|\.from\(|service_role|sb_secret_|console\./u);
  assert.doesNotMatch(`${action}\n${core}`, /error\.message|error\.details/u);
});

test("the Collares UI keeps the guard, role gate, ephemeral state, and accessible result", async () => {
  const page = await readFile(
    new URL("../../app/app/[dogId]/collars/page.tsx", import.meta.url),
    "utf8",
  );
  const formSource = await readFile(
    new URL("../../app/app/[dogId]/collars/claim-code-form.tsx", import.meta.url),
    "utf8",
  );
  const overviewSource = await readFile(
    new URL("../../app/components/collar-overview.tsx", import.meta.url),
    "utf8",
  );

  assert.match(page, /await requireCollarsPage/u);
  assert.match(page, /<CollarOverview snapshot=\{snapshot\} \/>/u);
  assert.match(overviewSource, /snapshot\.canIssueClaim/u);
  assert.match(overviewSource, /<ClaimCodeForm dogId=\{dog\.id\} \/>/u);
  assert.match(formSource, /useActionState/u);
  assert.match(formSource, /submitLocked\.current/u);
  assert.match(formSource, /aria-busy=\{pending\}/u);
  assert.match(formSource, /role="status"/u);
  assert.match(formSource, /aria-live="polite"/u);
  assert.match(formSource, /resultRef\.current\?\.focus\(\)/u);
  assert.doesNotMatch(
    `${page}\n${overviewSource}\n${formSource}`,
    /localStorage|sessionStorage|indexedDB|navigator\.clipboard|console\.|useOptimistic/u,
  );
});

test("the Edge issuer uses one timestamp, fresh identity, and UUID-gates the reveal", async () => {
  const source = await readFile(
    new URL("../../../../supabase/functions/user-v1-issue-claim/index.ts", import.meta.url),
    "utf8",
  );
  const gateway = await readFile(
    new URL("../../../../supabase/functions/_shared/gateway.ts", import.meta.url),
    "utf8",
  );

  assert.match(source, /const issuedAt = new Date\(\)/u);
  assert.match(source, /serverTime = issuedAt\.toISOString\(\)/u);
  assert.match(source, /issuedAt\.getTime\(\) \+ 900_000/u);
  assert.match(source, /const userId = userResult\.user\.id/u);
  assert.match(source, /if \(!isUuidV4\(claimId\)\)/u);
  assert.match(source, /"cache-control": "no-store"/u);
  assert.doesNotMatch(source, /console\.|code_digest.*return/u);
  assert.match(gateway, /Bearer realm="dog-rgb-user"/u);
});
