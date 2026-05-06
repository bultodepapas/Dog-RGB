# AP Portal Visual Screenshot Workflow Plan

Date: 2026-05-06

## Objective

Create a repeatable workflow that lets Codex, VS Code, and future developers preview the ESP32 Access Point portal without flashing hardware, render every important screen at a mobile reference size, feed the UI realistic mock data, capture screenshots, and review visual changes before firmware validation.

The workflow should focus on the web interface, not Arduino runtime behavior. Firmware builds still matter, but they should be a later validation step after the UI has already been visually checked.

## Research Summary

Playwright is the best fit for this repository.

- Playwright can emulate mobile browser behavior, including viewport, user agent, screen size, and touch settings through device descriptors or explicit viewport settings.
- Playwright can intercept and mock `fetch`/XHR calls with `page.route()` or browser-context routes, which fits the AP portal because every dynamic screen reads JSON APIs.
- Playwright can capture normal and full-page screenshots to files.
- Playwright Test can compare screenshots against committed reference images with `expect(page).toHaveScreenshot()`.
- Playwright UI Mode and the VS Code extension provide an interactive way to run, debug, and inspect tests inside VS Code.

Primary sources used:

- Playwright VS Code workflow: https://playwright.dev/docs/getting-started-vscode
- Playwright emulation: https://playwright.dev/docs/emulation
- Playwright API/network mocking: https://playwright.dev/docs/mock and https://playwright.dev/docs/network
- Playwright screenshots: https://playwright.dev/docs/screenshots
- Playwright visual comparisons: https://playwright.dev/docs/test-snapshots
- Playwright UI Mode: https://playwright.dev/docs/test-ui-mode
- iPhone 13 Pro Max viewport reference: https://blisk.io/devices/details/iphone-13-pro-max

## Recommended Approach

Use a local Playwright-based visual harness.

The harness should:

1. Extract or serve the embedded portal pages from `Platformio/Dog-RGB/src/web/pages.cpp`.
2. Start a lightweight local preview server on `127.0.0.1`.
3. Map firmware routes to local pages:
   - `/`
   - `/wifi`
   - `/config`
   - `/dev`
4. Mock all JSON endpoints used by those pages.
5. Render each page at the reference mobile viewport.
6. Capture screenshots into a predictable directory.
7. Optionally compare screenshots against approved reference images.

This keeps the UI review loop fast and independent of ESP32 flashing, AP connection stability, and live GPS/Wi-Fi state.

## Reference Device

Use iPhone 13 Pro Max as the primary mobile reference:

- CSS viewport: `428 x 926`
- Device pixel ratio: `3`
- Touch enabled: `true`
- Mobile viewport behavior: `isMobile: true`

Implementation detail:

- If the installed Playwright version includes `devices['iPhone 13 Pro Max']`, use it.
- If not, define a custom project named `iphone-13-pro-max` with:

```ts
use: {
  viewport: { width: 428, height: 926 },
  deviceScaleFactor: 3,
  isMobile: true,
  hasTouch: true,
  colorScheme: 'light',
}
```

The first pass should use Chromium for speed and deterministic local screenshots. A later validation pass can add WebKit because iPhone Safari behavior matters, but Chromium is sufficient for the daily design loop.

## Proposed File Structure

```text
tools/
  web_pages_smoke.py
  ap_portal_preview/
    extract_pages.py
    server.mjs
    README.md

tests/
  ap-portal-visual/
    ap-portal.visual.spec.ts
    fixtures/
      summary.active.json
      status.connected.json
      status.connecting.json
      config.speed.json
      config.simple.json
      home.set.json
      track.preview.json
      dev.healthy.json
    screenshots/
      current/
      reference/
      diff/
    screenshot.css

playwright.config.ts
package.json
docs/
  ap_portal_visual_screenshot_workflow_plan_2026-05-06.md
  ap_portal_visual_screenshot_workflow_guide.md
```

## Preview Server Design

The preview server should be deliberately small.

Recommended implementation:

- `extract_pages.py` reads `pages.cpp` and reconstructs the HTML returned by:
  - `web_pages::html_page()`
  - `web_pages::html_wifi_page()`
  - `web_pages::html_config_page()`
  - `web_pages::html_dev_page()`
- It should reuse the parsing approach already proven in `tools/web_pages_smoke.py`, including extracting `BASE_CSS` and raw string fragments.
- It should write generated HTML files into a temporary or ignored directory, for example `.ap-portal-preview/`.
- `server.mjs` serves those generated files:
  - `/` -> `.ap-portal-preview/index.html`
  - `/wifi` -> `.ap-portal-preview/wifi.html`
  - `/config` -> `.ap-portal-preview/config.html`
  - `/dev` -> `.ap-portal-preview/dev.html`
- The server should return `404` for unmocked API routes so missing mock coverage is obvious during tests.

Why not use the ESP32 itself for the main workflow:

- Hardware AP tests are slower and less repeatable.
- GPS values, Wi-Fi states, uptime, and route samples change constantly.
- Visual regression screenshots need deterministic data.
- The ESP32 should remain the final integration target, not the primary layout workbench.

## Mock Data Strategy

Use deterministic fixture JSON files that look like realistic collar data.

Mock data rules:

- No `--` placeholders in the baseline screenshots unless the specific test is an empty state.
- Use plausible GPS values, speeds, distances, timestamps, AP state, and LED settings.
- Keep values stable so screenshots do not change every run.
- Avoid extremely long values in the default happy path; add one dedicated stress case for long SSIDs, long mDNS names, and high counters.
- Keep fixtures small because this is an embedded UI workflow.

Mock routes required:

| Endpoint | Used By | Fixture |
| --- | --- | --- |
| `/api/summary` | Dashboard | `summary.active.json` |
| `/api/status` | Dashboard, Wi-Fi | `status.connected.json`, `status.connecting.json`, `status.ap-only.json` |
| `/api/track?session=...&max_points=250` | Dashboard route preview | `track.preview.json` |
| `/api/config` | Wi-Fi, Config | `config.speed.json`, `config.simple.json` |
| `/api/home` | Config | `home.set.json`, `home.empty.json` |
| `/api/dev` | Developer diagnostics | `dev.healthy.json` |
| `/api/mode` | Dashboard actions | Return `{ "status": "ok" }` |
| `/api/home/set` | Dashboard/Config action | Return `{ "status": "ok" }` |
| `/api/home/clear` | Config action | Return `{ "status": "ok" }` |
| `/api/config/reset` | Config action | Return `{ "status": "ok" }` |
| `/api/wifi` | Wi-Fi form action | Return `{ "status": "ok" }` or text matching firmware behavior |
| `/api/wifi/ap` | Wi-Fi AP form action | Return `{ "status": "ok" }` |

Playwright should set up route mocks before `page.goto()`.

## Screenshot Test Cases

The first implementation should keep the suite small and high-signal.

### Dashboard

Screenshots:

- `dashboard-active-full.png`
- `dashboard-route-open.png`
- `dashboard-empty-state.png`

Setup:

- Mock `/api/summary` with active day metrics, sessions, GPS fix, speed, distance, max speed, active minutes, and LED mode.
- Mock `/api/status` with STA connected and AP enabled.
- Mock `/api/track` with 30 to 80 points so the route canvas renders a believable track.

Visual checks:

- Hero status is visible without scrolling.
- Primary actions fit on mobile.
- Metrics are readable and not cramped.
- Route controls do not appear broken before loading.
- Canvas has a visible track after loading.
- Export buttons only appear when there is data.

### Wi-Fi

Screenshots:

- `wifi-connected.png`
- `wifi-connecting-after-save.png`
- `wifi-ap-only.png`
- `wifi-open-ap-warning.png`

Setup:

- Use connected, connecting, and AP-only variants of `/api/status`.
- Use `/api/config` with realistic AP SSID, mDNS, and masked password behavior.
- For the open AP case, check the AP abierto control and capture the warning state.

Visual checks:

- Home Wi-Fi and hotspot sections are clearly separated.
- The user can tell whether the collar is reachable through STA, AP, or mDNS.
- Password controls are usable on mobile.
- Save buttons and reconnect guidance remain visible and understandable.

### Config

Screenshots:

- `config-speed-default.png`
- `config-simple-presets.png`
- `config-geofence-advanced-open.png`
- `config-show-mode.png`
- `config-validation-errors.png`

Setup:

- Mock `/api/config` with speed mode as the default baseline.
- Use UI interactions to select Simple, Geofence, and Show modes.
- For validation errors, set bad values in the UI instead of mocking backend failure where possible.

Visual checks:

- Mode cards fit and clearly show active state.
- Brightness slider and number input align on mobile.
- Presets and color swatches are tappable.
- Advanced sections are discoverable but do not dominate the page.
- Validation errors are close to the affected controls and do not cause layout breakage.

### Developer Diagnostics

Screenshots:

- `dev-healthy.png`
- `dev-raw-json-open.png`
- `dev-ap-diagnostics-open.png`

Setup:

- Mock `/api/dev` with realistic system, Wi-Fi, AP diagnostics, GPS, LED, and geofence data.
- Keep raw JSON collapsed by default.
- Open `JSON crudo` only in the dedicated screenshot.

Visual checks:

- `/dev` reads as a technical page, not a normal user task.
- AP diagnostics are available and readable.
- Raw JSON is available but not surfaced by default.
- Long diagnostic values do not overflow the mobile viewport.

## Screenshot Capture Modes

Use two complementary modes.

### Current Screenshot Mode

Purpose: quickly see how the UI looks now.

Command:

```bash
npm run ap-portal:screenshots
```

Expected behavior:

- Starts the preview server.
- Runs Playwright against the mobile viewport.
- Writes fresh screenshots to `tests/ap-portal-visual/screenshots/current/`.
- Does not fail because screenshots differ.

This is the mode Codex should use while actively designing.

### Visual Regression Mode

Purpose: compare against approved reference screenshots.

Command:

```bash
npm run ap-portal:visual
```

Expected behavior:

- Runs Playwright Test with `expect(page).toHaveScreenshot()`.
- Fails when visual output differs beyond the configured threshold.
- Writes actual/diff artifacts to Playwright output.

Reference update command:

```bash
npm run ap-portal:visual:update
```

Use this only after manually reviewing screenshots and deciding the change is intentional.

## Playwright Configuration Plan

Add a single project first:

```ts
projects: [
  {
    name: 'iphone-13-pro-max-chromium',
    use: {
      browserName: 'chromium',
      viewport: { width: 428, height: 926 },
      deviceScaleFactor: 3,
      isMobile: true,
      hasTouch: true,
    },
  },
]
```

Recommended screenshot settings:

- `fullPage: true` for page-level review, because several portal screens scroll.
- Also capture viewport-only screenshots for first-screen hierarchy if useful.
- Disable animations during screenshots with `screenshot.css`.
- Use a small diff tolerance for text anti-aliasing, but keep it strict enough to catch layout shifts.
- Run the same baseline environment when updating snapshots because browser rendering can vary by OS and browser version.

## Test Implementation Pattern

Each visual test should follow this shape:

```ts
test('dashboard active', async ({ page }) => {
  await mockPortalApis(page, {
    summary: 'summary.active.json',
    status: 'status.connected.json',
    track: 'track.preview.json',
  });

  await page.goto('/');
  await expect(page.getByText('DOG-RGB')).toBeVisible();
  await page.screenshot({
    path: 'tests/ap-portal-visual/screenshots/current/dashboard-active-full.png',
    fullPage: true,
  });
});
```

For regression tests:

```ts
await expect(page).toHaveScreenshot('dashboard-active-full.png', {
  fullPage: true,
});
```

For interaction states:

```ts
await page.goto('/config');
await page.getByRole('button', { name: /Simple/ }).click();
await expect(page).toHaveScreenshot('config-simple-presets.png', {
  fullPage: true,
});
```

## Visual Review Checklist

Every screenshot review should check:

- First screen hierarchy: the user understands the page before scrolling.
- Mobile fit: no horizontal scrolling, no clipped button text, no overlapping labels.
- Tap targets: buttons, segmented controls, checkboxes, and swatches are large enough.
- Status clarity: connected, connecting, AP-only, no GPS, and error states are obvious.
- Data realism: numbers look plausible and do not expose `undefined`, `NaN`, or empty placeholders.
- Advanced content: diagnostics and raw JSON are intentionally hidden or grouped.
- Embedded suitability: UI remains simple, mostly static, and free of heavy assets.
- Spanish copy consistency: normal user labels are Spanish, technical abbreviations are retained where useful.

## Future Developer Guide Requirement

As part of implementation, create:

```text
docs/ap_portal_visual_screenshot_workflow_guide.md
```

That guide must explain the daily workflow for future developers:

1. Plan the UI change and identify affected screens.
2. Modify `pages.cpp` or supporting mock/test files.
3. Run the static smoke test:

```bash
python3 tools/web_pages_smoke.py
```

4. Generate fresh screenshots:

```bash
npm run ap-portal:screenshots
```

5. Open the screenshots from `tests/ap-portal-visual/screenshots/current/`.
6. Review every affected screenshot carefully using the checklist above.
7. Fix UI issues in the code.
8. Repeat screenshot generation and review until the UI is visually acceptable.
9. Run visual regression tests:

```bash
npm run ap-portal:visual
```

10. Update reference screenshots only after manual approval:

```bash
npm run ap-portal:visual:update
```

11. Run firmware validation:

```bash
cd Platformio/Dog-RGB
/Users/angel/.platformio/penv/bin/platformio run
```

The guide must make clear that screenshots are not decoration. They are development evidence. A developer should not update reference images until they have inspected the visual result and confirmed the UI is better or intentionally changed.

## Implementation Phases

### Phase 0: Tooling Decision

Objective: commit to the Playwright approach and avoid overbuilding.

Tasks:

- Add minimal Node/Playwright dependencies.
- Add `playwright.config.ts`.
- Add npm scripts for screenshot generation, visual comparison, UI Mode, and install.
- Document that the workflow is local-first and does not replace hardware testing.

Expected outcome:

- A developer can install and run Playwright from VS Code or terminal.

Validation:

- `npx playwright --version`
- `npx playwright test --list`

### Phase 1: Static Portal Preview Server

Objective: render firmware page templates locally.

Tasks:

- Build `tools/ap_portal_preview/extract_pages.py`.
- Build `tools/ap_portal_preview/server.mjs`.
- Map `/`, `/wifi`, `/config`, `/dev`.
- Fail clearly if `pages.cpp` cannot be parsed.

Expected outcome:

- Browser can open local versions of all AP portal pages.

Validation:

- `npm run ap-portal:serve`
- Open local `/`, `/wifi`, `/config`, `/dev`.
- Confirm each page has shared CSS and page scripts.

### Phase 2: Mock API Fixtures

Objective: make every screen render with deterministic realistic data.

Tasks:

- Add JSON fixtures for summary, status, config, home, track, and dev endpoints.
- Add a shared Playwright mock helper.
- Make unmocked API calls fail the test.

Expected outcome:

- Pages never show accidental `undefined`, `NaN`, missing metrics, or broken API states in baseline screenshots.

Validation:

- Run visual tests with request logging enabled once.
- Confirm every API call is intercepted.

### Phase 3: Screenshot Generation

Objective: generate current mobile screenshots for all screen states.

Tasks:

- Add Playwright tests for dashboard, Wi-Fi, config, and dev.
- Capture full-page screenshots into `screenshots/current/`.
- Add named output files per screen state.

Expected outcome:

- One command generates a complete mobile screenshot set.

Validation:

- `npm run ap-portal:screenshots`
- Confirm all expected PNG files exist.
- Manually inspect each screenshot.

### Phase 4: Visual Regression Baselines

Objective: make approved UI output comparable over time.

Tasks:

- Add `toHaveScreenshot()` assertions.
- Store approved reference screenshots.
- Configure diff tolerance.
- Add a clear update workflow.

Expected outcome:

- Unintended layout changes are caught before firmware flashing.

Validation:

- `npm run ap-portal:visual`
- Change a visible label or spacing locally and confirm the test fails.
- Revert the change and confirm it passes.

### Phase 5: Developer Workflow Guide

Objective: make the process repeatable for future maintainers.

Tasks:

- Create `docs/ap_portal_visual_screenshot_workflow_guide.md`.
- Include setup, commands, screenshot review checklist, update rules, and firmware validation sequence.
- Include guidance for Codex: when asked for UI work, run screenshots before claiming completion.

Expected outcome:

- A developer can change the AP UI, generate screenshots, review them, correct issues, and validate the final result without rediscovering the process.

Validation:

- Follow the guide from a clean checkout.
- Confirm the guide includes both terminal and VS Code paths.

## Acceptance Criteria

The workflow is complete when:

- `npm run ap-portal:screenshots` generates mobile screenshots for all primary portal screens.
- Screenshots use realistic mock data.
- `npm run ap-portal:visual` can compare the current UI against approved references.
- The local preview server does not require ESP32 hardware.
- The future developer guide exists and explains the full modify, capture, review, correct, repeat loop.
- Static smoke tests and firmware builds remain part of the final validation sequence.

## Open Decisions

- Whether to commit reference screenshots immediately or keep them generated-only until the UI stabilizes further.
- Whether to add WebKit as a second visual project after Chromium is reliable.
- Whether to integrate screenshot generation into CI, or keep it local-only to avoid noisy image diffs across operating systems.
- Whether to add a small HTML report that displays screenshots in route order for easier manual review.

## Recommended First Implementation

Start with local-only screenshot generation, not strict regression gates.

Order:

1. Add the preview extractor/server.
2. Add mock fixtures.
3. Add screenshot generation tests for the four routes.
4. Manually inspect the generated screenshots.
5. Add visual regression baselines only after the screenshots look stable and useful.

This gives Codex and developers immediate visual feedback in VS Code while keeping the workflow lightweight enough for an ESP32 project.
