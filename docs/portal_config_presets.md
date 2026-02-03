# Portal Config Presets (Profiles)

Este documento define un sistema de presets para guardar combinaciones de rangos, efectos y brillo.

Estado: no implementado en el firmware actual.

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
 - Modo: speed

2) Active
- Brillo medio
- Efectos dinamicos (CHASE/COMET)
- Colores mixtos
 - Modo: speed

3) Sport
- Brillo medio-alto
- Efectos rapidos (JUGGLE/BPM)
- Colores calidos
 - Modo: speed

4) Geofence (ejemplo)
- Brillo medio
- Modo: geofence
- `fence_max_m` definido por el usuario (default 300)

---

## Estructura (JSON)

```
{
  "name": "Calm",
  "mode": "speed",
  "fence_max_m": 300,
  "brightness": 60,
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0, 34.0],
  "effects": {
    "range1": {"a": 0, "b": 2, "speed": 30, "intensity": 60},
    "range2": {"a": 1, "b": 2, "speed": 40, "intensity": 70},
    "range3": {"a": 2, "b": 2, "speed": 50, "intensity": 80},
    "range4": {"a": 3, "b": 3, "speed": 60, "intensity": 90},
    "range5": {"a": 3, "b": 4, "speed": 70, "intensity": 100},
    "range6": {"a": 4, "b": 4, "speed": 80, "intensity": 110},
    "range7": {"a": 4, "b": 5, "speed": 90, "intensity": 120},
    "range8": {"a": 5, "b": 5, "speed": 100, "intensity": 130},
    "range9": {"a": 5, "b": 6, "speed": 110, "intensity": 140},
    "range10": {"a": 6, "b": 6, "speed": 120, "intensity": 150}
  }
}
```

---

## UI

- Dropdown "Perfil" con presets.
- Boton "Aplicar perfil".
- Permitir editar manualmente despues.
- Preset default: "Velocidad" (speed).

---

## Notas

- Presets se almacenan en firmware (hardcoded) o en NVS.
- Cambiar perfil aplica igual que POST /api/config.
- `home` no forma parte de presets (solo se cambia en la seccion Home del AP).
