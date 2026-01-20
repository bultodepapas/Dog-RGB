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

Recommended LEDs:

- APA102 / SK9822 (5V, clocked)
- Density: 144 LEDs/m
- Length: 45 cm
- Total LEDs: ~65

Why APA102/SK9822:

- Smooth animations at low brightness
- Better stability than WS2812
- High-quality color control

Note:

- Requires regulated 5V (do not power directly from battery)

---

## Power System

Battery:

- 21700 Li-ion
- ~5000 mAh
- 18.5 Wh nominal energy

LED power budget (65 LEDs, average current per LED):

- 5 mA -> ~1.6 W -> ~10 hours
- 10 mA -> ~3.25 W -> ~5 hours
- 20 mA -> ~6.5 W -> ~2.5 hours

Design goal: 5-10 mA per LED average

---

## Power Architecture

```
21700 Battery (3.0-4.2V)
   -> BMS 1S + USB-C charger
   -> 5V boost converter (>=2A continuous)
   -> APA102 LED strip
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

- Color mapping:
  - Resting -> Blue
  - Walking -> Purple
  - Running -> Red
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
- GPS UART: 9600 baud
- Pins (XIAO ESP32-S3):
  - GPS RX: D6 / GPIO7
  - GPS TX: D7 / GPIO8
  - Status LED: D2 / GPIO3 (external LED)

---

## Firmware Status (Phase 1)

- NMEA RMC parsing (lat/lon/speed/date/time)
- Distance calculation (Haversine) with spike filtering
- Active time tracking and speed thresholds
- Daily reset using GPS date
- Max/avg speed metrics
- NVS persistence (periodic save)
- Serial heartbeat logs

Base project: `firmware/esp32s3_base/`

---

## Documentation Index

- Phase 0 freeze: `docs/phase0_freeze.md`
- Tasks backlog: `docs/tasks.md`
- GPS -> calculation -> BLE flow: `docs/flow_wireframe.md`
- Web portal spec: `docs/web_portal_spec.md`

---

## Next Steps

- Build a detailed BOM and sourcing list
- Create power budget with real component efficiencies
- Draft schematic for power + LEDs + IMU
- Define IMU thresholds for activity levels
- Sketch enclosure and cable routing
