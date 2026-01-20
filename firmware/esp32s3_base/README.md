# ESP32-S3 Base Firmware

Minimal starting point for ESP32-S3 firmware using Arduino framework (PlatformIO).

## Build

- Install PlatformIO (VS Code or CLI)
- Open this folder in PlatformIO
- Build/Upload for env `esp32s3`

## Notes

- Adjust pin mappings in `include/pins.h` for your board and wiring.
- LED driver uses SK6812 via Adafruit NeoPixel (see `platformio.ini`).
- Configure system parameters in `include/config.h`.
- This firmware provides GPS metrics, Wi-Fi portal, BLE summary, and LED UI.
