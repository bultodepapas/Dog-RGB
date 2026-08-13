# Plan — Pagina de Desarrolladores (/dev) con diagnosticos

> **Document status:** Historical implementation plan (Spanish). `/dev` and `/api/dev` now exist in an evolved form; use the [HTTP API reference](../api-reference.md) for the current surface.

Fecha: 2026-02-03

## 1) Objetivo
Crear una pagina /dev para diagnostico avanzado con datos en vivo del collar DOG-RGB (GPS, Wi-Fi, LED, geofence, config, sistema), sin afectar rendimiento ni UI principal.

## 2) Alcance
Incluye:
- Nueva pagina web `GET /dev`.
- Nuevo endpoint JSON `GET /api/dev` (o `/api/dev/status`) con datos agregados.
- Reutilizar CSS base y evitar assets pesados.

No incluye:
- Cambios en logica LED o GPS.
- Persistencia adicional.

## 3) Fuentes de datos (segun codigo actual)
GPS (gps::):
- `has_fix()`, `has_current_fix()`, `sats()`, `fix_quality()`.
- `last_speed_kph()`, `current_lat_deg()`, `current_lon_deg()`.
- `current_date()`, `last_update_min()`.
- Telemetria cruda: `bytes_rx()`, `sentences_rx()`, `rmc_seen()`, `rmc_valid()`, `gga_seen()`, `overflow()`.
- Tiempos: `last_byte_ms()`, `last_rmc_ms()`, `last_gga_ms()`, `last_fix_ms()`.

Wi-Fi (wifi_mgr:: + WiFi):
- `sta_connected()`, `sta_connecting()`, `ap_enabled()`, `ap_station_count()`, `wifi_off()`, `is_ap_mode()`.
- SSID y mDNS desde `config::get()`.
- IPs: `WiFi.localIP()`, `WiFi.softAPIP()`.
- RSSI: `WiFi.RSSI()` (si STA conectada).

Geofence (geofence::):
- `is_set()`, `source_name(source())`, `home_lat()`, `home_lon()`.
- `distance_to_home_m()`.

LED / Effects (led_ui:: + config::):
- `config::get().mode`, `config::get().brightness`.
- Rangos y efectos via `config::get().ranges` y `config::get().effects`.
- `led_ui::speed_range(kph)`, `led_ui::get_range_config(...)`, `led_ui::effect_name(id)`.
- Base color por rango: `led_ui::base_color_for_range(range)`.
- Simple mode: `config::get().single`.
- Show mode: requiere exponer `show_effect_id` via nuevo getter en `led_ui`.

Sistema:
- `millis()` para uptime.
- `ESP.getFreeHeap()` para heap libre.
- `ESP.getFlashChipSize()` opcional.
- `__DATE__` y `__TIME__` para build info.

## 4) Nuevo endpoint JSON (propuesta)
Ruta sugerida: `GET /api/dev`.

Estructura sugerida:
```
{
  "time": { "uptime_ms": 1234567, "build": "2026-02-03 14:21" },
  "wifi": {
    "mode": "AP|STA|AP+STA|OFF",
    "sta_connected": true,
    "sta_connecting": false,
    "ap_enabled": true,
    "ap_stations": 1,
    "ap_ssid": "DogRGB",
    "mdns": "dog-collar",
    "sta_ip": "192.168.1.10",
    "ap_ip": "192.168.4.1",
    "rssi": -55
  },
  "gps": {
    "fix": true,
    "current_fix": true,
    "sats": 10,
    "fix_quality": 1,
    "speed_kph": 3.2,
    "lat": -34.123456,
    "lon": -58.123456,
    "date": 20260203,
    "last_update_min": 845,
    "bytes_rx": 123456,
    "sentences_rx": 1240,
    "rmc_seen": 620,
    "rmc_valid": 600,
    "gga_seen": 620,
    "overflow": 0,
    "age_last_byte_ms": 120,
    "age_last_fix_ms": 850
  },
  "geofence": {
    "set": true,
    "source": "auto",
    "home_lat": -34.0,
    "home_lon": -58.0,
    "distance_m": 42.1,
    "range": 2
  },
  "led": {
    "mode": "speed",
    "brightness": 77,
    "range": 2,
    "effect_a": { "id": 7, "name": "JUGGLE", "speed": 58, "intensity": 95 },
    "effect_b": { "id": 7, "name": "JUGGLE", "speed": 58, "intensity": 95 },
    "base_rgb": { "r": 0, "g": 60, "b": 60 },
    "simple": { "effect": 2, "speed": 60, "intensity": 90, "rgb": { "r": 0, "g": 60, "b": 60 } },
    "show_effect": { "id": 4, "name": "COMET" }
  }
}
```

Notas:
- Calcular `age_last_*` con `millis() - last_*_ms`.
- Calcular `mode` de Wi-Fi leyendo `WiFi.getMode()`.
- `range` depende del modo: speed usa `led_ui::speed_range(gps::last_speed_kph())`, geofence usa `geofence::geofence_range(distance)` y `apply_hysteresis` si aplica.

## 5) Exponer estado LED actual (nuevo getter)
Agregar en `led_ui.h/.cpp`:
- `uint8_t current_show_effect();` para modo SHOW.
- Opcional: `uint8_t last_range();` si se quiere exponer el rango elegido por la UI.

Justificacion: el efecto real en SHOW rota por tiempo y hoy es interno.

## 6) Pagina /dev (UI)
Secciones:
- System: uptime, build, heap libre.
- Wi-Fi: modo, STA/AP, SSID, mDNS, IPs, RSSI, stations.
- GPS: fix, sats, fix_quality, speed, lat/lon, ages, counters NMEA.
- LED: modo, rango actual, efecto A/B, speed/intensity, color base, simple/show info.
- Geofence: home set, source, distancia, rango.
- Raw JSON: boton para copiar respuesta completa.

UX:
- Boton "Actualizar" y toggle "Auto" (5s).
- Mostrar timestamp de ultima lectura.
- Todo local, sin assets externos.

## 7) Cambios de codigo (resumen)
1) `portal_http.cpp`
- Nuevo handler `handle_dev_page()` y `handle_dev_get()`.
- Registrar rutas `/dev` y `/api/dev`.

2) `pages.cpp`
- `html_dev_page()` con UI simple y JS que consume `/api/dev`.
- Reusar `BASE_CSS`.

3) `led_ui.h/.cpp`
- Exponer `current_show_effect()` para modo SHOW.

## 8) Criterios de aceptacion
- Pagina /dev muestra datos en <2s en AP.
- JSON incluye minimo GPS sats, fix_quality, wifi state, modo LED y efecto actual.
- No rompe pagina principal ni /config.
- Payload razonable (< 4 KB).

## 9) Secuencia recomendada
1) Agregar endpoint `/api/dev` con JSON minimo.
2) Implementar pagina /dev y render basico.
3) Agregar campos avanzados (NMEA counters, heap).
4) Validar en movil y revisar carga.
