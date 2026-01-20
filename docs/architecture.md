# Architecture

## System Blocks

- Power: 21700 cell -> BMS/charger -> 5V boost -> LEDs -> 3.3V regulator -> MCU/sensors
- Control: ESP32-S3 manages LEDs, IMU, optional GPS
- Interface: button or Hall sensor, BLE (later)

## Notes

- APA102/SK9822 require stable 5V.
- IMU used for activity classification.
- GPS only powered when activity is detected.
