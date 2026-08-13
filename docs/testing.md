# Testing and Simulation

**Status:** Current repository workflows, verified on 2026-08-13.

Dog-RGB uses several layers because no single test environment can validate firmware logic, embedded HTML, radio behavior, and real electrical safety.

## Verification matrix

| Layer | What it catches | What it does not prove |
| --- | --- | --- |
| PlatformIO build | Toolchain, libraries, board target, partitions, compilation/linking | Runtime behavior or physical wiring |
| Python host contracts | Persistence recovery, time rollover, track integrity/retention/streaming, Wi-Fi queue/backoff, Wokwi assets, LED/source/API boundaries | Native execution of every target-specific C++ branch |
| Native LED/scene characterization | Pure renderer goldens, registry metadata, layout, policy, scene wire/player/store and JSON codec | Physical color, ESP32 heap/timing, electrical or thermal behavior |
| Static portal smoke | Embedded page size budgets, escaping rules, required functions, write-header use | Browser layout or real ESP32 heap behavior |
| Playwright | Portal interactions, accessibility assertions, mock API states, mobile layout | ESP32 networking/radio timing |
| Visual regression | Pixel drift against reviewed Linux baselines | Usability judgment or physical display appearance |
| Wokwi scenarios | Real firmware image, GNSS UART, LED buses, resets, modes, faults, loop diagnostics | Battery, boost, antenna, waterproofing, heat, comfort |
| Physical bench/field tests | Electrical, RF, GNSS, thermal, runtime, mechanical, and weather behavior | Only the exact tested build and conditions |

## Prerequisites

- Python 3.13 for parity with CI (the host suite uses the standard library).
- PlatformIO Core.
- Node.js 24.18.0, exactly matching [`.node-version`](../.node-version), whenever portal assets are regenerated or browser tests run.
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
- all 12 LED renderers at fixed times and seed, stable effect/palette metadata, segment guards, policy-priority boundaries, semantic layout/orientation, mirror, RGBW round-trip, crossfade and alert preemption;
- `SceneV1` 44-byte wire goldens, four built-ins, ID/key/version rules, validation boundaries, manual/Show player semantics, stale snapshots, bag shuffle and `millis()` wrap;
- the 196-byte scene-bank A/B machine against a fake backend, including torn/corrupt/future/ambiguous records, read/write/readback failure, generation wrap and 1,000 deterministic power-cycle/fault sequences;
- strict scene JSON allowlists, types and ID/key consistency, exact 4096/4097-byte boundary, nesting 6/7, export/import round-trip, dry-run and negative secret scanning;
- Wokwi diagrams, custom GNSS chip assets, scenarios, and analysis contracts.

Most modules use source-contract assertions. Phase 2 compiles `effect_registry`, `led_policy`, and `led_state` as native C++17 with warnings treated as errors. Phase 3 adds a harness for `led_color`, `palette_registry`, `led_layout`, and `led_compositor`; it proves a non-black crossfade midpoint and next-frame alert interruption. Phase 4 compiles the scene model/catalog/player/store plus the ArduinoJson codec natively, with fault injection at the record backend. The complete local suite baseline is 131/131. None of these layers replaces target execution or physical validation.

## Portal checks

From the repository root:

```powershell
npm ci
npm run webui:check
npm run webui:unit
npm run smoke
npx playwright test --project=iphone-13-pro-max-chromium
```

`webui:check` regenerates expected tracked outputs in memory and proves that the manifest and flash arrays match `webui/src`. `webui:unit` contains four tests for canonical gzip metadata, CRLF/LF fingerprints, binary C++ array rendering, and complete manifest/array/decoded-byte equivalence. `npm run smoke` verifies source contracts, capability-driven UI, input/output hashes, gzip payloads, budgets, generated arrays, and the HTTP-serving contract.

Both `webui:unit` and smoke are clean-checkout safe: they validate authoritative tracked arrays directly and do not require `.ap-portal-preview/` to exist. When preview files do exist, smoke additionally compares them byte-for-byte. The gzip unit test fixes timestamp and OS metadata, so the same sources generate identical compressed bytes on Windows and Unix.

The default preview port is 4173. If another project already owns it, select an isolated port instead of stopping an unrelated process:

```powershell
$env:AP_PORTAL_PREVIEW_PORT = '4184'
npx playwright test --project=iphone-13-pro-max-chromium
Remove-Item Env:AP_PORTAL_PREVIEW_PORT
```

Useful focused commands:

```powershell
npm run webui:build
npm run ap-portal:serve
npm run ap-portal:screenshots
npm run ap-portal:ui
```

The preview serves the exact decompressed production bundles generated from `webui/src`. Disposable HTML lives in `.ap-portal-preview/`; the manifest and C++ gzip arrays are tracked so an offline PlatformIO build can verify and embed them without running npm. The PlatformIO pre-script uses only Python's standard library to validate canonical input sizes/hashes, the aggregate source fingerprint, and generated-output hashes before compilation.

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

Fase 4 adds software diagnostics for scene-save duration, LED gap during a write, store recovery and player counters. Its build is covered locally, but the HTTP/live-runtime gate still requires Wokwi CLI plus a token or a physical ESP32: exercise all seven scene routes, apply visibility within one LED tick, reboot recovery, heap after 100 save/import cycles and the 100 ms maximum write gap.

For interactive controls, GNSS profiles, GDB, VCD channels, and portal-network limitations, read the detailed [Wokwi guide](../Platformio/Dog-RGB/docs/wokwi.md).

## CI

`.github/workflows/ci.yml` runs on pushes to `main` and pull requests:

- **Host tests:** the complete Python firmware contract suite;
- **Portal:** stale-asset check, four deterministic generator unit tests, clean-checkout static smoke, and Playwright behavior/a11y tests;
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
