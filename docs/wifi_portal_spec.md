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

### Defaults
- SSID: `dog`
- Password: `Dog123456789`
- AP abierto: opcional via `/config` (sin password)

---

## Modo 2: STA (Wi-Fi normal)

### Flujo de usuario (setup inicial)
1) Usuario entra al AP del collar.
2) Pagina de configuracion solicita SSID y password.
3) Collar guarda credenciales y se conecta al router.
4) Portal accesible por mDNS (ej: `http://dog-collar.local`).

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
- `POST /api/wifi` guardar SSID/password
- `GET /config` UI de configuracion runtime
- `GET /api/config` leer config runtime
- `POST /api/config` guardar config runtime
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
- AP ON sin clientes por `AP_IDLE_TIMEOUT_MS`: AP OFF.
- Si AP OFF y no hay STA conectado, Wi-Fi OFF para ahorrar bateria.
- Si Wi-Fi OFF y se cumple "sin GPS" o "estacionario", se reactiva AP.

---

## Seguridad basica

- AP con password configurable (opcionalmente abierto).
- No exponer credenciales en el frontend.
- Limitar endpoints a LAN.
