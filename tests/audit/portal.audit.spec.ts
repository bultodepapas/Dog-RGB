import { expect, test, type Page, type ConsoleMessage } from '@playwright/test';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const outDir = path.join(__dirname, 'evidence');
mkdirSync(outDir, { recursive: true });

const PAGES = [
  { route: '/', name: 'index' },
  { route: '/wifi', name: 'wifi' },
  { route: '/config', name: 'config' },
  { route: '/dev', name: 'dev' },
];

const VIEWPORTS = [
  { name: 'iphone-se', width: 320, height: 568 },
  { name: 'iphone-13-pro-max', width: 428, height: 926 },
  { name: 'tablet', width: 768, height: 1024 },
  { name: 'desktop', width: 1280, height: 900 },
];

type Report = Record<string, unknown>;
const report: Report[] = [];

function record(entry: Report) {
  report.push(entry);
}

/** Minimal API mocking so pages reach a rendered steady state. */
async function mockApis(page: Page) {
  await page.route('**/api/**', async (route) => {
    const url = new URL(route.request().url());
    const p = url.pathname;
    const send = (json: unknown) => route.fulfill({ json });

    if (p === '/api/summary') {
      return send({
        date: 20260811, distance_m: 4820, avg_speed_cmps: 145, max_speed_cmps: 610,
        active_s: 3320, last_update_min: 745, flags: 0b0001,
        current: { start_date: 20260811, start_min: 700, end_date: 20260811, end_min: 745, distance_m: 4820, avg_speed_cmps: 145, max_speed_cmps: 610, active_s: 3320, flags: 1 },
        history: [],
      });
    }
    if (p === '/api/status') {
      return send({
        mode: 'speed',
        wifi: { ap_enabled: true, ap_ssid: 'DOG-RGB', ap_stations: 1, sta_connected: true, sta_connecting: false, wifi_off: false, mdns: 'dogrgb', sta_ip: '192.168.1.42', ap_ip: '192.168.4.1' },
        gps: { fix: true, raw_fix: true, quality_ok: true, speed_usable: true, speed_kph: 5.2, sats: 9, fix_quality: 1, hdop: 1.1 },
        home: { set: true, source: 'manual', distance_m: 120.5 },
        day_mode: { enabled: true, active: false, state: 'night', time_available: true, local_min: 745 },
      });
    }
    if (p === '/api/config' && route.request().method() === 'GET') {
      const effects: Record<string, unknown> = {};
      for (let i = 1; i <= 10; i++) effects[`range${i}`] = { a: 1, b: 2, speed: 128, intensity: 200 };
      return send({
        version: 3, mode: 'speed', fence_max_m: 300,
        led: { brightness: 180 },
        day_mode: { enabled: true, start_min: 480, end_min: 1200, tz_offset_min: 120 },
        gps: { min_fix_quality: 1, min_sats: 5, max_hdop: 3.5, max_gga_age_ms: 3000, min_segment_m: 2.5, hdop_factor: 1.5, max_min_segment_m: 12 },
        speed_ranges_kph: [1, 2, 3, 5, 8, 12, 18, 25, 35],
        effects,
        single: { effect: 1, speed: 120, intensity: 200, rgb: { r: 255, g: 80, b: 0 } },
        wifi: { ap_ssid: 'DOG-RGB', has_ap_pass: true, mdns: 'dogrgb' },
      });
    }
    if (p === '/api/lock') {
      return send(route.request().method() === 'GET' ? { enabled: false } : { status: 'ok' });
    }
    if (p === '/api/home') {
      return send({ home_set: true, home_source: 'manual', home_lat: 40.4168, home_lon: -3.7038, gps_fix: true, current_lat: 40.4175, current_lon: -3.7041, distance_m: 120.5 });
    }
    if (p === '/api/track') {
      return send({ count: 3, open: true, sample_ms: 5000, start_date: 20260811, start_min: 700, end_date: 20260811, end_min: 745, bbox: { min_lat: 40.416, max_lat: 40.418, min_lon: -3.705, max_lon: -3.703 }, points: [[40.4168, -3.7038], [40.4172, -3.7041], [40.4175, -3.7044]] });
    }
    if (p === '/api/dev') {
      return send({
        time: { uptime_ms: 3600000, build: 'Aug 11 2026 10:00:00' },
        system: { free_heap: 180000, config_storage: { slot: 0, generation: 12, save_failures: 0 } },
        wifi: { mode: 'AP+STA', sta_connected: true, sta_connecting: false, ap_enabled: true, ap_stations: 1, wifi_off: false, ap_ssid: 'DOG-RGB', mdns: 'dogrgb', sta_ip: '192.168.1.42', ap_ip: '192.168.4.1', rssi: -58, ap_mac: 'AA:BB:CC:DD:EE:01', sta_mac: 'AA:BB:CC:DD:EE:00', storage: { slot: 0, generation: 3, save_failures: 0 }, diagnostics: {} },
        gps: { fix: true, sats: 9, hdop: 1.1, metrics_storage: {}, session_storage: {}, daily_journal: {} },
        geofence: { set: true, source: 'manual', home_lat: 40.4168, home_lon: -3.7038, storage: {}, distance_m: 120.5, range: 2 },
        led: { mode: 'speed', brightness: 180, range: 2, simple: { rgb: {} }, show: {} },
        day_mode: { enabled: true, active: false, state: 'night' },
      });
    }
    return send({ status: 'ok' });
  });
}

function attachConsole(page: Page, sink: { errors: string[]; warnings: string[] }) {
  page.on('console', (msg: ConsoleMessage) => {
    if (msg.type() === 'error') sink.errors.push(msg.text());
    if (msg.type() === 'warning') sink.warnings.push(msg.text());
  });
  page.on('pageerror', (err) => sink.errors.push(`PAGEERROR: ${err.message}`));
  page.on('requestfailed', (req) => {
    if (!req.url().includes('/api/')) sink.errors.push(`REQFAIL: ${req.url()} ${req.failure()?.errorText}`);
  });
}

test.describe('AP portal audit', () => {
  for (const pg of PAGES) {
    test(`static & a11y :: ${pg.name}`, async ({ page }) => {
      const sink = { errors: [] as string[], warnings: [] as string[] };
      attachConsole(page, sink);
      await mockApis(page);
      await page.goto(pg.route, { waitUntil: 'networkidle' });

      const doc = await page.evaluate(() => {
        const q = (s: string) => Array.from(document.querySelectorAll(s));
        // Inputs lacking a programmatic accessible name.
        const unlabeled = q('input,select,textarea')
          .filter((el) => {
            const e = el as HTMLInputElement;
            if (e.type === 'hidden') return false;
            if (e.id && document.querySelector(`label[for="${CSS.escape(e.id)}"]`)) return false;
            if (e.closest('label')) return false;
            if (e.getAttribute('aria-label') || e.getAttribute('aria-labelledby')) return false;
            return true;
          })
          .map((e) => `${e.tagName.toLowerCase()}#${(e as HTMLInputElement).id || '(no id)'}[name=${(e as HTMLInputElement).name || '-'}]`);

        // <label> elements with neither for= nor a wrapped control.
        const orphanLabels = q('label')
          .filter((l) => !l.getAttribute('for') && !l.querySelector('input,select,textarea'))
          .map((l) => (l.textContent || '').trim().slice(0, 40));

        const headings = q('h1,h2,h3,h4').map((h) => `${h.tagName}: ${(h.textContent || '').trim().slice(0, 40)}`);

        // Interactive controls smaller than the 44x44 CSS px touch target.
        const smallTargets = q('button,a,input[type=checkbox],.swatch')
          .map((el) => {
            // A checkbox wrapped in a label is clickable across the whole
            // label, so that is the target the user actually hits.
            const hit = el.closest('label') ?? el;
            const r = hit.getBoundingClientRect();
            return { tag: el.tagName.toLowerCase(), cls: el.className, w: Math.round(r.width), h: Math.round(r.height), text: (el.textContent || '').trim().slice(0, 24) };
          })
          .filter((t) => t.w > 0 && t.h > 0 && (t.w < 44 || t.h < 44));

        return {
          lang: document.documentElement.getAttribute('lang'),
          title: document.title,
          viewport: (document.querySelector('meta[name=viewport]') as HTMLMetaElement)?.content ?? null,
          charset: document.characterSet,
          hasCsp: !!document.querySelector('meta[http-equiv="Content-Security-Policy"]'),
          h1Count: q('h1').length,
          headings,
          landmarks: { main: q('main').length, nav: q('nav').length, header: q('header').length },
          unlabeled,
          orphanLabels,
          smallTargets: smallTargets.slice(0, 25),
          smallTargetCount: smallTargets.length,
          inlineHandlers: q('[onclick],[onchange],[oninput],[onsubmit]').length,
          imagesNoAlt: q('img:not([alt])').length,
          docBytes: document.documentElement.outerHTML.length,
        };
      });

      // Horizontal overflow across breakpoints.
      const overflow: Record<string, unknown> = {};
      for (const vp of VIEWPORTS) {
        await page.setViewportSize({ width: vp.width, height: vp.height });
        await page.waitForTimeout(120);
        overflow[vp.name] = await page.evaluate(() => {
          const de = document.documentElement;
          const offenders = Array.from(document.querySelectorAll('*'))
            .filter((el) => el.getBoundingClientRect().right > de.clientWidth + 1)
            .map((el) => `${el.tagName.toLowerCase()}.${(el.className || '').toString().split(' ')[0]}`);
          return { scrollW: de.scrollWidth, clientW: de.clientWidth, overflows: de.scrollWidth > de.clientWidth + 1, offenders: Array.from(new Set(offenders)).slice(0, 8) };
        });
      }

      await page.setViewportSize({ width: 428, height: 926 });
      await page.waitForTimeout(150);
      await page.screenshot({ path: path.join(outDir, `${pg.name}-mobile.png`), fullPage: true });
      await page.setViewportSize({ width: 1280, height: 900 });
      await page.waitForTimeout(150);
      await page.screenshot({ path: path.join(outDir, `${pg.name}-desktop.png`), fullPage: true });

      record({ page: pg.name, ...doc, overflow, console: sink });
      expect(doc.title.length, 'page must have a title').toBeGreaterThan(0);
    });
  }

  test('config POST payload shape', async ({ page }) => {
    await mockApis(page);
    let posted: unknown = null;
    await page.route('**/api/config', async (route) => {
      if (route.request().method() === 'POST') {
        posted = route.request().postDataJSON();
        return route.fulfill({ json: { status: 'ok', wifi_restart: false } });
      }
      return route.fallback();
    });
    await page.goto('/config', { waitUntil: 'networkidle' });
    const saveBtn = page.locator('button:has-text("Guardar")').first();
    if (await saveBtn.count()) {
      await saveBtn.click({ force: true }).catch(() => {});
      await page.waitForTimeout(600);
    }
    const keys = posted && typeof posted === 'object' ? Object.keys(posted as object) : [];
    const eff = (posted as Record<string, Record<string, unknown>> | null)?.effects;
    record({ page: 'config-post', postedKeys: keys, hasRanges: keys.includes('speed_ranges_kph'), effectKeys: eff ? Object.keys(eff).length : 0 });
  });

  // The server rejects any POST without this header (csrf_ok in
  // portal_http.cpp), which is what stops a hostile page from submitting to
  // the portal cross-origin. Every write the UI performs must carry it.
  for (const route of ['/config', '/wifi']) {
    test(`portal POSTs carry the CSRF header :: ${route}`, async ({ page }) => {
      const posts: { url: string; header: string | undefined }[] = [];
      page.on('request', (req) => {
        if (req.method() === 'POST') {
          posts.push({ url: new URL(req.url()).pathname, header: req.headers()['x-dog-portal'] });
        }
      });
      page.on('dialog', (d) => d.accept());
      await mockApis(page);
      await page.goto(route, { waitUntil: 'networkidle' });

      for (const label of ['Guardar', 'Restaurar', 'Set Home']) {
        const buttons = page.locator(`button:has-text("${label}")`);
        for (let i = 0; i < (await buttons.count()); i++) {
          await buttons.nth(i).click({ force: true }).catch(() => {});
          await page.waitForTimeout(250);
        }
      }

      record({ page: `csrf-${route}`, posts });
      expect(posts.length, 'expected at least one POST to be exercised').toBeGreaterThan(0);
      for (const p of posts) {
        expect(p.header, `${p.url} must send X-Dog-Portal`).toBe('1');
      }
    });
  }

  test('portal lock is off by default and adds no steps', async ({ page }) => {
    await mockApis(page);
    await page.goto('/config', { waitUntil: 'networkidle' });
    const section = page.locator('#lock_section');
    await expect(section).toHaveCount(1);
    // Collapsed and unchecked: a fresh build must not ask the user for anything.
    await expect(section).not.toHaveAttribute('open', /.*/);
    await expect(page.locator('#lock_enabled')).not.toBeChecked();
    await expect(page.locator('#lock_pin_field')).toBeHidden();
  });

  test('lock rejects a malformed PIN before hitting the device', async ({ page }) => {
    await mockApis(page);
    const posts: string[] = [];
    page.on('request', (r) => { if (r.method() === 'POST') posts.push(new URL(r.url()).pathname); });
    await page.goto('/config', { waitUntil: 'networkidle' });
    await page.locator('#lock_section').evaluate((el: HTMLDetailsElement) => { el.open = true; });
    await page.locator('#lock_enabled').check();
    await page.locator('#lock_pin').fill('12');
    await page.locator('button:has-text("Guardar bloqueo")').click();
    await page.waitForTimeout(300);
    await expect(page.locator('#lock_status')).toContainText('4 y 8');
    expect(posts.filter((p) => p === '/api/lock'), 'must not POST an invalid PIN').toHaveLength(0);
  });

  test('a locked portal prompts once and retries with the PIN', async ({ page }) => {
    await mockApis(page);
    const attempts: (string | undefined)[] = [];
    // First write is rejected as locked; the retry carrying the PIN succeeds.
    await page.route('**/api/config', async (route) => {
      if (route.request().method() !== 'POST') return route.fallback();
      const pin = route.request().headers()['x-dog-pin'];
      attempts.push(pin);
      if (!pin) return route.fulfill({ status: 401, json: { status: 'error', reason: 'locked' } });
      return route.fulfill({ json: { status: 'ok', wifi_restart: false } });
    });
    page.on('dialog', (d) => d.accept('4321'));

    await page.goto('/config', { waitUntil: 'networkidle' });
    await page.locator('button:has-text("Guardar cambios")').first().click({ force: true });
    await page.waitForTimeout(800);

    record({ page: 'lock-retry', attempts });
    expect(attempts.length, 'expected an initial attempt and one retry').toBe(2);
    expect(attempts[0], 'first attempt carries no PIN').toBeFalsy();
    expect(attempts[1], 'retry carries the PIN from the prompt').toBe('4321');
  });

  // Regression guard for the stored-XSS sink at pages.cpp:579-612. The SSID is
  // the only runtime value the firmware interpolates into markup; it reaches
  // the page from NVS and is attacker-settable via the unauthenticated
  // POST /api/wifi. Render it through the same escaping the firmware applies
  // and assert the payload stays inert.
  //
  // Fidelity note: extract_pages.py mirrors html_escape_attr() in Python. The
  // static rule in tools/web_pages_smoke.py is what guarantees the C++ side
  // routes every interpolation through that helper.
  for (const hostileSsid of [
    `" autofocus onfocus="window.__xss=1`,
    `"><img src=x onerror="window.__xss=1">`,
    `Casa "El Pino" & Cía`,
  ]) {
    test(`SSID reflection stays inert :: ${hostileSsid.slice(0, 28)}`, async ({ page }) => {
      const repoRoot = path.join(__dirname, '..', '..');
      execFileSync('python3', ['tools/ap_portal_preview/extract_pages.py'], {
        cwd: repoRoot,
        env: { ...process.env, AP_PORTAL_SUBST: JSON.stringify({ 'wifi_mgr::ssid()': hostileSsid }) },
        stdio: 'pipe',
      });
      const injected = readFileSync(path.join(repoRoot, '.ap-portal-preview', 'wifi.html'), 'utf-8');

      await mockApis(page);
      await page.route('**/wifi-xss', (route) => route.fulfill({ contentType: 'text/html; charset=utf-8', body: injected }));
      await page.goto('/wifi-xss', { waitUntil: 'domcontentloaded' });
      await page.waitForTimeout(400);

      const executed = await page.evaluate(() => (window as unknown as { __xss?: number }).__xss === 1);
      // The escaped entities must decode back to the exact SSID: the fix has to
      // neutralise the payload without corrupting legitimate names.
      const ssidValue = await page.locator('input[name=ssid]').inputValue();
      record({ page: 'xss-regression', hostileSsid, scriptExecuted: executed, ssidInputValue: ssidValue });

      expect(executed, 'escaped SSID must not execute').toBe(false);
      expect(ssidValue, 'escaped SSID must round-trip intact').toBe(hostileSsid);
    });
  }

  test.afterAll(async () => {
    // Leave the preview in its default state for the other suites.
    execFileSync('python3', ['tools/ap_portal_preview/extract_pages.py'], {
      cwd: path.join(__dirname, '..', '..'),
      stdio: 'pipe',
    });
  });

  test.afterAll(async () => {
    writeFileSync(path.join(outDir, 'audit-report.json'), JSON.stringify(report, null, 2), 'utf-8');
    console.log(`\n=== AUDIT REPORT ===\n${JSON.stringify(report, null, 2)}`);
  });
});
