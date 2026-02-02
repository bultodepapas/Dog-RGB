# Config Storage Schema (NVS)

Este documento define el esquema de almacenamiento para configuracion runtime en NVS.

---

## Namespace

- NVS namespace: `dogrgb_cfg`

---

## Versionado

- Key: `ver` (uint8)
- Version actual: 2
- Si `ver` es distinto, usar defaults y reescribir.

---

## Claves y tipos

### Valores simples
- `brightness` (uint8)
- `ap_ssid` (string)
- `ap_pass` (string, puede estar vacio para AP abierto)
- `mdns` (string)

### Rangos de velocidad
- `ranges` (blob)
  - 9 floats en orden: r1..r9

### Efectos por rango
- `effects` (blob)
  - 10 entradas (range1..range10)
  - Cada entrada:
    - effect_a (uint8)
    - effect_b (uint8)
    - speed (uint8)
    - intensity (uint8)

---

## Tamano estimado

- ranges: 9 * 4 = 36 bytes
- effects: 10 * 4 = 40 bytes
- Total binario: ~76 bytes + strings

---

## Reglas

- Guardar solo cuando el usuario pulsa "Guardar".
- Validar antes de escribir.
- Si falta alguna clave, usar default de `config.h`.
- Si se solicita AP abierto, guardar `ap_pass` como string vacio.

---

## Reset

- Borrar namespace `dogrgb_cfg`.
- Reiniciar config a defaults.

---

## Notas

- NVS es suficiente para este volumen.
- Evitar escritura frecuente para prolongar flash.
