# Embedded Web Portal Specification

**Status:** Current implemented product surface, reviewed 2026-08-12.

The Dog-RGB portal is embedded as C++ raw-string HTML/CSS/JavaScript and served directly by the ESP32-S3. It is local, offline-capable, mobile-first, and has no backend dependency.

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
| Configuration `/config` | Mode/brightness, Day Mode, Simple/Speed/Geofence/Show controls, advanced LED current model/ceiling, GNSS gates, Home, restore defaults, optional write PIN |
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
- Stored strings are escaped server-side for HTML attributes/text and client-side before DOM insertion.

## Embedded resource constraints

- Pages are compiled into firmware; there is no CDN, web font, framework bundle, map SDK, or external runtime asset.
- Static smoke checks enforce page-size reserves and required escaping/write-header patterns.
- Route responses stream; pages and ordinary JSON responses remain bounded.
- Wi-Fi scan and route preview are request-driven rather than timer-driven.
- `Cache-Control: no-store` prevents stale configuration/telemetry views.

## Local security posture

- Every state-changing request carries `X-Dog-Portal` to block hostile cross-origin form/no-CORS writes.
- An optional `X-Dog-Pin` protects all writes when enabled.
- Responses deny framing, MIME sniffing, referrer leakage, and caching.
- Read APIs remain accessible to clients that can reach the collar. There is no TLS or account authorization.
- AP credentials and station password values are never returned by the config API.

This posture is intentional for a recoverable hobby device. Product/Internet exposure would require a different threat model and provisioning/update architecture.

## Verification

The portal is exercised by static checks, behavior/UX/a11y Playwright suites, fixture states, and reviewed Linux visual baselines. See [Testing and simulation](testing.md) and [Visual screenshot workflow](ap_portal_visual_screenshot_workflow_guide.md).

Routes and payloads are normative in [Local HTTP API](api-reference.md); configuration is normative in [Runtime configuration](portal_config.md).
