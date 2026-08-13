# Dog-RGB Documentation

This is the canonical documentation index for Dog-RGB. English is the source language; Spanish pages are maintained as convenience translations for builders and users.

Last code-alignment review: **2026-08-13** against the active firmware in [`Platformio/Dog-RGB`](../Platformio/Dog-RGB/).

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
| [ADR-0001: WLED clean-room and provenance](adr/0001-wled-clean-room-y-licencia-del-proyecto.md) | Current policy, amended | Prevents unreviewed literal reuse of WLED material; its former license-selection prerequisite is resolved |
| [ADR-0002: MIT project license](adr/0002-project-license-mit.md) | Current decision | Licenses original project material under MIT and defines the third-party boundary |
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
| [Portal preview tool](../tools/ap_portal_preview/README.md) | Current | Extract and serve embedded pages locally |
| [Visual screenshot workflow](ap_portal_visual_screenshot_workflow_guide.md) | Current | Playwright workflow and baseline rules |
| [Wokwi guide](../Platformio/Dog-RGB/docs/wokwi.md) | Current | Simulation assets, scenarios, controls, and limitations |

## Optional future work

| Document | Status | Notes |
| --- | --- | --- |
| [Companion app MVP](app_mvp_spec.md) | Proposed | BLE reader concept; blocked while BLE remains disabled by default |
| [Portal configuration presets](portal_config_presets.md) | Proposed | User-selectable profiles; no persistence/UI implementation yet |
| [WLED lessons and implementation plan](analisis-wled-y-plan-implementacion.md) | Phases 0–3 implemented in software; Phase 3 physical checks and later phases pending, Spanish | Current-limiting, effect-registry, palette, segment, preset, and web-asset roadmap |
| [App wireframe and data flow](flow_wireframe.md) | Proposed | Companion-app concept, separate from the implemented local portal |
| [Cloud portal master plan](PLANS/2026-08-01_cloud-portal-master-plan.md) | Proposed snapshot | Large optional plan; no cloud application exists in this repository |
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
