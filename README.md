# Smart LED Dog Collar

[English](README.en.md) | [Espanol](README.es.md) | [User Manual](docs/manual_de_uso.md) | [Build Manual](docs/manual_de_construccion.md)

Smart, high-visibility LED collar for medium-to-large dogs. Built for safety, comfort, and future expansion (GPS, BLE, advanced modes).

---

## Quick Links

- User guide: [docs/manual_de_uso.md](docs/manual_de_uso.md)
- Build guide: [docs/manual_de_construccion.md](docs/manual_de_construccion.md)
- Architecture: [docs/architecture.md](docs/architecture.md)
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
- LEDs: SK6812 (5V, single-wire), dual strips
- Power: 21700 Li-ion + BMS + 5V boost (>=3A)
- Portal: AP + STA with local dashboard and config UI

More details:
- Hardware freeze: [docs/phase0_freeze.md](docs/phase0_freeze.md)
- Wiring: [docs/sk6812_wiring.md](docs/sk6812_wiring.md)
- Power budget: [docs/bom_power_budget.md](docs/bom_power_budget.md)

---

## Firmware (Current Status)

The base firmware is in [firmware/esp32s3_base](firmware/esp32s3_base) with:

- NMEA RMC parsing (lat/lon/speed/date/time)
- Distance calculation (Haversine) with spike filtering
- Active time tracking and speed thresholds
- Daily reset using GPS date
- Max/avg speed metrics
- NVS persistence (periodic save)
- Runtime config editable in the portal at `/config`

Key files:
- Firmware entrypoint: [firmware/esp32s3_base/src/main.cpp](firmware/esp32s3_base/src/main.cpp)
- Pin mapping: [firmware/esp32s3_base/include/pins.h](firmware/esp32s3_base/include/pins.h)
- Runtime defaults: [firmware/esp32s3_base/include/config.h](firmware/esp32s3_base/include/config.h)
- Build config: [firmware/esp32s3_base/platformio.ini](firmware/esp32s3_base/platformio.ini)

---

## Portal Configuration (Runtime)

The portal exposes runtime config via `/config` and `/api/config`.

- Plan: [docs/portal_config_plan.md](docs/portal_config_plan.md)
- UI spec: [docs/portal_config_ui_plan.md](docs/portal_config_ui_plan.md)
- Validation flow: [docs/portal_config_validation_flow.md](docs/portal_config_validation_flow.md)
- Apply flow: [docs/portal_config_apply_flow.md](docs/portal_config_apply_flow.md)
- NVS plan: [docs/portal_config_nvs_plan.md](docs/portal_config_nvs_plan.md)
- NVS schema: [docs/portal_config_nvs_schema.md](docs/portal_config_nvs_schema.md)
- Presets: [docs/portal_config_presets.md](docs/portal_config_presets.md)

Wi-Fi portal docs:
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- Wi-Fi plan: [docs/wifi_portal_plan.md](docs/wifi_portal_plan.md)
- State diagram: [docs/wifi_portal_state_diagram.md](docs/wifi_portal_state_diagram.md)

---

## LED Behavior

- UI spec: [docs/led_ui_spec.md](docs/led_ui_spec.md)
- Effects plan: [docs/led_effects_plan.md](docs/led_effects_plan.md)

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
- GPS RX: D6 / GPIO7
- GPS TX: D7 / GPIO8
- Status LED: D2 / GPIO3 (external LED)
- LED A data: GPIO11
- LED B data: GPIO12

Wiring reference:
- [docs/manual_de_uso.md](docs/manual_de_uso.md)
- [docs/sk6812_wiring.md](docs/sk6812_wiring.md)

---

## Repo Structure

- `docs/` specs, architecture, decisions, roadmap
- `hardware/` schematics, PCB, power notes
- `firmware/` embedded firmware source
- `software/` app/BLE tooling (future)
- `assets/` diagrams, renders, images
- `research/` datasheets, references, calculations
- `tests/` validation and test plans
- `tools/` scripts and utilities

---

## Next Steps

- Build a detailed BOM and sourcing list
- Validate the power budget with real component efficiencies
- Draft schematic for power + LEDs + IMU
- Define IMU thresholds for activity levels
- Sketch enclosure and cable routing
