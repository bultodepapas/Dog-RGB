# Portal Config Presets (Profiles)

Este documento define un sistema de presets para guardar combinaciones de rangos, efectos y brillo.

---

## Objetivo

- Permitir al usuario seleccionar perfiles predefinidos.
- Cambiar rapidamente el comportamiento visual sin editar cada campo.

---

## Presets base

1) Calm
- Brillo bajo
- Efectos suaves (SOLID/BREATH)
- Colores frios

2) Active
- Brillo medio
- Efectos dinamicos (CHASE/COMET)
- Colores mixtos

3) Sport
- Brillo medio-alto
- Efectos rapidos (JUGGLE/BPM)
- Colores calidos

---

## Estructura (JSON)

```
{
  "name": "Calm",
  "brightness": 60,
  "speed_ranges_kph": [1.5, 3.0, 4.5, 6.0, 7.5],
  "effects": {
    "range1": {"a": 0, "b": 2, "speed": 30, "intensity": 60},
    "range2": {"a": 1, "b": 2, "speed": 40, "intensity": 70},
    "range3": {"a": 2, "b": 2, "speed": 50, "intensity": 80},
    "range4": {"a": 3, "b": 3, "speed": 60, "intensity": 90},
    "range5": {"a": 3, "b": 4, "speed": 70, "intensity": 100},
    "range6": {"a": 4, "b": 4, "speed": 80, "intensity": 110}
  }
}
```

---

## UI

- Dropdown "Perfil" con presets.
- Boton "Aplicar perfil".
- Permitir editar manualmente despues.

---

## Notas

- Presets se almacenan en firmware (hardcoded) o en NVS.
- Cambiar perfil aplica igual que POST /api/config.
