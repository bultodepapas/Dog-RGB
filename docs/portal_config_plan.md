# Portal Config Plan (Runtime Params)

Este documento define el JSON y validaciones para exponer parametros editables desde el portal web.

---

## Alcance

Solo se exponen:
- Rangos de velocidad
- Efectos por rango + speed/intensity
- Brillo
- Wi-Fi (AP SSID/PASS + mDNS)

---

## Endpoint

- GET /api/config
- POST /api/config

---

## JSON (GET /api/config)

```
{
  "version": 2,
  "led": {
    "brightness": 77
  },
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0, 34.0],
  "effects": {
    "range1": {"a": 0, "b": 1, "speed": 40, "intensity": 80},
    "range2": {"a": 1, "b": 2, "speed": 58, "intensity": 95},
    "range3": {"a": 2, "b": 3, "speed": 76, "intensity": 110},
    "range4": {"a": 3, "b": 4, "speed": 94, "intensity": 125},
    "range5": {"a": 5, "b": 6, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 8, "speed": 130, "intensity": 155},
    "range7": {"a": 8, "b": 9, "speed": 148, "intensity": 170},
    "range8": {"a": 9, "b": 11, "speed": 166, "intensity": 180},
    "range9": {"a": 11, "b": 4, "speed": 184, "intensity": 190},
    "range10": {"a": 10, "b": 3, "speed": 200, "intensity": 200}
  },
  "wifi": {
    "ap_ssid": "dog",
    "has_ap_pass": true,
    "mdns": "dog-collar"
  }
}
```

---

## Validaciones (POST /api/config)

- brightness: 1..255
- speed_ranges_kph: 9 valores en orden ascendente
- effect ids: 0..11
- effect speed/intensity: 0..255
- ap_ssid: 1..32
- ap_pass: >= 8 (si se envia)
- ap_open: true/false (si true, AP sin password)
- mdns: 1..32 (solo letras, numeros y guiones)

---

## Comportamiento al guardar

- Guardar en NVS (config runtime).
- Aplicar en caliente:
  - Brillo
  - Rangos
  - Efectos
- Para Wi-Fi:
  - Reiniciar AP con nuevo SSID/PASS
  - Si ap_open=true, limpiar password y dejar AP abierto
  - Reiniciar mDNS si STA activo

---

## Notas

- No expone parametros de buffers o GNSS.
- Si el JSON es invalido, responder 400 con mensaje simple.
