# Dog-RGB Documentation

This is the canonical documentation index for Dog-RGB. English is the source language; Spanish pages are maintained as convenience translations for builders and users.

Last full code-alignment review: **2026-08-13**. Display documentation reconciled through **I6a on 2026-09-12** against implementation and recorded evidence; development subsequently delivered I6c opt-in bench preparation and I6d State. This scoped update does not revalidate unrelated workstreams.

## Document status

| Label | Meaning |
| --- | --- |
| **Current** | Describes behavior implemented in the active repository and was checked against source/configuration. |
| **Translation** | Convenience translation; follow the linked English page if the two differ. |
| **Proposed** | Optional future work. It is not implemented unless a section explicitly says otherwise. |
| **Historical snapshot** | Audit, review, or implementation plan retained for traceability. Paths, line numbers, findings, and recommendations may have aged. |

The firmware and tests are the final source of truth. A document must never turn a proposal into an implemented claim.

## Start here

| Document | Status | Purpose |
| --- | --- | --- |
| [Project README](../README.md) | Current | Project scope, implemented features, quick start, and boundaries |
| [English README alias](../README.en.md) | Redirect | Compatibility entry point for older English links |
| [Spanish project overview](../README.es.md) | Translation | Concise Spanish entry point |
| [User guide](user-guide.md) | Current | Daily operation, portal pages, LED modes, route exports, and troubleshooting |
| [Spanish user guide](manual_de_uso.md) | Translation | Convenience summary of the current user workflow |
| [Build guide](manual_de_construccion.en.md) | Current | Parts, wiring, assembly, flashing, and bench checks |
| [Architecture](architecture.md) | Current | Modules, runtime flow, data flow, storage, and constraints |
| [Local HTTP API](api-reference.md) | Current | Routes, methods, headers, request formats, response shapes, and errors |
| [Runtime configuration](portal_config.md) | Current | Config schema, defaults, validation, application, and recovery |
| [Testing and simulation](testing.md) | Current | Firmware, portal, visual, CI, and Wokwi verification |
| [WLED Phase 0 baseline](baselines/fase-0-2026-08-12.md) | Current evidence | Host-test result, CI capture, software status, and pending physical measurements |
| [WLED Phase 1 baseline](baselines/fase-1-2026-08-12.md) | Current evidence | LED bus boundary, estimated-current limiter, persistence migration, portal controls, diagnostics, and verification |
| [WLED Phase 2 baseline](baselines/fase-2-2026-08-13.md) | Current evidence | Effect registry, deterministic characterization, LED state/policy split, capabilities-driven portal, APIs, and verification |
| [WLED Phase 3 baseline](baselines/fase-3-2026-08-13.md) | Current software evidence; physical checks pending | Semantic LED layout, RGBW palettes, mirror, crossfade, alert priority, resource delta, and verification |
| [WLED Phase 4 baseline](baselines/fase-4-2026-08-13.md) | Current software evidence; HTTP live/HIL/physical checks pending | Scene model/catalog/player, A/B store, strict API/import/export, resource delta, verification, and remaining gates |
| [WLED Phase 5 baseline](baselines/fase-5-2026-08-13.md) | Current software evidence; device/captive-view checks pending | Source-owned portal, deterministic cross-platform gzip/C++ generation, direct flash transport, scene workspace, resource budgets, and verification |
| [ADR-0001: WLED clean-room and provenance](adr/0001-wled-clean-room-y-licencia-del-proyecto.md) | Current policy, amended | Prevents unreviewed literal reuse of WLED material; its former license-selection prerequisite is resolved |
| [ADR-0002: MIT project license](adr/0002-project-license-mit.md) | Current decision | Licenses original project material under MIT and defines the third-party boundary |
| [ADR-0003: LED scene model and store](adr/0003-scene-model-and-store.md) | Current decision | Freezes scene authority, IDs/wire, A/B recovery, player semantics, API limits and accepted resource budget |
| [ADR index](adr/README.md) | Current decisions | Cloud ADRs 0005–0010 and their implementation/evidence maturity |
| [Device-v1 contract](../contracts/device-v1/README.md) | Phase 0 frozen target | Schemas, fixtures, HLC, error catalog, compatibility, and executable cross-codec checks; not deployed |
| [Cloud Phase 0 field matrix](cloud/phase0-field-matrix.md) | Phase 0 evidence | Unit/range/privacy/ownership/sync disposition for current config and telemetry fields |
| [Cloud Phase 0 documentation index](cloud/README.md) | Phase 0 evidence | Storage/capacity, threat, privacy, retention, and credential artifacts with remaining gates |
| [Requirements](requirements.md) | Current | Functional, safety, and quality requirements with verification state |
| [Roadmap](roadmap.md) | Current | Implemented baseline and optional next phases |
| [Work queue](tasks.md) | Current | Concrete remaining validation and engineering tasks |

## Hardware and electrical

| Document | Status | Notes |
| --- | --- | --- |
| [Hardware baseline decision](phase0_freeze.md) | Current | Chosen components, pins, and fixed baseline; despite the filename, updated to show current deviations |
| [BOM and power budget](bom_power_budget.md) | Current, estimates | Assumptions and mandatory measurements; not a runtime guarantee |
| [SK6812 wiring](sk6812_wiring.md) | Current | Level shifting, decoupling, power distribution, and bring-up |
| [XIAO ESP32-S3 pin map](../xiao_s3_pin.md) | Current reference | Board pin reference plus Dog-RGB assignments |
| [ESP32-S3 datasheet](../Datasheets/esp32-s3_datasheet.pdf) | External vendor artifact | Locally retained PDF; not authored by this project |
| [Spanish build guide](manual_de_construccion.es.md) | Translation | Spanish counterpart to the English build guide |
| [Legacy build-guide path](manual_de_construccion.md) | Redirect | Compatibility pointer to the language-specific guides |

## Firmware behavior

| Document | Status | Notes |
| --- | --- | --- |
| [Firmware project README](../Platformio/Dog-RGB/README.md) | Current | Developer quick reference inside the PlatformIO project |
| [Board profiles and Waveshare I0–I3](../Platformio/Dog-RGB/docs/boards.md) | Current software, physical acceptance pending | Seven build targets, candidate No Touch V2 profile, power/USB/LED/GPS diagnostics and I3 text LCD |
| [Display I0 baseline](baselines/display-i0-2026-09-12.md) | Software evidence; physical/Wokwi runtime pending | Before/after builds, native GPIO tests, size deltas, CI matrix and open hardware gates |
| [I3 text LCD guide](../Platformio/Dog-RGB/docs/display-i3.md) | Software implemented; physical acceptance pending | Driver pin, snapshot semantics, partial updates, USB controls and timing protocol |
| [I4 consolidation guide](../Platformio/Dog-RGB/docs/display-i4.md) | In progress; full bench pending | LCD-only demo, serial capture and real-peripheral/portal acceptance sequence |
| [Display I4 progress](baselines/display-i4-2026-09-12.md) | Software and partial physical evidence | Connected USB board, memory/reboots, LCD measurements, native configuration recovery and embedded browser checks |
| [I5 shared LVGL guide](../Platformio/Dog-RGB/docs/display-i5.md) | Static view implementation | Shared PC/device components, pinned LVGL, comparison controls and measurement limits |
| [Display I5 progress](baselines/display-i5-2026-09-12.md) | See recorded acceptance | Three real-renderer captures, UI/service contracts and USB bench results |
| [Display I6a pages and input](../Platformio/Dog-RGB/docs/display-i6.md) | Implemented, see physical evidence | Activity/Connection, read-only Wi-Fi adapter, BOOT release/wake and bounded redraw |
| [Display I6c inactivity](../Platformio/Dog-RGB/docs/display-i6c.md) | Experimental, disabled at boot | 30 s backlight timer, wake-only input and separate [evidence](baselines/display-i6c-2026-09-12.md); joint load remains open |
| [Display I6d State](../Platformio/Dog-RGB/docs/display-i6d.md) | Experimental third page | GPS and effective LED policy; [evidence](baselines/display-i6d-2026-09-12.md), fourteen PNGs, five CTest contracts |
| [Display I6a baseline](baselines/display-i6-2026-09-12.md) | See recorded acceptance | Nine shared-renderer captures, navigation tests, resource changes and USB timing |
| [Waveshare LCD 1.69 technical investigation](waveshare-lcd169-technical-research.md) | Historical I5 research with I6a reconciliation, Spanish | Black/backlight diagnosis, official V2 circuit, SPI budget and forum cases; follow the incremental plan for current tasks |
| [Display I3 baseline](baselines/display-i3-2026-09-12.md) | Software evidence; physical acceptance pending | Seven builds, native adapter/rendering checks, layout previews and resource deltas |
| [Display I2 baseline](baselines/display-i2-2026-09-12.md) | Software evidence; physical acceptance pending | GPS reception/readout, normal LED policy with bench limits, six builds and 137 host tests |
| [Display I1 baseline](baselines/display-i1-2026-09-12.md) | Software evidence; physical acceptance pending | LED commands, actual bus/limiter native tests, five builds, resource comparison and bench limits |
| [Configuration parameters](config_params.md) | Current | Compile-time constants versus persisted runtime fields |
| [GNSS and metrics](gps_analysis.md) | Current | Parser, trust gates, accounting, date rollover, and limitations |
| [LED UI](led_ui_spec.md) | Current | Status pixels, priorities, modes, and Day Mode interaction |
| [LED effect catalog](led_effects.md) | Current | IDs, inputs, defaults, and effect-specific color behavior |
| [Color reference](color-reference.md) | Current | Default speed/geofence colors and system indicators |
| [Spanish color guide](manual_de_colores.md) | Translation | Spanish convenience reference |
| [Geofence mode](geofence_mode_plan.md) | Current implementation note | Original plan converted into an implemented-behavior reference |
| [Day Mode](../Platformio/Dog-RGB/docs/modo-dia.md) | Current | Trusted-time gate and effect-pixel power saving |
| [BLE summary](ble_spec.md) | Current, disabled by default | Implemented wire format and coexistence limitation |
| [Main-loop modularization](main_refactor.md) | Current design note | Result of the completed refactor, not an open plan |

## Portal and developer tooling

| Document | Status | Notes |
| --- | --- | --- |
| [Web portal product spec](web_portal_spec.md) | Current | User-facing pages, states, accessibility, and offline constraints |
| [Wi-Fi/AP behavior](wifi_portal_spec.md) | Current | AP/STA policy, scanning, captive portal, retries, and persistence |
| [Wi-Fi state diagram](wifi_portal_state_diagram.md) | Current | Simplified state and policy flow |
| [Portal preview tool](../tools/ap_portal_preview/README.md) | Current | Build and serve the same generated bundles represented by the firmware arrays |
| [Visual screenshot workflow](ap_portal_visual_screenshot_workflow_guide.md) | Current | Playwright workflow and baseline rules |
| [Wokwi guide](../Platformio/Dog-RGB/docs/wokwi.md) | Current | Simulation assets, scenarios, controls, and limitations |

## Display planning and supporting research

| Document | Status | Notes |
| --- | --- | --- |
| [Waveshare display variant research and integration plan](PLANS/2026-09-12_waveshare-display-variant.md) | Supporting research reconciled through I6a, Spanish | V2 schematic, implemented profiles/UI and explicitly optional hardware/tooling extensions |
| [Display incremental delivery](PLANS/2026-09-12_display-incremental-delivery.md) | Governing contract; resumed after I6a, Spanish | V1 optical/button/radio, V2 physical I0–I3, V3 joint load; separate I6b–I6e packages and I7 decisions |
| [Display visual identity subplan](PLANS/2026-09-12_display-visual-identity.md) | Planned, Spanish | Name, WhatsApp QR and phone; incremental persistence/UI/motion with separate acceptance |
| [Visual identity research](display-visual-identity-research.md) | Primary sources and qualified forum observations, Spanish | Pet identification, Wear typography, repositories, LVGL 8.4 constraints and [conceptual board](assets/display-visual-identity/concepts.svg) |
| [28 Display GitHub/MCP investigations](display-github-research-2026-09-12.md) | Research and limited host experiment, Spanish | QR geometry/decoder, fonts, assets, effects, drivers, editors and simulators; adoption decisions mapped to VIS increments |
| [Display AI design workflow](PLANS/2026-09-12_display-ai-workflow.md) | Current workflow through I6d, Spanish | Shared components → existing renderer → fourteen captures → corrections → board validation; animation capture remains proposed |
| [Display use and screen contract](PLANS/2026-09-12_display-use-and-screens.md) | I6d implemented; rest states proposed, Spanish | Activity/Wi-Fi/State, rest versus missing GPS, opt-in inactivity and future pause contract |
| [Display libraries and repository research](display-library-research.md) | Research with applied stack, Spanish | Arduino_GFX/LVGL pinned and observed locally; other repositories were reviewed, not benchmarked; future evaluations stay conditional |
## Optional future work

| Document | Status | Notes |
| --- | --- | --- |
| [Companion app MVP](app_mvp_spec.md) | Proposed | BLE reader concept; blocked while BLE remains disabled by default |
| [Portal configuration presets](portal_config_presets.md) | Proposed | Whole-runtime profiles distinct from implemented visual scenes; no selector/config-preset persistence |
| [WLED lessons and implementation plan](analisis-wled-y-plan-implementacion.md) | Phases 0–5 implemented in software; physical-HIL acceptance remains separate, Spanish | Current-limiting, effect-registry, palette, segment, scene, and web-asset roadmap |
| [App wireframe and data flow](flow_wireframe.md) | Proposed | Companion-app concept, separate from the implemented local portal |
| [Cloud platform and bidirectional sync plan](PLANS/2026-08-13_web-platform-bidirectional-sync-plan.md) | Accepted optional direction; Phase 0 and owner-authorized local Phase 1 in progress | Current detailed implementation plan; device-v1 passes 48/48, the corrected 664-slot host candidate remains review/open, and the physical-storage/credentialed-map gates remain open. Phase 1 local-cloud foundation work proceeds by explicit owner exception; Phase 2 remains unauthorized. |
| [Cloud portal master plan](PLANS/2026-08-01_cloud-portal-master-plan.md) | Superseded proposed snapshot | Older upload-oriented design retained for history |
| [Software area](../software/README.md) | Proposed | Placeholder and boundaries for future companion/cloud software |

## Repository entry points

- [Active firmware area](../firmware/README.md)
- [Hardware area](../hardware/README.md)
- [Future software area](../software/README.md)

## Historical audits and reviews

These documents are evidence of how the design evolved. Read their status notes before acting on a finding.

- [Firmware and electronics audit](../AUDIT_ANALYSIS_AND_IMPROVEMENT_PLAN.md)
- [Wi-Fi AP deep audit](../WIFI_AP_DEEP_AUDIT.md)
- [External audit notes](auditoria_externa.md)
- [Early AP analysis](ap_analysis.md)
- [AP comprehensive review — 2026-05-05](ap_access_point_comprehensive_review_2026-05-05.md)
- [AP portal UI review — 2026-05-06](ap_portal_ui_deep_review_2026-05-06.md)
- [Portal screenshot workflow plan — 2026-05-06](ap_portal_visual_screenshot_workflow_plan_2026-05-06.md)
- [GPS speed-mode hardening review](gps_speed_mode_hardening_review.md)
- [Show-mode hardening review](show_mode_hardening_review.md)
- [Show-mode manual test checklist](show_mode_manual_test_checklist.md)
- [Original Show-mode implementation plan](led_show_mode_plan.md)
- [Dependency audit — 2026-08-01](dependency_update_audit_2026-08-01.md)
- [Dependency update execution — 2026-08-01](dependency_update_execution_2026-08-01.md)
- [Web portal deep audit — 2026-08-11](web_portal_deep_audit_2026-08-11.md)
- [Web portal UX review — 2026-08-11](web_portal_ux_review_2026-08-11.md)

Implementation plans are catalogued in [PLANS/README.md](PLANS/README.md). Firmware-local historical plans live under [`Platformio/Dog-RGB/docs/superpowers/plans`](../Platformio/Dog-RGB/docs/superpowers/plans/).

- [Day Mode implementation plan](../Platformio/Dog-RGB/docs/superpowers/plans/2026-05-06-modo-dia.md) — historical; completed
- [Retro-console UI implementation plan](../Platformio/Dog-RGB/docs/superpowers/plans/2026-retro-console-ui.md) — historical; completed and evolved

## Maintaining this documentation

- Update a current page whenever its source behavior, defaults, route, or command changes.
- Add a date and a status banner to audits and plans; do not silently rewrite their historical conclusions.
- Prefer relative links and symbol/file references over volatile line-number-only references.
- Check local links and run the commands in [Testing and simulation](testing.md) before marking a documentation update complete.
- Follow the repository-wide rules in [CONTRIBUTING.md](../CONTRIBUTING.md).
