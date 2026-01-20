# Smart LED Dog Collar

## Overview

Smart illuminated collar for a 14 kg Siberian Husky using high-density addressable LEDs, motion-based behavior, and a single 21700 Li-ion cell. Designed for visibility, safety, and future expandability (GPS, BLE app, advanced modes).

## Key Goals

- High visibility at night with premium LED animation quality.
- Motion- and speed-reactive color and brightness.
- Good battery life from a single 21700 cell.
- Safe, comfortable, and weather-resistant mechanical design.
- Expandable architecture for GPS and BLE configuration.

## Core Specs (Initial Target)

- Collar length: 45 cm
- LED strip: APA102 / SK9822, 144 LEDs/m
- LED count: ~65
- MCU: ESP32-S3
- IMU: BMI270 or ICM-42688 (MPU6050 acceptable)
- Battery: 21700 Li-ion, ~5000 mAh
- Power: 5V boost converter (>=2A continuous)

## Power Architecture

```
21700 Battery (3.0-4.2V)
   -> BMS 1S + USB-C charger
   -> 5V boost converter
   -> APA102 LED strip
   -> 3.3V regulator -> ESP32-S3 + sensors
```

## Behavior Logic (High Level)

- Color transitions from blue to red as activity increases.
- Brightness scales with movement intensity.
- Effects: breathing (idle), pulsing (medium), chase/strobe (high).

## Project Phases

1) MVP: LED + IMU + ESP32-S3 + battery + boost + button
2) Advanced: GPS, BLE app, profiles, extended patterns

## Repository Structure

- `docs/` product specs, architecture, decisions, roadmap
- `hardware/` schematics, PCB, power design notes
- `firmware/` embedded firmware source (later)
- `software/` app/BLE tooling (later)
- `assets/` diagrams, renders, images
- `research/` datasheets, references, calculations
- `tests/` test plans and validation notes
- `tools/` scripts and utilities (later)

## Next Steps

- Collect parts list and sourcing options
- Draft electrical schematic and power budget
- Define IMU thresholds and LED profiles
- Sketch enclosure and strain relief
