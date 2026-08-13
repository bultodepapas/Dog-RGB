import { expect, test, type Page } from '@playwright/test';
import { mkdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const fixturesDir = path.join(__dirname, 'fixtures');
const currentDir = path.join(__dirname, 'screenshots', 'current');
const screenshotCss = path.join(__dirname, 'screenshot.css');
const compareScreenshots = process.env.AP_PORTAL_VISUAL === '1';

type MockState = {
  summary?: string;
  status?: string;
  config?: string;
  home?: string;
  track?: string;
  dev?: string;
};

function fixtureJson(name: string): unknown {
  return JSON.parse(readFileSync(path.join(fixturesDir, name), 'utf-8'));
}

async function mockPortalApis(page: Page, state: MockState = {}) {
  const fixtures = {
    summary: state.summary ?? 'summary.active.json',
    status: state.status ?? 'status.connected.json',
    config: state.config ?? 'config.speed.json',
    home: state.home ?? 'home.set.json',
    track: state.track ?? 'track.preview.json',
    dev: state.dev ?? 'dev.healthy.json',
  };

  await page.route('**/api/**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const method = request.method();

    if (url.pathname === '/api/summary') {
      await route.fulfill({ json: fixtureJson(fixtures.summary) });
      return;
    }
    if (url.pathname === '/api/status') {
      await route.fulfill({ json: fixtureJson(fixtures.status) });
      return;
    }
    if (url.pathname === '/api/v1/led/capabilities') {
      await route.fulfill({ json: fixtureJson('led.capabilities.json') });
      return;
    }
    if (url.pathname === '/api/v1/led/state') {
      await route.fulfill({ json: fixtureJson('led.state.json') });
      return;
    }
    if (url.pathname === '/api/config' && method === 'GET') {
      await route.fulfill({ json: fixtureJson(fixtures.config) });
      return;
    }
    if (url.pathname === '/api/config' && method === 'POST') {
      await route.fulfill({ json: { status: 'ok', wifi_restart: false } });
      return;
    }
    if (url.pathname === '/api/config/reset') {
      await route.fulfill({ json: { status: 'ok' } });
      return;
    }
    if (url.pathname === '/api/home') {
      await route.fulfill({ json: fixtureJson(fixtures.home) });
      return;
    }
    if (url.pathname === '/api/home/set' || url.pathname === '/api/home/clear') {
      await route.fulfill({ json: { status: 'ok' } });
      return;
    }
    if (url.pathname === '/api/track') {
      await route.fulfill({ json: fixtureJson(fixtures.track) });
      return;
    }
    if (url.pathname === '/api/track.csv') {
      await route.fulfill({
        contentType: 'text/csv; charset=utf-8',
        body: 'date,min,lat,lon\n20260506,602,18.4851000,-69.9328000\n',
      });
      return;
    }
    if (url.pathname === '/api/track.geojson') {
      await route.fulfill({
        contentType: 'application/geo+json; charset=utf-8',
        body: '{"type":"FeatureCollection","features":[]}',
      });
      return;
    }
    if (url.pathname === '/api/wifi') {
      await route.fulfill({ json: { status: 'ok' } });
      return;
    }
    if (url.pathname === '/api/wifi/scan') {
      if (method === 'POST') {
        await route.fulfill({ json: { status: 'scanning' } });
        return;
      }
      await route.fulfill({
        json: {
          state: 'ready',
          total: 4,
          networks: [
            { ssid: 'Casa-Principal', rssi: -47, open: false },
            { ssid: 'Casa-Principal-5G', rssi: -58, open: false },
            { ssid: 'CafeLibre', rssi: -66, open: true },
            { ssid: 'Vecino-2G', rssi: -84, open: false },
          ],
        },
      });
      return;
    }
    if (url.pathname === '/api/wifi/ap') {
      await route.fulfill({ json: { status: 'ok', wifi_restart: true } });
      return;
    }
    if (url.pathname === '/api/dev') {
      await route.fulfill({ json: fixtureJson(fixtures.dev) });
      return;
    }

    await route.fulfill({
      status: 500,
      json: { status: 'error', reason: `unmocked ${method} ${url.pathname}` },
    });
  });
}

async function capture(page: Page, name: string) {
  mkdirSync(currentDir, { recursive: true });
  await page.addStyleTag({ path: screenshotCss });
  await page.screenshot({
    path: path.join(currentDir, name),
    fullPage: true,
  });

  if (compareScreenshots) {
    await expect(page).toHaveScreenshot(name, {
      fullPage: true,
      stylePath: screenshotCss,
    });
  }
}

async function expectCanvasHasInk(page: Page, selector: string) {
  const hasInk = await page.locator(selector).evaluate((canvas: HTMLCanvasElement) => {
    const ctx = canvas.getContext('2d');
    if (!ctx || canvas.width <= 1 || canvas.height <= 1) {
      return false;
    }
    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    for (let i = 3; i < data.length; i += 4) {
      if (data[i] !== 0) {
        return true;
      }
    }
    return false;
  });
  expect(hasInk).toBe(true);
}

test.describe('AP portal mobile screenshots', () => {
  test('dashboard active', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/');
    // The GPS pill carries this now; the note line stays quiet when there is
    // nothing to add beyond what the pills already say.
    await expect(page.locator('#pill-gps')).toHaveText('GPS OK (11)');
    await expect(page.locator('#status')).toHaveText('');
    await expect(page.getByText('STA conectada')).toBeVisible();
    await capture(page, 'dashboard-active-full.png');
  });

  test('dashboard route preview open', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/');
    await page.locator('summary').filter({ hasText: 'Historial y ruta' }).click();
    await page.getByRole('button', { name: 'Ver trazo' }).click();
    await expect(page.getByText('Trazo GPS:')).toBeVisible();
    await expectCanvasHasInk(page, '#track_map');
    await capture(page, 'dashboard-route-open.png');
  });

  test('dashboard empty state', async ({ page }) => {
    await mockPortalApis(page, { summary: 'summary.empty.json' });
    await page.goto('/');
    await expect(page.getByText('Sin actividad registrada hoy')).toBeVisible();
    await capture(page, 'dashboard-empty-state.png');
  });

  test('wifi connected', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/wifi');
    await expect(page.getByText('Home Wi-Fi conectado.')).toBeVisible();
    await expect(page.getByText('Conectado (192.168.1.74)')).toBeVisible();
    await capture(page, 'wifi-connected.png');
  });

  test('wifi connecting after save', async ({ page }) => {
    await mockPortalApis(page, { status: 'status.connecting.json' });
    await page.goto('/wifi');
    await page.locator('input[name="ssid"]').fill('Casa-Principal');
    await page.locator('#pass').fill('correct-horse-battery');
    await page.getByRole('button', { name: 'Guardar y conectar' }).click();
    await expect(page.getByText('Guardado, conectando')).toBeVisible();
    await capture(page, 'wifi-connecting-after-save.png');
  });

  test('wifi ap only', async ({ page }) => {
    await mockPortalApis(page, { status: 'status.ap-only.json' });
    await page.goto('/wifi');
    await expect(page.getByText('Desconectado')).toBeVisible();
    await expect(page.getByText('Activo: DOG-RGB-42')).toBeVisible();
    await capture(page, 'wifi-ap-only.png');
  });

  test('wifi open ap warning', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/wifi');
    await page.locator('#ap_open').check();
    // The warning must state the consequence, not just the missing password:
    // an open AP hands portal write access to anyone in radio range.
    const warning = page.locator('#ap_open_warn');
    await expect(warning).toBeVisible();
    await expect(warning).toContainText('cambiar la configuracion');
    await capture(page, 'wifi-open-ap-warning.png');
  });

  test('wifi scan results', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/wifi');
    await page.getByRole('button', { name: 'Buscar redes' }).click();
    await expect(page.locator('.scan-item')).toHaveCount(4);
    await expect(page.getByText('4 redes encontradas.')).toBeVisible();
    await capture(page, 'wifi-scan-results.png');
  });

  test('config speed default', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/config');
    await expect(page.locator('[data-mode-card="speed"]')).toHaveClass(/active/);
    await expect(page.locator('#brightness')).toHaveValue('96');
    await capture(page, 'config-speed-default.png');
  });

  test('config simple presets', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/config');
    await page.locator('[data-mode-card="simple"]').click();
    await page.locator('[data-theme="aurora"]').click();
    await expect(page.locator('[data-mode-card="simple"]')).toHaveClass(/active/);
    await expect(page.locator('[data-theme="aurora"]')).toHaveClass(/active/);
    await capture(page, 'config-simple-presets.png');
  });

  test('config geofence advanced open', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/config');
    await page.locator('[data-mode-card="geofence"]').click();
    await page.locator('summary').filter({ hasText: 'GPS calidad' }).click();
    await expect(page.locator('[data-mode-card="geofence"]')).toHaveClass(/active/);
    await expect(page.getByText('Home (manual):')).toBeVisible();
    await capture(page, 'config-geofence-advanced-open.png');
  });

  test('config show mode', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/config');
    await page.locator('[data-mode-card="show"]').click();
    await expect(page.locator('[data-mode-card="show"]')).toHaveClass(/active/);
    await expect(page.getByText('Modo demo')).toBeVisible();
    await capture(page, 'config-show-mode.png');
  });

  test('config validation errors', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/config');
    await page.locator('#ln2_thr').fill('1');
    await page.getByRole('button', { name: 'Guardar cambios' }).first().click();
    // The message names the zone that is wrong and what it must beat, and the
    // offending input itself is marked -- not the whole card, which holds nine
    // of them.
    await expect(page.getByText('Z2 debe ser mayor que Z1')).toBeVisible();
    await expect(page.locator('#ln2_thr')).toHaveClass(/invalid/);
    await capture(page, 'config-validation-errors.png');
  });

  test('dev healthy', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/dev');
    await expect(page.getByText('Diagnostico tecnico')).toBeVisible();
    await expect(page.getByText('STA conectada')).toBeVisible();
    await capture(page, 'dev-healthy.png');
  });

  test('dev degraded', async ({ page }) => {
    await mockPortalApis(page, { dev: 'dev.unhealthy.json' });
    await page.goto('/dev');
    await expect(page.getByText('Diagnostico tecnico')).toBeVisible();
    // The whole point of the page: what is wrong has to look wrong.
    await expect(page.locator('#dev-heap')).toHaveClass(/health-bad/);
    await expect(page.locator('#diag-ap-fail')).toHaveClass(/health-bad/);
    await capture(page, 'dev-degraded.png');
  });

  test('dev raw json open', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/dev');
    await page.locator('summary').filter({ hasText: 'JSON crudo' }).click();
    await expect(page.locator('#dev-json')).toContainText('"free_heap"');
    await capture(page, 'dev-raw-json-open.png');
  });

  test('dev ap diagnostics open', async ({ page }) => {
    await mockPortalApis(page);
    await page.goto('/dev');
    await expect(page.getByText('Inicios AP', { exact: true })).toBeVisible();
    await expect(page.locator('#diag-ap-start')).toHaveText('3');
    await capture(page, 'dev-ap-diagnostics-open.png');
  });
});
