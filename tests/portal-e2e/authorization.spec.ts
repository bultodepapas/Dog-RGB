import { createHash, randomUUID } from "node:crypto";
import { mkdir, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";

import { expect, test, type BrowserContext, type Page } from "@playwright/test";

import {
  AUTHORIZATION_RPCS,
  AUTHORIZATION_TABLES,
  authorizationGraphSnapshot,
  authorizationInvokeRpc,
  authorizationPassword,
  authorizationPasswordLogin,
  authorizationRequestJson,
  authorizationSurfaceInventory,
  deleteAuthorizationViewer,
} from "../../tools/portal-e2e/authorization-fixtures.mjs";

type Account = Readonly<{ id: string; email: string; role: string }>;
type Target = Readonly<{ id: string; name: string; collarId: string; recordingId: string }>;
type Manifest = Readonly<{
  cycle: number;
  accounts: Readonly<{
    ownerA: Account;
    ownerB: Account;
    editor: Account;
    viewer: Account;
  }>;
  dogA: Target;
  dogB: Target;
  brightness: number;
  missing: Readonly<{
    dogId: string;
    collarId: string;
    recordingId: string;
    jobId: string;
  }>;
}>;

type ApiResult = Readonly<{ status: number; payload: unknown }>;
type Identity = "ownerA" | "ownerB" | "editor" | "viewer";

const cycle = Number(process.env.M114_CYCLE);
const apiUrl = process.env.M114_SUPABASE_URL ?? "";
const publishableKey = process.env.M114_PUBLISHABLE_KEY ?? "";
const manifest = JSON.parse(process.env.M114_MANIFEST ?? "null") as Manifest | null;
const artifactDirectory = resolve("output", "playwright", "m114");
const artifactPath = join(artifactDirectory, `cycle-${cycle}.json`);
const UUID_V4_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu;
const EMPTY_GRAPH_COUNTS = Object.freeze({
  profiles: 0,
  dogs: 0,
  memberships: 0,
  collars: 0,
  recordings: 0,
  telemetry_points: 0,
  daily_summaries: 0,
  recording_summaries: 0,
  config_revisions: 0,
  config_heads: 0,
  config_reported: 0,
  claims: 0,
  credentials: 0,
  sync_requests: 0,
  chunks: 0,
  deletion_tombstones: 0,
  deletion_jobs: 0,
  deletion_receipts: 0,
});

if (
  ![1, 2].includes(cycle) ||
  !apiUrl.startsWith("http://127.0.0.1:") ||
  !publishableKey.startsWith("sb_publishable_") ||
  manifest?.cycle !== cycle
) {
  throw new Error("M1.14 received an invalid local fixture environment.");
}

const fixture = manifest;
const password = authorizationPassword(cycle);

function asRecord(value: unknown): Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value)
    ? value as Record<string, unknown>
    : {};
}

function expectPostgrestError(
  result: ApiResult,
  status: number,
  code: string,
  message: string,
): void {
  expect(result).toEqual({
    status,
    payload: { code, details: null, hint: null, message },
  });
}

function expectNotAuthorized(result: ApiResult): void {
  expectPostgrestError(result, 403, "42501", "not_authorized");
}

function expectAuthenticationRequired(result: ApiResult): void {
  expectPostgrestError(result, 403, "28000", "authentication_required");
}

function expectEdgeProblem(result: ApiResult, status: number, code: string): void {
  const payload = asRecord(result.payload);
  expect(result.status).toBe(status);
  expect(Object.keys(payload).sort()).toEqual([
    "code", "detail", "request_id", "status", "title", "type",
  ]);
  expect(payload.status).toBe(status);
  expect(payload.code).toBe(code);
  expect(payload.type).toBe(`urn:dog-rgb:problem:${code}`);
  expect(typeof payload.title).toBe("string");
  expect(typeof payload.detail).toBe("string");
}

function expectAnonymousRpcHidden(result: ApiResult): void {
  expectPostgrestError(
    result,
    401,
    "42501",
    "permission denied for schema api",
  );
}

function expectAnonymousEdgeDenied(result: ApiResult): void {
  expect(result).toEqual({
    status: 401,
    payload: { message: "Invalid credentials", code: "INVALID_CREDENTIALS" },
  });
}

function rowsOf(payload: unknown): Array<Record<string, unknown>> {
  expect(Array.isArray(payload)).toBe(true);
  return payload as Array<Record<string, unknown>>;
}

function exactColumn(rows: Array<Record<string, unknown>>, column: string): string[] {
  return rows.map((row) => String(row[column])).sort();
}

function expectExactProjectionScope(
  identity: Identity,
  table: string,
  payload: unknown,
): void {
  const rows = rowsOf(payload);
  const target = identity === "ownerB" ? fixture.dogB : fixture.dogA;
  const account = fixture.accounts[identity];
  const dogAMembers = [
    fixture.accounts.ownerA.id,
    fixture.accounts.editor.id,
    fixture.accounts.viewer.id,
  ].sort();

  switch (table) {
    case "profiles":
      expect(exactColumn(rows, "user_id")).toEqual([account.id]);
      break;
    case "dogs":
      expect(exactColumn(rows, "id")).toEqual([target.id]);
      break;
    case "dog_memberships":
      expect(exactColumn(rows, "dog_id")).toEqual(rows.map(() => target.id));
      expect(exactColumn(rows, "user_id")).toEqual(
        identity === "ownerB" ? [fixture.accounts.ownerB.id] : dogAMembers,
      );
      break;
    case "collars":
      expect(exactColumn(rows, "id")).toEqual([target.collarId]);
      expect(exactColumn(rows, "dog_id")).toEqual([target.id]);
      break;
    case "recordings":
      expect(exactColumn(rows, "id")).toEqual([target.recordingId]);
      expect(exactColumn(rows, "collar_id")).toEqual([target.collarId]);
      break;
    case "telemetry_points":
    case "config_revisions":
    case "config_resource_heads":
    case "config_reported":
      expect(exactColumn(rows, "collar_id")).toEqual(rows.map(() => target.collarId));
      break;
    case "daily_summaries":
      expect(exactColumn(rows, "dog_id")).toEqual(rows.map(() => target.id));
      break;
    case "recording_summaries":
      expect(exactColumn(rows, "recording_id")).toEqual(rows.map(() => target.recordingId));
      break;
    default:
      throw new Error(`M1.14 projection assertion is missing for ${table}.`);
  }
}

function brightnessBody(collarId: string, value: number, baseServerVersion: number) {
  const body = { brightness: value };
  const digest = createHash("sha256").update(JSON.stringify(body), "utf8").digest("hex");
  return {
    p_collar_id: collarId,
    p_resource_key: "brightness",
    p_resource_schema: 1,
    p_mutation_id: randomUUID(),
    p_base_server_version: baseServerVersion,
    p_body: body,
    p_body_sha256: `\\x${digest}`,
  };
}

async function login(context: BrowserContext, account: Account, dogId: string): Promise<Page> {
  const page = await context.newPage();
  await page.goto("/login");
  await page.getByLabel("Correo").fill(account.email);
  await page.getByLabel("Contraseña").fill(password);
  await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
  await expect(page).toHaveURL(/\/onboarding|\/app\//u);
  if (new URL(page.url()).pathname === "/onboarding") {
    await page.goto(`/app/${dogId}/today`);
  }
  await expect(page).toHaveURL(new RegExp(`/app/${dogId}/today$`, "u"));
  return page;
}

async function token(account: Account): Promise<string> {
  return authorizationPasswordLogin(apiUrl, publishableKey, account, password);
}

async function tableRead(table: string, accessToken?: string): Promise<ApiResult> {
  return authorizationRequestJson(`${apiUrl}/rest/v1/${table}?select=*`, {
    publishableKey,
    accessToken,
  });
}

async function filteredRead(
  table: string,
  column: string,
  value: string,
  accessToken: string,
): Promise<ApiResult> {
  const query = new URLSearchParams({ select: "*", [column]: `eq.${value}` });
  return authorizationRequestJson(`${apiUrl}/rest/v1/${table}?${query}`, {
    publishableKey,
    accessToken,
  });
}

async function replaceHiddenValue(page: Page, name: string, value: string): Promise<void> {
  await page.locator(`input[type="hidden"][name="${name}"]`).first().evaluate(
    (element, nextValue) => {
      (element as HTMLInputElement).value = nextValue;
    },
    value,
  );
}

test("identity and object authorization remains bounded across portal and Data API", async ({
  browser,
}) => {
  test.setTimeout(180_000);
  const checkpoints: string[] = [];
  const contexts: BrowserContext[] = [];
  const pageErrors: string[] = [];
  let artifactPhase: "failed" | "passed" = "failed";
  let graphCounts: Record<string, number> = { ...EMPTY_GRAPH_COUNTS };
  const remember = (name: string) => checkpoints.push(name);
  const newContext = async () => {
    const context = await browser.newContext({ serviceWorkers: "block" });
    contexts.push(context);
    context.on("page", (page) => {
      page.on("pageerror", (error) => pageErrors.push(error.name));
    });
    return context;
  };

  try {
    expect(authorizationSurfaceInventory()).toEqual({
      tables: [...AUTHORIZATION_TABLES].sort(),
      rpcs: [...AUTHORIZATION_RPCS].sort(),
      anonRpcs: [],
    });
    remember("exact-surface-inventory");

    const anonymous = await newContext();
    const anonymousPage = await anonymous.newPage();
    for (const path of [
      "/onboarding",
      `/app/${fixture.dogA.id}/today`,
      `/app/${fixture.dogA.id}/history`,
      `/app/${fixture.dogA.id}/configuration`,
      `/app/${fixture.dogA.id}/collars`,
      `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`,
    ]) {
      await anonymousPage.goto(path);
      await expect(anonymousPage).toHaveURL(/\/login\?next=/u);
      await expect(anonymousPage.getByText(fixture.dogA.name)).toHaveCount(0);
    }
    remember("anonymous-portal-denial");

    const ownerBContext = await newContext();
    const ownerBPage = await login(ownerBContext, fixture.accounts.ownerB, fixture.dogB.id);
    const deniedMainTexts = new Set<string>();
    for (const path of [
      `/app/${fixture.dogA.id}/today`,
      `/app/${fixture.dogA.id}/history`,
      `/app/${fixture.dogA.id}/configuration`,
      `/app/${fixture.dogA.id}/collars`,
      `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`,
      `/app/${fixture.dogB.id}/recordings/${fixture.dogA.recordingId}`,
      `/app/${fixture.dogB.id}/recordings/${fixture.missing.recordingId}`,
      `/app/${fixture.missing.dogId}/today`,
      "/app/not-a-uuid/today",
    ]) {
      const response = await ownerBPage.goto(path);
      expect(response?.status()).toBe(404);
      await expect(
        ownerBPage.getByRole("heading", { name: "Este espacio no está disponible." }),
      ).toBeVisible();
      deniedMainTexts.add((await ownerBPage.locator("main").innerText()).trim());
      await expect(ownerBPage.getByText(fixture.dogA.name)).toHaveCount(0);
    }
    expect(deniedMainTexts.size).toBe(1);
    remember("missing-cross-malformed-page-denial");

    const editorContext = await newContext();
    const editorPage = await login(editorContext, fixture.accounts.editor, fixture.dogA.id);
    for (const path of [
      `/app/${fixture.dogA.id}/today`,
      `/app/${fixture.dogA.id}/history`,
      `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`,
    ]) {
      expect((await editorPage.goto(path))?.status()).toBe(200);
      await expect(editorPage.getByText(fixture.dogA.name).first()).toBeVisible();
    }
    expect((await editorPage.goto(`/app/${fixture.dogA.id}/configuration`))?.status()).toBe(200);
    await expect(editorPage.getByText(fixture.dogA.name).first()).toBeVisible();
    await expect(editorPage.getByRole("button", { name: "GUARDAR BRILLO" })).toBeVisible();
    expect((await editorPage.goto(`/app/${fixture.dogA.id}/collars`))?.status()).toBe(200);
    await expect(editorPage.getByText(fixture.dogA.name).first()).toBeVisible();
    await expect(editorPage.getByRole("button", { name: "Generar código" })).toBeVisible();
    await expect(editorPage.getByText(/Revocar acceso de/u)).toHaveCount(0);

    const viewerContext = await newContext();
    const viewerPage = await login(viewerContext, fixture.accounts.viewer, fixture.dogA.id);
    for (const path of [
      `/app/${fixture.dogA.id}/today`,
      `/app/${fixture.dogA.id}/history`,
      `/app/${fixture.dogA.id}/recordings/${fixture.dogA.recordingId}`,
    ]) {
      expect((await viewerPage.goto(path))?.status()).toBe(200);
      await expect(viewerPage.getByText(fixture.dogA.name).first()).toBeVisible();
    }
    expect((await viewerPage.goto(`/app/${fixture.dogA.id}/configuration`))?.status()).toBe(200);
    await expect(viewerPage.getByText(fixture.dogA.name).first()).toBeVisible();
    await expect(viewerPage.getByRole("button", { name: "GUARDAR BRILLO" })).toHaveCount(0);
    expect((await viewerPage.goto(`/app/${fixture.dogA.id}/collars`))?.status()).toBe(200);
    await expect(viewerPage.getByText(fixture.dogA.name).first()).toBeVisible();
    await expect(viewerPage.getByRole("button", { name: "Generar código" })).toHaveCount(0);
    await expect(viewerPage.getByText(/Revocar acceso de/u)).toHaveCount(0);
    remember("editor-viewer-role-ui");

    const tokens = {
      ownerA: await token(fixture.accounts.ownerA),
      ownerB: await token(fixture.accounts.ownerB),
      editor: await token(fixture.accounts.editor),
      viewer: await token(fixture.accounts.viewer),
    };
    const expectedCounts: Record<keyof typeof tokens, Record<string, number>> = {
      ownerA: {
        profiles: 1, dogs: 1, dog_memberships: 3, collars: 1, recordings: 1,
        telemetry_points: 3, daily_summaries: 1, recording_summaries: 1,
        config_revisions: 1, config_resource_heads: 1, config_reported: 1,
      },
      editor: {
        profiles: 1, dogs: 1, dog_memberships: 3, collars: 1, recordings: 1,
        telemetry_points: 3, daily_summaries: 1, recording_summaries: 1,
        config_revisions: 1, config_resource_heads: 1, config_reported: 1,
      },
      viewer: {
        profiles: 1, dogs: 1, dog_memberships: 3, collars: 1, recordings: 1,
        telemetry_points: 3, daily_summaries: 1, recording_summaries: 1,
        config_revisions: 1, config_resource_heads: 1, config_reported: 1,
      },
      ownerB: {
        profiles: 1, dogs: 1, dog_memberships: 1, collars: 1, recordings: 1,
        telemetry_points: 3, daily_summaries: 0, recording_summaries: 0,
        config_revisions: 0, config_resource_heads: 0, config_reported: 0,
      },
    };
    for (const [identity, accessToken] of Object.entries(tokens) as Array<[
      keyof typeof tokens,
      string,
    ]>) {
      for (const table of AUTHORIZATION_TABLES) {
        const result = await tableRead(table, accessToken);
        expect(result.status).toBe(200);
        const rows = rowsOf(result.payload);
        expect(rows.length).toBe(expectedCounts[identity][table]);
        expectExactProjectionScope(identity, table, rows);
      }
    }
    for (const table of AUTHORIZATION_TABLES) {
      expectPostgrestError(
        await tableRead(table),
        401,
        "42501",
        "permission denied for schema api",
      );
    }
    expect(await filteredRead("dogs", "id", fixture.dogA.id, tokens.ownerB)).toEqual({
      status: 200,
      payload: [],
    });
    expect(await filteredRead("dogs", "id", fixture.missing.dogId, tokens.ownerB)).toEqual({
      status: 200,
      payload: [],
    });
    expect(await filteredRead("collars", "id", fixture.dogA.collarId, tokens.ownerB)).toEqual({
      status: 200,
      payload: [],
    });
    expect(await filteredRead("collars", "id", fixture.missing.collarId, tokens.ownerB)).toEqual({
      status: 200,
      payload: [],
    });
    remember("raw-projection-matrix");

    const edgeSuccess = await authorizationRequestJson(
      `${apiUrl}/functions/v1/user-v1-issue-claim`,
      {
        method: "POST",
        publishableKey,
        accessToken: tokens.editor,
        body: {
          protocol_version: 1,
          request_id: randomUUID(),
          dog_id: fixture.dogA.id,
        },
      },
    );
    expect(edgeSuccess.status).toBe(200);
    expect(typeof asRecord(asRecord(edgeSuccess.payload).claim).code).toBe("string");
    const afterEdgeSuccess = authorizationGraphSnapshot(fixture);
    for (const [accessToken, expectedStatus, expectedCode] of [
      [tokens.viewer, 403, "dog_access_denied"],
      [tokens.ownerB, 403, "dog_access_denied"],
    ] as const) {
      const result = await authorizationRequestJson(
        `${apiUrl}/functions/v1/user-v1-issue-claim`,
        {
          method: "POST",
          publishableKey,
          accessToken,
          body: {
            protocol_version: 1,
            request_id: randomUUID(),
            dog_id: fixture.dogA.id,
          },
        },
      );
      expectEdgeProblem(result, expectedStatus, expectedCode);
    }
    expectAnonymousEdgeDenied(await authorizationRequestJson(
      `${apiUrl}/functions/v1/user-v1-issue-claim`,
      {
        method: "POST",
        publishableKey,
        body: {
          protocol_version: 1,
          request_id: randomUUID(),
          dog_id: fixture.dogA.id,
        },
      },
    ));
    expect(authorizationGraphSnapshot(fixture)).toEqual(afterEdgeSuccess);
    remember("claim-edge-role-and-zero-effect-matrix");

    const editorMutation = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.editor,
      "mutate_config_resource_v1",
      brightnessBody(fixture.dogA.collarId, fixture.brightness + 1, 1),
    );
    expect(editorMutation.status).toBe(200);
    expect(Number(asRecord(editorMutation.payload).server_version)).toBe(2);

    const createSuccess = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerA,
      "create_dog_v1",
      { p_name: `M114 deletion probe ${cycle}`, p_timezone: "America/Bogota" },
    );
    expect(createSuccess.status).toBe(200);
    expect(typeof createSuccess.payload).toBe("string");
    const deletionDogId = createSuccess.payload as string;
    expect(deletionDogId).toMatch(UUID_V4_PATTERN);
    const deletionRequest = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerA,
      "request_dog_deletion_v1",
      {
        p_dog_id: deletionDogId,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      },
    );
    expect(deletionRequest.status).toBe(200);
    const deletionJobId = String(asRecord(deletionRequest.payload).job_id);
    expect(deletionJobId).toMatch(UUID_V4_PATTERN);
    expect(asRecord(deletionRequest.payload).status).toBe("pending");
    const deletionStatus = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerA,
      "get_deletion_job_v1",
      { p_job_id: deletionJobId },
    );
    expect(deletionStatus.status).toBe(200);
    expect(asRecord(deletionStatus.payload).job_id).toBe(deletionJobId);
    expect(asRecord(deletionStatus.payload).status).toBe("pending");
    const beforeDenials = authorizationGraphSnapshot(fixture);

    const actionMessages: Record<string, string[]> = {
      brightness: [],
      claim: [],
      revoke: [],
    };
    for (const collarId of [fixture.dogA.collarId, fixture.missing.collarId]) {
      await ownerBPage.goto(`/app/${fixture.dogB.id}/configuration`);
      await replaceHiddenValue(ownerBPage, "collarId", collarId);
      await ownerBPage.getByLabel("Brillo deseado").fill("77");
      await ownerBPage.getByRole("button", { name: "GUARDAR BRILLO" }).click();
      const result = ownerBPage.locator(".configuration-result");
      await expect(result).toBeVisible();
      actionMessages.brightness.push((await result.innerText()).trim());
    }
    for (const dogId of [fixture.dogA.id, fixture.missing.dogId]) {
      await ownerBPage.goto(`/app/${fixture.dogB.id}/collars`);
      await replaceHiddenValue(ownerBPage, "dogId", dogId);
      await ownerBPage.getByRole("button", { name: "Generar código" }).click();
      const result = ownerBPage.locator(".claim-form .form-message--error");
      await expect(result).toContainText("No pudimos generar el código. Inténtalo de nuevo.");
      actionMessages.claim.push((await result.innerText()).trim());
    }
    for (const collarId of [fixture.dogA.collarId, fixture.missing.collarId]) {
      await ownerBPage.goto(`/app/${fixture.dogB.id}/collars`);
      await ownerBPage.getByRole("button", { name: "REVISAR REVOCACIÓN" }).click();
      await ownerBPage.getByRole("checkbox").check();
      await replaceHiddenValue(ownerBPage, "collarId", collarId);
      await ownerBPage.getByRole("button", { name: "REVOCAR ACCESO EN LA NUBE" }).click();
      const result = ownerBPage.locator(".collar-result--error");
      await expect(result).toContainText("El collar seleccionado cambió. Recarga antes de continuar.");
      actionMessages.revoke.push((await result.innerText()).trim());
    }
    expect(new Set(actionMessages.brightness).size).toBe(1);
    expect(new Set(actionMessages.claim).size).toBe(1);
    expect(new Set(actionMessages.revoke).size).toBe(1);

    const crossMutation = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "mutate_config_resource_v1",
      brightnessBody(fixture.dogA.collarId, fixture.brightness + 2, 2),
    );
    const missingMutation = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "mutate_config_resource_v1",
      brightnessBody(fixture.missing.collarId, fixture.brightness + 2, 2),
    );
    expectNotAuthorized(crossMutation);
    expectNotAuthorized(missingMutation);
    expect(crossMutation).toEqual(missingMutation);
    expectNotAuthorized(await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.viewer,
      "mutate_config_resource_v1",
      brightnessBody(fixture.dogA.collarId, fixture.brightness + 2, 2),
    ));

    const crossRevoke = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "revoke_collar_v1",
      { p_collar_id: fixture.dogA.collarId },
    );
    const missingRevoke = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "revoke_collar_v1",
      { p_collar_id: fixture.missing.collarId },
    );
    expectNotAuthorized(crossRevoke);
    expectNotAuthorized(missingRevoke);
    expect(crossRevoke).toEqual(missingRevoke);
    for (const accessToken of [tokens.editor, tokens.viewer]) {
      expectNotAuthorized(await authorizationInvokeRpc(
        apiUrl,
        publishableKey,
        accessToken,
        "revoke_collar_v1",
        { p_collar_id: fixture.dogA.collarId },
      ));
    }

    const crossDeletion = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "request_dog_deletion_v1",
      {
        p_dog_id: fixture.dogA.id,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      },
    );
    const missingDeletion = await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.ownerB,
      "request_dog_deletion_v1",
      {
        p_dog_id: fixture.missing.dogId,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      },
    );
    expectNotAuthorized(crossDeletion);
    expectNotAuthorized(missingDeletion);
    expect(crossDeletion).toEqual(missingDeletion);
    expectNotAuthorized(await authorizationInvokeRpc(
      apiUrl,
      publishableKey,
      tokens.editor,
      "request_dog_deletion_v1",
      {
        p_dog_id: fixture.dogA.id,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      },
    ));

    const existingJobDenial = await authorizationInvokeRpc(
      apiUrl, publishableKey, tokens.ownerB, "get_deletion_job_v1", { p_job_id: deletionJobId },
    );
    const missingJobDenial = await authorizationInvokeRpc(
      apiUrl, publishableKey, tokens.ownerB, "get_deletion_job_v1",
      { p_job_id: fixture.missing.jobId },
    );
    expectNotAuthorized(existingJobDenial);
    expectNotAuthorized(missingJobDenial);
    expect(existingJobDenial).toEqual(missingJobDenial);

    const anonymousRpcCalls = [
      authorizationInvokeRpc(apiUrl, publishableKey, undefined, "create_dog_v1", {
        p_name: "M114 anonymous denial", p_timezone: "America/Bogota",
      }),
      authorizationInvokeRpc(
        apiUrl, publishableKey, undefined, "mutate_config_resource_v1",
        brightnessBody(fixture.dogA.collarId, fixture.brightness + 2, 2),
      ),
      authorizationInvokeRpc(apiUrl, publishableKey, undefined, "revoke_collar_v1", {
        p_collar_id: fixture.dogA.collarId,
      }),
      authorizationInvokeRpc(apiUrl, publishableKey, undefined, "request_dog_deletion_v1", {
        p_dog_id: fixture.dogA.id,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      }),
      authorizationInvokeRpc(apiUrl, publishableKey, undefined, "get_deletion_job_v1", {
        p_job_id: deletionJobId,
      }),
    ];
    for (const result of await Promise.all(anonymousRpcCalls)) expectAnonymousRpcHidden(result);
    expect(authorizationGraphSnapshot(fixture)).toEqual(beforeDenials);
    remember("actions-and-five-rpc-role-zero-effect-matrix");

    const staleContext = await newContext();
    const staleToday = await login(staleContext, fixture.accounts.viewer, fixture.dogA.id);
    const staleOnboarding = await staleContext.newPage();
    await staleOnboarding.goto("/onboarding");
    await expect(staleOnboarding.getByLabel("Nombre de tu perro")).toBeVisible();
    const jwtClaims = JSON.parse(
      Buffer.from(tokens.viewer.split(".")[1], "base64url").toString("utf8"),
    ) as Record<string, unknown>;
    expect(jwtClaims.sub).toBe(fixture.accounts.viewer.id);
    expect(typeof jwtClaims.session_id).toBe("string");
    expect(Number(jwtClaims.exp) * 1_000).toBeGreaterThan(Date.now());

    expect(deleteAuthorizationViewer(fixture)).toEqual({
      deletedUser: true,
      authStateRows: 0,
      applicationRows: 0,
    });
    const afterDeletionBaseline = authorizationGraphSnapshot(fixture);
    await staleToday.reload();
    await expect(staleToday).toHaveURL(/\/login\?next=/u);
    await staleOnboarding.getByLabel("Nombre de tu perro").fill("M114 forbidden stale dog");
    await staleOnboarding.getByRole("button", { name: "Crear perfil" }).click();
    await expect(staleOnboarding).toHaveURL(/\/login|\/onboarding/u);
    if (new URL(staleOnboarding.url()).pathname === "/onboarding") {
      await expect(
        staleOnboarding.getByText("No pudimos crear el perfil. Inténtalo de nuevo."),
      ).toBeVisible();
    }

    for (const table of AUTHORIZATION_TABLES) {
      const result = await tableRead(table, tokens.viewer);
      expect(result.status).toBe(200);
      expect(result.payload).toEqual([]);
    }
    const staleCalls = await Promise.all([
      authorizationInvokeRpc(apiUrl, publishableKey, tokens.viewer, "create_dog_v1", {
        p_name: "M114 forbidden stale RPC",
        p_timezone: "America/Bogota",
      }),
      authorizationInvokeRpc(
        apiUrl,
        publishableKey,
        tokens.viewer,
        "mutate_config_resource_v1",
        brightnessBody(fixture.dogA.collarId, fixture.brightness + 3, 2),
      ),
      authorizationInvokeRpc(apiUrl, publishableKey, tokens.viewer, "revoke_collar_v1", {
        p_collar_id: fixture.dogA.collarId,
      }),
      authorizationInvokeRpc(apiUrl, publishableKey, tokens.viewer, "request_dog_deletion_v1", {
        p_dog_id: fixture.dogA.id,
        p_request_id: randomUUID(),
        p_confirmation_version: "dog-delete-v1",
      }),
      authorizationInvokeRpc(apiUrl, publishableKey, tokens.viewer, "get_deletion_job_v1", {
        p_job_id: fixture.missing.jobId,
      }),
    ]);
    expectAuthenticationRequired(staleCalls[0]);
    expectNotAuthorized(staleCalls[1]);
    expectNotAuthorized(staleCalls[2]);
    expectAuthenticationRequired(staleCalls[3]);
    expectAuthenticationRequired(staleCalls[4]);
    expectEdgeProblem(await authorizationRequestJson(
      `${apiUrl}/functions/v1/user-v1-issue-claim`,
      {
        method: "POST",
        publishableKey,
        accessToken: tokens.viewer,
        body: {
          protocol_version: 1,
          request_id: randomUUID(),
          dog_id: fixture.dogA.id,
        },
      },
    ), 401, "authentication_required");
    expect(authorizationGraphSnapshot(fixture)).toEqual(afterDeletionBaseline);
    remember("deleted-auth-stale-session-denial");

    expect(pageErrors).toEqual([]);
    artifactPhase = "passed";
    graphCounts = afterDeletionBaseline.counts as Record<string, number>;
  } finally {
    await mkdir(artifactDirectory, { recursive: true });
    await writeFile(artifactPath, `${JSON.stringify({
      schemaVersion: 1,
      phase: artifactPhase,
      cycle,
      surface: { tables: AUTHORIZATION_TABLES.length, rpcs: AUTHORIZATION_RPCS.length },
      graphCounts,
      checkpoints,
    }, null, 2)}\n`, "utf8");
    await Promise.all(contexts.map((context) => context.close().catch(() => undefined)));
  }
});
