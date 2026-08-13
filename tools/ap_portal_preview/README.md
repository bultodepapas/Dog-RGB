# Embedded Portal Preview

This development tool extracts the four HTML pages embedded in `Platformio/Dog-RGB/src/web/pages.cpp`, serves them locally, and supplies fixture API responses for Playwright. It does not run the ESP32 firmware or emulate radio/storage timing.

From the repository root, install the locked Node dependencies once:

```powershell
npm ci
```

## Preview locally

```powershell
npm run ap-portal:extract
npm run ap-portal:serve
```

Open `http://127.0.0.1:4173/`. The server extracts pages at startup as well, so the explicit extract command is mainly useful for inspecting generated output.

Generated files live under `tools/ap_portal_preview/generated/` and are ignored by Git.

## Tests and screenshots

```powershell
npm run smoke
npm run ap-portal:screenshots
npm run ap-portal:ui
```

`ap-portal:screenshots` runs interaction screenshots without requiring pixel comparison. On Linux/macOS, `npm run ap-portal:visual` enables committed baseline comparison. On PowerShell, use:

```powershell
$env:AP_PORTAL_VISUAL = '1'
npx playwright test tests/ap-portal-visual/ --project=iphone-13-pro-max-chromium
Remove-Item Env:AP_PORTAL_VISUAL
```

After an intentional visual change, regenerate deterministic Linux baselines with:

```text
npm run ap-portal:visual:baseline
```

Review the actual/expected/diff output before accepting changes. The helper uses the pinned Playwright 1.62.1 Noble container to match CI.

Full workflow and fixture/baseline rules: [Visual screenshot guide](../../docs/ap_portal_visual_screenshot_workflow_guide.md). Repository-wide matrix: [Testing and simulation](../../docs/testing.md).
