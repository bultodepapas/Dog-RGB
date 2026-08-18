# ADR-0009: MapLibre renderer and credential-gated Colombia basemap bake-off

**Status:** Accepted

**Date:** 2026-08-13

**Decision maturity:** MapLibre and the provider-neutral route contract are accepted. Stadia Maps/Alidade Smooth Dark is the provisional front-runner, not a final provider winner, until the full credentialed provider matrix, origin-control proofs, and two-reviewer scoring are evidenced.

**Scope:** Web route renderer, basemap abstraction, launch candidates, visual/privacy/cost evaluation, and later Google Maps portability.

## Context

Dog-RGB needs attractive route review with speed/quality context on desktop and mobile. The local portal already uses a dark, high-contrast visual identity. A basemap that overwhelms the route, omits Colombian park/trail/rural context, or looks generic would make the result feel disconnected from the product.

“Free map” is ambiguous. OpenStreetMap data is open, but the public `tile.openstreetmap.org` service is a donation-funded service with usage requirements, no SLA, and the right to block heavy or inappropriate use. It is not the production tile backend. Commercially operated styles also have usage/attribution/domain-key conditions that can change and must be rechecked before launch.

The product may later move to Google Maps. Route observations therefore must not be shaped around one renderer/provider SDK.

## Decision

### Renderer and application contract

Use **MapLibre GL JS** as the first web renderer. It consumes a provider style URL but the Dog-RGB application owns a renderer-neutral route view model:

- WGS84 GeoJSON in `[longitude, latitude]` order;
- separate ordered `LineString` features per continuous speed/quality segment;
- no line across an explicit gap, recording boundary, fix loss, or unknown interval;
- per-feature stable point/segment IDs, start/end time, speed band/value when defensible, time/fix quality, and legacy flag;
- separate start/end/event markers and a non-map table/timeline representation.

MapLibre's GeoJSON source and style expressions can render gradients, but the initial implementation uses segmented features. They provide reliable gap semantics, hit targets, tooltips, quality styling, timeline linkage, and a straightforward adapter to another map SDK. Keep MapLibre imports and provider style selection inside a lazy-loaded map adapter; analytics/domain code never imports provider objects.

Do not render user coordinates into static-map URLs, tile query parameters, style URLs, or provider search/geocoding calls. Route GeoJSON stays in the authenticated browser. The basemap provider still sees the visitor IP, requested tile/view area, user agent/referrer, and account/key; that disclosure belongs in privacy copy.

### Basemap candidates

The launch comparison is:

1. **Stadia Maps, Alidade Smooth Dark** — provisional front-runner because its deliberately subdued style fits a data overlay and current Dog-RGB night interface. Production web auth should use allowed domains/referrer/origin behavior supported by Stadia, not a committed secret.
2. **MapTiler Cloud, Dataviz Dark** — required direct comparison. Also inspect Dataviz Light/Outdoor only as scenario-specific evidence; do not let changing styles per scenario hide weaknesses in the primary dark candidate. Use an origin-restricted production key.

Pricing and terms are acceptance inputs, not constants in code. On 2026-08-13, the official pages reported:

| Provider tier | Published allowance/behavior on the evidence date |
| --- | --- |
| Stadia Free | `$0`, 200,000 credits/month, standard basemaps, no commercial use, no additional usage; standard vector tiles cost one credit each |
| Stadia Starter | `$20/month`, 1,000,000 credits/month, commercial use, then `$0.03/1,000` additional credits |
| MapTiler Free | `$0`, testing/personal/non-commercial, 5,000 map sessions and 100,000 API requests/month; service pauses at quota without upgrade |
| MapTiler Flex | `$25/month`, commercial feature set, 25,000 sessions and 500,000 API requests/month; automatic overuse (`$2/1,000` sessions, `$0.10/1,000` requests) unless controlled by a spending limit |

These numbers are dated research, not a budget guarantee. Recheck exact credits/sessions/requests, commercial eligibility, attribution, taxes, overage/stop behavior, and the expected MapLibre third-party-SDK billing unit immediately before selecting a plan. Because this harness uses MapLibre rather than MapTiler SDK, forecast MapTiler from captured API/tile-request counts—not from the published SDK-session allowance. Do not promise a perpetual free tier.

Stadia officially allows strict-rate-limit keyless `localhost` development and recommends domain-based production authentication using browser `Origin`/`Referer`. If selected, the Vercel application must not inherit the local AP's `Referrer-Policy: no-referrer` for map tile requests; test an appropriate origin-only policy such as `strict-origin-when-cross-origin` with every production/preview hostname. MapTiler requires a key even for tests and warns that browser keys remain visible/read-only and can consume quota; use a separate protected key with exact allowed HTTP origins. The production app pins/bundles MapLibre through its lockfile rather than loading the harness's CDN script at runtime.

The OSM standard tile server is excluded from production. Self-hosted/vector archives such as PMTiles may be reconsidered when measured traffic, offline needs, privacy, or cost justify owning tile operations.

### Reproducible Colombian bake-off

The checked harness at [`tools/map_bakeoff`](../../tools/map_bakeoff/) defines six deterministic, invented routes near public Colombian reference areas. They test cartography; they are not real dog tracks or verified path alignments:

| Fixture | What it stresses |
| --- | --- |
| Bogotá urban | dense road hierarchy, Spanish/local labels, POI clutter, route contrast |
| Simón Bolívar park | green space/water/path context and route visibility |
| Monserrate trail | terrain/steep geometry, minor paths, and an explicit quality gap |
| La Calera rural | sparse labels, minor roads, orientation, and empty-space treatment |
| Sparse approximately 1 km | 11 observations over 996 m; long chords and missing-point legibility |
| Dense two-hour track | 241 observations at 30-second cadence over exactly 7,200 seconds; overlap, gaps, and render density |

Run every provider/style against identical GeoJSON and overlays:

- stationary cyan, steady green, fast gold, explicit low-quality gap dashed magenta;
- black route casing, identical widths, start/end markers, legend, and interactive hit line;
- desktop `1280×720` and exact mobile `428×844` CSS-pixel viewports, each at DPR `1` and `2`; retain the earlier `390×844` evidence only as legacy provenance;
- dark, light, and outdoor variants against identical fixtures, plus label-deemphasis, deuteranopia/protanopia review approximations, cache-disabled, and throttled-network diagnostics;
- record exact browser/version, MapLibre version, provider style URL/version/date, cache/network profile, screenshot scale, and run time;
- wait for all maps to reach `idle`; capture full-page screenshots, console/CORS errors, network request/tile failure counts, transferred bytes, accessibility/layout assertions, and all-maps-ready timing. Keep a durable evidence manifest; a disposable screenshot alone is not acceptance evidence.

Score each cell `0..5` with written observations and evidence links:

| Criterion | Weight | Scoring anchors |
| --- | ---: | --- |
| Streets/trails/parks/minor-road completeness in all six fixtures | 18 | `0` unusable/absent; `3` adequate with notable gaps; `5` consistently clear/context-rich |
| Route and gap contrast/hierarchy | 18 | `0` route lost; `3` readable with conflicts; `5` route dominant and gap unmistakable |
| Spanish/local label legibility and clutter | 12 | `0` missing/overlapping; `3` usable; `5` clear/local/contextual at both viewports |
| Dog-RGB dark visual identity | 12 | `0` clashes; `3` neutral; `5` cohesive without custom basemap surgery |
| Mobile layout/gesture clarity | 10 | `0` broken/overflow; `3` usable; `5` clear at `428×844` and DPR `1`/`2`, attribution and controls intact |
| Accessibility/non-colour cues | 8 | `0` colour-only/low contrast; `3` adequate; `5` contrast plus dash/labels/table alternative plan |
| Reliability/performance evidence | 8 | `0` errors/missing tiles; `3` acceptable; `5` all idle, low failures and within future budgets |
| Privacy/key/origin controls | 6 | `0` unsafe/route sent; `3` workable caveats; `5` documented restrictions and no route disclosure |
| Attribution/licence/terms fit | 4 | `0` incompatible; `3` compliant with caveats; `5` clear fit |
| Forecast cost and migration risk | 4 | `0` unbounded/lock-in; `3` acceptable; `5` predictable and adapter-tested |

Weighted result is `sum(score / 5 × weight)`, maximum 100. Two reviewers should score independently after the technical hard gates. A difference under five points is a tie: choose the lower-risk operational/price option, document the tie, and preserve the adapter. A higher-scoring candidate wins only if no hard gate fails.

Hard gates:

1. all six scenarios reach idle without fatal/CORS/style/tile errors for every required viewport/DPR/style cell;
2. route/gap/start/end remain distinguishable and visible; attribution is readable and never obscured;
3. no route coordinate appears in provider request URLs or logs;
4. production key/domain restriction rejects an unapproved origin;
5. terms permit the intended commercial/non-commercial use and cost fits the measured forecast;
6. keyboard/table alternative and WebGL/provider-failure fallback are designed/testable;
7. provider adapter can render the same route view model without domain/analytics changes.

### Evidence captured on 2026-08-13

The durable schema-v2 [evidence manifest](../../tools/map_bakeoff/evidence/2026-08-13/manifest.json), SHA-256 `4509749e573e27a2d82e6ba2247bccb1c0d6a9d87f4f0f4f1fecd3f4b968decb`, records fixture/source/external-asset hashes, environment, requests, errors, accessibility/layout assertions, credential blockers, screenshot names, and screenshot SHA-256 values. It used Chromium `151.0.7922.34`, Node.js `v24.12.0`, and MapLibre GL JS `5.23.0`.

The runner passed **17/17** requested cells: 12 Stadia dark/light/outdoor matrix cells covering desktop `1280×720` and mobile `428×844`, each at DPR `1` and `2`, plus five dark-style diagnostics for label de-emphasis, deuteranopia, protanopia, explicitly disabled browser cache, and a throttled `1.6 Mbps`/`300 ms` profile. Every standard cell rendered all six fixtures with six unique accessible map regions, a six-row keyboard-scrollable table alternative, visible attribution, zero document overflow, zero failed requests, zero console/page errors, and zero captured raw-route-coordinate leaks. That retained capture's harness suite passed **7/7**, its source hashes matched 7/7 files, its generated screenshot hashes matched 17/17 files, and all five retained external asset snapshots returned HTTP `200` during verification. The hardened credentialed-runner readiness suite now passes **12/12**, but no new credentialed provider evidence is accepted until the external gate below is run.

Readiness and transferred-byte fields are reproducibility diagnostics, **not a product performance SLO or a defensible provider-speed comparison**. OS/provider/CDN caches remain outside full control, cross-origin resource timing can omit bytes without `Timing-Allow-Origin`, CVD matrices are review approximations rather than certification, and low-bandwidth timing is one synthetic browser profile. The [human review ledger](../../tools/map_bakeoff/evidence/2026-08-13/README.md) therefore leaves aesthetic, touch, CVD, and two-reviewer scoring unchecked. Preliminary visual inspection still favors Dark for continuity with the black AP interface; that is not a provider acceptance score.

Limits remain material:

- the routes are synthetic cartographic stress fixtures and do not prove trail correctness or animal behavior;
- the run remains one Windows/Chromium engine, not a production browser/device matrix;
- **MapTiler dark/light/outdoor were not rendered because no MapTiler key was available.** The harness retained three explicit missing-credential artifacts and proved that it stopped before making any `api.maptiler.com` request rather than substituting another map or inventing a result;
- Stadia used documented keyless loopback development, so an unapproved-origin rejection cannot be claimed without a temporary Stadia property/domain-auth setup.

Therefore the renderer decision is closed, Stadia Dark remains a justified provisional front-runner, and provider selection remains an external credential plus human-review gate. No comparative score, MapTiler visual result, or production-origin-control result is claimed.

## Consequences

### Positive

- MapLibre provides a mature vector/WebGL renderer without coupling route/domain code to Google or a tile vendor.
- Segmented GeoJSON expresses speed/quality/gaps honestly and supports precise interaction.
- The bake-off makes aesthetics testable across Colombian contexts instead of choosing from marketing thumbnails.
- Provider IP/viewport disclosure and key/cost constraints are explicit.

### Costs and limits

- MapLibre/styles increase client bundle and WebGL cost; load the map only on detail views and provide a non-map fallback.
- Provider terms, pricing, styles, and tile coverage can change.
- Basemap requests still disclose approximate viewed area and network metadata even when route GeoJSON stays local to the browser.
- Google Maps migration will need a new renderer adapter and design/licensing review; portability is not zero work.

## Rejected alternatives

- **Public OSM standard tiles in production:** violates the service's intended capacity/risk model and has no SLA.
- **Google Maps immediately:** credentials/cost/product need are not yet proven; adopting it now creates avoidable SDK coupling.
- **Static map images:** poor route inspection, accessibility, segmentation, and timeline linkage.
- **Send route coordinates to a static-map/directions provider:** unnecessary location disclosure; the collar already supplies observations.
- **Declare Stadia the comparative winner from one-provider screenshots:** would fabricate the missing MapTiler evidence.

## Review trigger and remaining gate

Before Phase 0 exits, obtain a temporary origin-restricted MapTiler key and a temporary Stadia property/domain-auth setup. Run the full checked matrix for both providers, prove that each candidate rejects an unapproved origin, retain screenshots/network/timing/error manifests, score both providers independently using captured MapLibre request counts, recheck current terms/pricing, and amend this ADR with the final winner. The later Phase 6 product integration receives a separate selected-provider browser credential. Also rerun against sanitized representative field routes after explicit privacy approval; never use Home or a user's raw private route in public artifacts.

## References

- [MapLibre GL JS documentation](https://maplibre.org/maplibre-gl-js/docs/)
- [MapLibre line-layer example and `line-gradient` requirements](https://maplibre.org/maplibre-gl-js/docs/examples/create-a-gradient-line-using-an-expression/)
- [Stadia Maps Alidade Smooth Dark](https://docs.stadiamaps.com/map-styles/alidade-smooth-dark/)
- [Stadia Maps authentication](https://docs.stadiamaps.com/authentication/)
- [Stadia Maps pricing](https://stadiamaps.com/pricing/)
- [MapTiler map styles](https://docs.maptiler.com/sdk-js/api/map-styles/)
- [MapTiler API key protection](https://docs.maptiler.com/cloud/api/authentication-key/)
- [MapTiler Cloud pricing](https://www.maptiler.com/cloud/pricing/)
- [OpenStreetMap tile usage policy](https://operations.osmfoundation.org/policies/tiles/)
