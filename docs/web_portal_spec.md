# Embedded Web Portal Specification

**Status:** Current implemented product surface, verified against `v2.0.0` on 2026-08-13.

The Dog-RGB portal is authored as normal HTML/CSS/JavaScript under `webui/src`. A pinned, deterministic build minifies and gzip-compresses four self-contained pages into versioned C++ `PROGMEM` arrays, which the ESP32-S3 serves directly from flash. It is local, offline-capable, mobile-first, and has no backend dependency.

## Product goals

- Make the collar usable from a phone without installing an app.
- Put daily metrics and health state before engineering detail.
- Keep configuration recoverable for a DIY owner.
- Bound page size, heap pressure, polling, and HTTP work on the MCU.
- Expose technical evidence on a separate diagnostics page.

## Information architecture

| Page | Primary jobs |
| --- | --- |
| Dashboard `/` | Current daily metrics, GNSS/Wi-Fi state, current/previous sessions, route preview, and JSON/CSV/GeoJSON export |
| Wi-Fi `/wifi` | AP/STA explanation, connection state, explicit network scan, home-network save, AP identity/password/open state, mDNS |
| Configuration `/config` | Mode/brightness, Day Mode, Simple/Speed/Geofence/Show controls, capabilities-driven effects/palettes/scenes, approximate collar preview, advanced LED current model/ceiling, GNSS gates, Home, restore defaults, optional write PIN |
| Diagnostics `/dev` | Overall health, degraded explanations, system/Wi-Fi/GNSS/storage/geofence/LED power counters, collapsible AP details, raw JSON |

The dashboard remains the user entry point. Diagnostics should not leak into the primary hierarchy unless a fault needs a concise action.

## Dashboard states

- **No data:** explain that a trusted GNSS fix/activity observation is required; do not show invented zeros as a completed day.
- **Active data:** show distance, average active speed, maximum valid speed, update time, and current session.
- **History:** show up to three complete session summaries and the last completed day when available.
- **Route:** load only on explicit interaction, draw a lightweight canvas preview, expose session/point limits, and offer three export formats.
- **Connectivity:** distinguish AP availability, station connected/connecting, and configured names/addresses.

The UI does not auto-poll route data. Lightweight status/summary refreshes use explicit bounded intervals/actions defined in the embedded script.

## Configuration behavior

- Mode-specific sections appear only when relevant while keeping their values in the complete runtime record.
- Validation errors are summarized and associated with the relevant control.
- Potentially disruptive actions explain the consequence before confirmation (AP credential change, open AP, restore defaults).
- Password inputs do not echo stored values; presence indicators explain that blank means “keep current” where applicable.
- Day Mode shows fixed local window/timezone and current active/waiting state.
- Home can be set only from the current trusted GNSS coordinate or explicitly cleared.
- Advanced LED power controls persist the estimated-current ceiling, base load, and per-channel coefficients; they remain collapsed by default because bench calibration is optional.
- The write PIN is optional and clearly described as a write guard, not encryption or read privacy.

## Scene and palette workspace

The scene editor is an API client, not a second implementation of the LED engine:

- effect IDs/names, palette IDs/names, defaults, control ranges, palette modes, layout, sentinel IDs, scene limits, and feature flags come from `/api/v1/led/capabilities`;
- built-ins and user slots come from `/api/v1/led/scenes`; no effect, palette, or built-in scene catalog is hard-coded in JavaScript;
- built-ins are immutable but can seed a user-slot draft;
- apply/cancel is volatile, while save/delete/import carries `expected_generation` to prevent silent lost updates;
- a `409 generation_conflict` refreshes the current bank without silently destroying the local draft;
- import performs `dry_run` validation before a separate confirmation replaces all four user slots;
- missing/incompatible scene capabilities disable only the scene workspace, not the rest of `/config`.

The dual-strip canvas is deliberately approximate. It explains intent using the announced layout, effect, palette and status regions at low cadence, pauses while hidden, and respects reduced motion. It is not a reproduction of the C++ PRNG/timing, RGB-to-RGBW conversion, compositor, current limiter, actual strip orientation, or electrical output.

## Wi-Fi scan behavior

Scanning starts only after the user presses the scan action. The page polls the scan endpoint until ready/failed, lists at most 20 unique visible SSIDs, shows signal strength, marks open networks, and lets a selection populate the form. Results are consumed after one ready response to release ESP32 driver memory.

## Diagnostics behavior

The page derives a concise healthy/degraded state from API evidence and exposes details progressively. It includes:

- build/uptime/free heap;
- runtime, metric, session, Home, credential, and route-storage slot/generation/failure/recovery state;
- Wi-Fi mode, addresses, RSSI, event queue, retry/backoff, AP holds/transitions, DNS, and reasons;
- GNSS fix layers, quality, coordinates/date/time, byte/sentence/parser/rejection counters and observation ages;
- current LED mode/range/effect, configured current-model inputs, requested/estimated/peak current, applied scale, estimate-only notice, and loop-phase maximum timing;
- raw `/api/dev` JSON for support/reproducibility.

Unknown/unavailable values must be labeled, not silently presented as healthy.

## Accessibility and resilience

- Semantic landmarks/headings/labels and meaningful button/link text.
- Keyboard-visible focus for links, buttons, inputs, selects, and summaries.
- Touch targets appropriate for a 428 × 926 reference viewport and usable down to 320 CSS pixels.
- Status never depends on color alone.
- `prefers-reduced-motion` disables decorative cursor/flicker motion.
- Validation and async state use live regions/status text where appropriate.
- Core explanatory copy remains visible without successful API data; `<noscript>` content directs users when JavaScript is unavailable.
- Static HTML contains no stored server interpolation. API-provided strings are inserted with safe text/attribute operations or explicit client escaping rather than trusted `innerHTML` concatenation.

## Source, generation, and embedded resource constraints

- `webui/src/pages/*.html` and `webui/src/styles/app.css` are the only editable portal sources; generated arrays, manifest, and preview files must not be edited by hand.
- The exact Node version in `.node-version`, locked minifier, explicit page order, UTF-8/LF normalization, zero gzip timestamp, and canonical gzip OS byte make tracked output byte-identical across Windows and Unix.
- The manifest records source/output hashes, routes, MIME, ETags, decoded/gzip sizes and per-route/total budgets. PlatformIO verifies it using a standard-library-only pre-script and never installs npm packages or accesses the network.
- Pages are compiled into firmware; there is no CDN, web font, framework bundle, map SDK, service worker, filesystem asset, or external runtime dependency.
- Static smoke checks enforce source contracts, scene capability discovery, write headers, generated-array/manifest equivalence, gzip integrity, and size budgets.
- Route responses stream; pages and ordinary JSON responses remain bounded.
- Wi-Fi scan and route preview are request-driven rather than timer-driven.
- HTML uses `Cache-Control: no-cache`, content-derived ETags, gzip negotiation and known-length `send_P`; APIs and dynamic responses use `no-store`.

Current gzip gates are 12 KiB `/`, 13 KiB `/wifi`, 23 KiB `/config`, 10 KiB `/dev`, and 55 KiB total. The `v2.0.0` bundles occupy 45,886 bytes gzip in total. Changing a gate requires an explicit technical decision rather than silently increasing it.

## Local security posture

- Every state-changing request carries `X-Dog-Portal` to block hostile cross-origin form/no-CORS writes.
- An optional `X-Dog-Pin` protects all writes when enabled.
- Responses deny framing, MIME sniffing, and referrer leakage. Dynamic data is not stored; immutable page bytes may be cached only with mandatory revalidation.
- Read APIs remain accessible to clients that can reach the collar. There is no TLS or account authorization.
- AP credentials and station password values are never returned by the config API.

This posture is intentional for a recoverable hobby device. Product/Internet exposure would require a different threat model and provisioning/update architecture.

## Verification

The portal is exercised by four generator unit tests, a clean-checkout-safe smoke verifier, behavior/UX/a11y Playwright suites, fixture states, reviewed Linux visual baselines, and production/Wokwi firmware builds. Host preview validates the exact decoded bytes represented by the firmware arrays but cannot prove ESP32 heap, radio, captive-view, NVS latency, or LED cadence. See [Testing and simulation](testing.md), [Phase 5 baseline](baselines/fase-5-2026-08-13.md), and [Visual screenshot workflow](ap_portal_visual_screenshot_workflow_guide.md).

Routes and payloads are normative in [Local HTTP API](api-reference.md); configuration is normative in [Runtime configuration](portal_config.md).
