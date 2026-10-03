# Testing and Simulation

**Status:** Current repository workflows, reconciled on 2026-08-24. Hardware and hosted evidence remain separate gates.

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
| Next.js production build | Web-portal bundling, route compilation, and TypeScript integration | Auth/product behavior or browser E2E |
| Local Supabase clean gate | Fresh migrations, pgTAP, grants/RLS, Edge boundaries, simulator replay, generated-type drift, and local operations | Hosted limits, TLS, email delivery, backups, or latency |
| Wokwi scenarios | Real firmware image, GNSS UART, LED buses, resets, modes, faults, loop diagnostics | Battery, boost, antenna, waterproofing, heat, comfort |
| Physical bench/field tests | Electrical, RF, GNSS, thermal, runtime, mechanical, and weather behavior | Only the exact tested build and conditions |
| Phase 0 cloud protocol suite | JSON schemas/refs, valid/invalid fixtures, semantic hashes/identities, problem catalog, HLC vectors and compatibility matrix | Deployed Edge/Postgres behavior or firmware integration |
| Phase 0 v3/storage model | Exact byte codec, retention arithmetic, deterministic seal/ACK/reclaim/cut/migration behavior | Physical ESP32 flash timing, actual LittleFS trace, brownout, wear or energy |
| PostgreSQL capacity fixture | One-million-point local storage/index/query-plan comparison | Hosted Supabase latency, RLS concurrency, egress, plan price or service limits |
| Map bake-off harness | Identical synthetic Colombian overlays/styles at fixed viewports | Real route/trail accuracy, full provider comparison without both credentials, or product usability |

## Prerequisites

- Python 3.13 for parity with CI (the host suite uses the standard library).
- PlatformIO Core.
- Node.js 24.18.0, exactly matching [`.node-version`](../.node-version), whenever portal assets are regenerated or browser tests run.
- npm exactly matching `packageManager` in the root `package.json` and Supabase CLI exactly matching [`.supabase-version`](../.supabase-version) for cloud artifacts.
- Docker or a compatible runtime for the disposable localhost-only Supabase stack.
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

Most modules use source-contract assertions. Phase 2 compiles `effect_registry`, `led_policy`, and `led_state` as native C++17 with warnings treated as errors. Phase 3 adds a harness for `led_color`, `palette_registry`, `led_layout`, and `led_compositor`; it proves a non-black crossfade midpoint and next-frame alert interruption. Phase 4 compiles the scene model/catalog/player/store plus the ArduinoJson codec natively, with fault injection at the record backend. The current Display/I4 local suite is 146/146, including the native LCD and actual configuration codec/save/load harnesses; see [I4 evidence](baselines/display-i4-2026-09-12.md). None of these layers replaces target execution or physical validation.

## Embedded AP portal checks

From the repository root:

```powershell
npm ci
npm run webui:check
npm run webui:unit
npm run smoke
npx playwright test --project=iphone-13-pro-max-chromium
```

`webui:check` regenerates expected tracked outputs in memory and proves that the manifest and flash arrays match `webui/src`. `webui:unit` contains four tests for canonical gzip metadata, CRLF/LF fingerprints, binary C++ array rendering, and complete manifest/array/decoded-byte equivalence. `npm run smoke` verifies source contracts, capability-driven UI, input/output hashes, gzip payloads, budgets, generated arrays, and the HTTP-serving contract.

The root Playwright configuration excludes `tests/portal-e2e/`, which belongs to the separate `playwright.portal.config.ts` and its own fixture lifecycle. Embedded tests do not require that application environment.

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

## Web portal and local Supabase baseline

The Vercel-targeted application is a separate Next.js workspace under `apps/portal`; do not confuse it with the embedded AP portal above. Its current production compilation gate is:

```powershell
npm run portal:build
```

Database types are generated only from a freshly migrated local `api` schema. Migrations remain authoritative; the generated file is a portal client artifact:

```powershell
supabase start
supabase db reset
npm run cloud:types:generate
npm run cloud:types:check
```

The destructive/reproducible local foundation command is explicit and targets only this repository's disposable local Supabase project:

```powershell
npm run phase1:local -- --clean
```

It recreates the local database, checks the committed `api` types, runs pgTAP, lint/advisors, contracts, Edge boundaries, simulator flows, and the retained local operations drills. Never expose this development stack beyond localhost.

The maintained M1.13–M1.16 owner/authorization/fault/privacy gate is a **separate** command. Both commands reset the repository's disposable local database; preserve any wanted development data first. Never use a hosted or `--linked` database:

```sh
node tools/portal-e2e/run.mjs --clean
# Twice-clean owner/authorization/fault/privacy matrix only:
node tools/portal-e2e/run.mjs --clean --core-only
# Focused privacy/cache rerun only:
node tools/portal-e2e/run.mjs --clean --m116-only
# One clean privacy + accessibility/performance + WebKit + deletion cycle:
node tools/portal-e2e/run.mjs --clean --quality-only

# Focused keyboard/accessibility, WebKit, lifecycle and privacy; retains earlier performance evidence
node tools/portal-e2e/run.mjs --clean --interaction-only
```

The E2E runner builds/starts the production portal and uses synthetic fixtures, Mailpit and the simulator with sanitized artifacts. CI invokes it for relevant portal/schema/contract/runner changes and published releases. The full command also runs Chromium axe/layout/performance measurements and a WebKit mobile smoke; install both browsers with `npx playwright install chromium webkit` first (`--with-deps` on Linux). The focused `--core-only` command preserves the twice-clean owner/authorization/fault/privacy matrix without the quality/lifecycle extension. `--m116-only` runs only two privacy cycles. `--quality-only` runs those gates and one privacy cycle; it does not replace the twice-clean owner/authorization/fault matrix. Results belong under `output/playwright/quality`; automated findings do not substitute for the manual assertions in the [master plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md). The AP/audit screenshot suites are not Next.js owner-portal evidence.

For a non-reset source/build check use `npm run phase1:check` and `npm run portal:build`. Run the opt-in summary worker with `node tools/cloud_analytics/run.mjs`; it consumes bounded batches and does not install a schedule. Acceptance results and remaining limits belong in the current cloud evidence, separately from historical review runs.

Focused portal gates clear only their own artifact directories: `--quality-only` preserves M1.13–M1.15 reports and clears the quality reports before execution. `--interaction-only` runs the same acceptance surfaces without repeating performance measurements; it writes `output/playwright/interaction` and preserves `output/playwright/quality`. Its report explicitly records `performanceExecuted: false`. Both run one clean privacy cycle. Owner-journey failures retain bounded phase/category/React-code/HTTP-status diagnostics, never raw console messages, stacks, URLs, credentials or response bodies. The quality matrix includes the account-receipt recovery page; account lifecycle tests exercise the current prepare/finalize/receipt/acknowledge protocol.

Keyboard checks use actual Tab/Shift+Tab at 320/1280 px, visible focus, skip-to-content and reduced-motion emulation. Revoke uses Enter/Space/Escape; malformed receipt recovery checks status focus and keyboard retry. These are automated assertions, not human acceptance. The receipt-expiry fixture signs a valid control and an expired token using the running disposable local Auth key in memory; it never changes clocks, Auth configuration or database rows, and never retains keys/tokens in artifacts. The control must recover the original receipt before the expired session is rejected.

The same runner also retains `product-states.json`, `summary-replay.json` and `export-recovery.json`. Product states exercise claim expiry with browser-only virtual time, scoped/restored stale and rejected device reports, failed brightness submission with same-value retry, and viewer read-only controls. This runs before M1.16 changes desired brightness. Summary replay uses real simulator ingestion, exact retry and a late upload, the existing bounded producer, visible invalidation/recomputation and teardown of its temporary dog. Export recovery uses valid/expired signed sessions without usable refresh on both download routes, a transport failure and a real browser download retry. These helpers write fixed stages/counts/flags; raw downloads, sessions and device requests remain transient. The quality report also records return from login to an authorized recording deep link.

If another project owns port 3000, focused `--quality-only` / `--interaction-only` / `--m116-only` accept `PORTAL_E2E_PORT` (1024–65535) and pass that exact loopback origin to the portal. Example in PowerShell: set `$env:PORTAL_E2E_PORT = '3107'`, run the focused command, then `Remove-Item Env:PORTAL_E2E_PORT`. Full/owner gates require port 3000 because the local Auth email redirect allowlist and Mailpit checks intentionally pin it.

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

`.github/workflows/ci.yml` runs on pushes to `main`, pull requests and published releases:

- **Host tests:** the complete Python firmware contract suite;
- **Web portal:** the Next.js production build; relevant changes/releases also run the maintained clean owner/authorization/fault/privacy matrix, Chromium quality checks and WebKit smoke;
- **Embedded AP portal:** stale-asset check, deterministic generator tests, clean-checkout static smoke, and Playwright behavior/a11y tests;
- **Embedded AP visual:** screenshot comparison in the pinned Playwright container;
- **Cloud foundation:** clean local Supabase reset, database/Edge/simulator/operations gates, committed `api` type drift, and the capacity fixture;
- **Firmware:** pinned PlatformIO production build, size report, environment/package inventory, hashes, and downloadable binary/ELF/partition evidence.

Failure artifacts retain Playwright reports or visual diffs for seven days; firmware baseline artifacts are retained for 14 days. Wokwi is intentionally not a default CI job because it needs an external token/service and can be run explicitly.

## Cloud foundation evidence and remaining pre-product artifacts

The commands in this section are local engineering gates. They do not deploy a hosted website/project or add a firmware cloud client.

### V3 codec and outbox model

From the repository root:

```powershell
python -m unittest discover -s tools/cloud_phase0 -p "test_*.py" -v
python tools/cloud_phase0/generate_evidence.py --format markdown
```

Native firmware codec interoperability (Python standard library and C++ compiler):

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_track_v3_native.py' -v
```

The harness compiles the actual C++ codec and compares binary frames with the
frozen Python implementation, including its four reference fixtures. SHA-256
callbacks in the host harness use Python `hashlib`; target builds compile the
SDK mbedTLS adapter. This is codec evidence, not physical storage acceptance.
See [M2.4a evidence and environment](cloud/m24a-track-v3-codec-evidence.md).

Durable identity/boot allocation and the NVS adapter:

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_device_identity*.py' -v
```

These native tests inject byte-image write/read faults and exercise the actual
SDK adapter against a fake NVS API. They require no board or credentials and run
in the existing firmware host suite. See [M2.3a scope and recovery rules](cloud/m23a-device-identity-evidence.md).

The native identity/codec bridge is tested separately:

```sh
python3 -m unittest discover -s Platformio/Dog-RGB/test -p 'test_chunk_assembler_native.py' -v
```

This exercises bounded batching, temporal boundaries, reservation/encoding retries
and local-sink backpressure using the real identity service and codec. See
[M2.4b handoff contract and evidence](cloud/m24b-chunk-assembly-evidence.md).

The superseded RAM-only suite passed 20/20, but that historical green result is invalid recovery/reclaim evidence: it accepted `acknowledge_through(999)` after only chunks `0..2` existed and then reclaimed all three. It also recovered from retained Python objects instead of constructing a fresh runtime from persisted flash bytes.

The byte-addressed model reconstructs from NOR bytes, retains durable logical
identity, validates exact ACKs, and reclaims only a contiguous proven prefix.
Journal v3 and emergency v2 preserve identity/loss state across modeled cuts.
The 67/67 host suite and 10/10 verifier suite support the [independent AI
acceptance](cloud/phase0-outbox-remediation-review-2026-10-02.md) at `fb6dbef`.
Its 664 chunks/63,744 points remain modeled geometry. Preserve all regressions
and source pins; storage changes require regenerated evidence and review.

Passing this model does not close the hardware gate. Before the M2 firmware exit, execute at least 10,000 production-codec seal/ACK/reclaim cycles on the target ESP32-S3 with randomized physical reset/power removal at data/header/metadata/ACK/erase boundaries. Record mount/recovery latency, maximum GNSS/LED/cooperative-loop gap, watchdog margin, heap, programmed/erased bytes and sector distribution, current/energy by cadence, full-pressure/loss-marker behavior, and legacy preservation. The raw-ring ADR must be revisited if metadata wear concentration or timing is unsafe.

### Device-v1 protocol

```powershell
node --test contracts/device-v1/test-contracts.mjs
```

This dependency-free suite must validate every schema/reference, positive/negative fixture, canonical hash, point/chunk/ACK identity, sequence hole/final rule, LWW/HLC vector, problem behavior, local-only exclusion, and compatibility tuple.

Phase 0 requires a cross-implementation gate, not two independently green suites. The protocol tuple/hash/flags/time-quality/chunk bounds/legacy encoding must exactly match `tools/cloud_phase0/track_v3.py`; generated native payload bytes must validate under the JSON semantic tests and vice versa. Any future disagreement stops schema work. On 2026-08-13 the complete protocol suite passed 48/48. The contract tests cover the six-value time-quality mapping, exact chunk ACK identity, out-of-order holes, and dedicated revoke identity/exact-replay/disposition behavior; their wire-vector check matches the Python codec. This closes protocol reconciliation only. The Python storage model passes 67/67 and has independent AI host acceptance; this does not validate physical storage, the map provider or the firmware integration.

### PostgreSQL capacity evidence

The [capacity report](cloud/phase0-capacity-benchmark.md) records the exact container/image/environment and runner. It loaded one million synthetic Track-v3-shaped points and measured heap/index sizes and representative plans. It supports an initially unpartitioned table and no GiST index until a spatial query justifies one.

Reproduction provisions a disposable local PostgreSQL container and one million rows, so review the runner and Docker resources before executing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/cloud_capacity/run.ps1
```

The result is local sizing evidence only. M3 must repeat representative authenticated/RLS queries in hosted development, and M5 must recheck current storage, egress, backup, Edge limits, and pricing before production authorization. A free development tier is not a field-retention commitment.

### Colombian map bake-off

Start the checked synthetic harness:

```powershell
node tools/map_bakeoff/server.mjs --port 4174
```

For the complete repeatable Chromium capture, use the runner instead; it owns an isolated local server and writes screenshots plus a hashed manifest:

```powershell
node tools/map_bakeoff/capture-evidence.mjs
```

Then render the same six invented urban, park, steep-trail, rural, sparse approximately-one-kilometre, and dense two-hour fixtures at `1280×720` desktop and exact `428×844` mobile, both at DPR `1` and `2`, across dark/light/outdoor variants. Run the label-deemphasis, CVD-approximation, cache-disabled, and throttled-network diagnostics. Use the exact provider URLs in [`tools/map_bakeoff/README.md`](../tools/map_bakeoff/README.md). Temporary provider credentials belong only in the documented local secret path and never in source, screenshots, or server logs.

For every provider/style retain:

- exact date, browser/version, DPR/screenshot scale, MapLibre/style version and URLs;
- cold/warm state, screenshots, all-maps-idle/fatal state, attribution and overflow checks;
- console/CORS/tile errors, request/tile failure counts, transferred bytes and readiness timings;
- two independent weighted rubric sheets and current price/terms/origin-restriction evidence;
- a network proof that route coordinates never enter provider requests.

The checked schema-v2 [2026-08-13 evidence manifest](../tools/map_bakeoff/evidence/2026-08-13/manifest.json), SHA-256 `4509749e573e27a2d82e6ba2247bccb1c0d6a9d87f4f0f4f1fecd3f4b968decb`, records Chromium `151.0.7922.34`, pinned MapLibre `5.23.0`, source/style/screenshot hashes, all viewports/DPRs, network profiles/origins/failures, console/page errors, route-coordinate leak assertions, accessible regions/table, layout, attribution, and credential blockers. That retained run passed its capture-time 7/7 unit suite and 17/17 requested Stadia matrix/diagnostic cells. The current credentialed-runner readiness suite passes 12/12, including secret redaction, child-environment isolation, safe request descriptors, non-overwriting run IDs, and URL-free init-script injection. These readiness/byte values are diagnostics under incompletely controlled OS/CDN caching, not a performance SLO; CVD filters require human review. MapTiler has still not been rendered and Stadia unapproved-origin rejection has not been exercised because temporary provider credentials remain unavailable. Therefore MapLibre is accepted, Stadia Dark is only provisional, and the credentialed/two-reviewer map gate remains open; no score/result may be fabricated. See [ADR-0009](adr/0009-map-renderer-provider-and-colombia-bakeoff.md).

## Remaining cloud/web verification strategy

Every phase adds its tests without weakening the current local suite:

| Layer | Required evidence before the phase exits |
| --- | --- |
| Local Supabase migrations | clean reset from zero; explicit grants/default privileges; private schema not exposed; service-only wrapper denial to public/anon/authenticated; constraints/indexes; migration lint |
| RLS/Auth | anonymous, other user, former member, viewer/editor/owner, user-controlled metadata, crafted URL/REST/RPC, delete/cascade, email confirmation/recovery/session/logout cases |
| Edge gateway/device simulator | claim expiry/attempt/concurrent consume; unique credential; website revoke; device `REVOKE_PENDING`; exact replay returning original disposition; lost response; prior website/different-request revoke returning `already_revoked`; generic error retaining state; forced-clear warning; content/depth bounds; same-ID/different-hash; out-of-order chunks/holes/finals; transaction rollback; safe problems/logs |
| Configuration | every AP/web/sync order; all HLC ties/trust/rebase/overflow cases; no-op/stale editor; capability mismatch; validation/storage rejection; reboot at each A/B boundary; desired versus reported truth |
| Physical firmware sync | DNS/TLS/hostname/bad clock/CA/interception; known-Wi-Fi outage/backoff; full outbox; response/ACK cuts; loop/heap/energy; seven-day accelerated run; cloud kill switch and offline regression |
| Analytics | stationary/movement/poor-fix/gap/offline reference data; interval conservation; no gap→inactivity; algorithm versions/recompute; device/cloud discrepancy; 23/24/25-hour days, leap day, current day and timezone changes |
| Web/maps | auth/RLS server/client boundaries; loading/empty/stale/error/legacy states; route gaps/speed/quality/timeline; map provider/WebGL/offline failure; keyboard table alternative; mobile, a11y, visual and performance budgets |
| Security/privacy/operations | secret/bundle/binary/log/network scans; rate/load/cost alerts; credential rotation/lost-device revoke; export/delete/24-hour purge; backup restore plus deletion replay; DNS/certificate/custom-domain migration; rollback |

Cloud-disabled regression is a hard gate in every firmware phase: run the complete host suite, production build, Wokwi/HIL scenarios, AP portal behavior/a11y/visual checks, route export, scenes, GNSS metrics, storage recovery and physical loop/power measurements with no cloud credentials/network. An unavailable cloud must never become a failing local test dependency.

The detailed security cases are in the [threat model](cloud/threat-model.md); field ownership/exclusions are in the [Phase 0 matrix](cloud/phase0-field-matrix.md). The [active master plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) controls current M0–M6 order: independent/physical outbox proof gates M2, while the provider comparison gates M4 map integration and does not block the local M1 portal slice.

## Display I5 / I6a shared renderer

The optional [display simulator](../tools/display-simulator/README.md) compiles
LVGL 8.4.0 with the same configuration, formatter and view as the device. Run
`python tools/display-simulator/render.py` after resolving stage-3 dependencies.
It produces fourteen PNGs and runs five CTest contracts: static UI semantics/layout,
actual display service with fake SPI, detected panel-init failure and the real
Wi-Fi and LED snapshot adapters with read-only stubs. These are
additional to the 147-test firmware host suite, not browser mocks or measurements
of physical FPS. CI has a separate job and capture artifact for this tool.
The [I6a baseline](baselines/display-i6-2026-09-12.md) records current results,
image/source hashes and the final 44.768 ms USB service maximum. The
[I5 baseline](baselines/display-i5-2026-09-12.md) retains the earlier comparison.
Neither closes the outstanding I4 real-peripheral load gate.
The [I6a guide](../Platformio/Dog-RGB/docs/display-i6.md) adds page selection,
BOOT release/wake tests and the Connection scenarios.

[I6c](baselines/display-i6c-2026-09-12.md) extends the service tests with a
disabled-by-default timer, exact deadline/rollover, stale data/draws while dark,
twenty timeout/wake cycles and disable/resume semantics. The firmware suite now
has 147 tests. I6c retained four contracts and nine pixel-identical images.
[I6d](baselines/display-i6d-2026-09-12.md) adds the LED adapter contract and five
State captures (fourteen total); thirty navigation and thirty timeout/wake cycles
cover all three pages, including unchanged-state no-flush and bounded memory.

Display now includes I6c opt-in inactivity and I6d State. Keep V1–V3 in the
[incremental plan](PLANS/2026-09-12_display-incremental-delivery.md): physical
button/optical/radio, staged peripheral bring-up and then thirty-minute joint
load. Record physical versus serial-injected events, separate normal-update and
navigation histograms, counter deltas within each boot and visible latency
separately from service time. The two-phase diagnostic restore is not one-frame
latency evidence. For documentation-only edits, check links, status consistency
and `git diff --check`; no firmware rebuild or board manipulation is required.

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
