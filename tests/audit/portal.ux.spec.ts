/**
 * Regression tests for fase 1 of the UI/UX review
 * (docs/web_portal_ux_review_2026-08-11.md).
 *
 * Every test here failed before the fix. They exist because each of these
 * defects was invisible to the existing suite: the pages rendered, the requests
 * fired, and the portal still lost or falsified the user's work.
 */
import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const FIX = path.join(__dirname, '..', 'ap-portal-visual', 'fixtures');
const fx = (n: string) => JSON.parse(readFileSync(path.join(FIX, n), 'utf-8'));

type Posted = { url: string; body: string };

async function mockPortal(page: Page, opts: { scan?: unknown; failConfig?: boolean } = {}) {
  const posted: Posted[] = [];
  await page.route('**/api/**', async (route) => {
    const rq = route.request();
    const p = new URL(rq.url()).pathname;
    const m = rq.method();
    if (m === 'POST') posted.push({ url: p, body: rq.postData() ?? '' });

    if (p === '/api/summary') return route.fulfill({ json: fx('summary.active.json') });
    if (p === '/api/status') return route.fulfill({ json: fx('status.connected.json') });
    if (p === '/api/config' && m === 'GET') {
      if (opts.failConfig) return route.fulfill({ status: 503, json: { status: 'error' } });
      return route.fulfill({ json: fx('config.speed.json') });
    }
    if (p === '/api/home') return route.fulfill({ json: fx('home.set.json') });
    if (p === '/api/track') return route.fulfill({ json: fx('track.preview.json') });
    if (p === '/api/lock') return route.fulfill({ json: { enabled: false } });
    if (p === '/api/wifi/scan' && m === 'GET') {
      return route.fulfill({ json: opts.scan ?? { state: 'idle' } });
    }
    return route.fulfill({ json: { status: 'ok' } });
  });
  return posted;
}

const canvasHasInk = (page: Page) =>
  page.locator('#track_map').evaluate((c: HTMLCanvasElement) => {
    const ctx = c.getContext('2d');
    if (!ctx || c.width <= 1) return false;
    const d = ctx.getImageData(0, 0, c.width, c.height).data;
    for (let i = 3; i < d.length; i += 4) if (d[i] !== 0) return true;
    return false;
  });

test.describe('D1 · the GPS track survives a rotation', () => {
  test('canvas still has ink after the viewport changes', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/');
    await page.locator('summary').filter({ hasText: 'Historial y ruta' }).click();
    await page.getByRole('button', { name: 'Ver trazo' }).click();
    await expect(page.getByText('Trazo GPS:')).toBeVisible();
    expect(await canvasHasInk(page)).toBe(true);

    // Assigning canvas.width wipes the bitmap. Before the fix the resize handler
    // did exactly that and never repainted, so rotating the phone lost the route.
    await page.setViewportSize({ width: 926, height: 428 });
    await expect.poll(() => canvasHasInk(page), { timeout: 3000 }).toBe(true);

    await page.setViewportSize({ width: 428, height: 926 });
    await expect.poll(() => canvasHasInk(page), { timeout: 3000 }).toBe(true);
  });
});

test.describe('CC2 · restoring defaults refreshes the form', () => {
  test('edited values are replaced by what the collar now holds', async ({ page }) => {
    await mockPortal(page);
    page.on('dialog', (d) => d.accept());
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');

    const original = await page.locator('#brightness').inputValue();
    await page.locator('#brightness').fill('222');
    await page.locator('#reset_btn').click();

    // The danger was silent: a stale form plus a later "Guardar cambios" wrote
    // the pre-reset values straight back over the reset.
    await expect(page.locator('#brightness')).toHaveValue(original);
    await expect(page.locator('#status')).toContainText('restaurados');
  });
});

test.describe('CC3 · saving always reports what happened', () => {
  test('a bottom-of-page save scrolls the offending field into view', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');

    await page.locator('#ln2_thr').fill('0.1'); // breaks ascending order vs Z1
    const bottomSave = page.locator('.action-bar button', { hasText: 'Guardar cambios' }).last();
    await bottomSave.scrollIntoViewIfNeeded();
    await bottomSave.click();

    const errors = page.locator('#errors');
    await expect(errors).toContainText('Z2 debe ser mayor que Z1');
    await expect(page.locator('#status')).toContainText('Revisa los campos marcados');

    // Before the fix the error box sat ~1800px above the viewport and focus
    // stayed on the button, so pressing save looked like nothing happened.
    await expect.poll(async () =>
      page.locator('#ln2_thr').evaluate((el) => {
        const r = el.getBoundingClientRect();
        return r.top >= 0 && r.bottom <= window.innerHeight;
      }), { timeout: 3000 }).toBe(true);
    await expect(page.locator('#ln2_thr')).toBeFocused();
  });

  test('a successful save reports in words, not the protocol status', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('.action-bar button', { hasText: 'Guardar cambios' }).first().click();
    const status = page.locator('#status');
    await expect(status).toContainText('Guardado');
    await expect(status).not.toHaveText('ok');
  });
});

test.describe('CC1 · unsaved changes are not thrown away silently', () => {
  test('leaving with edits pending asks first, and staying keeps the page', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('#brightness').fill('7');

    let asked = false;
    page.on('dialog', (d) => { asked = true; d.dismiss(); });
    await page.locator('a.back-link').click();
    await page.waitForTimeout(500);

    expect(asked).toBe(true);
    expect(new URL(page.url()).pathname).toBe('/config');
    await expect(page.locator('#brightness')).toHaveValue('7');
  });

  test('a saved page lets you leave without nagging', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('#brightness').fill('120');
    await page.locator('.action-bar button', { hasText: 'Guardar cambios' }).first().click();
    await expect(page.locator('#status')).toContainText('Guardado');

    let asked = false;
    page.on('dialog', (d) => { asked = true; d.dismiss(); });
    await page.locator('a.back-link').click();
    await page.waitForTimeout(500);
    expect(asked).toBe(false);
    expect(new URL(page.url()).pathname).toBe('/');
  });
});

test.describe('F2 · validation marks the field, not the card', () => {
  test('only the offending zone input is flagged', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('#ln4_thr').fill('0.2');
    await page.locator('.action-bar button', { hasText: 'Guardar cambios' }).first().click();

    await expect(page.locator('#ln4_thr')).toHaveClass(/invalid/);
    await expect(page.locator('#speed_lanes_block')).not.toHaveClass(/invalid/);
    await expect(page.locator('#ln5_thr')).not.toHaveClass(/invalid/);
  });
});

test.describe('F1 · clearing Home is confirmed', () => {
  test('declining the prompt sends nothing', async ({ page }) => {
    const posted = await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('[data-mode-card="geofence"]').click();

    let asked = false;
    page.on('dialog', (d) => { asked = true; d.dismiss(); });
    await page.locator('button', { hasText: 'Borrar Home' }).click();
    await page.waitForTimeout(400);

    expect(asked).toBe(true);
    expect(posted.filter((p) => p.url === '/api/home/clear')).toHaveLength(0);
  });
});

test.describe('CC6 · a collar that does not answer says so', () => {
  test('config load failure offers a retry instead of an empty form', async ({ page }) => {
    await mockPortal(page, { failConfig: true });
    await page.goto('/config');
    await expect(page.locator('#errors')).toContainText('No se pudo leer la configuracion');
    await expect(page.locator('#errors').getByRole('button', { name: 'Reintentar' })).toBeVisible();
  });
});

test.describe('W1 · scanning for networks', () => {
  const READY = {
    state: 'ready',
    total: 3,
    networks: [
      { ssid: 'Casa-Principal', rssi: -47, open: false },
      { ssid: 'Vecino-2G', rssi: -81, open: false },
      { ssid: 'CafeLibre', rssi: -66, open: true },
    ],
  };

  test('results are listed strongest first and fill the SSID on tap', async ({ page }) => {
    await mockPortal(page, { scan: READY });
    await page.goto('/wifi');
    await page.getByRole('button', { name: 'Buscar redes' }).click();

    const items = page.locator('.scan-item');
    await expect(items).toHaveCount(3);
    await expect(items.first()).toContainText('Casa-Principal');
    await expect(items.last()).toContainText('Vecino-2G');
    await expect(items.filter({ hasText: 'CafeLibre' })).toContainText('ABIERTA');

    await items.first().click();
    await expect(page.locator('#ssid')).toHaveValue('Casa-Principal');
  });

  test('a hostile SSID from the air cannot inject markup', async ({ page }) => {
    // SSIDs are attacker-controlled: anyone nearby can name their AP anything.
    await mockPortal(page, {
      scan: {
        state: 'ready',
        total: 1,
        networks: [{ ssid: '"><img src=x onerror="window.__xss=1">', rssi: -50, open: false }],
      },
    });
    await page.goto('/wifi');
    await page.getByRole('button', { name: 'Buscar redes' }).click();
    await expect(page.locator('.scan-item')).toHaveCount(1);

    expect(await page.evaluate(() => (window as never as { __xss?: number }).__xss)).toBeUndefined();
    await page.locator('.scan-item').first().click();
    await expect(page.locator('#ssid')).toHaveValue('"><img src=x onerror="window.__xss=1">');
    expect(await page.evaluate(() => (window as never as { __xss?: number }).__xss)).toBeUndefined();
  });

  test('an empty result set explains itself', async ({ page }) => {
    await mockPortal(page, { scan: { state: 'ready', total: 0, networks: [] } });
    await page.goto('/wifi');
    await page.getByRole('button', { name: 'Buscar redes' }).click();
    await expect(page.locator('#scan_status')).toContainText('No se encontro ninguna red');
  });

  test('a failed scan does not leave the button stuck', async ({ page }) => {
    await mockPortal(page, { scan: { state: 'failed' } });
    await page.goto('/wifi');
    await page.getByRole('button', { name: 'Buscar redes' }).click();
    await expect(page.locator('#scan_status')).toContainText('No se pudo completar');
    await expect(page.locator('#scan_btn')).toBeEnabled();
  });
});

/* ---------- fase 2: the portal explaining itself ---------- */

test.describe('CC4/CC5 · messages are human and do not linger', () => {
  test('no page renders the protocol status as its confirmation', async ({ page }) => {
    for (const route of ['/config', '/wifi']) {
      await mockPortal(page);
      await page.goto(route);
      await page.waitForTimeout(400);
      const text = await page.evaluate(() => document.body.innerText);
      expect(text.split('\n').map((l) => l.trim())).not.toContain('ok');
    }
  });

  test('a confirmation clears itself instead of standing forever', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('.action-bar button', { hasText: 'Guardar cambios' }).first().click();
    await expect(page.locator('#status')).toContainText('Guardado');
    // Otherwise the card keeps claiming "Guardado" over edits made a minute later.
    await expect(page.locator('#status')).toHaveText('', { timeout: 8000 });
  });

  test('an error stays put until it is dealt with', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('#ln2_thr').fill('0.1');
    await page.locator('.action-bar button', { hasText: 'Guardar cambios' }).first().click();
    await expect(page.locator('#status')).toContainText('Revisa los campos marcados');
    await page.waitForTimeout(5000);
    await expect(page.locator('#status')).toContainText('Revisa los campos marcados');
  });
});

test.describe('CC8 · a disabled control looks disabled', () => {
  test('the hotspot password field changes appearance when it is inert', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/wifi');
    await page.waitForTimeout(400);
    const look = (sel: string) =>
      page.locator(sel).evaluate((el) => {
        const c = getComputedStyle(el);
        return `${c.opacity}|${c.borderStyle}|${c.color}`;
      });

    const enabled = await look('#ap_pass');
    await page.locator('#ap_open').check();
    await expect(page.locator('#ap_pass')).toBeDisabled();
    const disabled = await look('#ap_pass');
    expect(disabled).not.toBe(enabled);
  });
});

test.describe('W2 · advisories are ranked, not stacked', () => {
  test('the security warning is separated and carries the danger colour', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/wifi');
    await page.waitForTimeout(400);
    await page.locator('#ap_open').check();
    await page.locator('#ap_ssid').fill('OTRO-NOMBRE');
    await page.locator('#ap_ssid').dispatchEvent('input');

    const warn = page.locator('#ap_open_warn');
    const note = page.locator('#ap_warn');
    await expect(warn).toBeVisible();
    await expect(note).toBeVisible();

    const styles = await page.evaluate(() => {
      const g = (id: string) => {
        const el = document.getElementById(id)!;
        const c = getComputedStyle(el);
        return { color: c.color, top: el.getBoundingClientRect().top, bottom: el.getBoundingClientRect().bottom };
      };
      return { warn: g('ap_open_warn'), note: g('ap_warn') };
    });
    // Before, both were the same yellow with no gap and read as one block.
    expect(styles.warn.color).not.toBe(styles.note.color);
    expect(styles.note.top - styles.warn.bottom).toBeGreaterThanOrEqual(6);
  });
});

test.describe('W4 · the stored home password is not a mystery', () => {
  test('says a password is already saved', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/wifi');
    await expect(page.locator('#sta_pass_hint')).toContainText('Ya hay una password guardada');
  });

  test('says when there is none', async ({ page }) => {
    await page.route('**/api/**', async (route) => {
      const p = new URL(route.request().url()).pathname;
      if (p === '/api/config' && route.request().method() === 'GET') {
        const cfg = fx('config.speed.json');
        cfg.wifi.has_sta_pass = false;
        return route.fulfill({ json: cfg });
      }
      if (p === '/api/status') return route.fulfill({ json: fx('status.connected.json') });
      return route.fulfill({ json: { status: 'ok' } });
    });
    await page.goto('/wifi');
    await expect(page.locator('#sta_pass_hint')).toContainText('No hay ninguna password guardada');
  });
});

test.describe('W6 · mDNS is explained in terms of what you type', () => {
  test('the preview follows the field and stays inline', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/wifi');
    await expect(page.locator('#mdns_preview')).toHaveText('dog-rgb');
    await page.locator('#mdns').fill('collar-luna');
    await page.locator('#mdns').dispatchEvent('input');
    await expect(page.locator('#mdns_preview')).toHaveText('collar-luna');

    // `.field span` used to force block layout and uppercase on this preview.
    const style = await page.locator('#mdns_preview').evaluate((el) => {
      const c = getComputedStyle(el);
      return { display: c.display, transform: c.textTransform };
    });
    expect(style.display).not.toBe('block');
    expect(style.transform).toBe('none');
  });
});

test.describe('CC9 · one vocabulary across the portal', () => {
  test('the dashboard names the mode the way /config does', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/');
    await expect(page.locator('#pill-mode')).toHaveText('Modo: Velocidad');
    await expect(page.locator('#pill-mode')).not.toContainText('speed');
  });

  test('every page uses the same word for going home', async ({ page }) => {
    for (const route of ['/wifi', '/config', '/dev']) {
      await mockPortal(page);
      await page.goto(route);
      const labels = await page.evaluate(() =>
        Array.from(document.querySelectorAll('a[href="/"]')).map((a) => (a.textContent ?? '').trim()),
      );
      expect(labels.length).toBeGreaterThan(0);
      for (const l of labels) expect(l).toContain('Inicio');
      expect(labels).not.toContain('Volver');
    }
  });

  test('the geofence section is called Geocerca', async ({ page }) => {
    await mockPortal(page);
    await page.goto('/config');
    await expect(page.locator('#brightness')).not.toHaveValue('');
    await page.locator('[data-mode-card="geofence"]').click();
    await expect(page.locator('#geofence_block summary')).toHaveText('Geocerca');
    expect(await page.evaluate(() => document.body.innerText)).not.toContain('Geofence');
  });
});
