# AP Portal Visual Screenshot Workflow Guide

This guide explains how to preview, screenshot, and visually review the DOG-RGB Access Point portal from VS Code or a terminal without flashing the ESP32.

Use this workflow whenever you change the portal UI in `Platformio/Dog-RGB/src/web/pages.cpp`.

## What The Workflow Does

- Extracts the embedded HTML pages from `pages.cpp`.
- Serves `/`, `/wifi`, `/config`, and `/dev` locally.
- Uses Playwright to emulate an iPhone 13 Pro Max style mobile viewport.
- Mocks portal API responses with deterministic JSON fixtures.
- Generates screenshots for the important screen states.
- Optionally compares the UI against approved visual baselines.

The workflow is for UI review. It does not replace the firmware build or real-device AP testing.

## One-Time Setup

Install Node dependencies:

```bash
npm install
```

Install Playwright browsers:

```bash
npx playwright install chromium
```

Optional VS Code setup:

- Install the official Playwright extension from Microsoft.
- Open the Testing sidebar.
- Use the `iphone-13-pro-max-chromium` project when running AP portal visual tests.

## Daily UI Change Loop

1. Plan the UI change and list affected screens.
2. Edit `Platformio/Dog-RGB/src/web/pages.cpp`.
3. Run the embedded page smoke test:

```bash
python3 tools/web_pages_smoke.py
```

4. Generate mobile screenshots:

```bash
npm run ap-portal:screenshots
```

5. Open the generated screenshots:

```text
tests/ap-portal-visual/screenshots/current/
```

6. Review every affected screenshot carefully.
7. Fix any visual or usability problems in `pages.cpp`.
8. Repeat screenshot generation and review until the UI is acceptable.
9. Run optional visual regression comparison:

```bash
npm run ap-portal:visual
```

10. Update reference screenshots only after manual review confirms the changes are intentional:

```bash
npm run ap-portal:visual:update
```

11. Run firmware validation:

```bash
cd Platformio/Dog-RGB
/Users/angel/.platformio/penv/bin/platformio run
```

## Commands

Extract embedded pages:

```bash
npm run ap-portal:extract
```

Serve local preview pages:

```bash
npm run ap-portal:serve
```

Generate current screenshots:

```bash
npm run ap-portal:screenshots
```

Run visual comparisons against reference screenshots:

```bash
npm run ap-portal:visual
```

Update visual reference screenshots:

```bash
npm run ap-portal:visual:update
```

Open Playwright UI Mode:

```bash
npm run ap-portal:ui
```

## Screenshot Output

Current screenshots are written to:

```text
tests/ap-portal-visual/screenshots/current/
```

These are generated review artifacts and are ignored by git.

Reference screenshots created by Playwright visual comparisons live beside the test file in Playwright snapshot directories. Commit those only when the UI state is intentionally approved.

## Screen Review Checklist

For each screenshot, check:

- The first visible screen explains the task clearly.
- There is no horizontal scrolling.
- Buttons and inputs fit within the mobile viewport.
- Text does not overlap, clip, or wrap awkwardly.
- Status values are realistic and understandable.
- No `undefined`, `NaN`, accidental `--`, or broken API placeholders appear.
- Advanced sections do not dominate normal user tasks.
- Raw JSON is collapsed unless the specific screenshot intentionally opens it.
- Tap targets are large enough for a phone.
- The page still feels lightweight enough for an ESP32-hosted portal.

## Mock Data

Mock API fixtures live in:

```text
tests/ap-portal-visual/fixtures/
```

Update fixtures when a UI change needs a new screen state.

Keep fixture data:

- Deterministic.
- Realistic.
- Small.
- Representative of actual ESP32 API payloads.

If a test hits an unmocked `/api/...` route, the preview server or Playwright test should fail clearly. Add the missing mock instead of allowing silent broken UI.

## When Screenshots Fail

If `npm run ap-portal:visual` fails:

1. Open the actual, expected, and diff images from Playwright output.
2. Decide whether the change is intentional.
3. If the change is not intentional, fix the UI and rerun screenshots.
4. If the change is intentional, inspect all affected screenshots before updating references.
5. Do not update reference screenshots just to make the test pass.

## Codex Completion Rule

When Codex changes the AP portal UI, completion should include:

- Static smoke test passed.
- Mobile screenshots generated.
- A visual review summary of the affected screens.
- Firmware build passed if the embedded C++ file changed.

For UI work, screenshots are development evidence, not decoration.
