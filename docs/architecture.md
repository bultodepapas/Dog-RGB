# Architecture

## System Blocks

- Power: 21700 cell -> BMS/charger -> 5V boost -> LEDs -> 3.3V regulator -> MCU/GNSS
- Control: ESP32-S3 (XIAO) manages GNSS parsing, LEDs, Wi-Fi portal, BLE summary
- GNSS: EBYTE E108-GN02 on UART (RMC + GGA for fix and satellites)
- LED UI: SK6812 RGBW strips driven by Adafruit NeoPixel with runtime effects
- Connectivity: Wi-Fi AP/STA portal + mDNS in STA, BLE read-only summary
- Storage: NVS for daily metrics and runtime config

## Data Flow

1) GNSS -> NMEA parsing -> metrics (distance, avg/max speed, active time)
2) Metrics -> LED UI (speed ranges) + portal JSON (`/api/summary`) + BLE payload
3) Runtime config -> NVS -> live apply (brightness, ranges, effects, AP settings)

## Notes

- AP policy is GPS-aware: AP stays on with no fix, auto-on when stationary, auto-off after idle.
- Homogeneous LED mode is enabled after GPS fix is stable and Wi-Fi is off.
- LED brightness defaults to ~30% for thermal and battery safety.
