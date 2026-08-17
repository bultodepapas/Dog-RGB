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

MapTiler candidates deliberately stop at the credential gate unless a temporary
testing key is supplied in the URL fragment:

```text
http://127.0.0.1:4174/?provider=maptiler-dark#key=<temporary-testing-key>
http://127.0.0.1:4174/?provider=maptiler-light#key=<temporary-testing-key>
http://127.0.0.1:4174/?provider=maptiler-outdoor#key=<temporary-testing-key>
```

The fragment keeps a test key out of local HTTP logs; it does not make the key
secret from browser code. Never paste a key into source, screenshots, or
committed evidence. Production credentials require exact origin restrictions.
The current provider IDs follow the official
[Stadia Outdoors](https://docs.stadiamaps.com/map-styles/outdoors/) and
[MapTiler Maps API](https://docs.maptiler.com/cloud/api/maps/) documentation.

## Test and capture durable evidence

```powershell
node --test tools/map_bakeoff/test-harness.mjs
node tools/map_bakeoff/capture-evidence.mjs
```

The Playwright runner creates a fresh isolated context per cell and writes its
screenshots plus `evidence/2026-08-13/manifest.json`. The standard matrix covers:

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
