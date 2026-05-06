# AP Portal Preview

This folder contains the local preview helper for the embedded AP portal.

Run:

```bash
npm run ap-portal:serve
```

The server extracts HTML from `Platformio/Dog-RGB/src/web/pages.cpp` into `.ap-portal-preview/` and serves:

- `/`
- `/wifi`
- `/config`
- `/dev`

API routes intentionally return `404` from the preview server. Playwright tests mock those endpoints with deterministic fixtures before each page loads.
