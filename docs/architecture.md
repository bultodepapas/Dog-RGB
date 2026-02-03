# Architecture

## System Blocks

- Power: 21700 cell -> BMS/charger -> 5V boost -> LEDs -> 3.3V regulator -> MCU/GNSS
- Control: ESP32-S3 (XIAO) manages GNSS parsing, LEDs, Wi-Fi portal, BLE summary
- GNSS: EBYTE E108-GN02 on UART (RMC + GGA for fix and satellites)
- LED UI: SK6812 RGBW strips driven by Adafruit NeoPixel with runtime effects
- Connectivity: Wi-Fi AP/STA portal + mDNS in STA, BLE read-only summary
- Storage: NVS for daily metrics, sessions, Wi-Fi creds, home geofence, and runtime config

## Firmware Layout (PlatformIO)

| Module | Files | Responsibilities |
| --- | --- | --- |
| Entry | `Platformio/Dog-RGB/src/main.cpp` | Orchestrates setup/loop, heartbeat LED, periodic logs |
| GPS + Metrics + Sessions | `Platformio/Dog-RGB/include/gps/gps.h`, `Platformio/Dog-RGB/src/gps/gps.cpp` | NMEA parsing, daily metrics, session history, BLE/JSON summaries |
| Geofence/Home | `Platformio/Dog-RGB/include/geofence/home.h`, `Platformio/Dog-RGB/src/geofence/home.cpp` | Home persistence, auto-home, distance, hysteresis |
| Wi-Fi/AP Policy | `Platformio/Dog-RGB/include/wifi/wifi_mgr.h`, `Platformio/Dog-RGB/src/wifi/wifi_mgr.cpp` | AP/STA mode, retry policy, AP idle shutdown, Wi-Fi creds |
| Web Portal (HTTP) | `Platformio/Dog-RGB/include/web/portal_http.h`, `Platformio/Dog-RGB/src/web/portal_http.cpp` | API handlers, portal routes, config updates |
| Web Pages (HTML) | `Platformio/Dog-RGB/include/web/pages.h`, `Platformio/Dog-RGB/src/web/pages.cpp` | HTML templates for `/`, `/wifi`, `/config` |
| BLE Summary | `Platformio/Dog-RGB/include/ble/summary_ble.h`, `Platformio/Dog-RGB/src/ble/summary_ble.cpp` | BLE service and summary characteristic updates |
| LED UI | `Platformio/Dog-RGB/include/led/led_ui.h`, `Platformio/Dog-RGB/src/led/led_ui.cpp` | Effects, status LEDs, mode rendering, welcome animation |
| Runtime Config | `Platformio/Dog-RGB/include/config/runtime_config.h`, `Platformio/Dog-RGB/src/config/runtime_config.cpp` | Defaults, validation, load/save, apply brightness + mDNS |
| NVS Storage | `Platformio/Dog-RGB/include/storage/nvs_store.h`, `Platformio/Dog-RGB/src/storage/nvs_store.cpp` | Opens namespaces and exposes `Preferences` instances |
| Geo Utils | `Platformio/Dog-RGB/include/util/geo.h`, `Platformio/Dog-RGB/src/util/geo.cpp` | Haversine distance |
| Hardware Config | `Platformio/Dog-RGB/include/config.h`, `Platformio/Dog-RGB/include/pins.h` | Constants and pin mapping |

## State Strategy

- Opcion B: cada modulo mantiene su estado interno `static`.
- El estado compartido se expone mediante getters/setters para evitar dependencias directas.
- No hay `AppState` global; el acoplamiento ocurre via APIs del modulo.

## Runtime Flow

Setup (orden real en `Platformio/Dog-RGB/src/main.cpp`):
- `storage::begin()` abre NVS (`dogrgb` y `dogrgb_cfg`).
- `config::load()` carga configuracion (y aplica defaults si hace falta).
- `gps::begin()` configura UART GNSS y restaura métricas/historial.
- `geofence::begin()` restaura home geofence.
- `led_ui::begin()` + `led_ui::start_welcome()` si `LED_UI_ENABLED`.
- `wifi_mgr::begin()` inicia AP/STA segun credenciales.
- `portal_http::begin()` registra rutas HTTP.
- `summary_ble::begin()` inicia BLE.

Loop (orden real):
- `gps::tick()` lee UART y actualiza métricas.
- `geofence::tick(now_ms)` actualiza estabilidad de fix y auto-home.
- `gps::save_if_due(now_ms)` persiste métricas/sesión cada `SAVE_INTERVAL_MS`.
- Heartbeat LED + logs seriales cada `HEARTBEAT_MS`/`LOG_MS`.
- `summary_ble::tick()` refresca payload BLE.
- `wifi_mgr::tick(now_ms)` mantiene política AP/STA y reintentos.
- `led_ui::tick()` dibuja UI de LEDs.
- `portal_http::handle_client()` atiende HTTP.

## Data Flow

1) GNSS -> NMEA parsing -> métricas (distancia, velocidad, tiempo activo)
2) Métricas -> JSON `/api/summary` + BLE summary + LED UI (rangos de velocidad)
3) Config runtime -> NVS -> apply (brillo LED, ranges/efectos, AP settings, mDNS)
4) Geofence usa GPS para distancia + rangos con histéresis

## NVS Namespaces and Keys

Namespace `dogrgb` (runtime + sesiones):
- `date` (uint32) fecha YYYYMMDD
- `dist_m` (float) distancia acumulada del día
- `active_ms` (uint32) tiempo activo
- `max_kph` (float) velocidad máxima
- `upd_min` (uint16) última lectura (minutos del día)
- `h0`/`h1`/`h2` (bytes) historial `SessionSummary`
- `h_cnt` (uint8) cantidad de sesiones
- `h_idx` (uint8) índice circular
- `h_ver` (uint8) versión de sesión
- `s_cur` (bytes) snapshot sesión actual
- `s_open` (uint8) bandera sesión en curso
- `wifi_ssid` (String) SSID STA
- `wifi_pass` (String) password STA

Namespace `dogrgb_cfg` (config + home):
- `ver` (uint8) versión de config
- `brightness` (uint8) brillo LED
- `ranges` (float[9]) límites de velocidad
- `effects` (RangeEffect[10]) mapping rango -> efecto
- `single_eff`, `single_speed`, `single_intensity`, `single_r`, `single_g`, `single_b`
- `ap_ssid` (String), `ap_pass` (String), `mdns` (String)
- `mode` (uint8) modo UI
- `fence_max` (uint16) distancia máxima geofence
- `home_set` (uint8), `home_lat` (float), `home_lon` (float), `home_src` (uint8)

## Key Public APIs

- `gps::begin()`, `gps::tick()`, `gps::save_if_due(now_ms)`
- `geofence::begin()`, `geofence::tick(now_ms)`
- `wifi_mgr::begin()`, `wifi_mgr::tick(now_ms)`
- `portal_http::begin()`, `portal_http::handle_client()`
- `summary_ble::begin()`, `summary_ble::tick()`
- `led_ui::begin()`, `led_ui::start_welcome()`, `led_ui::tick()`
- `config::load()`, `config::save()`, `config::apply(previous)`

## Notes

- AP policy is GPS-aware: AP stays on with no fix, auto-on when stationary, auto-off after idle.
- Homogeneous LED mode is enabled after GPS fix is stable and Wi-Fi is off.
- LED brightness defaults to ~30% for thermal and battery safety.
