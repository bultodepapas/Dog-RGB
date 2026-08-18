# Phase 0 map-provider bakeoff harness

This local-only harness renders deterministic synthetic route fixtures against
candidate basemaps. It is an evaluation surface, not the product UI. It never
loads animal, owner, account, or production location data.

## Visual thesis

A restrained night-field instrument: near-black terrain, low-noise labels, and
the existing Dog RGB green/gold/magenta route language carrying the visual focus.
The light and outdoor providers deliberately test whether that hierarchy survives
outside the preferred dark presentation.

## Test content and interaction

- Four short invented cartographic shapes stress urban, park, steep-trail, and
  rural contexts.
- An 11-point route measures approximately 1 km and exposes sparse-sampling
  chords and a deliberate data-quality gap.
- A deterministic 241-point route represents exactly two hours at a 30-second
  cadence and stresses overlap, rendering density, and two quality gaps.
- Route width supplements color for speed bands. Dashed magenta segments mark
  gaps. Start/end circles and a textual route table provide additional cues.
- Pointer hover or click exposes synthetic speed, band, and quality. MapLibre's
  canvas remains keyboard-focusable. Reduced-motion mode disables animation.

The generated shapes are fixtures, not verified trails or a dog's historical
route. Basemap requests inherently identify viewed tile bounds; the evidence
runner separately asserts that raw fixture coordinates never appear in provider
request URLs or DOM text.

## Run locally

```powershell
node tools/map_bakeoff/server.mjs --port 4174
```

Open a keyless Stadia candidate:

```text
http://127.0.0.1:4174/?provider=stadia-dark
http://127.0.0.1:4174/?provider=stadia-light
http://127.0.0.1:4174/?provider=stadia-outdoor
```

Optional diagnostic query parameters are `scenarios=all|stress`,
`labels=normal|deemphasized`, and
`cvd=none|deuteranopia|protanopia|grayscale`. CVD modes are screenshot review
aids, not accessibility certification.

MapTiler candidates deliberately stop before their first provider request in
manual/keyless mode. Credential input through a URL, including a fragment, is
not supported. The current provider IDs follow the official
[Stadia Outdoors](https://docs.stadiamaps.com/map-styles/outdoors/) and
[MapTiler Maps API](https://docs.maptiler.com/cloud/api/maps/) documentation.

## Test and capture durable evidence

```powershell
node --test tools/map_bakeoff/test-harness.mjs
$env:DOG_RGB_MAP_EVIDENCE_RUN_ID = '2026-08-18-keyless-01'
node tools/map_bakeoff/capture-evidence.mjs
Remove-Item Env:DOG_RGB_MAP_EVIDENCE_RUN_ID
```

The Playwright runner creates a fresh isolated context per cell and writes its
screenshots plus `evidence/<run-id>/manifest.json`. A dated, unique, filesystem-safe
run ID is mandatory and an existing non-empty directory is never overwritten.
The standard keyless matrix covers:

- Stadia dark, light, and outdoor;
- desktop 1280×720 at DPR 1 and DPR 2;
- mobile 428×844 at DPR 1 and DPR 2;
- all six fixtures in each standard cell.

Focused diagnostics cover label de-emphasis, deuteranopia/protanopia
approximations, an explicitly disabled browser cache, and a cache-disabled
1.6 Mbps / 300 ms latency profile using the two sparse/dense stress fixtures.
The earlier 390 px DPR 1 screenshots are retained as legacy regression evidence.

Automated gates check rendering, attribution, exact DPR and physical screenshot
width, overflow, request/browser errors, coordinate leakage, accessible names,
focusable canvases, table structure, and applied diagnostic state. A failed CDN
or browser cell is written as a failure and makes the command exit non-zero; it
is never silently omitted. Human aesthetic/CVD decisions remain explicitly
pending in the evidence README and manifest.

## Run the credentialed comparison

The credentialed mode is intentionally stricter than the keyless regression
mode. Before running it:

1. Create two temporary test hostnames that both resolve exclusively to
   loopback, for example `maps-allowed.dog-rgb.test` and
   `maps-rejected.dog-rgb.test`. The runner refuses `localhost`, literal
   loopback hosts, non-loopback DNS answers, paths, query strings, fragments,
   user info, different ports, or identical origins.
2. Configure a temporary protected MapTiler key for only the allowed hostname.
3. Configure a temporary Stadia property/domain for only the allowed hostname.
   Stadia exempts `localhost` and `127.0.0.1` from authentication, which is why
   aliases are required to prove rejection.
4. Inject the key only into the current process environment and run the matrix:

```powershell
$env:DOG_RGB_MAPTILER_KEY = '<temporary-origin-restricted-key>'
$env:DOG_RGB_MAP_ALLOWED_ORIGIN = 'http://maps-allowed.dog-rgb.test:4174'
$env:DOG_RGB_MAP_REJECTED_ORIGIN = 'http://maps-rejected.dog-rgb.test:4174'
$env:DOG_RGB_MAP_EVIDENCE_RUN_ID = '2026-08-18-credentialed-01'
node tools/map_bakeoff/capture-evidence.mjs --credentialed
Remove-Item Env:DOG_RGB_MAPTILER_KEY
Remove-Item Env:DOG_RGB_MAP_ALLOWED_ORIGIN
Remove-Item Env:DOG_RGB_MAP_REJECTED_ORIGIN
Remove-Item Env:DOG_RGB_MAP_EVIDENCE_RUN_ID
```

Never paste real values into shell history, issue trackers, source, or review
documents. Use the operating system's ephemeral secret-injection mechanism when
available. MapTiler's browser API requires its public API key in upstream request
query parameters and explicitly documents that browser keys are visible; the
protection is an exact allowed-origin restriction. The harness therefore keeps
the input out of its own URL, removes the runtime global immediately after style
construction, excludes the key from the static-server environment, stores only
request origins and paths, recursively redacts diagnostics, and refuses to write
a manifest containing the supplied value. See MapTiler's
[API-key guidance](https://docs.maptiler.com/cloud/api/authentication-key/) and
[origin protection guidance](https://docs.maptiler.com/guides/maps-apis/maps-platform/how-to-protect-your-map-key/).
Stadia's browser flow uses its documented
[domain authentication](https://docs.stadiamaps.com/authentication/) and no key.

Credentialed mode expands the standard matrix to both families: 24 standard
cells (six provider/style variants × four viewport/DPR profiles), ten symmetric
diagnostic cells, and one rejected-origin proof for each provider. An
unauthorized-origin proof passes only when the page fails and the provider
returns HTTP 401 or 403; an omitted or unexpectedly successful request fails the
run. After the automated run, give separate copies of
[`review-scorecard-template.md`](review-scorecard-template.md) to two reviewers.
Do not reconcile their scores until both independent files are complete. Revoke
the temporary key and remove the temporary Stadia domain after artifact and
secret-scan verification.
