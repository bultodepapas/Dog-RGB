# Smart LED Dog Collar

[English](README.en.md) | [Espanol](README.es.md) | [Manual de uso](docs/manual_de_uso.md) | [Manual de construccion](docs/manual_de_construccion.md)

Collar LED inteligente y de alta visibilidad para perros medianos y grandes. Disenado para seguridad, comodidad y expansion futura (GPS, BLE, modos avanzados).

---

## Enlaces rapidos

- Manual de uso: [docs/manual_de_uso.md](docs/manual_de_uso.md)
- Manual de construccion: [docs/manual_de_construccion.md](docs/manual_de_construccion.md)
- Arquitectura: [docs/architecture.md](docs/architecture.md)
- Requisitos: [docs/requirements.md](docs/requirements.md)
- Roadmap: [docs/roadmap.md](docs/roadmap.md)
- Tareas: [docs/tasks.md](docs/tasks.md)

---

## Que es

Un collar wearable con telemetria GPS, comportamiento LED configurable y un portal Wi-Fi local (AP/STA) para datos y ajustes runtime.

---

## Resumen del sistema

- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz)
- LEDs: SK6812 (5V, single-wire), dos tiras
- Energia: 21700 Li-ion + BMS + boost 5V (>=3A)
- Portal: AP + STA con dashboard local y UI de configuracion

Mas detalles:
- Freeze hardware: [docs/phase0_freeze.md](docs/phase0_freeze.md)
- Wiring: [docs/sk6812_wiring.md](docs/sk6812_wiring.md)
- Presupuesto de energia: [docs/bom_power_budget.md](docs/bom_power_budget.md)

---

## Firmware (Estado actual)

El firmware base esta en [firmware/esp32s3_base](firmware/esp32s3_base) con:

- Parsing NMEA RMC (lat/lon/velocidad/fecha/hora)
- Calculo de distancia (Haversine) con filtro de picos
- Tracking de tiempo activo y umbrales de velocidad
- Reset diario usando fecha GPS
- Metricas max/promedio
- Persistencia NVS (guardado periodico)
- Configuracion runtime editable en el portal `/config`

Archivos clave:
- Entrypoint firmware: [firmware/esp32s3_base/src/main.cpp](firmware/esp32s3_base/src/main.cpp)
- Pines: [firmware/esp32s3_base/include/pins.h](firmware/esp32s3_base/include/pins.h)
- Defaults runtime: [firmware/esp32s3_base/include/config.h](firmware/esp32s3_base/include/config.h)
- Build config: [firmware/esp32s3_base/platformio.ini](firmware/esp32s3_base/platformio.ini)

---

## Configuracion del portal (Runtime)

El portal expone configuracion runtime via `/config` y `/api/config`.

- Plan: [docs/portal_config_plan.md](docs/portal_config_plan.md)
- UI spec: [docs/portal_config_ui_plan.md](docs/portal_config_ui_plan.md)
- Validation flow: [docs/portal_config_validation_flow.md](docs/portal_config_validation_flow.md)
- Apply flow: [docs/portal_config_apply_flow.md](docs/portal_config_apply_flow.md)
- NVS plan: [docs/portal_config_nvs_plan.md](docs/portal_config_nvs_plan.md)
- NVS schema: [docs/portal_config_nvs_schema.md](docs/portal_config_nvs_schema.md)
- Presets: [docs/portal_config_presets.md](docs/portal_config_presets.md)

Docs del portal Wi-Fi:
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- Wi-Fi plan: [docs/wifi_portal_plan.md](docs/wifi_portal_plan.md)
- Diagrama de estados: [docs/wifi_portal_state_diagram.md](docs/wifi_portal_state_diagram.md)

---

## Comportamiento LED

- UI spec: [docs/led_ui_spec.md](docs/led_ui_spec.md)
- Plan de efectos: [docs/led_effects_plan.md](docs/led_effects_plan.md)

---

## Specs y docs de producto

- Requisitos: [docs/requirements.md](docs/requirements.md)
- Arquitectura: [docs/architecture.md](docs/architecture.md)
- App MVP spec: [docs/app_mvp_spec.md](docs/app_mvp_spec.md)
- BLE spec: [docs/ble_spec.md](docs/ble_spec.md)
- Web portal spec: [docs/web_portal_spec.md](docs/web_portal_spec.md)

---

## Hardware Setup (Fase 1 MVP)

Pines (XIAO ESP32-S3):
- GPS RX: D6 / GPIO7
- GPS TX: D7 / GPIO8
- LED estado: D2 / GPIO3 (LED externo)
- LED A data: GPIO11
- LED B data: GPIO12

Referencia de wiring:
- [docs/manual_de_uso.md](docs/manual_de_uso.md)
- [docs/sk6812_wiring.md](docs/sk6812_wiring.md)

---

## Estructura del repo

- `docs/` specs, arquitectura, decisiones, roadmap
- `hardware/` esquemas, PCB, notas de energia
- `firmware/` codigo embebido
- `software/` app/BLE (futuro)
- `assets/` diagramas, renders, imagenes
- `research/` datasheets, referencias, calculos
- `tests/` validacion y planes de prueba
- `tools/` scripts y utilidades

---

## Proximos pasos

- Construir BOM detallado y sourcing
- Validar presupuesto de energia con eficiencias reales
- Borrador de esquema para energia + LEDs + IMU
- Definir umbrales IMU para niveles de actividad
- Boceto de enclosure y routing de cables
