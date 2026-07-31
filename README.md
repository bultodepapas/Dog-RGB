# Smart LED Dog Collar

[English](README.en.md) | [Espanol](README.es.md) | [User Manual](docs/manual_de_uso.md) | [Build Manual](docs/manual_de_construccion.md)

Smart, high-visibility LED collar for medium-to-large dogs. Built for safety, comfort, and GPS-first telemetry with a local Wi-Fi portal and runtime LED configuration.

---

## Quick Links

- User guide: [docs/manual_de_uso.md](docs/manual_de_uso.md)
- Build guide: [docs/manual_de_construccion.md](docs/manual_de_construccion.md)
- Color reference: [docs/manual_de_colores.md](docs/manual_de_colores.md)
- XIAO ESP32-S3 pinout (official): [xiao_s3_pin.md](xiao_s3_pin.md)
- Architecture: [docs/architecture.md](docs/architecture.md)
- Firmware refactor: [docs/main_refactor.md](docs/main_refactor.md)
- Requirements: [docs/requirements.md](docs/requirements.md)
- Roadmap: [docs/roadmap.md](docs/roadmap.md)
- Tasks: [docs/tasks.md](docs/tasks.md)

---

## What This Is

A wearable LED collar with GPS-first telemetry, configurable LED behavior, and a local Wi-Fi portal (AP/STA) for data and runtime settings.

---

## System Summary

- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz)
- LEDs: SK6812 RGBW (5V, single-wire), dual strips
- Power: 21700 Li-ion + BMS + 5V boost (>=3A)
- Portal: AP + STA with local dashboard and config UI
- BLE: read-only daily summary characteristic

More details:
- Hardware freeze: [docs/phase0_freeze.md](docs/phase0_freeze.md)
- Wiring: [docs/sk6812_wiring.md](docs/sk6812_wiring.md)
- Power budget: [docs/bom_power_budget.md](docs/bom_power_budget.md)

---

## Firmware (Current Status)

The active firmware project is in [Platformio/Dog-RGB](Platformio/Dog-RGB) with:

- NMEA RMC + GGA parsing (fix, speed, satellites)
- Distance calculation (Haversine) with spike filtering
- Active time tracking and speed thresholds
- GNSS-timestamp active-time accounting that survives buffered loop stalls
- Confirmed, monotonic GPS-date rollover with a CRC-protected completed-day journal
- Max/avg speed metrics
- NVS persistence for metrics + runtime config
- CRC32-protected A/B runtime-configuration recovery across interrupted writes
- Validated A/B home/geofence recovery across interrupted set and clear operations
- Wi-Fi portal (AP/STA) with `/`, `/api/summary`, `/wifi`
- Bounded JSON/CSV/GeoJSON track streaming with GNSS servicing during exports
- Runtime config UI at `/config` with `/api/config` + `/api/config/reset`
- BLE read-only daily summary payload
- LED UI with 12 effects, configurable per speed range, plus Show/Simple modes

Short summary: the firmware is now modularized by domain (GPS, Wi-Fi, web portal, BLE, LED UI, config, storage), and `main.cpp` only orchestrates setup/loop. See [docs/architecture.md](docs/architecture.md) and [docs/main_refactor.md](docs/main_refactor.md).

Key files:
- Firmware entrypoint: [Platformio/Dog-RGB/src/main.cpp](Platformio/Dog-RGB/src/main.cpp)
- Pin mapping: [Platformio/Dog-RGB/include/pins.h](Platformio/Dog-RGB/include/pins.h)
- Runtime defaults: [Platformio/Dog-RGB/include/config.h](Platformio/Dog-RGB/include/config.h)
- Build config: [Platformio/Dog-RGB/platformio.ini](Platformio/Dog-RGB/platformio.ini)

---

## Portal Configuration (Runtime)

The portal exposes runtime config via `/config` and `/api/config` (plus `/api/config/reset`).

- Portal config (runtime): [docs/portal_config.md](docs/portal_config.md)
- Presets (no implementado): [docs/portal_config_presets.md](docs/portal_config_presets.md)

Wi-Fi portal docs:
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- State diagram: [docs/wifi_portal_state_diagram.md](docs/wifi_portal_state_diagram.md)

---

## LED Behavior

- UI spec: [docs/led_ui_spec.md](docs/led_ui_spec.md)
- Effects reference: [docs/led_effects.md](docs/led_effects.md)
- Color reference: [docs/manual_de_colores.md](docs/manual_de_colores.md)

---

## Specs and Product Docs

- Requirements: [docs/requirements.md](docs/requirements.md)
- Architecture: [docs/architecture.md](docs/architecture.md)
- App MVP spec: [docs/app_mvp_spec.md](docs/app_mvp_spec.md)
- BLE spec: [docs/ble_spec.md](docs/ble_spec.md)
- Web portal spec: [docs/web_portal_spec.md](docs/web_portal_spec.md)

---

## Hardware Setup (Phase 1 MVP)

Pins (XIAO ESP32-S3):
- GPS RX: D7 / GPIO44
- GPS TX: D6 / GPIO43
- Status LED: D2 / GPIO3 (external LED)
- LED A data: D0 / GPIO1
- LED B data: D1 / GPIO2

Wiring reference:
- [docs/manual_de_uso.md](docs/manual_de_uso.md)
- [docs/sk6812_wiring.md](docs/sk6812_wiring.md)

---

## Repo Structure

- `Datasheets/` component datasheets
- `docs/` specs, architecture, decisions, roadmap
- `firmware/` firmware notes and references
- `hardware/` hardware notes
- `Platformio/` active PlatformIO firmware project
- `software/` app/BLE tooling (future)

---

## Next Steps

- Validate the power budget with real component efficiencies
- Finalize BOM and sourcing list
- Draft enclosure and cable routing
- Add IMU motion classification (Phase 2)
