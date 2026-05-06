# DOG-RGB Access Point Portal UI Deep Review

Prepared: 2026-05-06  
Scope: Access Point web portal UI/UX, mobile behavior, embedded web constraints, and redesign direction.  
Primary source of truth: firmware code in `Platformio/Dog-RGB/src/web/pages.cpp`, `Platformio/Dog-RGB/src/web/portal_http.cpp`, `Platformio/Dog-RGB/src/wifi/wifi_mgr.cpp`, runtime config code, and project README.

---

## Executive Summary

The Access Point portal is functional and substantially more capable than the original MVP described in the older docs. It now exposes a dashboard, Wi-Fi setup, runtime LED/geofence/GPS configuration, route viewing/export, mode switching, home location controls, status pills, a developer console, captive portal redirects, and AP/STA diagnostics.

The main UI problem is not lack of features. The main problem is that the interface exposes too much firmware structure directly to a mobile user. The dashboard mixes daily safety metrics, mode control, sessions, route visualization, exports, Wi-Fi, config, and developer access in one scrolling page. The configuration page is powerful but reads like an internal control panel: raw numeric fields, internal effect names, many advanced sections, and a long effects matrix. On a phone, this creates a heavy, fragile experience for common flows like "connect to the collar", "check if GPS is working", "change LED mode", or "connect the collar to home Wi-Fi".

The portal should be restructured around user intent:

- First screen: status, live safety summary, and the next likely action.
- Setup: AP/STA connection and reconnect guidance.
- Modes: simple mode cards and presets first, raw tuning second.
- Advanced: GPS quality, per-range effects, diagnostics, and raw JSON only when needed.

The existing code has useful foundations worth preserving: shared CSS tokens, responsive grid collapse, no external assets, plain HTML/CSS/JS, JSON APIs, local validation, captive portal support, and a useful `/api/status` model. The redesign should reuse those foundations while reducing page weight, polling, cognitive load, and mobile scrolling.

One immediate functional defect was found: the Wi-Fi AP settings form calls `validMdns(mdnsVal)` in `pages.cpp`, but no `validMdns` JavaScript function is defined in the generated Wi-Fi page. Saving AP settings from `/wifi` can throw a client-side `ReferenceError` before the request reaches `/api/config`.

---

## Current Implementation Snapshot

### Project Context

The README defines DOG-RGB as a smart high-visibility LED dog collar built on a Seeed XIAO ESP32-S3 with GPS telemetry, SK6812 RGBW LEDs, BLE summary output, and a local Wi-Fi portal. The active firmware lives in `Platformio/Dog-RGB`.

The portal is an embedded, local-only interface. It must be:

- usable from a phone connected to the collar AP;
- small enough for ESP32 memory and single-threaded HTTP handling;
- reliable in AP and AP+STA conditions;
- understandable without external internet;
- safe for runtime configuration of LEDs, GPS filters, Wi-Fi, and geofence behavior.

### Served Pages

The HTTP routes are registered in `Platformio/Dog-RGB/src/web/portal_http.cpp`:

- `/` dashboard.
- `/wifi` Wi-Fi setup and AP identity settings.
- `/config` runtime configuration.
- `/dev` developer console.
- captive portal probe routes such as `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, and unknown routes redirect to `/`.

### API Surface Used By The UI

The UI uses:

- `/api/summary` for daily metrics and session summaries.
- `/api/status` for GPS/Wi-Fi/mode/home status.
- `/api/track`, `/api/track.csv`, `/api/track.geojson` for route data and exports.
- `/api/mode` for quick mode changes from the dashboard.
- `/api/config` and `/api/config/reset` for runtime config.
- `/api/home`, `/api/home/set`, `/api/home/clear` for geofence home.
- `/api/wifi` for STA credential saving.
- `/api/dev` for diagnostics.

### Shared UI System

`pages.cpp` defines a shared `BASE_CSS` block with:

- light "Glacier Tech" palette;
- card surfaces;
- status pills;
- responsive grids;
- reusable buttons, fields, labels, help text, error text;
- media query that collapses two/three-column grids to one column under 760px;
- reduced-motion guard.

This is a good base for an ESP32 portal because it avoids external CSS, images, fonts, and frameworks.

---

## Page-By-Page UI And UX Analysis

## 1. Dashboard (`/`)

### Current State

The dashboard includes:

- brand header: `DOG-RGB` and "Collar inteligente de seguridad";
- status pills for GPS, Wi-Fi, mode, and home;
- a mode selector with Apply button;
- four metric cards: distance, average speed, max speed, date;
- current session and history;
- route canvas with session selector and CSV/GeoJSON export links;
- actions: refresh, update home when in geofence mode, Config, Wi-Fi, Dev;
- auto-refresh intervals: status every 5s, summary every 10s, track every 15s.

### What Works Well

- The first visible area communicates device identity and operational status.
- Status pills are compact and suitable for a mobile AP portal.
- The key daily metrics are visible and formatted clearly.
- The dashboard is useful even without internet or map tiles.
- Mode can be changed without entering advanced config.
- Route export is practical for later debugging or data analysis.

### Problems

- The page is doing too many jobs. It is simultaneously a dashboard, mode switcher, session history view, route viewer, data export page, navigation hub, and developer entry point.
- The primary setup actions are buried after metrics, sessions, and route. On a phone, a first-time user may need to scroll before finding Wi-Fi setup.
- The mode selector is powerful but context-light. A user can switch to geofence, simple, or show without seeing prerequisites or consequences.
- The Dev link is visible beside normal user actions. This makes the interface feel internal and increases the chance of a non-technical user entering a diagnostic screen.
- Date is presented as a metric card with the same visual weight as distance/speed. It is metadata, not a primary metric.
- The route canvas has no basemap, scale, orientation, or labels. It can be useful as a shape preview, but a user may misread it as a map.
- CSV/GeoJSON export links are always visible, even when there is no route data.
- Nested cards inside the sessions card create extra visual weight and padding on mobile.
- Polling track data every 15 seconds is expensive relative to its value, especially if the user is not looking at the route.

### Mobile Reconstruction

On a typical 360-390px wide phone:

- The container keeps 20px side padding, leaving about 320-350px content width.
- The hero stacks vertically. The four pills wrap to multiple lines.
- The mode selector and Apply button wrap under the status chips.
- Metrics collapse to a single column: distance, average speed, max speed, date.
- Sessions appear as card-in-card blocks, increasing scroll length.
- Route controls wrap: title, select, CSV button, GeoJSON button. The canvas is fixed at 220px high and consumes a large viewport chunk.
- Main navigation actions are near the bottom after a long scroll.

This is readable, but not mobile-first for the highest-frequency flows. The first screen should prioritize "is the collar working?", "what is it doing now?", and "what should I tap next?".

---

## 2. Wi-Fi Page (`/wifi`)

### Current State

The Wi-Fi page includes:

- STA form: SSID, password, show password checkbox, "Guardar y conectar".
- AP settings: AP SSID, mDNS, AP password, AP open checkbox, save button.
- Back link to `/`.
- Client-side state loading from `/api/config`.
- STA save via `/api/wifi`.
- AP save via full `/api/config` payload.

### What Works Well

- The page separates home network credentials from collar hotspot settings.
- The STA password can be revealed.
- AP password is not returned by `/api/config`; the UI only indicates whether one exists.
- The UI warns that changing AP settings may disconnect the session.
- The form is simple enough to load quickly on an AP connection.

### Problems

- Functional bug: `saveAp()` calls `validMdns(mdnsVal)`, but the Wi-Fi page does not define `validMdns`. Saving AP settings can fail in the browser before any request is sent.
- After saving STA credentials, the user only sees "Guardado, conectando...". There is no live state showing connected, failed, IP address, next retry, or whether the AP will remain available.
- The page does not explain what to do if the phone disconnects after AP SSID/password changes.
- AP settings are saved by posting the entire runtime config object fetched from `/api/config`. This couples a Wi-Fi-only page to every LED/GPS/effects field and can create stale-write risks as config grows.
- AP password has no show/hide option.
- The "AP abierto" control is technically useful but risky. It should require clearer copy because it removes hotspot protection.
- There is no scan/list of nearby Wi-Fi networks. Manual SSID entry is lightweight, but error-prone on phones.
- The same validator used for AP SSID is used for STA SSID in `handle_wifi_save`. This is probably acceptable for basic SSID length/control character handling, but the UI should label the field as "Home network SSID" to avoid confusing it with AP SSID.

### Mobile Reconstruction

On a phone, `/wifi` is one of the cleaner pages. The forms stack naturally. The risk is less visual and more flow-related:

- A user may save home Wi-Fi and wait without knowing if anything happened.
- A user changing the AP SSID/password may be disconnected and not know the new network/password path.
- A client-side JS error can make AP saving look like a dead button.

---

## 3. Runtime Config (`/config`)

### Current State

The config page includes:

- hero: "Configuracion avanzada";
- error box;
- top action bar with Save and Restore defaults;
- `details` sections for Common, Speed ranges, Geofence, GPS quality, Simple, Show, and Effects by range;
- mode-dependent section visibility;
- client-side validation mirroring backend validation;
- theme presets for Simple mode;
- generated 10-row effects matrix for ranges;
- home set/clear controls;
- reset defaults flow.

### What Works Well

- The page has extensive local validation before POSTing.
- Backend error reasons are mapped to user-facing messages.
- Mode-specific sections are hidden after config loads, reducing some clutter.
- `details` provides a lightweight native disclosure pattern.
- Simple mode themes are a good direction because they hide raw effect tuning behind user-friendly presets.
- Geofence range preview helps translate a max distance into 10 ranges.
- The page can configure the real firmware model without external tooling.

### Problems

- The page calls itself advanced, but it contains common controls like brightness and mode. Common users are forced into an advanced mental model.
- All `details` blocks are initially `open` in the HTML. Until JS loads and `updateModeVisibility()` runs, the page can briefly present every section.
- GPS quality controls are highly technical and should not be part of the same primary config path as mode/brightness.
- Speed ranges are raw numeric thresholds. There is no explanation of how ranges map to collar behavior beyond "R10 es mayor que R9".
- The effects table exposes internal effect IDs/names and raw speed/intensity values for 10 ranges and two strips. This is powerful but overwhelming on mobile.
- Editing range effects near the bottom requires scrolling back to the top to Save.
- Destructive actions are not visually distinguished. "Restaurar defaults" and "Clear Home" look like regular secondary actions.
- Reset defaults can clear stored home config according to docs, but the UI confirmation does not say that clearly.
- RGB is raw numeric input only. There are no swatches, color preview, or preset colors.
- Brightness is numeric only. A slider plus number would be faster on mobile.
- There is no dirty-state indicator, leave warning, or disabled Save state while saving.
- The page always builds and validates the full config object, even when only one mode is visible. This is internally consistent, but it increases the blast radius of small edits.
- Hidden sections still contribute to conceptual complexity because the user sees mode as a filter over a large internal config object, not a guided workflow.

### Mobile Reconstruction

On a phone:

- The top action bar appears early, but once the user scrolls into Geofence, Simple, or Effects, Save is far away.
- Numeric fields produce repeated mobile keyboard transitions.
- The effects matrix is very long: 10 ranges x A select x B select x Speed x Intensity. The responsive layout stacks each row into a narrow two-column arrangement with the range label on the left and four controls vertically on the right.
- Long effect names such as `GRADIENT_WAVE` may be clipped in selects.
- The page is technically usable but tedious for real configuration.

The page should be split conceptually into "Mode and brightness", "Mode presets", and "Advanced tuning".

---

## 4. Developer Console (`/dev`)

### Current State

The developer page shows:

- GPS and Wi-Fi status pills;
- manual refresh and optional 5s auto-refresh;
- System, Wi-Fi, GPS, LED, Geofence diagnostic sections;
- raw JSON from `/api/dev`;
- back link to `/`.

### What Works Well

- This is valuable for field debugging AP, GPS, BLE coexistence, heap, and route behavior.
- Auto-refresh is opt-in, which is better than continuous default polling.
- The raw JSON block is useful during firmware work.

### Problems

- The page is exposed as a normal dashboard action.
- It is very long on mobile and not task-oriented.
- The raw JSON can be large and visually dominates the lower page.
- Labels mix Spanish and English.

### Recommendation

Keep `/dev`, but remove it from the primary action row. Link it from an "Advanced" or "Diagnostics" section. The page should remain utilitarian and not compete with the end-user portal.

---

## 5. Captive Portal Entry

### Current State

The firmware runs wildcard DNS while AP is active and handles common captive probe URLs. Unknown routes redirect to the AP base URL.

### What Works Well

- This improves phone onboarding.
- Manual fallback remains `http://192.168.4.1/`.
- The captive probe HTML is tiny.

### Problems

- Captive mini-browsers can have inconsistent JavaScript, downloads, and navigation behavior. The current first page assumes a JS-capable browser for fresh data.
- There is no ultra-simple no-JS fallback view beyond initial `--` placeholders.
- If the OS captive sheet opens the portal, export links and advanced pages may not behave like a full browser.

### Recommendation

Make the first dashboard useful even before JS completes by embedding a minimal static message: AP name, portal IP, and "open in browser for configuration". Keep the JS dashboard for normal browsers.

---

## Navigation And User Flow Analysis

### Current Navigation Model

Navigation is shallow:

- Captive portal or manual URL opens `/`.
- `/` links to `/config`, `/wifi`, `/dev`.
- `/wifi`, `/config`, and `/dev` each only link back to `/`.

This is simple and robust, but it lacks task hierarchy.

### Primary User Flows

#### Flow A: First-time AP connection

Current path:

1. Connect phone to AP `dog`.
2. Captive portal opens `/` or user visits `192.168.4.1`.
3. User sees dashboard, metrics, sessions, route.
4. User scrolls to action row and taps Wi-Fi.
5. User enters home network credentials.
6. UI says "Guardado, conectando..." with no final status.

Issue: first-time setup is not prioritized. The user lands in operational telemetry before setup.

#### Flow B: Check collar status

Current path:

1. Open `/`.
2. Read pills and metrics.
3. Wait for async status/summary refresh.

This flow is mostly good. It needs better hierarchy and fewer competing controls.

#### Flow C: Change mode

Current path:

1. Open `/`.
2. Use mode selector.
3. Tap Apply.
4. See terse "OK" or "Error".
5. For tuning, go to `/config`.

Issue: mode names are exposed without preview or explanation. Geofence also depends on GPS and home state, which is only partially visible.

#### Flow D: Configure LEDs

Current path:

1. Open `/config`.
2. Select mode.
3. Edit raw fields and effects.
4. Save from top action bar.

Issue: this is a firmware-facing workflow, not a user-facing one. It works for developers but is heavy for mobile tuning.

#### Flow E: Set geofence home

Current path:

1. In dashboard, switch to geofence.
2. Conditional "Actualizar Home" appears.
3. Or open `/config`, Geofence section, "Nuevo Home".

Issue: home setup is split across pages, labels differ, and failure state is terse when GPS is unavailable.

---

## Mobile-First Evaluation

### Strengths On Mobile

- The CSS collapses grids to one column under 760px.
- Touch targets are generally large enough.
- Forms are plain native inputs/selects and should work in mobile captive browsers.
- No external images, fonts, or frameworks block rendering.
- The color contrast is generally acceptable for main text.

### Weaknesses On Mobile

- The primary actions on `/` are below lower-priority sections.
- The dashboard is long before the user reaches setup/navigation.
- The config page is too long for comfortable phone editing.
- Save controls are not close to the fields being edited.
- Raw numeric tuning is keyboard-heavy.
- Nested cards add vertical padding and reduce information density.
- Developer access appears as a normal user action.
- Export actions are visible on captive/mobile flows where file handling may be awkward.
- Status/error messages are terse and often disappear into muted small text.

### Likely Phone Experience

The current portal will look clean, but it will feel like a developer dashboard rather than a polished embedded product interface. A technical user can operate it. A normal user may be unsure which page matters, what "STA", "AP", "mDNS", "HDOP", "range", "intensity", or `GRADIENT_WAVE` mean, and whether saving actually worked.

---

## Strengths To Preserve

- Plain embedded HTML/CSS/JS with no remote dependencies.
- Shared CSS tokens and consistent visual language.
- Responsive grid collapse.
- Compact status pills.
- JSON API separation between data and pages.
- Captive portal DNS/probe support.
- Runtime config persistence and validation.
- Local validation before saving config.
- Developer console and raw diagnostics for field debugging.
- Track export endpoints and streaming server responses.
- Mode support on dashboard.
- Simple mode themes as the start of a preset model.

---

## Weaknesses, Risks, Inconsistencies, And Usability Problems

### Functional Risks

- `/wifi` AP settings save can break because `validMdns` is missing in the page JavaScript.
- Track polling every 15s can create unnecessary load on the ESP32 and AP link.
- `/wifi` posts a full config payload to save AP-only settings, increasing stale-write risk.
- No final STA connection result is shown after saving credentials.
- Changing AP SSID/password can disconnect the active phone session without strong recovery guidance.

### UX Risks

- First-time setup is not the first-class flow.
- Advanced diagnostics are exposed too prominently.
- Config exposes internal firmware concepts directly.
- Mode changes lack explanatory context and prerequisites.
- Destructive actions are visually understated.
- Save location is inconvenient after editing lower sections.
- Empty states are too terse.
- Language is mixed: Spanish user copy, English technical labels, internal constants.

### Visual/Interaction Inconsistencies

- Date is styled like a primary metric.
- Some buttons are commands, some are navigation links, and some are exports, but they share the same weight.
- Error/warning/success feedback is often just small muted text.
- The card system is overused, especially nested session cards.
- `/dev` is visually similar to user pages despite being diagnostic.

### Embedded Reliability Risks

- Auto-refresh happens regardless of page visibility.
- Dashboard starts summary, status, and track fetches on load.
- Track JSON sends up to 400 points to the browser repeatedly.
- Large generated HTML strings and duplicated inline CSS are acceptable now, but will become harder to maintain as UI grows.

---

## ESP32 Web Interface Constraints

The redesign should respect these constraints:

- No frontend framework.
- No remote fonts, images, icon packs, map tiles, or CDN assets.
- Avoid large SVG/icon systems unless carefully inlined and reused.
- Keep each HTML page small; target under roughly 20-30 KB per page where practical.
- Prefer native controls over custom widgets.
- Avoid frequent polling and large JSON transfers.
- Use server-side streaming for large data where possible.
- Treat AP connections as high latency and potentially unstable.
- Design for captive browser limitations.
- Avoid flows that require many round trips.
- Avoid UI states that depend on internet availability.
- Keep diagnostics available, but separate from normal use.
- Build no-JS or degraded states for critical setup information.

---

## New Interface Direction

The portal should become a mobile-first control surface with four clear areas:

1. **Home**: live collar status and key daily metrics.
2. **Setup**: AP/STA Wi-Fi, connection status, and reconnect instructions.
3. **Modes**: choose LED behavior using simple mode cards and presets.
4. **Advanced**: detailed tuning, GPS filters, exports, developer diagnostics.

### Proposed First Screen

The first screen should show:

- DOG-RGB identity.
- AP/STA and GPS state.
- current LED mode.
- home/geofence state if relevant.
- primary daily metric: distance today.
- secondary metrics: average speed, max speed, last update.
- primary actions: Setup Wi-Fi, Change Mode, Refresh.

Sessions, route, exports, and diagnostics should move below a clear "More" or "Advanced" break.

### Proposed Navigation

Use a simple top or bottom action row:

- Home
- Setup
- Modes
- Advanced

On ESP32, these can remain separate routes or simple links. Avoid building a single-page app.

### Proposed Visual Language

Keep the current light palette and cards, but make the interface less card-heavy:

- Status strip at top.
- Primary metric block.
- Compact secondary metric grid.
- Full-width sections instead of nested cards.
- Stronger visual distinction for destructive actions.
- Clear success/error banners for save operations.

---

## Recommendations

### Reuse

- `BASE_CSS` token approach.
- Status pill concept.
- Native `<details>` for advanced sections.
- Existing JSON endpoints.
- `/api/status` data model.
- Local validation pattern.
- Simple mode themes.
- Developer console data.
- Captive portal route handling.

### Improve

- Move primary actions higher on `/`.
- Add clear setup state after Wi-Fi save.
- Reduce dashboard polling and stop polling when document is hidden.
- Load track only on demand or when the route section is expanded.
- Add missing `validMdns` client validation or remove the client check and rely on backend validation.
- Add dedicated AP settings endpoint instead of posting full `/api/config` from `/wifi`.
- Add dirty-state and save-in-progress handling.
- Add bottom/sticky save action on long config pages.
- Add clearer empty states for no GPS, no sessions, no route, STA failed, and AP changed.
- Use safer labels: "Home Wi-Fi", "Collar hotspot", "Portal address".
- Hide `/dev` behind Advanced/Diagnostics.

### Replace

- Replace raw-first LED tuning with preset-first mode cards.
- Replace always-visible route exports with contextual exports.
- Replace raw RGB fields as the primary color control with color presets/swatches plus optional numeric advanced fields.
- Replace numeric-only brightness with slider plus exact value.
- Replace long effects matrix as the default editing surface with grouped presets and "Advanced range tuning".
- Replace terse save feedback with explicit result states and recovery steps.

---

## Phased Improvement Plan

## Phase 0: Immediate Defects And Safety

### Objectives

Fix current UI breakage and reduce avoidable ESP32 load without redesigning the whole portal.

### Concrete Tasks

- Define `validMdns()` in the `/wifi` page JavaScript or remove the call and show backend errors from `/api/config`.
- Disable buttons while save requests are in flight.
- Add AP save error mapping for `ssid`, `pass`, `pass required`, and `mdns`.
- Pause dashboard polling when `document.hidden` is true.
- Stop auto-loading track every 15s; load it on demand or only when the session selector changes.
- Hide export links until route data exists.
- Move `/dev` out of the primary dashboard action row or relabel it as Diagnostics under Advanced.

### Expected Outcomes

- AP settings can be saved from `/wifi`.
- Dashboard causes fewer repeated requests.
- Users get clearer failure feedback.
- Normal users are less likely to enter diagnostics accidentally.

### Validation Criteria

- No browser console errors on `/`, `/wifi`, `/config`, `/dev`.
- AP SSID/password/mDNS changes can be saved and handled after reconnect.
- Dashboard status and summary still refresh.
- Track still draws after manual load.
- Pages remain usable in a phone captive portal and full browser.

---

## Phase 1: Mobile Information Architecture

### Objectives

Make the first screen useful for real mobile users and make setup discoverable immediately.

### Concrete Tasks

- Restructure `/` so first viewport contains status, primary metrics, and primary actions.
- Move sessions and route below a "History and route" section.
- Place Wi-Fi setup and mode change actions near the top.
- Turn date into metadata near "last update" instead of a full metric card.
- Replace action row with consistent navigation: Home, Setup Wi-Fi, Modes, Advanced.
- Add clear no-data states:
  - "Waiting for GPS"
  - "No activity recorded today"
  - "No route points yet"
  - "Home not set"

### Expected Outcomes

- A user connecting from a phone can understand the collar state without scrolling.
- Setup is obvious.
- The dashboard feels like a product UI instead of a debug aggregate.

### Validation Criteria

- On 360px width, GPS/Wi-Fi/mode state and at least one primary action are visible in the first viewport.
- No horizontal scrolling.
- Touch targets remain at least comfortable native control size.
- Main actions are reachable without scrolling past route/history.

---

## Phase 2: Wi-Fi Setup Redesign

### Objectives

Make provisioning reliable, understandable, and recoverable.

### Concrete Tasks

- Split labels into "Home Wi-Fi" and "Collar hotspot".
- Add current connection status from `/api/status` or a small `/api/wifi/status`.
- After saving STA credentials, poll connection state for a bounded period and show:
  - connecting;
  - connected with IP/mDNS;
  - failed with retry guidance;
  - AP still available at `192.168.4.1`.
- Add show/hide for AP password.
- Add explicit warning for open AP.
- Add a recovery block when AP settings change: new SSID, whether password changed, and fallback portal IP.
- Add a dedicated AP settings endpoint or a narrow request body so `/wifi` does not POST the full runtime config.

### Expected Outcomes

- A user knows whether home Wi-Fi setup succeeded.
- AP identity changes are less likely to feel like the portal broke.
- Wi-Fi page no longer has broad config side effects.

### Validation Criteria

- Wrong home Wi-Fi password produces a visible failure state.
- Successful STA connection shows connected state and address.
- AP SSID/password change gives reconnect instructions before restart.
- Open AP requires intentional confirmation.

---

## Phase 3: Mode And LED Configuration Redesign

### Objectives

Make LED behavior understandable without exposing raw firmware tuning first.

### Concrete Tasks

- Create a `/modes` route or mode section with cards:
  - Speed: reacts to movement.
  - Geofence: reacts to distance from Home.
  - Simple: one chosen effect.
  - Show: demo rotation.
- Show prerequisites inline:
  - Geofence requires GPS and Home.
  - Speed requires trusted GPS.
- Add Simple mode presets as the primary UI.
- Add brightness slider with numeric value.
- Add color swatches/presets for Simple mode, with raw RGB hidden under Advanced.
- Move per-range effects into "Advanced range tuning".
- Add a sticky or repeated Save bar for long advanced sections.
- Use destructive styling for reset and clear home.

### Expected Outcomes

- Common mode changes become fast and low-risk.
- Advanced power remains available for firmware tuning.
- Mobile editing becomes shorter and less keyboard-heavy.

### Validation Criteria

- A user can switch to Simple mode and choose a preset in under one viewport of scrolling.
- Geofence mode clearly blocks or warns when Home/GPS is unavailable.
- Advanced effects matrix remains available but is no longer the default path.
- Save feedback is visible from wherever the user edits.

---

## Phase 4: History, Route, And Export Refinement

### Objectives

Keep data features useful while reducing default dashboard weight.

### Concrete Tasks

- Move sessions and route into a collapsible "History" section or separate route.
- Load route data only when expanded.
- Add route empty state and point count before drawing.
- Label route as "shape preview" or "GPS trace" so users do not expect a real map.
- Show exports only after data exists.
- Consider a smaller default point budget for preview and keep full export endpoints separate.

### Expected Outcomes

- Faster dashboard load.
- Less AP traffic.
- Route behavior is clearer and less visually dominant.

### Validation Criteria

- Initial `/` load does not call `/api/track`.
- Expanding route draws trace successfully.
- Export links include the selected session.
- No route state is clear and non-error-looking.

---

## Phase 5: Advanced, Diagnostics, And Maintainability

### Objectives

Keep developer power while making the firmware UI easier to maintain.

### Concrete Tasks

- Group GPS quality, raw per-range tuning, AP diagnostics, and raw JSON under Advanced.
- Keep `/dev` route but make it intentionally diagnostic.
- Audit page sizes after each phase.
- Consider moving large page fragments and scripts into separate `PROGMEM` constants.
- Consider streaming HTML responses for larger pages instead of building large `String` objects.
- Add a tiny HTML/JS smoke test outside firmware build that checks for missing functions and route page integrity.
- Standardize UI language. Choose Spanish or English for user copy; keep firmware constants out of labels where possible.

### Expected Outcomes

- Developer diagnostics remain available.
- User pages become smaller and clearer.
- UI regressions like missing JavaScript functions are caught earlier.

### Validation Criteria

- Page byte budgets are documented.
- No page has missing client-side functions.
- `/dev` still exposes required AP/GPS/LED diagnostics.
- Normal user navigation does not surface raw JSON by default.

---

## Final Guidance

The existing portal is a strong embedded prototype with real operational value. It should not be thrown away. The right path is to keep the simple embedded architecture and progressively reshape the interface around mobile user tasks.

Reuse the shared CSS, status pills, JSON APIs, validation, captive portal support, and diagnostics. Improve the flow hierarchy, feedback, polling behavior, Wi-Fi setup, and mobile save ergonomics. Replace raw-first configuration with preset-first controls and reserve the full internal tuning surface for Advanced.

The target interface should feel like a small reliable device control panel: fast to load, obvious on a phone, calm under weak AP conditions, and powerful only when the user intentionally enters advanced configuration.
