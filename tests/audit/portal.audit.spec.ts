import { expect, test, type Page, type ConsoleMessage } from '@playwright/test';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
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
            const r = el.getBoundingClientRect();
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

  test('SSID reflection XSS (firmware concatenation reproduced)', async ({ page }) => {
    // pages.cpp emits:  ...value=")  +  wifi_mgr::ssid()  +  ("> ...
    // with no escaping. Reproduce that exact concatenation with a hostile SSID
    // that config::valid_ap_ssid() accepts (printable ASCII, <=32 chars).
    const hostileSsid = `" autofocus onfocus="window.__xss=1`;
    const wifiHtml = readFileSync(path.join(__dirname, '..', '..', '.ap-portal-preview', 'wifi.html'), 'utf-8');
    const injected = wifiHtml.replace('<input name="ssid" value="">', `<input name="ssid" value="${hostileSsid}">`);
    writeFileSync(path.join(outDir, 'xss-poc.html'), injected, 'utf-8');

    await mockApis(page);
    await page.route('**/wifi-xss', (route) => route.fulfill({ contentType: 'text/html; charset=utf-8', body: injected }));
    await page.goto('/wifi-xss', { waitUntil: 'domcontentloaded' });
    await page.waitForTimeout(400);

    const executed = await page.evaluate(() => (window as unknown as { __xss?: number }).__xss === 1);
    const ssidValue = await page.locator('input[name=ssid]').inputValue().catch(() => '(input not found)');
    await page.screenshot({ path: path.join(outDir, 'xss-poc.png'), fullPage: false });
    record({ page: 'xss-poc', hostileSsid, scriptExecuted: executed, ssidInputValue: ssidValue });
    expect(executed, 'unescaped SSID reflection executes attacker JS').toBe(true);
  });

  test.afterAll(async () => {
    writeFileSync(path.join(outDir, 'audit-report.json'), JSON.stringify(report, null, 2), 'utf-8');
    console.log(`\n=== AUDIT REPORT ===\n${JSON.stringify(report, null, 2)}`);
  });
});
