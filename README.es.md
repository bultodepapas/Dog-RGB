# Smart LED Dog Collar

[English](README.en.md) | [Espanol](README.es.md) | [Manual de uso](docs/manual_de_uso.md)

Collar LED inteligente y de alta visibilidad para perros medianos y grandes, disenado para seguridad, comodidad y expansion futura (GPS, app BLE, modos avanzados).

---

## Equipo e intencion

Somos dos ingenieros (electrico e industrial). Este proyecto es un experimento que queriamos construir desde hace mucho tiempo, ahora impulsado por nuestra experiencia y agentes de AI durante el desarrollo.

---

## Objetivo

Construir un collar que sea:

- Muy visible de noche con animaciones LED de alta calidad.
- Reactivo al movimiento y a la velocidad para color y brillo.
- Eficiente con una sola celda Li-ion 21700.
- Seguro, comodo y resistente al clima.
- Listo para expansion GPS y BLE.

---

## Metas de diseno mecanico

- Circunferencia de cuello: 40-55 cm (perros medianos/grandes)
- Ancho de correa: 20-25 mm
- Difusor LED: tubo de silicona sobre correa de nylon
- Peso total objetivo: 120-160 g
- Enclosure: compacto, IP65-IP67, con alivio de tension

---

## Sistema LED

LEDs seleccionados (Fase 1):

- SK6812 (5V, single-wire)
- Cantidad por tira: 20 (min 10, max 50)
- Tira simple o dual (pines de datos independientes)

Notas:

- Requiere 5V regulados (no alimentar directo desde bateria)
- Brillo limitado a ~30% por seguridad y bateria
- Datos SK6812 pueden requerir logica 5V; usar level shifter y buen desacoplo

---

## Sistema de energia

Bateria:

- 21700 Li-ion
- ~5000 mAh
- 18.5 Wh energia nominal

Presupuesto LED (ejemplo, 2 tiras x 20 LEDs):

- 3-5 mA por LED (UI idle) -> 120-200 mA total LEDs
- Mas brillo y animaciones incrementan consumo

Objetivo: mantener corriente promedio baja para buena autonomia

---

## Arquitectura de energia

```
21700 Bateria (3.0-4.2V)
   -> BMS 1S + cargador USB-C
   -> Boost 5V (>=3A continuo)
   -> Tiras SK6812
   -> Regulador 3.3V -> ESP32-S3 + sensores
```

Funciones adicionales:

- Monitoreo de voltaje de bateria
- Proteccion por bateria baja
- Interruptor fisico o sensor Hall

---

## Sistema de control

MCU:

- ESP32-S3 (BLE, control LED, procesamiento sensores)

Opciones de IMU:

- BMI270
- ICM-42688
- (MPU6050 aceptable en prototipos)

GPS seleccionado (Fase 1):

- EBYTE E108-GN02 (10 Hz, BDS/GPS/GLONASS)
- UART 9600 baud

---

## Logica de comportamiento

Entradas:

- Intensidad de movimiento IMU
- Velocidad GPS (cuando este habilitado)

Salidas:

- Mapeo de color (Segmento B, velocidad GPS):
  - Baja velocidad -> Azul
  - Media velocidad -> Morado
  - Alta velocidad -> Rojo
- Brillo escala con actividad
- Efectos:
  - Breathing (idle)
  - Pulsing (medio)
  - Chase o strobe (alto)

---

## Roadmap de desarrollo

Fase 1 - MVP GPS-First (Perros medianos/grandes)

- Bateria + cargador + boost
- ESP32-S3 + GPS + control LED
- Patrones basicos y limites de brillo
- Monitoreo de bateria
- Portal Wi-Fi (AP + STA)

Fase 2 - Logica basada en movimiento

- Agregar IMU para clasificacion de movimiento
- Combinar velocidad GPS + intensidad IMU
- Refinar perfiles de actividad

Fase 3 - Integracion de ritmo cardiaco

- Agregar sensor HR
- Usar HR como senal de actividad
- Calibrar umbrales y limites de seguridad

Fase 4 - Miniaturizacion (perros pequenos)

- Reducir footprint electronico
- Bateria mas ligera
- Menor densidad LED y correas mas delgadas

---

## Estructura del repositorio

- `docs/` specs, arquitectura, decisiones, roadmap
- `hardware/` esquemas, PCB, notas de energia
- `firmware/` codigo embebido
- `software/` app/BLE (futuro)
- `assets/` diagramas, renders, imagenes
- `research/` datasheets, referencias, calculos
- `tests/` validacion y planes de prueba
- `tools/` scripts y utilidades

---

## Hardware actual (Fase 1 MVP)

- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz)
- LEDs: SK6812 (single-wire), dos tiras
- GPS UART: 9600 baud
- Pines (XIAO ESP32-S3):
  - GPS RX: D6 / GPIO7
  - GPS TX: D7 / GPIO8
  - LED estado: D2 / GPIO3 (LED externo)
  - LED A data: GPIO11
  - LED B data: GPIO12

---

## Estado del firmware (Fase 1)

- Parsing NMEA RMC (lat/lon/velocidad/fecha/hora)
- Calculo de distancia (Haversine) con filtro de picos
- Tracking de tiempo activo y umbrales
- Reset diario usando fecha GPS
- Metricas max/promedio
- Persistencia NVS (guardado periodico)
- Logs seriales heartbeat
- Rangos y tiras configurables en `firmware/esp32s3_base/include/config.h`
- Efectos FastLED por rango de velocidad (Segmento B)
- Configuracion runtime editable en `/config`

Proyecto base: `firmware/esp32s3_base/`

---

## Indice de documentacion

- Phase 0 freeze: [`docs/phase0_freeze.md`](docs/phase0_freeze.md)
- Tasks backlog: [`docs/tasks.md`](docs/tasks.md)
- GPS -> calculation -> BLE flow: [`docs/flow_wireframe.md`](docs/flow_wireframe.md)
- Web portal spec: [`docs/web_portal_spec.md`](docs/web_portal_spec.md)
- BLE spec: [`docs/ble_spec.md`](docs/ble_spec.md)
- App MVP spec (future): [`docs/app_mvp_spec.md`](docs/app_mvp_spec.md)
- Wi-Fi portal spec: [`docs/wifi_portal_spec.md`](docs/wifi_portal_spec.md)
- Wi-Fi portal plan: [`docs/wifi_portal_plan.md`](docs/wifi_portal_plan.md)
- Wi-Fi portal state diagram: [`docs/wifi_portal_state_diagram.md`](docs/wifi_portal_state_diagram.md)
- LED UI spec: [`docs/led_ui_spec.md`](docs/led_ui_spec.md)
- SK6812 wiring guide: [`docs/sk6812_wiring.md`](docs/sk6812_wiring.md)
- BOM + power budget: [`docs/bom_power_budget.md`](docs/bom_power_budget.md)
- Config params: [`docs/config_params.md`](docs/config_params.md)
- LED effects plan: [`docs/led_effects_plan.md`](docs/led_effects_plan.md)
- Manual de uso: [`docs/manual_de_uso.md`](docs/manual_de_uso.md)
- Portal config plan: [`docs/portal_config_plan.md`](docs/portal_config_plan.md)
- Portal config NVS plan: [`docs/portal_config_nvs_plan.md`](docs/portal_config_nvs_plan.md)
- Portal config UI plan: [`docs/portal_config_ui_plan.md`](docs/portal_config_ui_plan.md)
- Portal config NVS schema: [`docs/portal_config_nvs_schema.md`](docs/portal_config_nvs_schema.md)
- Portal config apply flow: [`docs/portal_config_apply_flow.md`](docs/portal_config_apply_flow.md)
- Portal config presets: [`docs/portal_config_presets.md`](docs/portal_config_presets.md)

---

## Proximos pasos

- Construir BOM detallado y sourcing
- Crear presupuesto de energia con eficiencias reales
- Borrador de esquema para energia + LEDs + IMU
- Definir umbrales IMU para niveles de actividad
- Boceto de enclosure y routing de cables
