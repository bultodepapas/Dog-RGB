# Dog-RGB Firmware

**Active PlatformIO project** for the Seeed Studio XIAO ESP32-S3 collar. Repository-wide onboarding and hardware cautions are in the [root README](../../README.md).

## Implemented subsystems

- RMC/GGA GNSS parsing with runtime quality gates, distance, active time, daily metrics, trusted date rollover, and diagnostics.
- Transactional current session, three completed summaries, completed-day journal, and two-hour route ring.
- JSON/CSV/GeoJSON route streaming that services GNSS around bounded socket writes.
- Two SK6812 RGBW strips with reserved status pixels, 12 effects, four modes, welcome animation, optional Day Mode, and one estimated-current limiter across both buses.
- AP/STA Wi-Fi with captive DNS/probes, network scan, event-queue ownership, bounded retry/backoff, AP hold/idle policy, and mDNS.
- Embedded dashboard, Wi-Fi/configuration/diagnostic pages, CSRF-intent header, output escaping, response headers, and optional write PIN.
- A/B + CRC persistence for config, metrics, sessions, Home, and station credentials; dedicated route NVS partition.
- Read-only BLE summary code, disabled by default for radio coexistence.
- Wokwi production-image simulation with custom controllable NMEA chip and eight scenarios.

## Build

```powershell
pio run -e seeed_xiao_esp32s3
```

Pinned environment:

- pioarduino `55.03.311`;
- Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5;
- ArduinoJson 7.4.3;
- Adafruit NeoPixel 1.15.5.

Flash and monitor:

```powershell
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -e seeed_xiao_esp32s3
```

The complete upload installs `partitions_dog_rgb.csv`. Do not distribute only `firmware.bin` as a first-time upgrade path: the route store needs the dedicated `tracknvs` partition.

## Host tests

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

These tests cover timing/date rules, A/B recovery, routes/streaming, Wi-Fi queue/retry behavior, and simulation assets. See [repository testing guide](../../docs/testing.md).

## Wokwi

```powershell
.\tools\wokwi.ps1 -Action prepare
.\tools\wokwi.ps1 -Action suite -TimeoutMs 90000
```

Copy `.env.example` to ignored `.env` and provide `WOKWI_CLI_TOKEN`. Full controls/scenarios/limitations: [docs/wokwi.md](docs/wokwi.md).

## Portal

Default AP: `DogRGB` / `Dog12345`, portal `http://192.168.4.1/`. Change the password before normal use.

Pages:

- `/` dashboard, sessions, route preview/export;
- `/wifi` scan and AP/STA settings;
- `/config` runtime settings, Home, Day Mode, advanced LED power calibration, optional PIN;
- `/dev` health, LED power-model telemetry, and detailed diagnostics.

API routes and write headers are documented in [HTTP API reference](../../docs/api-reference.md). A custom client must send `X-Dog-Portal` for every POST and `X-Dog-Pin` when the optional lock is enabled.

## Key defaults

- Pins: A GPIO1, B GPIO2, status GPIO3, GNSS TX→ESP RX GPIO44, optional ESP TX GPIO43.
- Two × 24 pixels, two reserved status pixels per strip, brightness 77/255.
- Estimated-current limit enabled at a provisional 1,000 mA whole-device budget (200 mA base, 20 mA per RGB/W channel); calibrate on physical hardware.
- GNSS 9,600 baud, 1-second metric sample, 0.7 km/h active threshold, 40 km/h valid-speed ceiling.
- Day Mode off, 06:00–16:00 fixed UTC-5 when enabled.
- BLE off.

Authoritative values: `include/config.h`, `include/pins.h`, and `platformio.ini`.

## Code map

| Area | Implementation |
| --- | --- |
| Orchestration | `src/main.cpp` |
| GNSS, metrics, sessions, routes | `src/gps/gps.cpp` |
| Config | `src/config/runtime_config.cpp` |
| Wi-Fi | `src/wifi/wifi_mgr.cpp` |
| HTTP and pages | `src/web/portal_http.cpp`, `src/web/pages.cpp` |
| Optional PIN | `src/web/portal_lock.cpp` |
| LEDs / layout / palettes / composition | `src/led/led_ui.cpp`, `src/led/led_layout.cpp`, `src/led/palette_registry.cpp`, `src/led/led_compositor.cpp` |
| LED transport / current limiting / Day Mode | `src/led/led_bus.cpp`, `src/led/power_limiter.cpp`, `src/power/day_mode.cpp` |
| Home/geofence | `src/geofence/home.cpp` |
| BLE | `src/ble/summary_ble.cpp` |
| Storage/utilities | `src/storage/nvs_store.cpp`, `src/util/geo.cpp`, `include/util/*` |

Detailed boot/loop/storage/concurrency design: [Architecture](../../docs/architecture.md).

## License

Dog-RGB's original firmware and accompanying project documentation are available under the repository's [MIT License](../../LICENSE). Third-party libraries and externally authored material retain their own license terms.
