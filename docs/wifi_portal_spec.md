# Portal Wi-Fi (AP + STA) - Especificacion (Fase 1)

Este documento describe el portal web local del collar y su comportamiento en AP/STA.

---

## Objetivo

- Mostrar 3 metricas basicas del collar:
  - Distancia recorrida (hoy)
  - Velocidad promedio (hoy)
  - Velocidad maxima (hoy)
- Configuracion minima de conectividad.

---

## Modo 1: AP (Wi-Fi Direct)

### Flujo de usuario
1) El collar crea una red Wi-Fi local.
2) Usuario se conecta desde el telefono.
3) Abre `http://192.168.4.1`.
4) Ve la pagina con las metricas y boton "Actualizar".

### Defaults (runtime)
- SSID: `DogRGB`
- Password: `Dog12345`
- IP portal: `http://192.168.4.1/`
- Canal AP por defecto: `AP_CHANNEL` (canal 1 mientras STA no esta conectado)
- Clientes AP maximos: `AP_MAX_CLIENTS` (2)
- AP abierto: opcional via `/config` (sin password)

Los valores por defecto vienen de `config.h`, pero se pueden cambiar en runtime desde `/config`.

### Portal cautivo

El firmware levanta `DNSServer` mientras el AP esta activo y responde DNS wildcard hacia `WiFi.softAPIP()`.
Tambien redirige rutas desconocidas hacia `/` y atiende endpoints comunes de deteccion de portal cautivo
(`/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt`).

Esto no garantiza que todos los sistemas abran el portal automaticamente, pero mejora el flujo en telefonos
que detectan redes Wi-Fi sin Internet. El fallback manual sigue siendo `http://192.168.4.1/`.

---

## Modo 2: STA (Wi-Fi normal)

### Flujo de usuario (setup inicial)
1) Usuario entra al AP del collar.
2) Pagina de configuracion solicita SSID y password.
3) Collar guarda credenciales y se conecta al router.
4) Portal accesible por mDNS (ej: `http://dog-collar.local`).

Mientras conecta, el firmware usa modo `AP+STA`. El AP puede apagarse despues por politica de energia.

### Fallback
- Si STA no conecta en `STA_CONNECT_TIMEOUT_MS`, vuelve a AP.

---

## UI (Portal Web)

### Pagina principal
- Titulo: "Dog Collar"
- Estado: GPS OK / Sin GPS / Sin datos
- Cards:
  - Distancia (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Boton: "Actualizar"
- Footer: "Ultima lectura: HH:MM"

### Pagina de configuracion Wi-Fi
- Ruta: `/wifi`
- Campo SSID
- Campo Password
- Boton "Guardar y conectar"

---

## Endpoints

- `GET /` pagina principal
- `GET /api/summary` JSON con metricas
- `GET /wifi` pagina de setup Wi-Fi
- `POST /api/wifi` guardar SSID/password (form-data, respuesta texto "saved, connecting")
- `GET /config` UI de configuracion runtime
- `GET /api/config` leer config runtime (JSON)
- `POST /api/config` guardar config runtime (JSON)
- `POST /api/config/reset` restaurar defaults

---

## Datos (JSON ejemplo)

```
{
  "date": 20260202,
  "distance_m": 12400,
  "avg_speed_cmps": 480,
  "max_speed_cmps": 1820,
  "last_update_min": 1115,
  "gps_fix": true,
  "has_data": true
}
```

---

## Politica AP/Wi-Fi (auto)

- Sin GPS fix: AP forzado ON.
- Con GPS OK: si velocidad <= `AP_STATIONARY_ON_KPH` por `AP_STATIONARY_MS`, AP ON.
- Cada arranque/reinicio del AP mantiene el AP visible al menos `AP_SETUP_HOLD_MS`.
- La actividad HTTP del portal extiende el hold por `AP_PORTAL_ACTIVITY_HOLD_MS`.
- AP ON sin clientes, sin hold activo y sin actividad por `AP_IDLE_TIMEOUT_MS`: AP OFF.
- Si AP OFF y no hay STA conectado, Wi-Fi OFF para ahorrar bateria.
- Si Wi-Fi OFF y se cumple "sin GPS" o "estacionario", se reactiva AP.
- Si STA falla, los reintentos usan backoff hasta `STA_RETRY_BACKOFF_MAX_MS` y se posponen mientras hay clientes AP.

## Diagnostico

`GET /api/dev` expone diagnostico Wi-Fi/AP:

- Resultado del ultimo `WiFi.softAP()`.
- Contadores de AP start/stop/restart/fail.
- Contadores de eventos AP station connect/disconnect y STA got IP/disconnect.
- Timestamps de ultimo evento, proximo retry STA y hold AP.
- Canal AP, MAC AP/STA y estado del DNS cautivo.

---

## Persistencia

- Credenciales STA se guardan en NVS (`wifi_ssid`, `wifi_pass`, namespace `dogrgb`).
- Resumen diario y metricas tambien se guardan en NVS (`dogrgb`).

---

## Seguridad basica

- AP con password configurable (opcionalmente abierto).
- `GET /api/config` no expone el password del AP.
- Limitar endpoints a LAN.
