# Testing and Simulation

**Status:** Current repository workflows, verified on 2026-08-12.

Dog-RGB uses several layers because no single test environment can validate firmware logic, embedded HTML, radio behavior, and real electrical safety.

## Verification matrix

| Layer | What it catches | What it does not prove |
| --- | --- | --- |
| PlatformIO build | Toolchain, libraries, board target, partitions, compilation/linking | Runtime behavior or physical wiring |
| Python host contracts | Persistence recovery, time rollover, track integrity/retention/streaming, Wi-Fi queue/backoff, Wokwi assets | Native execution of every C++ branch |
| Static portal smoke | Embedded page size budgets, escaping rules, required functions, write-header use | Browser layout or real ESP32 heap behavior |
| Playwright | Portal interactions, accessibility assertions, mock API states, mobile layout | ESP32 networking/radio timing |
| Visual regression | Pixel drift against reviewed Linux baselines | Usability judgment or physical display appearance |
| Wokwi scenarios | Real firmware image, GNSS UART, LED buses, resets, modes, faults, loop diagnostics | Battery, boost, antenna, waterproofing, heat, comfort |
| Physical bench/field tests | Electrical, RF, GNSS, thermal, runtime, mechanical, and weather behavior | Only the exact tested build and conditions |

## Prerequisites

- Python 3.13 for parity with CI (the host suite uses the standard library).
- PlatformIO Core.
- Node.js 24, matching [`.node-version`](../.node-version).
- `npm ci` from the repository root for Playwright 1.62.1 and Chromium.
- Optional: Wokwi CLI 0.26.x and a personal CI token for simulator automation.

## Firmware build

From `Platformio/Dog-RGB`:

```powershell
pio run -e seeed_xiao_esp32s3
```

The simulation build is separate so UART0 routing and LED transport throttling never leak into the physical image:

```powershell
pio run -e wokwi
```

## Host contract tests

From `Platformio/Dog-RGB`:

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

The suite covers:

- active-time observations and bounded GNSS gaps;
- date transitions, leap/calendar boundaries, and the completed-day journal;
- CRC/generation selection for config, metrics, sessions, Home, Wi-Fi credentials, and route chunks;
- current plus three completed session behavior;
- two-hour route retention and bounded streaming in three formats;
- `millis()` rollover-safe intervals/deadlines;
- Wi-Fi event queue ownership, saturation diagnostics, AP retry backoff, and reconciliation;
- Wokwi diagrams, custom GNSS chip assets, scenarios, and analysis contracts.

These tests include source-contract assertions. They are useful regression guards, but are not a replacement for native C++ unit tests or target execution.

## Portal checks

From the repository root:

```powershell
npm ci
npm run smoke
npx playwright test --project=iphone-13-pro-max-chromium
```

`npm run smoke` runs `tools/web_pages_smoke.py`. The full Playwright command starts the local extracted-page server automatically and runs tests under `tests/`.

Useful focused commands:

```powershell
npm run ap-portal:extract
npm run ap-portal:serve
npm run ap-portal:screenshots
npm run ap-portal:ui
```

The preview tool extracts the C++ raw HTML templates into generated files under `tools/ap_portal_preview/generated/`; that output is disposable and ignored by Git.

## Visual regression

On Linux/macOS, use the package script:

```bash
npm run ap-portal:visual
```

On PowerShell, set the flag explicitly because the package script uses POSIX environment syntax:

```powershell
$env:AP_PORTAL_VISUAL = '1'
npx playwright test tests/ap-portal-visual/ --project=iphone-13-pro-max-chromium
Remove-Item Env:AP_PORTAL_VISUAL
```

Committed baselines live next to `tests/ap-portal-visual/ap-portal.visual.spec.ts` and use the `-linux` suffix. They were generated with the Playwright 1.62.1 Noble container used in CI. Host rendering differences can cause noise.

After an intentional visual change:

1. Run behavior/a11y tests first.
2. Generate actual/expected/diff artifacts.
3. Review every state, including empty, degraded, validation, Wi-Fi, and route views.
4. Regenerate deterministic Linux baselines with:

```powershell
npm run ap-portal:visual:baseline
```

5. Run `npm run ap-portal:visual` again before committing.

The baseline helper requires a Docker-compatible runtime because its shell script uses the pinned Linux container. See [Visual screenshot workflow](ap_portal_visual_screenshot_workflow_guide.md).

## Wokwi

Copy `Platformio/Dog-RGB/.env.example` to `Platformio/Dog-RGB/.env` and replace the placeholder with a token. The local `.env` is ignored; never commit it.

From `Platformio/Dog-RGB`:

```powershell
.\tools\wokwi.ps1 -Action prepare
.\tools\wokwi.ps1 -Action suite -TimeoutMs 90000
```

Focused scenarios:

```powershell
.\tools\wokwi.ps1 -Action test -Scenario wokwi/boot.test.yaml
.\tools\wokwi.ps1 -Action test -Scenario wokwi/modes.test.yaml -TimeoutMs 90000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/session-persistence.test.yaml -TimeoutMs 45000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-profiles.test.yaml -TimeoutMs 60000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-faults.test.yaml -TimeoutMs 90000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/speed-validity.test.yaml -TimeoutMs 25000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/loop-diagnostics.test.yaml -TimeoutMs 20000
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-rate-ranges.test.yaml -TimeoutMs 60000
```

The wrapper builds the `wokwi` environment, compiles the custom NMEA chip, generates/validates the diagram, runs scenarios, captures serial/VCD evidence, and applies `tools/analyze_wokwi.py` checks. Transient backend WebSocket closures are retried; firmware assertions are not.

For interactive controls, GNSS profiles, GDB, VCD channels, and portal-network limitations, read the detailed [Wokwi guide](../Platformio/Dog-RGB/docs/wokwi.md).

## CI

`.github/workflows/ci.yml` runs on pushes to `main` and pull requests:

- **Host tests:** the complete Python firmware contract suite;
- **Portal:** static smoke plus Playwright behavior/a11y tests;
- **Visual:** screenshot comparison in the pinned Playwright container;
- **Firmware:** pinned PlatformIO production build, size report, environment/package inventory, hashes, and downloadable binary/ELF/partition evidence.

Failure artifacts retain Playwright reports or visual diffs for seven days; firmware baseline artifacts are retained for 14 days. Wokwi is intentionally not a default CI job because it needs an external token/service and can be run explicitly.

## Physical validation checklist

Before calling a build field-ready, record at minimum:

- 5 V and 3.3 V rails at idle, representative animation, Wi-Fi transmit, and worst intended brightness;
- current at the cell and 5 V output, converter efficiency, connector/wire drop, and brownout margin;
- temperatures at the cell, charger/BMS, boost, MCU, and strip after sustained operation;
- GNSS acquisition/quality and route comparison in open sky and representative surroundings;
- AP visibility, station retry, and phone captive-portal behavior;
- runtime using the actual cell and intended effect/Day Mode profile;
- strain relief, fit, sharp edges, flex cycles, and controlled water-ingress checks.

Store measurements with date, hardware revision, firmware commit, instruments, ambient conditions, and pass/fail limits. Estimates in the BOM are planning inputs, not evidence.
