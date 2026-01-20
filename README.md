# Smart LED Dog Collar

Smart, high-visibility LED collar for medium-to-large dogs, designed for safety, comfort, and future expansion (GPS, BLE app, advanced modes).

---

## Team and Intent

Somos dos ingenieros (electrico e industrial). Este proyecto es un experimento que por fin vamos a probar: una idea planeada desde hace mucho tiempo, ahora impulsada por nuestra experiencia y el apoyo de AI agents durante el desarrollo.

---

## Objective

Build a collar that is:

- Highly visible at night with premium LED animation quality.
- Motion- and speed-reactive for color and brightness changes.
- Efficient with a single 21700 Li-ion cell.
- Safe, comfortable, and weather-resistant.
- Ready for GPS and BLE expansion.

---

## Mechanical Design Targets

- Neck circumference: 40-55 cm (medium/large dogs)
- Strap width: 20-25 mm
- LED diffuser: silicone tube over nylon strap
- Total weight target: 120-160 g
- Enclosure: compact, IP65-IP67, strain-relieved

---

## LED System

Selected LEDs (Phase 1):

- SK6812 (5V, single-wire)
- Default count per strip: 20 (min 10, max 50)
- Configurable single or dual strips (independent data pins)

Notes:

- Requires regulated 5V (do not power directly from battery)
- Brightness limited to ~30% for safety and battery life
- SK6812 data may require 5V logic; consider a level shifter and proper decoupling

---

## Power System

Battery:

- 21700 Li-ion
- ~5000 mAh
- 18.5 Wh nominal energy

LED power budget (example, 2 strips x 20 LEDs):

- 3-5 mA per LED (idle UI) -> 120-200 mA total LEDs
- Higher brightness and animations will increase draw

Design goal: keep average LED current low for battery life

---

## Power Architecture

```
21700 Battery (3.0-4.2V)
   -> BMS 1S + USB-C charger
   -> 5V boost converter (>=2A continuous)
   -> SK6812 LED strips
   -> 3.3V regulator -> ESP32-S3 + sensors
```

Additional features:

- Battery voltage monitoring
- Low-battery protection
- Physical switch or Hall sensor

---

## Control System

MCU:

- ESP32-S3 (BLE-ready, LED control, sensor processing)

IMU options:

- BMI270
- ICM-42688
- (MPU6050 acceptable for prototypes)

Selected GPS (Phase 1):

- EBYTE E108-GN02 (10 Hz, BDS/GPS/GLONASS)
- UART 9600 baud

---

## Behavior Logic

Inputs:

- IMU movement intensity
- GPS speed (when enabled)

Outputs:

- Color mapping (Segment B, GPS speed):
  - Low speed -> Blue
  - Medium speed -> Purple
  - High speed -> Red
- Brightness scales with activity
- Effects:
  - Breathing (idle)
  - Pulsing (medium)
  - Chase or strobe (high)

---

## Development Roadmap

Phase 1 - GPS-First MVP (Medium/Large Dogs)

- Battery + charger + boost
- ESP32-S3 + GPS + LED control
- Basic patterns and brightness limits
- Battery monitoring
- Wi-Fi portal (AP + STA)

Phase 2 - Motion-Based Logic

- Add IMU for movement classification
- Merge GPS speed + IMU intensity
- Refine activity profiles

Phase 3 - Heart Rate Integration

- Add heart-rate sensor
- Use HR as additional activity signal
- Calibrate thresholds and safety limits

Phase 4 - Miniaturization (Small Dogs)

- Reduced electronics footprint
- Lower weight battery options
- Smaller LED density and strap width variants

---

## Repository Structure

- `docs/` product specs, architecture, decisions, roadmap
- `hardware/` schematics, PCB, power design notes
- `firmware/` embedded firmware source
- `software/` app/BLE tooling (future)
- `assets/` diagrams, renders, images
- `research/` datasheets, references, calculations
- `tests/` validation and test plans
- `tools/` scripts and utilities

---

## Current Hardware (Phase 1 MVP)

- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz)
- LEDs: SK6812 (single-wire), dual strips
- GPS UART: 9600 baud
- Pins (XIAO ESP32-S3):
  - GPS RX: D6 / GPIO7
  - GPS TX: D7 / GPIO8
  - Status LED: D2 / GPIO3 (external LED)
  - LED A data: GPIO11
  - LED B data: GPIO12

---

## Firmware Status (Phase 1)

- NMEA RMC parsing (lat/lon/speed/date/time)
- Distance calculation (Haversine) with spike filtering
- Active time tracking and speed thresholds
- Daily reset using GPS date
- Max/avg speed metrics
- NVS persistence (periodic save)
- Serial heartbeat logs
- Configurable ranges and strip settings in `firmware/esp32s3_base/include/config.h`
- FastLED effects per speed range (Segment B)

Base project: `firmware/esp32s3_base/`

---

## Documentation Index

- Phase 0 freeze: [`docs/phase0_freeze.md`](docs/phase0_freeze.md)
- Tasks backlog: [`docs/tasks.md`](docs/tasks.md)
- GPS -> calculation -> BLE flow: [`docs/flow_wireframe.md`](docs/flow_wireframe.md)
- Web portal spec: [`docs/web_portal_spec.md`](docs/web_portal_spec.md)
- BLE spec: [`docs/ble_spec.md`](docs/ble_spec.md)
- App MVP spec (future): [`docs/app_mvp_spec.md`](docs/app_mvp_spec.md)
- Wi-Fi portal spec: [`docs/wifi_portal_spec.md`](docs/wifi_portal_spec.md)
- Wi-Fi portal plan: [`docs/wifi_portal_plan.md`](docs/wifi_portal_plan.md)
- Wi-Fi portal state diagram: [`docs/wifi_portal_state_diagram.md`](docs/wifi_portal_state_diagram.md)
- LED UI spec: [`docs/led_ui_spec.md`](docs/led_ui_spec.md)
- SK6812 wiring guide: [`docs/sk6812_wiring.md`](docs/sk6812_wiring.md)
- BOM + power budget: [`docs/bom_power_budget.md`](docs/bom_power_budget.md)
- Config params: [`docs/config_params.md`](docs/config_params.md)
- LED effects plan: [`docs/led_effects_plan.md`](docs/led_effects_plan.md)
- Manual de uso: [`docs/manual_de_uso.md`](docs/manual_de_uso.md)
- Portal config plan: [`docs/portal_config_plan.md`](docs/portal_config_plan.md)
- Portal config NVS plan: [`docs/portal_config_nvs_plan.md`](docs/portal_config_nvs_plan.md)
- Portal config UI plan: [`docs/portal_config_ui_plan.md`](docs/portal_config_ui_plan.md)

---

## Next Steps

- Build a detailed BOM and sourcing list
- Create power budget with real component efficiencies
- Draft schematic for power + LEDs + IMU
- Define IMU thresholds for activity levels
- Sketch enclosure and cable routing
