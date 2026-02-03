Propuesta de modularizacion de main.cpp (Dog-RGB)

Resumen
- El `main.cpp` actual concentra GPS/NMEA, metricas diarias y de sesion, BLE, Wi-Fi/AP policy, HTTP + HTML, geofence, UI de LEDs y utilidades matematicas.
- Es posible dividirlo en modulos sin cambiar el comportamiento, manteniendo `setup()` y `loop()` en `src/main.cpp`.
- PlatformIO compila automaticamente multiples `.cpp` en `src/` y headers en `include/`. Otra opcion es mover modulos a `lib/` si quieres tratarlos como librerias internas.

Objetivo
- Bajar el acoplamiento y el tamaño de archivo.
- Encapsular estado por modulo y hacer mas legible el flujo.
- Facilitar futuras mejoras sin romper el firmware.

Propuesta de estructura (orientativa)
```
Platformio/Dog-RGB/
  src/
    main.cpp
    app/
      app.h
      app.cpp
    gps/
      nmea.h
      nmea.cpp
      gnss.h
      gnss.cpp
    metrics/
      metrics.h
      metrics.cpp
      session.h
      session.cpp
    storage/
      nvs_store.h
      nvs_store.cpp
    geofence/
      home.h
      home.cpp
    config/
      runtime_config.h
      runtime_config.cpp
    wifi/
      wifi_mgr.h
      wifi_mgr.cpp
      portal_http.h
      portal_http.cpp
    web/
      pages.h
      pages.cpp
    ble/
      summary_ble.h
      summary_ble.cpp
    led/
      led_ui.h
      led_ui.cpp
      effects.h
      effects.cpp
      color.h
      color.cpp
    util/
      math_u8.h
      math_u8.cpp
      time_wave.h
      time_wave.cpp
```

Mapa de responsabilidades (por bloques actuales)
- GPS/NMEA:
  - `parse_rmc`, `parse_gga`, `nmea_to_decimal_degrees` -> `gps/nmea.*`
  - `read_gps`, `handle_nmea_line` y contadores de UART -> `gps/gnss.*`
- Metricas diarias y sesion:
  - `total_distance_m`, `active_time_ms`, `max_speed_kph`, `last_update_min`
  - `SessionSummary`, historial y calculos de promedio -> `metrics/*`
- Persistencia NVS:
  - `Preferences prefs`, `save_metrics`, `load_metrics`, `save_wifi_creds`, `load_wifi_creds`
  - `load_config`, `save_config`, `load_home`, `save_home` -> `storage/*` y `config/*`
- Geofence:
  - `home_*`, `distance_to_home_m`, `geofence_range`, `maybe_auto_set_home` -> `geofence/*`
- Configuracion runtime:
  - `RuntimeConfig`, `set_default_config`, `apply_config`, validaciones -> `config/*`
- Wi-Fi/AP policy:
  - `start_ap_mode`, `start_sta_mode`, `enable_ap`, `disable_ap`, `set_wifi_off`, `update_ap_policy`, `setup_wifi` -> `wifi/wifi_mgr.*`
- HTTP y portal:
  - Handlers `handle_*`, `setup_http` -> `wifi/portal_http.*`
  - HTML `html_page`, `html_wifi_page`, `html_config_page` -> `web/pages.*`
- BLE:
  - `setup_ble`, `build_summary_payload` -> `ble/summary_ble.*`
- LED UI + efectos:
  - `led_begin`, `update_led_ui`, efectos, paletas, estados -> `led/*`
- Utilidades:
  - `clamp_u8`, `scale8`, `hsv_to_rgb`, `beat8`, `beat16`, `pulse_scale`, `double_pulse_scale`, `qadd8`, `qsub8`, `random8` -> `util/*`

Recomendacion clave de diseño
- Mantener `setup()` y `loop()` en `src/main.cpp` como orquestadores.
- Evitar depender de demasiadas variables globales expuestas. Alternativa pragmatica:
  - Crear un `struct AppState` en `app/app.h` con todo el estado compartido.
  - Cada modulo recibe un puntero o referencia a `AppState` en su `begin()` y `tick()`.
- Otra opcion minima: que cada modulo tenga su propio estado `static` interno, y exponga funciones `begin()`/`tick()` sin exponer globals.

Ejemplo de orquestacion (idea, no cambio)
- `app.begin()` llama a `gps::begin`, `led::begin`, `wifi::begin`, `web::begin`, `ble::begin`.
- `app.tick(now_ms)` llama a `gps::tick`, `metrics::tick`, `wifi::tick`, `led::tick`, `server.handleClient()`.

Orden sugerido de separacion (bajo riesgo)
1. Mover utilidades puras a `util/*` (no dependen de estado).
2. Mover conversiones y parseo NMEA a `gps/nmea.*`.
3. Separar HTML en `web/pages.*` (son funciones puras).
4. Extraer Wi-Fi/AP policy a `wifi/wifi_mgr.*` y dejar `setup_http` en `wifi/portal_http.*`.
5. Mover BLE a `ble/summary_ble.*`.
6. Mover LED UI y efectos a `led/*`.
7. Encapsular geofence y home.
8. Consolidar metricas y persistencia con `metrics/*` y `storage/*`.

Notas practicas para Arduino/PlatformIO
- No hay problema con multiples `.cpp` en `src/`; `setup()` y `loop()` siguen en `main.cpp`.
- Los `static` globales que quedan en `main.cpp` no son visibles fuera. Si un modulo los necesita, moverlos al modulo o pasarlos como parametros.
- Los headers deben incluir sus dependencias (`Arduino.h`, `Preferences.h`, `WiFi.h`, etc.) para evitar errores de compilacion por include order.
- Evitar `String` en utilidades que no lo requieren, pero no es necesario cambiar ahora.

Resultado esperado
- Un `main.cpp` corto que solo coordina modulos.
- Cambios aislados por dominio (GPS, Wi-Fi, BLE, LED) y mas facil de mantener.
