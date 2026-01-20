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
  "version": 1,
  "led": {
    "brightness": 77
  },
  "speed_ranges_kph": [1.5, 3.0, 4.5, 6.0, 7.5],
  "effects": {
    "range1": {"a": 0, "b": 1, "speed": 40, "intensity": 80},
    "range2": {"a": 1, "b": 3, "speed": 60, "intensity": 100},
    "range3": {"a": 6, "b": 5, "speed": 80, "intensity": 120},
    "range4": {"a": 7, "b": 8, "speed": 110, "intensity": 150},
    "range5": {"a": 9, "b": 4, "speed": 140, "intensity": 180},
    "range6": {"a": 10, "b": 3, "speed": 170, "intensity": 200}
  },
  "wifi": {
    "ap_ssid": "dog",
    "ap_pass": "Dog123456789",
    "mdns": "dog-collar"
  }
}
```

---

## Validaciones (POST /api/config)

- brightness: 1..255
- speed_ranges_kph: 5 valores en orden ascendente
- effect ids: 0..11
- effect speed/intensity: 0..255
- ap_ssid: 1..32
- ap_pass: >= 8
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
  - Reiniciar mDNS si STA activo

---

## Notas

- No expone parametros de buffers o GNSS.
- Si el JSON es invalido, responder 400 con mensaje simple.
