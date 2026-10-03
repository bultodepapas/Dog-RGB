import assert from "node:assert/strict";
import { readFile, mkdir, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { cpus, platform, arch } from "node:os";
import { gzipSync } from "node:zlib";
import { join } from "node:path";
import { authorizationPassword, setQualitySelectionMembership } from "./authorization-fixtures.mjs";

const require = createRequire(import.meta.url);
const axeSource = await readFile(require.resolve("axe-core/axe.min.js"), "utf8");

async function login(page, fixture) {
  await page.goto("/login");
  await page.getByLabel("Correo").fill(fixture.accounts.ownerA.email);
  await page.getByLabel("Contraseña").fill(authorizationPassword(fixture.cycle));
  await page.getByRole("button", { name: "INICIAR SESIÓN" }).click();
  await page.waitForURL(`**/app/${fixture.dogA.id}/today`);
}

function routes(fixture) {
  const base = `/app/${fixture.dogA.id}`;
  return [
    ["today", `${base}/today`], ["history", `${base}/history`],
    ["detail", `${base}/recordings/${fixture.dogA.recordingId}`],
    ["configuration", `${base}/configuration`], ["collars", `${base}/collars`],
    ["data", `${base}/data`], ["account", "/account"],
    ["deletion-receipt", "/account/deletion-receipt"],
  ];
}

export async function runPortalQuality({ browser, fixture, portalUrl, outputDirectory, performance = true }) {
  const context = await browser.newContext({ baseURL: portalUrl, serviceWorkers: "block", reducedMotion: "reduce" });
  const page = await context.newPage();
  const report = { schemaVersion: 1, browser: browser.version(), platform: `${platform()}-${arch()}`,
    cpu: cpus()[0]?.model ?? "unknown", a11y: [], performance: [], manualReview: "pending" };
  let selectionMembership = false;
  try {
    // Public and empty auth states are independent of fixture credentials.
    for (const [name, path] of [["home", "/"], ["login", "/login"], ["signup", "/signup"], ["recovery", "/forgot-password"], ["privacy", "/privacy"]]) {
      await page.goto(path);
      await inspectAccessibility(page, name, report);
    }
    await login(page, fixture);
    const storage = await context.storageState(); // memory only; never an artifact
    setQualitySelectionMembership(fixture, true);
    selectionMembership = true;
    const privateRoutes = [["selection", "/onboarding"], ...routes(fixture)];
    for (const [name, path] of privateRoutes) {
      await page.goto(path);
      if (name === "deletion-receipt") {
        await page.getByText("No pudimos confirmar la eliminación con la sesión disponible.", { exact: false }).waitFor();
      }
      await inspectAccessibility(page, name, report);
    }
    await page.goto(`/app/${fixture.dogA.id}/collars`);
    await page.getByRole("button", { name: "REVISAR REVOCACIÓN" }).click();
    assert.equal(await page.locator(".collar-confirmation h3").evaluate(node => node === document.activeElement), true, "confirmation heading receives focus");
    await inspectAccessibility(page, "revoke-confirmation", report);
    await page.keyboard.press("Escape");
    assert.equal(await page.getByRole("button", { name: "REVISAR REVOCACIÓN" }).evaluate(node => node === document.activeElement), true, "cancel returns focus");

    if (performance) {
      for (const profile of ["desktop", "mobile"]) {
        for (const [name, path] of [["login", "/login"], ["privacy", "/privacy"], ...privateRoutes]) {
          console.log(`Portal quality: ${profile}/${name}, five cold and five warm samples...`);
          const samples = [];
          for (let sample = 0; sample < 5; sample++) {
            const sampleContext = await browser.newContext({ baseURL: portalUrl, storageState: ["login", "privacy"].includes(name) ? undefined : storage,
              serviceWorkers: "block", viewport: profile === "mobile" ? { width: 428, height: 844 } : { width: 1280, height: 800 } });
            try {
              const samplePage = await sampleContext.newPage();
              const cdp = await sampleContext.newCDPSession(samplePage);
              await cdp.send("Network.enable");
              if (profile === "mobile") {
                await cdp.send("Emulation.setCPUThrottlingRate", { rate: 4 });
                await cdp.send("Network.emulateNetworkConditions", { offline: false, latency: 150, downloadThroughput: 1_600_000 / 8, uploadThroughput: 750_000 / 8 });
              }
              await samplePage.addInitScript(() => {
                window.__portalMetrics = { lcp: 0, cls: 0, longTasks: 0 };
                new PerformanceObserver(list => { for (const e of list.getEntries()) window.__portalMetrics.lcp = e.startTime; }).observe({ type: "largest-contentful-paint", buffered: true });
                new PerformanceObserver(list => { for (const e of list.getEntries()) if (!e.hadRecentInput) window.__portalMetrics.cls += e.value; }).observe({ type: "layout-shift", buffered: true });
                new PerformanceObserver(list => { window.__portalMetrics.longTasks += list.getEntries().length; }).observe({ type: "longtask", buffered: true });
              });
              for (const cache of ["cold", "warm"]) {
                const js = new Map();
                const pending = [];
                const collect = response => {
                  if (/\/_next\/static\/.*\.js(?:\?|$)/u.test(response.url())) {
                    pending.push(response.body().then(body => js.set(response.url(), gzipSync(body).length)));
                  }
                };
                samplePage.on("response", collect);
                if (cache === "cold") await cdp.send("Network.clearBrowserCache");
                await cdp.send("Network.setCacheDisabled", { cacheDisabled: false });
                await samplePage.goto(path, { waitUntil: "networkidle" });
                await samplePage.waitForTimeout(500);
                await Promise.all(pending);
                samplePage.off("response", collect);
                const metrics = await samplePage.evaluate(() => {
                  const nav = performance.getEntriesByType("navigation")[0];
                  const resources = performance.getEntriesByType("resource");
                  return { ...window.__portalMetrics, ttfb: nav.responseStart - nav.requestStart,
                    requests: resources.length + 1, bytes: resources.reduce((n, e) => n + e.transferSize, nav.transferSize) };
                });
                samples.push({ cache, ...metrics, jsGzip: [...js.values()].reduce((a, b) => a + b, 0) });
              }
            } finally { await sampleContext.close(); }
          }
          report.performance.push({ name, profile, samples, groups: ["cold", "warm"].map(cache => {
            const group = samples.filter(row => row.cache === cache);
            const percentile = (field, fraction) => [...group].map(row => row[field]).sort((a,b) => a-b)[Math.ceil(group.length * fraction) - 1];
            return { cache, medianLcp: percentile("lcp", .5), p95Lcp: percentile("lcp", .95), medianTtfb: percentile("ttfb", .5), p95Ttfb: percentile("ttfb", .95),
              maxCls: Math.max(...group.map(row => row.cls)), maxJsGzip: Math.max(...group.map(row => row.jsGzip)) };
          }) });
        }
      }
    }
  } finally {
    if (selectionMembership) setQualitySelectionMembership(fixture, false);
    await context.close();
    await mkdir(outputDirectory, { recursive: true });
    await writeFile(join(outputDirectory, "quality.json"), `${JSON.stringify(report, null, 2)}\n`);
  }
  assert.equal(report.a11y.every(row => row.violations.length === 0 && row.overflow === false && row.smallTargets === 0 && row.zoomOverflow !== true), true, "Portal accessibility findings require remediation (see sanitized quality.json)");
  assert.equal(report.performance.every(route => route.groups.every(group => group.medianLcp > 0 && group.medianLcp <= 2500 && group.medianTtfb <= 800 && group.maxCls <= .1 && group.maxJsGzip <= 180 * 1024)), true, "Portal performance budgets exceeded (see quality.json)");
  return report;
}

export async function inspectAccessibility(page, name, report) {
  for (const width of [320, 428, 768, 1280]) {
    await page.setViewportSize({ width, height: 900 });
    await page.evaluate(() => document.fonts.ready);
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1);
    const smallTargets = await page.evaluate(() => [...document.querySelectorAll("a,button,input:not([type=hidden])")]
      .filter(node => node.getClientRects().length > 0 && getComputedStyle(node).visibility !== "hidden")
      .filter(node => {
        const target = node.matches("input[type=checkbox]") ? node.closest("label") ?? node : node;
        const rect = target.getBoundingClientRect();
        return rect.width < 43.9 || rect.height < 43.9;
      }).length);
    await page.addScriptTag({ content: axeSource });
    const violations = await page.evaluate(async () => {
      const result = await window.axe.run(document, { runOnly: { type: "tag", values: ["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22aa"] } });
      // No DOM, labels, HTML, paths, URLs or fixture values enter the report.
      return result.violations.map(item => ({ id: item.id, impact: item.impact, count: item.nodes.length }));
    });
    let zoomOverflow = null;
    if (width === 1280) {
      zoomOverflow = await page.evaluate(() => {
        document.documentElement.style.zoom = "2";
        const overflows = document.documentElement.scrollWidth > innerWidth + 1;
        document.documentElement.style.zoom = "";
        return overflows;
      });
    }
    report.a11y.push({ name, width, overflow, smallTargets, zoomOverflow, violations });
  }
}

export async function runWebkitSmoke({ browserType, fixture, portalUrl, outputDirectory }) {
  const browser = await browserType.launch();
  const report = { schemaVersion: 1, browser: "webkit", version: browser.version(), viewport: { width: 390, height: 844 }, checks: [], passed: false };
  try {
    const context = await browser.newContext({ baseURL: portalUrl, viewport: report.viewport, isMobile: true, hasTouch: true, serviceWorkers: "block" });
    const page = await context.newPage();
    await login(page, fixture);
    report.checks.push("returning-login");
    for (const [name, path] of routes(fixture)) {
      await page.goto(path);
      if (name === "deletion-receipt") {
        await page.getByText("No pudimos confirmar la eliminación con la sesión disponible.", { exact: false }).waitFor();
      }
      assert.equal(await page.locator("h1").count(), 1, `WebKit ${name} missing heading`);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, `WebKit ${name} overflows`);
      report.checks.push(name);
    }
    await page.goto(`/app/${fixture.dogA.id}/configuration`);
    await page.getByLabel("Brillo deseado").fill("120");
    await page.getByRole("button", { name: "GUARDAR BRILLO" }).click();
    await page.locator(".configuration-result").waitFor();
    report.checks.push("brightness-save");
    await page.goto(`/app/${fixture.dogA.id}/collars`);
    await page.getByRole("button", { name: "REVISAR REVOCACIÓN" }).click();
    await page.locator(".collar-revoke-form input[type=checkbox]").check();
    await page.getByRole("button", { name: "REVOCAR ACCESO EN LA NUBE" }).click();
    await page.locator(".collar-result--success").waitFor();
    report.checks.push("revoke");
    report.passed = true;
  } finally {
    await browser.close();
    await mkdir(outputDirectory, { recursive: true });
    await writeFile(join(outputDirectory, "webkit.json"), `${JSON.stringify(report, null, 2)}\n`);
  }
}
