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

Most modules use source-contract assertions. Phase 2 compiles `effect_registry`, `led_policy`, and `led_state` as native C++17 with warnings treated as errors. Phase 3 adds a harness for `led_color`, `palette_registry`, `led_layout`, and `led_compositor`; it proves a non-black crossfade midpoint and next-frame alert interruption. Phase 4 compiles the scene model/catalog/player/store plus the ArduinoJson codec natively, with fault injection at the record backend. The complete local suite baseline is 131/131. None of these layers replaces target execution or physical validation.

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
- **Web portal:** the Next.js production build;
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

The superseded RAM-only suite passed 20/20, but that historical green result is invalid recovery/reclaim evidence: it accepted `acknowledge_through(999)` after only chunks `0..2` existed and then reclaimed all three. It also recovered from retained Python objects instead of constructing a fresh runtime from persisted flash bytes.

A corrected byte-addressed candidate now reconstructs from a NOR image, carries globally monotonic outbox identities, requires exact manifest-bound per-slot ACK evidence, derives reclaim only across a contiguous proven prefix, journals metadata A/B, and reserves two independently erasable emergency sectors. Its provisional geometry is 664 chunks/63,744 points. The expanded 51/51 suite now covers destructive stale-intent fallback, irreversible intent consumption before refill, corrupt-refill quarantine and unreadable-header fail-closed behavior, sequence reuse after tombstone/journal fallback, bounded recovery of maximum loss intervals, durable loss capture and automatic sparse-loss finalization during ACK transitions, and ACKed-corrupt-payload classification. The host recovery/reclaim gate remains **review/open** until independent acceptance; keep every reproduction, rerun from fresh immutable images, and regenerate the [storage feasibility report](cloud/phase0-storage-feasibility.md) after storage changes.

Passing this model does not close the hardware gate. Before the M2 firmware exit, execute at least 10,000 production-codec seal/ACK/reclaim cycles on the target ESP32-S3 with randomized physical reset/power removal at data/header/metadata/ACK/erase boundaries. Record mount/recovery latency, maximum GNSS/LED/cooperative-loop gap, watchdog margin, heap, programmed/erased bytes and sector distribution, current/energy by cadence, full-pressure/loss-marker behavior, and legacy preservation. The raw-ring ADR must be revisited if metadata wear concentration or timing is unsafe.

### Device-v1 protocol

```powershell
node --test contracts/device-v1/test-contracts.mjs
```

This dependency-free suite must validate every schema/reference, positive/negative fixture, canonical hash, point/chunk/ACK identity, sequence hole/final rule, LWW/HLC vector, problem behavior, local-only exclusion, and compatibility tuple.

Phase 0 requires a cross-implementation gate, not two independently green suites. The protocol tuple/hash/flags/time-quality/chunk bounds/legacy encoding must exactly match `tools/cloud_phase0/track_v3.py`; generated native payload bytes must validate under the JSON semantic tests and vice versa. Any future disagreement stops schema work. On 2026-08-13 the complete protocol suite passed 48/48. The contract tests cover the six-value time-quality mapping, exact chunk ACK identity, out-of-order holes, and dedicated revoke identity/exact-replay/disposition behavior; their wire-vector check matches the Python codec. This closes protocol reconciliation only. The corrected Python storage candidate passes 51/51 but still awaits independent host acceptance; it does not validate physical storage, the map provider, or any implementation gate.

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
