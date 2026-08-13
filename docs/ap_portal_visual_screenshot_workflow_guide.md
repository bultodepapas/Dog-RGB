# AP Portal Visual Verification

**Status:** Current developer workflow, reviewed on 2026-08-13.

Use this workflow after changing portal markup, styles, scripts, the generator, or API fixtures. It builds the editable files under `webui/src`, serves the same decompressed bundles embedded in firmware, mocks device APIs, and exercises mobile layouts with Playwright.

It validates UI behavior and pixel output; it does not replace a firmware build or a real-device AP test.

## Prerequisites

- Node.js 24, matching [`.node-version`](../.node-version)
- Python 3
- npm dependencies and the pinned Chromium build

Install reproducibly from the repository root:

```powershell
npm ci
```

`postinstall` installs Chromium. On a minimal Linux runner, system packages may also require `npx playwright install-deps chromium`.

## Normal change loop

```powershell
# Fast source/contract checks
npm run webui:check
npm run webui:unit
npm run smoke

# Generate the reviewed mobile states
npm run ap-portal:screenshots

# Compare against committed baselines on Linux/macOS
npm run ap-portal:visual
```

Review the generated images and Playwright report. If a visual change is intentional, regenerate baselines only after examining all affected states:

```bash
npm run ap-portal:visual:update
```

The two visual package scripts set an environment variable with POSIX syntax. On PowerShell, run the equivalent commands explicitly:

```powershell
$env:AP_PORTAL_VISUAL = '1'
npx playwright test tests/ap-portal-visual/ --project=iphone-13-pro-max-chromium
# Add --update-snapshots only after approving an intentional change.
Remove-Item Env:AP_PORTAL_VISUAL
```

Linux baselines are authoritative in CI. To regenerate them in the same pinned Playwright container, use:

```powershell
npm run ap-portal:visual:baseline
```

The helper uses Bash/container tooling; run it in an environment that provides those dependencies.

## Other commands

| Command | Purpose |
| --- | --- |
| `npm run webui:build` | Regenerate manifest, flash arrays and disposable preview HTML |
| `npm run webui:check` | Fail if tracked generated assets are stale or over budget |
| `npm run webui:unit` | Test deterministic gzip and binary C++ rendering |
| `npm run ap-portal:serve` | Serve the preview at `http://127.0.0.1:4173` |
| `npm run ap-portal:screenshots` | Run deterministic mobile screenshot scenarios |
| `npm run ap-portal:visual` | Enable snapshot comparisons |
| `npm run ap-portal:visual:update` | Replace snapshots for the current platform |
| `npm run ap-portal:visual:baseline` | Rebuild Linux baselines in the pinned container |
| `npm run ap-portal:ui` | Open Playwright UI mode |

The scripts themselves are the source of truth; check [`package.json`](../package.json) if a command changes.

## What to inspect

- no horizontal overflow, clipped labels, overlaps, or accidental scroll traps;
- readable hierarchy at the configured phone viewport;
- touch targets, visible focus, labels, error messages, and disabled/loading states;
- realistic values with no `undefined`, `NaN`, broken placeholders, or raw errors;
- advanced diagnostics subordinate to normal user tasks;
- correct behavior for no-fix, empty history, lock enabled, failed request, scanning, open network, and long SSID states;
- page weight and source-size limits suitable for an ESP32-hosted portal.

Fixtures live under `tests/ap-portal-visual/fixtures/`. Keep them deterministic, small, and aligned with the real response contracts in [api-reference.md](api-reference.md). An unmocked `/api/...` dependency should fail loudly.

## Failure triage

1. Open the expected, actual, and diff images in `test-results/` or the HTML report.
2. Decide whether the change is intended or a regression.
3. Fix source markup/fixtures and rerun when unintended.
4. When intended, review every affected viewport and state before updating snapshots.
5. Build the firmware after any portal change; browser checks cannot catch target linking, flash-size or `send_P` regressions.

CI runs static portal checks, the complete Playwright project, and visual comparisons in the pinned Linux image. See [Testing and simulation](testing.md) for the full matrix.
