import { test, type Page } from '@playwright/test';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const outDir = path.join(__dirname, 'evidence');
const fixturesDir = path.join(__dirname, '..', 'ap-portal-visual', 'fixtures');
const fixtureJson = (name: string): unknown =>
  JSON.parse(readFileSync(path.join(fixturesDir, name), 'utf-8'));
mkdirSync(outDir, { recursive: true });

const PAGES = ['/', '/wifi', '/config', '/dev'];
const out: unknown[] = [];

/** WCAG relative luminance / contrast ratio, computed in-page. */
const CONTRAST_FN = `
function lum(c){const s=c.map(v=>{v/=255;return v<=0.03928?v/12.92:Math.pow((v+0.055)/1.055,2.4);});return 0.2126*s[0]+0.7152*s[1]+0.0722*s[2];}
function parse(c){const m=c.match(/rgba?\\(([^)]+)\\)/);if(!m)return null;const p=m[1].split(',').map(x=>parseFloat(x));return {rgb:[p[0],p[1],p[2]],a:p.length>3?p[3]:1};}
function bgOf(el){let n=el;while(n&&n!==document.documentElement){const c=parse(getComputedStyle(n).backgroundColor);if(c&&c.a>0.99)return c.rgb;n=n.parentElement;}return [255,255,255];}
function ratio(a,b){const L1=lum(a),L2=lum(b);const hi=Math.max(L1,L2),lo=Math.min(L1,L2);return (hi+0.05)/(lo+0.05);}
`;

test.describe('portal visual audit', () => {
  for (const route of PAGES) {
    test(`contrast & focus :: ${route}`, async ({ page }: { page: Page }) => {
      await page.route('**/api/**', (r) => {
        const request = r.request();
        const uri = new URL(request.url()).pathname;
        if (uri === '/api/v1/led/capabilities') {
          return r.fulfill({ json: fixtureJson('led.capabilities.json') });
        }
        if (uri === '/api/config' && request.method() === 'GET') {
          return r.fulfill({ json: fixtureJson('config.speed.json') });
        }
        return r.fulfill({ json: {} });
      });
      await page.goto(route, { waitUntil: 'domcontentloaded' });
      await page.waitForTimeout(300);

      const res = await page.evaluate(`(() => {
        ${CONTRAST_FN}
        const fails = [];
        const seen = new Set();
        document.querySelectorAll('*').forEach(el => {
          const t = Array.from(el.childNodes).filter(n => n.nodeType === 3).map(n => n.textContent.trim()).join('');
          if (!t) return;
          const cs = getComputedStyle(el);
          if (cs.display === 'none' || cs.visibility === 'hidden' || parseFloat(cs.opacity) < 0.1) return;
          const fg = parse(cs.color); if (!fg) return;
          const bg = bgOf(el);
          const r = ratio(fg.rgb, bg);
          const size = parseFloat(cs.fontSize);
          const bold = parseInt(cs.fontWeight, 10) >= 700;
          const large = size >= 24 || (size >= 18.66 && bold);
          const need = large ? 3.0 : 4.5;
          if (r < need) {
            const key = cs.color + '|' + bg.join(',') + '|' + Math.round(size);
            if (seen.has(key)) return;
            seen.add(key);
            fails.push({ text: t.slice(0, 32), cls: (el.className || '').toString().split(' ')[0], color: cs.color, bg: 'rgb(' + bg.join(',') + ')', size: size, ratio: Math.round(r * 100) / 100, need: need });
          }
        });

        // Focus visibility: does :focus-visible produce any outline/box-shadow rule?
        let focusRules = 0;
        for (const sheet of Array.from(document.styleSheets)) {
          let rules; try { rules = sheet.cssRules; } catch { continue; }
          for (const rule of Array.from(rules)) {
            const sel = rule.selectorText || '';
            if (/:focus/.test(sel)) focusRules++;
          }
        }
        const prefersDark = matchMedia('(prefers-color-scheme: dark)');
        let darkRules = 0;
        for (const sheet of Array.from(document.styleSheets)) {
          let rules; try { rules = sheet.cssRules; } catch { continue; }
          for (const rule of Array.from(rules)) {
            if (rule.media && /prefers-color-scheme/.test(rule.media.mediaText)) darkRules++;
          }
        }
        return { contrastFailures: fails, focusRules: focusRules, darkModeRules: darkRules, reducedMotionRules: 0 };
      })()`);

      out.push({ route, ...(res as object) });
    });
  }

  test('no-JS behaviour', async ({ browser }) => {
    const ctx = await browser.newContext({ javaScriptEnabled: false, viewport: { width: 428, height: 926 } });
    const page = await ctx.newPage();
    for (const route of PAGES) {
      await page.goto(`http://127.0.0.1:4173${route}`, { waitUntil: 'domcontentloaded' });
      const state = await page.evaluate(() => ({
        placeholders: (document.body.innerText.match(/--/g) || []).length,
        hasNoscript: !!document.querySelector('noscript'),
        bodyChars: document.body.innerText.length,
      }));
      out.push({ route, noJs: state });
      await page.screenshot({ path: path.join(outDir, `nojs-${route.replace(/\//g, '') || 'index'}.png`), fullPage: true });
    }
    await ctx.close();
  });

  test.afterAll(() => {
    writeFileSync(path.join(outDir, 'visual-report.json'), JSON.stringify(out, null, 2), 'utf-8');
    console.log('\n=== VISUAL REPORT ===\n' + JSON.stringify(out, null, 2));
  });
});
