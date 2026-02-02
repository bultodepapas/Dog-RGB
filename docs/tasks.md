# Tareas Pendientes (Plan Detallado)

Este documento lista tareas por fase y el estado actual del MVP.

---

## Fase 1 - GPS-First MVP (Sin App, Portal Wi-Fi)

### Hardware Base (pendiente)
- Confirmar cableado final: XIAO ESP32-S3 + EBYTE E108-GN02.
- Revisar alimentacion GNSS (3.3 V estable) y GND comun.
- Definir LED de estado externo y resistencia.
- Validar disipacion y consumo real del boost 5V.

### Firmware (implementado)
- Parser NMEA RMC + GGA con filtros.
- Calculo distancia, promedio y maxima.
- Reset diario por fecha GPS.
- Persistencia NVS de metricas.
- Portal Wi-Fi AP/STA con `/api/summary`.
- Config runtime via `/config` + `/api/config`.
- Reset de config via `/api/config/reset`.
- BLE daily summary (read-only).

### Portal Wi-Fi (implementado)
- Pagina principal con 3 metricas.
- Pagina de setup Wi-Fi `/wifi`.
- mDNS en STA (`dog-collar.local`).
- Politica AP/Wi-Fi automatica (GPS/estacionario).

### UX y Copys (pendiente)
- Revisar textos finales para estados y errores.
- Ajustar mensajes para usuario final.

### Validacion (pendiente)
- Prueba estatica (distancia cercana a 0).
- Prueba caminata corta (200-500 m) comparando con GPS telefono.
- Prueba trote (velocidad maxima coherente).
- Verificar portal carga en <2 s.

---

## Fase 2 - Motion (IMU)

### Hardware
- Seleccion final IMU (BMI270 / ICM-42688).
- Integracion fisica y alimentacion.

### Firmware
- Driver IMU y calibracion basica.
- Clasificacion de movimiento.
- Fusion GPS + IMU.

---

## Fase 3 - Heart Rate

### Hardware
- Seleccion de sensor HR y montaje.

### Firmware
- Driver HR + validacion de senal.
- Integrar HR en perfiles de actividad.

---

## Fase 4 - Miniaturizacion

### Mecanico
- Reduccion de tamano y peso.
- Carcasa compacta.

### Electrico
- PCB mas pequeno y optimizado.
- Reduccion de consumo.

---

## Documentacion y Especificaciones

- BLE spec: `docs/ble_spec.md`
- Portal Wi-Fi: `docs/wifi_portal_spec.md`
- Web portal: `docs/web_portal_spec.md`
- App MVP (futuro): `docs/app_mvp_spec.md`

---

## Gestion y Riesgos

- Riesgo: GPS sin fix en interiores.
- Riesgo: portal no accesible por cambio de red.
- Mitigacion: modo AP siempre disponible y politica de auto-encendido.
