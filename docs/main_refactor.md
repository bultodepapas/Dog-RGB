Refactor de `main.cpp` (estado actual)

Resumen
- El refactor ya esta implementado con la **Opcion B**: cada modulo mantiene su estado interno `static`.
- `Platformio/Dog-RGB/src/main.cpp` ahora solo orquesta `setup()` y `loop()`.
- La funcionalidad se separo en modulos por dominio (GPS, Wi-Fi, BLE, LED, geofence, config, storage, web).

Estructura real del firmware
```
Platformio/Dog-RGB/
  include/
    ble/summary_ble.h
    config/runtime_config.h
    geofence/home.h
    gps/gps.h
    led/led_ui.h
    storage/nvs_store.h
    util/geo.h
    web/pages.h
    web/portal_http.h
    wifi/wifi_mgr.h
    config.h
    pins.h
  src/
    main.cpp
    ble/summary_ble.cpp
    config/runtime_config.cpp
    geofence/home.cpp
    gps/gps.cpp
    led/led_ui.cpp
    storage/nvs_store.cpp
    util/geo.cpp
    web/pages.cpp
    web/portal_http.cpp
    wifi/wifi_mgr.cpp
```

Mapa de responsabilidades (implementado)
- `gps/gps.*`: parsing NMEA (RMC + GGA), métricas diarias, sesiones, historial, JSON summary y payload BLE.
- `geofence/home.*`: home persistente, auto-home, distancia a home, histéresis.
- `wifi/wifi_mgr.*`: AP/STA policy, reintentos, credenciales STA.
- `web/pages.*`: HTML de `/`, `/wifi`, `/config`.
- `web/portal_http.*`: handlers `/api/*`, validaciones de config y endpoints de home.
- `ble/summary_ble.*`: BLE read-only summary.
- `led/led_ui.*`: efectos, estados, status LEDs y rendering por modo.
- `config/runtime_config.*`: defaults, validacion, load/save, apply (brillo + mDNS).
- `storage/nvs_store.*`: apertura de namespaces NVS.
- `util/geo.*`: Haversine.

Decisiones clave
- Sin `AppState` global. Los modulos se acoplan solo via getters/setters.
- Los “tipos de dominio” (sesiones, rangos, etc.) viven en el modulo que los usa.
- `main.cpp` no contiene logica de negocio, solo orquestacion y logging.

Orden real de ejecucion
Setup (orden real):
- `storage::begin()`
- `config::load()`
- `gps::begin()`
- `geofence::begin()`
- `led_ui::begin()`
- `led_ui::start_welcome()` (si `LED_UI_ENABLED`)
- `wifi_mgr::begin()`
- `portal_http::begin()`
- `summary_ble::begin()`

Loop (orden real):
- `gps::tick()`
- `geofence::tick(now_ms)`
- `gps::save_if_due(now_ms)`
- heartbeat + logs
- `summary_ble::tick()`
- `wifi_mgr::tick(now_ms)`
- `led_ui::tick()`
- `portal_http::handle_client()`

Pendientes opcionales (si quieres mas limpieza)
- Separar utilidades de LED a `util/` (hsv, pulsos, clamp, etc.).
- Dividir `gps/gps.cpp` en `nmea.*` y `metrics/session.*`.
- Actualizar ArduinoJson a `JsonDocument` para eliminar warnings de deprecacion.
