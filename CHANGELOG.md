# Changelog

All notable public changes to Dog-RGB are recorded here. Versions refer to
firmware releases for the Seeed Studio XIAO ESP32-S3, not to the private portal
tooling package.

## [2.0.0] - 2026-08-13

### Highlights

- Rebuilt the LED pipeline around two RGBW buses, a semantic collar layout,
  stable effect and palette registries, status-preserving transitions, alerts,
  and one global estimated-current limiter.
- Added four built-in scenes, four transactional user slots, Show-by-scenes,
  strict versioned JSON APIs, generation conflict handling, and safe
  import/export.
- Added the embedded graphical scene and palette workspace in `/config`, with
  a low-rate approximate dual-strip preview and capability-driven controls.
- Moved the portal to source-owned HTML/CSS and a deterministic Node 24 build
  that minifies, gzip-compresses, budgets, fingerprints, and embeds four pages
  directly from flash.
- Hardened GNSS metrics, session/route persistence, Wi-Fi event ownership and
  retry behavior, captive-portal handling, optional write PIN protection, CSRF
  intent checks, escaping, and diagnostics.
- Expanded verification with native C++ characterization, Python host
  contracts, portal smoke checks, Playwright behavior/accessibility coverage,
  pinned Linux visual baselines, reproducible PlatformIO builds, and Wokwi
  scenarios.

### Upgrade notes

- When upgrading from `v1.0.0`, flash the complete image or use PlatformIO's
  upload target at least once. App-only `firmware.bin` flashing does not install
  the current partition table and its dedicated `tracknvs` partition.
- Runtime configuration migrates transactionally; scenes use their own A/B
  store. Export any user scenes before downgrading to older firmware.
- The project remains local-first. OTA, cloud accounts, battery telemetry, and
  a native companion app are not part of this release.

### Software release gate

- 131/131 firmware host contracts and 4/4 deterministic generator tests.
- Static smoke validation for all four embedded pages.
- 84/84 Playwright behavior, accessibility, security, and responsive checks.
- 18/18 pixel comparisons in the pinned Playwright Linux renderer.
- Successful production and Wokwi PlatformIO builds; the production app uses
  57,636 bytes of RAM (17.6%) and 1,151,859 bytes of flash (34.5%).

## [1.0.0] - 2026-02-03

- First public functional prototype with GNSS telemetry, four LED modes,
  AP/STA portal configuration, NVS persistence, and optional BLE summary.

[2.0.0]: https://github.com/bultodepapas/Dog-RGB/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/bultodepapas/Dog-RGB/releases/tag/v1.0.0
