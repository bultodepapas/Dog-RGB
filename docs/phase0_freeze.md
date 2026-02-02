# Fase 0 - Parametros Congelados (Decision Tecnica)

Este documento fija los parametros base para comenzar la implementacion del MVP.

---

## Hardware seleccionado

- GNSS: EBYTE E108-GN02 Series (10 Hz, BDS/GPS/GLONASS)
- MCU: Seeed Studio XIAO ESP32-S3
- LEDs: SK6812 (5V, single-wire), una o dos tiras

---

## Pines recomendados (XIAO ESP32-S3)

Objetivo: pines estables, faciles de cablear y sin conflicto con USB.

- GPS RX (ESP32 recibe): D7 / GPIO44
- GPS TX (ESP32 transmite, opcional): D6 / GPIO43
- LED de estado: D2 / GPIO3 (LED externo con resistencia)
- LED A data: D0 / GPIO1
- LED B data: D1 / GPIO2

Notas:
- Se evita usar el LED RGB integrado para no mezclar con futuras tiras LED.
- Se usa D6/D7 para evitar conflicto con pines SD/SPI.
- Si se decide usar otro pin por disponibilidad, se actualiza en `pins.h`.

---

## GNSS (configuracion base)

- Baudrate: 9600
- Frecuencia de actualizacion: 10 Hz
- Mensajes requeridos: RMC (speed/fecha) + GGA (fix/sats)

---

## Umbrales y filtros (iniciales)

- Intervalo GPS: 1 s
- Umbral movimiento (activo): 0.7 km/h
- Velocidad maxima valida: 40 km/h
- Filtro de distancia: descartar saltos grandes (>50 m)

---

## BLE (formato de bloque fijo)

- Payload fijo: 16 bytes
- Endian: little-endian
- Campos:
  - date_yyyymmdd (uint32)
  - distance_m (uint32)
  - avg_speed_cmps (uint16)
  - max_speed_cmps (uint16)
  - last_update_min (uint16)
  - flags (uint8)
  - checksum XOR (uint8)

---

## Riesgos y mitigaciones

- GPS sin fix: mantener ultimo valor valido y marcar flag.
- Saltos por ruido: filtro de velocidad maxima y distancia.
- Escrituras en flash: guardar cada 1-5 minutos.

---

## Siguiente paso

- Validar cableado final con D6/D7.
- UUIDs BLE definidos y documentados en `docs/ble_spec.md`.
