# Portal Wi-Fi - Plan de Implementacion (Fase 1)

Este plan resume el comportamiento actual del portal local (AP + STA).

---

## 1. Objetivo tecnico

- Portal accesible localmente sin backend.
- AP mode por defecto, STA opcional.
- Pagina unica con 3 metricas y estado.

---

## 2. Flujo de arranque

Prioridad:
1) Si existen credenciales guardadas -> iniciar STA (AP+STA)
2) Si no hay credenciales -> iniciar AP
3) Si STA falla en `STA_CONNECT_TIMEOUT_MS` -> fallback a AP

---

## 3. Politica AP/Wi-Fi

- Sin GPS fix: AP forzado ON.
- Con GPS OK: si velocidad baja por `AP_STATIONARY_MS`, AP ON.
- AP sin clientes por `AP_IDLE_TIMEOUT_MS`: AP OFF.
- Si no hay STA conectado, Wi-Fi OFF para ahorro.
- Wi-Fi OFF se reactiva si vuelve a estar estacionario o se pierde GPS.

---

## 4. Endpoints

- `GET /` -> pagina principal
- `GET /api/summary` -> JSON con metricas
- `GET /wifi` -> pagina de configuracion Wi-Fi
- `POST /api/wifi` -> guardar SSID/password
- `GET /config` -> UI de configuracion runtime
- `GET /api/config` -> JSON config runtime
- `POST /api/config` -> guardar config runtime
- `POST /api/config/reset` -> defaults

---

## 5. JSON de resumen

Campos:
- date (yyyymmdd)
- distance_m
- avg_speed_cmps
- max_speed_cmps
- last_update_min
- gps_fix (bool)
- has_data (bool)

---

## 6. UI (pagina principal)

Componentes:
- Titulo: Dog Collar
- Estado: GPS OK / Sin GPS / Sin datos
- Cards: distancia, promedio, maxima
- Boton: Actualizar
- Footer: Ultima lectura HH:MM

---

## 7. Persistencia

- Guardar credenciales Wi-Fi en NVS.
- Guardar ultimo resumen diario en NVS.
- Guardar config runtime en NVS.

---

## 8. Seguridad basica

- AP con password configurable (opcionalmente abierto).
- No exponer password en frontend.
- Limitar endpoints a LAN.

---

## 9. Pruebas

- AP mode: conectar y abrir pagina en <5 s.
- STA mode: guardar credenciales y reconectar.
- Cambio de red: fallback a AP.
- JSON valido en `/api/summary`.
