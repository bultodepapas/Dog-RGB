# Dog-RGB Web UI

`webui/src` is the only editable source for the four pages embedded in the
ESP32 firmware. The generated C++ arrays and manifest are committed so a normal
PlatformIO build remains offline and does not require Node.js.

## Edit and build

Install the exact Node version in `.node-version`, then install dependencies and
regenerate:

```text
npm ci
npm run webui:build
```

The build performs these steps deterministically:

1. validates the four HTML sources and the shared CSS;
2. inlines the shared CSS;
3. minifies HTML, CSS and JavaScript without mangling global names;
4. produces gzip level 9 without timestamp or filename metadata;
5. writes the preview pages, manifest and `PROGMEM` arrays;
6. verifies gzip round trips and per-page size budgets.

Run `npm run webui:check` in CI or before committing. It regenerates everything
in memory and fails when a committed manifest or C++ asset is stale. PlatformIO
performs a cheaper hash-only check and never runs npm or accesses the network.

## Preview and tests

```text
npm run webui:serve
npm run webui:test
```

The local server uses the same minified, decompressed bytes represented by the
firmware arrays. API calls are supplied by the existing Playwright fixtures; it
does not emulate the ESP32 radio, storage or timing.

Do not edit files under `webui/generated`, `.ap-portal-preview`, or
`generated_assets.*` by hand.
