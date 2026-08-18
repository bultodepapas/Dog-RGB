# Map bakeoff evidence review ledger

This directory contains Phase 0 synthetic evidence only. It contains no dog,
owner, account, secret, or production coordinate. `manifest.json` is the
machine-readable source of truth for environment, source/asset hashes, every
matrix cell, network diagnostics, browser failures, and credential blockers.
It is retained schema-v2 evidence bound to the source hashes in that manifest;
the current hardened runner and its 12/12 readiness suite do not retroactively
change this capture. A later credentialed run writes schema v3 evidence and must
use two independent copies of the top-level
[`review-scorecard-template.md`](../../review-scorecard-template.md).

## Automated acceptance

Run these from the repository root:

```powershell
node --test tools/map_bakeoff/test-harness.mjs
$env:DOG_RGB_MAP_EVIDENCE_RUN_ID = 'YYYY-MM-DD-keyless-01'
node tools/map_bakeoff/capture-evidence.mjs
Remove-Item Env:DOG_RGB_MAP_EVIDENCE_RUN_ID
```

The capture command writes a new run directory and refuses to overwrite this
retained directory. Hash verification of this ledger is separate from a new run.

The capture command passes only when every requested cell reaches MapLibre idle,
has the expected canvas/table/accessibility structure and visible attribution,
matches its exact DPR, avoids page overflow and raw-coordinate URL leakage, and
has no failed request, console error, or page error. Cold-cache and low-bandwidth
timings are diagnostic only; upstream/provider caches remain uncontrolled.

## Human review checklist

Do not change a box based only on an automated assertion. Record reviewer names,
date, and findings next to the item or in the provider ADR before claiming it is
accepted.

- [ ] Outdoor aesthetic — compare
  `matrix-stadia-outdoor-desktop-1280x720-dpr1.png` and
  `matrix-stadia-outdoor-mobile-428x844-dpr2.png`; terrain/trails must remain
  useful while the route is the strongest visual element.
- [ ] Label de-emphasis — compare
  `matrix-stadia-dark-mobile-428x844-dpr1.png` with
  `diagnostic-label-deemphasis-stadia-dark-mobile-428-dpr1.png`; orientation
  context must survive the reduced label opacity.
- [ ] Deuteranopia diagnostic — inspect
  `diagnostic-cvd-deuteranopia-stadia-dark-mobile-428-dpr1.png`; trace the route,
  identify dashed gaps, and note ambiguous start/end or speed cues.
- [ ] Protanopia diagnostic — inspect
  `diagnostic-cvd-protanopia-stadia-dark-mobile-428-dpr1.png` with the same
  criteria. These color matrices are review approximations, not certification.
- [ ] Exact 428 px mobile — inspect dark/light/outdoor at DPR 1 and DPR 2; legend,
  endpoint marks, attribution, text alternative, and route casing must remain
  readable without document-level clipping.
- [ ] Two-reviewer provider decision — score route salience, label noise, terrain
  context, brand fit, and mobile clarity independently before resolving
  disagreements.

## Credential truth

- MapTiler dark/light/outdoor visual cells are blocked because no temporary key
  was provided. The three `maptiler-*-missing-credential-428-dpr1.png` artifacts
  prove the harness fails before contacting the MapTiler API.
- Stadia captures use only documented keyless loopback development. Rejected
  unapproved-origin authentication remains blocked because this run must not use
  a Stadia property credential.
- The preserved `stadia-*-mobile.png` files are the earlier 390 px DPR 1 evidence;
  their original hashes and provenance remain under `legacyEvidence`.
