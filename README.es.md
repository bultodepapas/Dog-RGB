# Smart LED Dog Collar

[English](README.en.md) | [Espanol](README.es.md) | [Manual de uso](docs/manual_de_uso.md) | [Manual de construccion](docs/manual_de_construccion.md)

Collar LED inteligente y de alta visibilidad para perros medianos y grandes. Disenado para seguridad, comodidad y telemetria GPS con portal Wi-Fi local y configuracion runtime de LEDs.

---

## Enlaces rapidos

- Manual de uso: [docs/manual_de_uso.md](docs/manual_de_uso.md)
- Manual de construccion: [docs/manual_de_construccion.md](docs/manual_de_construccion.md)
- Referencia de colores: [docs/manual_de_colores.md](docs/manual_de_colores.md)
- Pinout XIAO ESP32-S3 (oficial): [xiao_s3_pin.md](xiao_s3_pin.md)
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
- LEDs: SK6812 RGBW (5V, single-wire), dos tiras
- Energia: 21700 Li-ion + BMS + boost 5V (>=3A)
- Portal: AP + STA con dashboard local y UI de configuracion
- BLE: caracteristica de resumen diario en modo solo lectura

Mas detalles:
- Freeze hardware: [docs/phase0_freeze.md](docs/phase0_freeze.md)
- Wiring: [docs/sk6812_wiring.md](docs/sk6812_wiring.md)
- Presupuesto de energia: [docs/bom_power_budget.md](docs/bom_power_budget.md)

---

## Firmware (Estado actual)

El proyecto de firmware activo esta en [Platformio/Dog-RGB](Platformio/Dog-RGB) con:

- Parsing NMEA RMC + GGA (fix, velocidad, satelites)
- Calculo de distancia (Haversine) con filtro de picos
- Tracking de tiempo activo y umbrales de velocidad
- Reset diario usando fecha GPS
- Metricas max/promedio
- Persistencia NVS para metricas + config runtime
- Portal Wi-Fi (AP/STA) con `/`, `/api/summary`, `/wifi`
- UI de configuracion en `/config` con `/api/config` + `/api/config/reset`
- Resumen BLE diario en modo lectura
- UI LED con 12 efectos, configurable por rangos de velocidad

Archivos clave:
- Entrypoint firmware: [Platformio/Dog-RGB/src/main.cpp](Platformio/Dog-RGB/src/main.cpp)
- Pines: [Platformio/Dog-RGB/include/pins.h](Platformio/Dog-RGB/include/pins.h)
- Defaults runtime: [Platformio/Dog-RGB/include/config.h](Platformio/Dog-RGB/include/config.h)
- Build config: [Platformio/Dog-RGB/platformio.ini](Platformio/Dog-RGB/platformio.ini)

---

## Configuracion del portal (Runtime)

El portal expone configuracion runtime via `/config` y `/api/config` (mas `/api/config/reset`).

- Portal config (runtime): [docs/portal_config.md](docs/portal_config.md)
- Presets (no implementado): [docs/portal_config_presets.md](docs/portal_config_presets.md)

Docs del portal Wi-Fi:
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- Wi-Fi spec: [docs/wifi_portal_spec.md](docs/wifi_portal_spec.md)
- Diagrama de estados: [docs/wifi_portal_state_diagram.md](docs/wifi_portal_state_diagram.md)

---

## Comportamiento LED

- UI spec: [docs/led_ui_spec.md](docs/led_ui_spec.md)
- Referencia de efectos: [docs/led_effects.md](docs/led_effects.md)
- Referencia de colores: [docs/manual_de_colores.md](docs/manual_de_colores.md)

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
- GPS RX: D7 / GPIO44
- GPS TX: D6 / GPIO43
- LED estado: D2 / GPIO3 (LED externo)
- LED A data: D0 / GPIO1
- LED B data: D1 / GPIO2

Referencia de wiring:
- [docs/manual_de_uso.md](docs/manual_de_uso.md)
- [docs/sk6812_wiring.md](docs/sk6812_wiring.md)

---

## Estructura del repo

- `Datasheets/` datasheets de componentes
- `docs/` specs, arquitectura, decisiones, roadmap
- `firmware/` notas y referencias de firmware
- `hardware/` notas de hardware
- `Platformio/` proyecto de firmware activo (PlatformIO)
- `software/` app/BLE (futuro)

---

## Proximos pasos

- Validar presupuesto de energia con eficiencias reales
- Finalizar BOM y sourcing
- Boceto de enclosure y routing de cables
- Agregar IMU para clasificacion de movimiento (Fase 2)
