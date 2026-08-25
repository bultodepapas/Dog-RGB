# Implementation and historical plans

The 2026-08-13 web-platform plan is the active execution contract for the optional cloud workstream. Every other file in this directory is retained as design history and is **not** a current implementation contract. Executable contracts, migrations, and tests remain authoritative for behavior that already exists.

| Plan | Status in the current repository |
| --- | --- |
| [LED effects with FastLED — 2026-01-20](2026-01-20_led-effects-fastled.md) | Historical alternative; active firmware uses Adafruit NeoPixel and custom effects |
| [Developer portal page — 2026-02-03](2026-02-03_dev-portal-page.md) | Implemented in evolved form as `/dev` and `/api/dev` |
| [Three-session history — 2026-02-03](2026-02-03_historial-3-sesiones.md) | Implemented in evolved, transactional form |
| [Single-effect LED mode — 2026-02-03](2026-02-03_led-single-effect-mode.md) | Implemented as Simple mode |
| [Portal statistics ideas — 2026-02-03](2026-02-03_portal-stats-ideas.md) | Mixed ideas; verify each item individually |
| [Glacier-tech portal redesign — 2026-02-03](2026-02-03_portal-ui-redesign-glacier-tech.md) | Historical design direction; current embedded portal has since evolved |
| [Supabase sync — 2026-02-03](2026-02-03_supabase-sync-plan.md) | Not implemented; optional cloud idea |
| [Web UI improvement — 2026-02-03](2026-02-03_web-ui-improvement-plan.md) | Partially/evolutionarily implemented; current portal/tests are authoritative |
| [Welcome opposite directions — 2026-02-03](2026-02-03_welcome-opposite-directions.md) | Historical LED implementation plan |
| [Route portal — 2026-02-04](2026-02-04_plano-ruta-portal.md) | Implemented in evolved form with bounded JSON/CSV/GeoJSON streaming |
| [Cloud portal master plan — 2026-08-01](2026-08-01_cloud-portal-master-plan.md) | Superseded design history |
| [Cloud web platform and bidirectional sync — 2026-08-13](2026-08-13_web-platform-bidirectional-sync-plan.md) | **Active implementation contract**; local database/Edge/simulator foundation implemented, M0 baseline closure in progress, product portal/firmware client/hosted deployment pending |

When a plan is completed, keep the dated file as a snapshot and document the resulting behavior in a current reference page.
