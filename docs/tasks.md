# Tareas Pendientes (Plan Detallado)

Este documento lista todas las tareas pendientes para implementar la Fase 1 (GPS + Wi-Fi portal) y preparar fases futuras.

## Fase 1 - GPS-First MVP (Sin App, Portal Wi-Fi)

### Hardware Base
- Confirmar cableado final: XIAO ESP32-S3 + EBYTE E108-GN02.
- Revisar alimentacion GNSS (3.3 V estable) y GND comun.
- Definir LED de estado externo y resistencia.

### Firmware (ESP32-S3 + GPS)
- Verificar pines finales en `firmware/esp32s3_base/include/pins.h`.
- Validar baudrate del GNSS (9600) y frecuencia 10 Hz.
- Mantener parser RMC y filtros actuales.
- Confirmar umbrales: 0.7 km/h activo, 40 km/h max valida, 1 s sample.
- Validar reset diario con fecha GPS.
- Validar persistencia NVS (guardado cada 60 s).
- Estabilizar logs de diagnostico.

### Portal Wi-Fi (AP + STA)
- Definir modo por defecto: AP si no hay credenciales.
- Implementar pagina principal con 3 metricas.
- Implementar `/api/summary` JSON.
- Implementar pagina de setup Wi-Fi (STA) y endpoint `/api/wifi`.
- Implementar mDNS opcional (`dog-collar.local`) en STA.
- Mensajes simples: sin GPS, sin datos, conectado.

### UX y Copys
- Definir textos exactos para estados:
  - "Esperando GPS"
  - "Sin datos, intenta mas tarde"
  - "Conectado al collar"
  - "Conectado a Wi-Fi"

### Validacion
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
- Driver HR + validacion de se?al.
- Integrar HR en perfiles de actividad.

---

## Fase 4 - Miniaturizacion

### Mec?nico
- Reduccion de tama?o y peso.
- Carcasa compacta.

### Electrico
- PCB mas pequeno y optimizado.
- Reduccion de consumo.

---

## Documentacion y Especificaciones

- BLE spec: `docs/ble_spec.md` (fase futura)
- Portal Wi-Fi: `docs/wifi_portal_spec.md`
- Web portal general: `docs/web_portal_spec.md`
- Flow BLE: `docs/flow_wireframe.md`
- App MVP (futuro): `docs/app_mvp_spec.md`

---

## Gestion y Riesgos

- Riesgo: GPS sin fix en interiores.
- Riesgo: portal no accesible por cambio de red.
- Mitigacion: modo AP siempre disponible.

---

## Estado actual (implementado)

- Firmware GPS con calculo de distancia, promedio y max.
- Persistencia NVS activa.
- BLE payload definido y documentado (no usado en Fase 1).
