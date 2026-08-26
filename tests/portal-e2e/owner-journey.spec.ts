import { expect, test } from "@playwright/test";
import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";

import {
  checkpointAppliedBrightness,
  checkpointConfirmed,
  checkpointDesiredBrightness,
  checkpointDog,
  checkpointIssuedClaim,
  checkpointPairing,
  checkpointRevoked,
  checkpointSignup,
  checkpointUpload,
} from "../../tools/portal-e2e/checkpoints.mjs";
import {
  clearMailbox,
  takeConfirmationLink,
} from "../../tools/portal-e2e/mailpit.mjs";
import { createPairOnlySimulator } from "../../tools/device-simulator/pair-only.mjs";

const UUID_PATTERN =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/u;
const CLAIM_CODE_PATTERN = /^[0-9A-HJKMNP-TV-Z]{16}$/u;
const cycle = Number(process.env.M113_CYCLE);
const fixtureCycle = cycle === 1 || cycle === 2 ? cycle : 0;
const email = `m113-owner-${fixtureCycle}@example.test`;
const dogName = `M113 Dog ${fixtureCycle}`;
const password = ["M113", "local", "owner", String(fixtureCycle), "24!"].join("-");
const brightness = 160 + fixtureCycle;
const artifactDirectory = resolve("output", "playwright", "m113");
const artifactPath = join(artifactDirectory, `cycle-${cycle}.json`);

async function artifactContains(
  directory: string,
  containsPrivateMaterial: (value: string | Buffer) => boolean,
  confirmationMaterial: readonly string[],
): Promise<boolean> {
  const entries = await readdir(directory, { withFileTypes: true }).catch(() => []);
  for (const entry of entries) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      if (await artifactContains(path, containsPrivateMaterial, confirmationMaterial)) return true;
    } else if (entry.isFile()) {
      const content = await readFile(path);
      if (containsPrivateMaterial(content)) return true;
      if (confirmationMaterial.some((value) => content.includes(Buffer.from(value)))) return true;
      if (content.includes(Buffer.from(password))) return true;
    }
  }
  return false;
}

test("owner journey reaches exact collar convergence and protected logout", async ({ page }) => {
  if (cycle !== 1 && cycle !== 2) {
    throw new Error("M1.13 must run through the clean local orchestrator.");
  }
  const checkpoints: string[] = [];
  let phase = "readiness";
  let simulator: Awaited<ReturnType<typeof createPairOnlySimulator>> | null = null;
  let confirmationMaterial: readonly string[] = [];
  let browserErrors = 0;
  let externalRequests = 0;
  page.on("pageerror", () => { browserErrors += 1; });
  page.on("console", (message) => {
    if (message.type() === "error") browserErrors += 1;
  });
  page.on("request", (request) => {
    const origin = new URL(request.url()).origin;
    if (!["http://127.0.0.1:3000", process.env.M113_SUPABASE_URL].includes(origin)) {
      externalRequests += 1;
    }
  });

  const checkpoint = (name: string) => {
    checkpoints.push(name);
    phase = name;
  };

  try {
    await test.step("signup and exact pending identity", async () => {
      phase = "signup";
      await page.goto("/signup");
      await page.getByLabel("Correo").fill(email);
      await page.getByLabel("Contraseña", { exact: true }).fill(password);
      await page.getByLabel("Repite la contraseña").fill(password);
      await page.getByRole("button", { name: "CREAR CUENTA" }).click();
      await expect(page.getByRole("status")).toContainText("recibirás un enlace de confirmación");
      checkpointSignup(email);
      checkpoint("signup-persisted");
    });

    await test.step("Mailpit confirmation and explicit password login", async () => {
      phase = "confirmation";
      const confirmation = await takeConfirmationLink(email);
      confirmationMaterial = [confirmation.url, confirmation.tokenHash];
      await page.goto(confirmation.url);
      await expect(page).toHaveURL(/\/\?auth=confirmed$/u);
      await expect(page.getByRole("status")).toHaveText("Correo confirmado y sesión verificada.");
      checkpointConfirmed(email);
      checkpoint("confirmation-persisted");

      await page.getByRole("button", { name: "CERRAR ESTA SESIÓN" }).click();
      await expect(page).toHaveURL(/\/login\?logged_out=1$/u);
      await page.getByLabel("Correo").fill(email);
      await page.getByLabel("Contraseña").fill(password);
      await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
      await expect(page).toHaveURL(/\/onboarding$/u);
      checkpoint("explicit-login");
    });

    let dogId = "";
    await test.step("dog creation and owner membership", async () => {
      phase = "dog-creation";
      await page.getByLabel("Nombre de tu perro").fill(dogName);
      await page.getByRole("button", { name: "Crear perfil" }).click();
      await expect(page).toHaveURL(/\/app\/[0-9a-f-]{36}\/today$/u);
      const match = new URL(page.url()).pathname.match(/^\/app\/([^/]+)\/today$/u);
      if (!match || !UUID_PATTERN.test(match[1])) throw new Error("M1.13 dog redirect was not canonical.");
      dogId = match[1];
      await expect(page.getByRole("heading", { name: "Resumen de hoy." })).toBeVisible();
      checkpointDog({ email, dogId, dogName });
      checkpoint("dog-persisted");
    });

    let collarId = "";
    let recordingId = "";
    await test.step("one-time claim, pairing, and one recording upload", async () => {
      phase = "claim";
      await page.getByRole("link", { name: "Collares" }).click();
      await expect(page.getByRole("heading", { name: "Estado del collar." })).toBeVisible();
      await page.getByRole("button", { name: "Generar código" }).click();
      const claimCode = (await page.locator("code.claim-code").textContent())?.trim() ?? "";
      if (!CLAIM_CODE_PATTERN.test(claimCode)) throw new Error("M1.13 claim code shape was invalid.");
      checkpointIssuedClaim(dogId);
      checkpoint("claim-issued");
      await page.reload();
      await expect(page.locator("code.claim-code")).toHaveCount(0);

      simulator = await createPairOnlySimulator({
        claimCode,
        apiUrl: process.env.M113_SUPABASE_URL,
        expectedDogId: dogId,
      });
      const paired = await simulator.attempt();
      if (!paired.ok) throw new Error("M1.13 simulator pairing failed.");
      collarId = paired.pairing.collarId;
      checkpointPairing({
        dogId,
        collarId,
        deviceId: paired.pairing.deviceId,
      });
      checkpoint("pairing-persisted");

      const upload = await simulator.uploadJourneyRecording();
      const persisted = checkpointUpload(upload);
      recordingId = persisted.recordingId;
      checkpoint("upload-persisted");
    });

    await test.step("Today, History, and exact recording detail", async () => {
      phase = "read-surfaces";
      await page.getByRole("link", { name: "Hoy" }).click();
      await expect(page.getByRole("heading", { name: "Resumen de hoy." })).toBeVisible();
      await expect(page.getByText(dogName, { exact: true }).first()).toBeVisible();
      await expect(page.getByText("ACTUALIZADO EN LAS ÚLTIMAS 24 H")).toBeVisible();
      await expect(page.getByText("3", { exact: true }).last()).toBeVisible();
      checkpoint("today-projection");

      await page.getByRole("link", { name: "Historial" }).click();
      await expect(page.getByRole("heading", { name: "Historial de grabaciones." })).toBeVisible();
      const detailLink = page.getByRole("link", { name: /Ver detalle de la grabación/u });
      await expect(detailLink).toHaveCount(1);
      await expect(detailLink).toHaveAttribute("href", `/app/${dogId}/recordings/${recordingId}`);
      await detailLink.click();
      await expect(page).toHaveURL(new RegExp(`/app/${dogId}/recordings/${recordingId}$`, "u"));
      await expect(page.getByRole("heading", { name: "Detalle de la grabación." })).toBeVisible();
      for (const sequence of ["0", "1", "2"]) {
        await expect(page.getByRole("rowheader", { name: sequence, exact: true })).toBeVisible();
      }
      await expect(page.getByRole("cell", { name: "46.8123456", exact: true })).toBeVisible();
      await expect(page.getByRole("cell", { name: "-74.0123456", exact: true })).toBeVisible();
      checkpoint("recording-projection");
    });

    let desired: Readonly<{
      brightness: number;
      serverVersion: number;
      bodySha256Hex: string;
    }>;
    await test.step("web desired state and exact simulator convergence", async () => {
      phase = "brightness";
      await page.getByRole("link", { name: "Configuración" }).click();
      await expect(page.getByRole("heading", { name: "Brillo del collar." })).toBeVisible();
      await page.getByLabel("Brillo deseado").fill(String(brightness));
      await page.getByRole("button", { name: "GUARDAR BRILLO" }).click();
      await expect(page.getByRole("status")).toContainText("GUARDADO EN LA NUBE");
      desired = checkpointDesiredBrightness({ collarId, email, brightness });
      checkpoint("desired-persisted");

      if (!simulator) throw new Error("M1.13 simulator session was unavailable.");
      phase = "brightness-simulator-convergence";
      const converged = await simulator.convergeBrightness(brightness);
      if (
        converged.serverVersion !== desired.serverVersion ||
        converged.bodySha256Hex !== desired.bodySha256Hex
      ) {
        throw new Error("M1.13 simulator desired checkpoint did not match persistence.");
      }
      phase = "brightness-reported-persistence";
      checkpointAppliedBrightness({ collarId, ...desired });
      phase = "brightness-applied-ui";
      await page.reload();
      await expect(page.getByText("APLICADO EN EL COLLAR", { exact: true })).toBeVisible();
      checkpoint("brightness-applied");
    });

    const protectedUrl = `/app/${dogId}/collars`;
    await test.step("collar diagnostics and exact-collar revoke", async () => {
      phase = "revocation";
      await page.getByRole("link", { name: "Collares" }).click();
      await expect(page.getByText("COLA VACÍA AL REPORTAR", { exact: true })).toBeVisible();
      await expect(page.getByText("device-v1", { exact: true })).toBeVisible();
      await expect(page.getByText("SÍ", { exact: true }).first()).toBeVisible();
      await page.getByRole("button", { name: "REVISAR REVOCACIÓN" }).click();
      const target = await page.locator('input[name="collarId"]').inputValue();
      if (target !== collarId) throw new Error("M1.13 revoke target did not match the shown collar.");
      await page.getByRole("checkbox").check();
      await page.getByRole("button", { name: "REVOCAR ACCESO EN LA NUBE" }).click();
      await expect(page.getByRole("status")).toContainText("COLLAR REVOCADO EN LA NUBE");
      checkpointRevoked({ collarId, recordingId });
      if (!simulator) throw new Error("M1.13 simulator session was unavailable.");
      await simulator.assertRevoked();
      checkpoint("revoke-persisted");
    });

    await test.step("logout denies browser back and protected refresh", async () => {
      phase = "logout";
      await page.getByRole("button", { name: "CERRAR SESIÓN" }).click();
      await expect(page).toHaveURL(/\/login\?logged_out=1$/u);
      await expect(page.getByRole("status")).toContainText("sesión de este dispositivo se cerró");
      if ((await page.context().cookies()).some((cookie) => cookie.name.startsWith("sb-"))) {
        throw new Error("M1.13 logout retained an Auth cookie.");
      }
      await page.goBack();
      await page.reload();
      await expect(page).toHaveURL(/\/login\?next=/u);
      await expect(page.getByText(dogName, { exact: true })).toHaveCount(0);
      await page.goto(protectedUrl);
      await expect(page).toHaveURL(/\/login\?next=/u);
      await expect(page.getByRole("heading", { name: "Estado del collar." })).toHaveCount(0);
      checkpoint("protected-logout");
    });

    if (browserErrors !== 0 || externalRequests !== 0) {
      throw new Error("M1.13 browser runtime boundary was not clean.");
    }
    await mkdir(artifactDirectory, { recursive: true });
    await writeFile(artifactPath, `${JSON.stringify({
      milestone: "M1.13",
      cycle,
      status: "passed",
      checkpoints,
    }, null, 2)}\n`, "utf8");
    if (simulator && await artifactContains(
      artifactDirectory,
      simulator.artifactContainsPrivateMaterial,
      confirmationMaterial,
    )) {
      throw new Error("M1.13 retained private material in a test artifact.");
    }
  } catch {
    await mkdir(artifactDirectory, { recursive: true });
    await writeFile(artifactPath, `${JSON.stringify({
      milestone: "M1.13",
      cycle,
      status: "failed",
      failedPhase: phase,
      completedCheckpoints: checkpoints,
    }, null, 2)}\n`, "utf8");
    throw new Error(`M1.13 owner journey failed during ${phase}.`);
  } finally {
    await clearMailbox().catch(() => undefined);
  }
});
