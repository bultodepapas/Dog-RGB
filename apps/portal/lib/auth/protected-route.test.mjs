import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  classifyDogPageFailure,
  dogAppPath,
  isPrivatePortalPath,
  protectedLoginPath,
  recordingAppPath,
  resolveProtectedReturnPath,
} from "./protected-route.ts";

const DOG_ID = "20000000-0000-4000-8000-000000000001";
const RECORDING_ID = "30000000-0000-4000-8000-000000000001";

test("protected return paths accept only exact M1.4 routes", () => {
  const allowed = [
    "/onboarding",
    `/app/${DOG_ID}/today`,
    `/app/${DOG_ID}/history`,
    `/app/${DOG_ID}/collars`,
    `/app/${DOG_ID}/configuration`,
    `/app/${DOG_ID}/recordings/${RECORDING_ID}`,
  ];

  allowed.forEach((path) => assert.equal(resolveProtectedReturnPath(path), path));
});

test("hostile, ambiguous, and future return paths fail to onboarding", () => {
  const hostile = [
    undefined,
    null,
    1,
    "",
    "https://attacker.example/app",
    "//attacker.example/app",
    "\\\\attacker.example\\app",
    "/onboarding?next=https://attacker.example",
    "/onboarding#fragment",
    "/%2f%2fattacker.example",
    "/%5c%5cattacker.example",
    "/onboarding%0d%0aLocation:https://attacker.example",
    "/app/../onboarding",
    "/app/%2e%2e/onboarding",
    " /onboarding",
    `/app/${DOG_ID}/today?preview=1`,
    `/app/${DOG_ID}/today/`,
    `/app/${DOG_ID}/admin`,
    `/app/not-a-uuid/today`,
    `/app/${DOG_ID}/recordings/not-a-uuid`,
    `/app/${DOG_ID}/../${DOG_ID}/today`,
  ];

  hostile.forEach((path) =>
    assert.equal(resolveProtectedReturnPath(path), "/onboarding"),
  );
});

test("private cache scope covers protected segments only", () => {
  assert.equal(isPrivatePortalPath("/onboarding"), true);
  assert.equal(isPrivatePortalPath(`/app/${DOG_ID}/today`), true);
  assert.equal(isPrivatePortalPath("/app/not-a-route"), true);
  assert.equal(isPrivatePortalPath("/"), false);
  assert.equal(isPrivatePortalPath("/login"), false);
  assert.equal(isPrivatePortalPath("/_next/static/file.js"), false);
});

test("route builders and login redirect preserve one exact local path", () => {
  const today = dogAppPath(DOG_ID, "today");
  const recording = recordingAppPath(DOG_ID, RECORDING_ID);

  assert.equal(today, `/app/${DOG_ID}/today`);
  assert.equal(recording, `/app/${DOG_ID}/recordings/${RECORDING_ID}`);
  assert.equal(
    protectedLoginPath(today),
    `/login?next=${encodeURIComponent(today)}`,
  );
  assert.equal(
    protectedLoginPath("https://attacker.example"),
    "/login?next=%2Fonboarding",
  );
});

test("dog route failures hide membership and keep infrastructure errors distinct", () => {
  assert.equal(classifyDogPageFailure("authentication_required"), "login");
  assert.equal(classifyDogPageFailure("access_denied"), "not-found");
  assert.equal(classifyDogPageFailure("invalid_dog_id"), "not-found");
  assert.equal(classifyDogPageFailure("data_unavailable"), "error");
});

test("fresh page identity uses the Auth server and returns a minimal DTO", async () => {
  const source = await readFile(
    new URL("../supabase/identity.ts", import.meta.url),
    "utf8",
  );

  assert.match(source, /getFreshIdentity/u);
  assert.match(source, /supabase\.auth\.getUser\(\)/u);
  assert.match(source, /Object\.freeze\(\{ userId \}\)/u);
  assert.doesNotMatch(source, /user_metadata|service_role|sb_secret_/u);
});

test("route guard is server-only and delegates dog authorization to M1.3", async () => {
  const source = await readFile(new URL("./route-guard.ts", import.meta.url), "utf8");

  assert.match(source, /^import "server-only";/u);
  assert.match(source, /getFreshIdentity\(\)/u);
  assert.match(source, /getDogSummary\(dogId, "read"\)/u);
  assert.match(source, /classifyDogPageFailure\(error\.code\)/u);
  assert.match(source, /redirect\(protectedLoginPath\(returnTo\)\)/u);
  assert.match(source, /notFound\(\)/u);
  assert.doesNotMatch(source, /getVerifiedIdentity|user_metadata/u);
});

test("every private leaf route is dynamic and awaits its own page guard", async () => {
  const routes = [
    ["../../app/onboarding/page.tsx", /await requireFreshPageIdentity/u],
    ["../../app/app/[dogId]/today/page.tsx", /await requireTodayPage/u],
    ["../../app/app/[dogId]/history/page.tsx", /await requireHistoryPage/u],
    ["../../app/app/[dogId]/collars/page.tsx", /await requireDogPage/u],
    [
      "../../app/app/[dogId]/configuration/page.tsx",
      /await requireConfigurationPage/u,
    ],
    [
      "../../app/app/[dogId]/recordings/[recordingId]/page.tsx",
      /await requireRecordingPage/u,
    ],
  ];

  for (const [path, guardPattern] of routes) {
    const source = await readFile(new URL(path, import.meta.url), "utf8");
    assert.match(source, /export const dynamic = "force-dynamic";/u, path);
    assert.match(source, guardPattern, path);
    assert.doesNotMatch(source, /\bPageProps</u, path);
  }

  await assert.rejects(
    readFile(
      new URL("../../app/onboarding/loading.tsx", import.meta.url),
      "utf8",
    ),
    { code: "ENOENT" },
  );
  await assert.rejects(
    readFile(
      new URL("../../app/app/[dogId]/loading.tsx", import.meta.url),
      "utf8",
    ),
    { code: "ENOENT" },
  );
});

test("private shell keeps accessible navigation and does not query product data", async () => {
  const shell = await readFile(
    new URL("../../app/components/private-shell.tsx", import.meta.url),
    "utf8",
  );
  const recordingPage = await readFile(
    new URL(
      "../../app/app/[dogId]/recordings/[recordingId]/page.tsx",
      import.meta.url,
    ),
    "utf8",
  );

  assert.match(shell, /Saltar al contenido/u);
  assert.match(shell, /aria-current/u);
  assert.match(shell, /<nav/u);
  assert.match(shell, /<main/u);
  assert.match(recordingPage, /requireRecordingPage/u);
  assert.match(recordingPage, /searchParams\.after/u);
  assert.doesNotMatch(recordingPage, /requireDogPage|isCanonicalUuid/u);
  assert.doesNotMatch(
    `${shell}\n${recordingPage}`,
    /from\(["'`](?:recordings|track_points|collars|configuration)/u,
  );
});

test("proxy refreshes claims and marks private paths without duplicating authorization", async () => {
  const source = await readFile(
    new URL("../supabase/proxy.ts", import.meta.url),
    "utf8",
  );
  const refreshIndex = source.indexOf("await supabase.auth.getClaims()");
  const cacheIndex = source.indexOf("applyPrivateResponseHeaders(response)");

  assert.ok(refreshIndex >= 0);
  assert.ok(cacheIndex > refreshIndex);
  assert.match(source, /isPrivatePortalPath\(pathname\)/u);
  assert.doesNotMatch(source, /supabase\.auth\.getUser\(\)/u);
  assert.doesNotMatch(source, /redirect\(|notFound\(/u);
});

test("login round-trip sanitizes the return path on page and action", async () => {
  const action = await readFile(
    new URL("../../app/auth/actions.ts", import.meta.url),
    "utf8",
  );
  const loginPage = await readFile(
    new URL("../../app/login/page.tsx", import.meta.url),
    "utf8",
  );
  const forms = await readFile(
    new URL("../../app/components/auth-forms.tsx", import.meta.url),
    "utf8",
  );

  assert.match(action, /resolveProtectedReturnPath\(formData\.get\("next"\)\)/u);
  assert.match(loginPage, /resolveProtectedReturnPath\(first\(params\.next\)\)/u);
  assert.match(loginPage, /getFreshIdentity\(\)/u);
  assert.doesNotMatch(loginPage, /getVerifiedIdentity/u);
  assert.match(forms, /name="next" type="hidden" value=\{nextPath\}/u);
});

test("M1.4 private path contains no framework or React data cache", async () => {
  const files = [
    "./route-guard.ts",
    "../supabase/identity.ts",
    "../data-access/configuration.ts",
    "../../app/components/brightness-configuration.tsx",
  ];
  const sources = await Promise.all(
    files.map((path) => readFile(new URL(path, import.meta.url), "utf8")),
  );

  assert.doesNotMatch(
    sources.join("\n"),
    /unstable_cache|"use cache"|cacheComponents/u,
  );
});

test("Today leaf uses one composite guard and a server-rendered bounded view", async () => {
  const [page, guard, view] = await Promise.all([
    readFile(
      new URL("../../app/app/[dogId]/today/page.tsx", import.meta.url),
      "utf8",
    ),
    readFile(new URL("./route-guard.ts", import.meta.url), "utf8"),
    readFile(
      new URL("../../app/components/today-snapshot.tsx", import.meta.url),
      "utf8",
    ),
  ]);

  assert.match(page, /await requireTodayPage\(dogId,/u);
  assert.doesNotMatch(page, /requireDogPage|getDogSummary/u);
  assert.match(guard, /getTodaySnapshot\(dogId\)/u);
  assert.match(view, /<time dateTime=/u);
  assert.match(view, /PROCESANDO O DATOS INSUFICIENTES/u);
  assert.match(view, /HORA DE INICIO NO DISPONIBLE/u);
  assert.doesNotMatch(view, /"use client"|setInterval|Realtime|\.from\(|lat_e7|lon_e7|paseo/iu);
  assert.doesNotMatch(view, /role="status"|aria-live/u);
});

test("History leaf uses one composite guard and rejects ambiguous cursor input", async () => {
  const [page, guard, view] = await Promise.all([
    readFile(
      new URL("../../app/app/[dogId]/history/page.tsx", import.meta.url),
      "utf8",
    ),
    readFile(new URL("./route-guard.ts", import.meta.url), "utf8"),
    readFile(
      new URL("../../app/components/history-ledger.tsx", import.meta.url),
      "utf8",
    ),
  ]);

  assert.match(page, /await requireHistoryPage\(/u);
  assert.match(page, /searchParams\.cursor/u);
  assert.doesNotMatch(page, /requireDogPage|getDogSummary|Array\.isArray/u);
  assert.match(guard, /getHistoryPage\(dogId, cursor\)/u);
  assert.match(view, /HORA DE INICIO NO DISPONIBLE/u);
  assert.match(view, /VER MÁS GRABACIONES/u);
  assert.match(view, /ENLACE NO VÁLIDO/u);
  assert.match(view, /recordingAppPath/u);
  assert.match(view, /VER DETALLE DE LA GRABACIÓN/u);
  assert.doesNotMatch(
    view,
    /"use client"|role="status"|aria-live|Realtime|setInterval/iu,
  );
});
