# Geofence Mode — Implemented Behavior

**Status:** Implemented. This file originated as a plan and now documents the resulting firmware; presets remain a separate unimplemented proposal.

Geofence mode maps the distance from a persisted Home coordinate into the same ten color/effect ranges used by Speed mode.

## Home lifecycle

- Home is stored separately from runtime configuration as a CRC-protected A/B record.
- Source values are `none`, `auto`, or `manual`.
- When no Home exists, a trusted/current GNSS fix must remain stable for ten seconds before auto-Home is attempted.
- `POST /api/home/set` replaces Home with the current trusted coordinate and source `manual`.
- `POST /api/home/clear` persists an explicit unset state.
- Interrupted set/clear writes retain the previous complete generation; a failed auto-set remains unset and can retry.
- Restoring runtime configuration defaults does not clear Home.

## Range calculation

`fence_max_m` is runtime configurable from 50 to 5,000 m and defaults to 300 m.

```text
step = fence_max_m / 10
range 1 = distance <= step
range 2 = distance <= 2 × step
...
range 9 = distance <= 9 × step
range 10 = distance > 9 × step
```

For the default 300 m maximum, each band is 30 m. The compiled cyan-to-red palette progresses from near to far.

## Hysteresis

To avoid rapidly switching at a band boundary, the range changes only after crossing a margin:

```text
margin = max(5 m, step × 3%)
```

Moving outward must pass the current upper edge plus the margin; moving inward must pass the lower edge minus it.

## Fallbacks

- No trusted GNSS fix: normal animated rainbow fallback.
- Trusted fix but no Home: amber breathing body effect.
- Day Mode active: effect body off, status pixels retained.
- Simple/Show/Speed: Geofence selector is not used.

## Portal and API

Select `geofence` as `mode` and configure `fence_max_m` through `/config` or `/api/config`. Home state/action routes are documented in [Local HTTP API](api-reference.md#home).

Geofence shares `effects.range1..range10` with Speed mode. It does not have separate effects and is not changed by the proposed preset system.

## Privacy and limits

Home/current coordinates and distance are available to local read clients through `/api/home` and `/api/dev`. There is no cloud upload, but access to the collar network implies access to this telemetry. GNSS/geofence accuracy is not a containment or escape-warning guarantee.
