# Fase 0 - Parametros Congelados (Decision Tecnica)

Este documento fija los parametros base para comenzar la implementacion del MVP.

---

## Hardware seleccionado

- GNSS: EBYTE E108-GN02 Series (10 Hz, BDS/GPS/GLONASS)
- MCU: Seeed Studio XIAO ESP32-S3

---

## Pines recomendados (XIAO ESP32-S3)

Objetivo: pines estables, faciles de cablear y sin conflicto con USB.

- GPS RX (ESP32 recibe): D6 / GPIO7
- GPS TX (ESP32 transmite, opcional): D7 / GPIO8
- LED de estado: D2 / GPIO3 (LED externo con resistencia)

Notas:
- Se evita usar el LED RGB integrado para no mezclar con futuras tiras LED.
- Si se decide usar otro pin por disponibilidad, se actualiza en `pins.h`.

---

## GNSS (configuracion base)

- Baudrate: 9600
- Frecuencia de actualizacion: 10 Hz
- Mensajes requeridos: RMC (speed) y opcional VTG

---

## Umbrales y filtros (iniciales)

- Intervalo GPS: 1 s
- Umbral movimiento (activo): 0.7 km/h
- Tiempo de pausa: 10 s sin movimiento
- Velocidad maxima valida: 40 km/h

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

- Actualizar firmware con pines y parametros definidos.
- Definir UUIDs BLE y documentar el servicio.
