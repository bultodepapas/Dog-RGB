# Geofence Mode + Presets (Plan)

Objetivo: agregar un segundo modo de LED basado en geocerca (distancia a un punto "home"), manteniendo el modo actual por velocidad. Sin cambios de hardware ni sensores extra.

---

## Resumen de la idea

- **Modo 1: Speed (actual)**. Usa `speed_ranges_kph` y los 10 rangos actuales.
- **Modo 2: Geofence**. Usa 10 rangos de distancia a "home". A mayor distancia, el color se desplaza hacia rojo.
- **Presets**. Los presets incluyen el `mode` y los parametros asociados (rangos y efectos). El "home" no se cambia al aplicar presets.

---

## Decisiones de diseno (propuesta)

1) **Mantener 10 rangos** para ambos modos (consistencia con UI/efectos existentes).
2) **Rangos separados**: `speed_ranges_kph` para velocidad y `fence_max_m` para geocerca (los 10 rangos se derivan automaticamente).
3) **Reusar el set de efectos** (`effects.range1..range10`) tanto para speed como geofence. En el futuro se pueden separar si hace falta.
4) **Home separado de presets**:
   - Evita que un preset sobrescriba la ubicacion.
   - Se maneja con un boton "Set Home = GPS actual".
5) **Auto-home**: home se fija automaticamente **10 s despues de obtener fix** estable, salvo que el usuario lo cambie manualmente desde el AP.
6) **Fallback si no hay GPS fix**: se mantiene el comportamiento actual (rainbow en Segmento B).
7) **Histeresis** para evitar parpadeo entre rangos cercanos.

---

## Estrategia de modos (extensible)

Objetivo: que el codigo seleccione entre **speed**, **geofence** u otros modos futuros sin reescribir la logica principal.

Idea central: un **registro de modos** con una interfaz comun.

### Contrato del modo

Cada modo implementa:
- `id` (string)
- `requires_gps_fix`
- `requires_home`
- `compute_range(ctx) -> range_idx`
- `fallback(ctx) -> fallback_kind` (no gps, no home, etc.)

### Pseudocodigo de arquitectura

```
enum ModeId { "speed", "geofence", "activity", "hr", ... }

struct ModeContext {
  gps_fix: bool
  home_set: bool
  speed_kph: float
  distance_to_home_m: float
  // sensores futuros
  imu_activity_score: float
  hr_bpm: int
}

struct ModeResult {
  ok: bool
  range_idx: int     // 0..9
  fallback_kind: string // "no_gps" | "no_home" | "no_data"
}

interface ModeStrategy {
  id: ModeId
  requires_gps_fix: bool
  requires_home: bool
  compute_range(ctx) -> int
}

mode_registry = {
  "speed": SpeedMode,
  "geofence": GeofenceMode,
  // futuros
}

select_mode(cfg):
  if cfg.mode in mode_registry:
    return mode_registry[cfg.mode]
  return mode_registry["speed"] // fallback seguro

run_mode(ctx, mode):
  if mode.requires_gps_fix and !ctx.gps_fix:
    return ModeResult{ok:false, fallback_kind:"no_gps"}
  if mode.requires_home and !ctx.home_set:
    return ModeResult{ok:false, fallback_kind:"no_home"}
  idx = mode.compute_range(ctx)
  return ModeResult{ok:true, range_idx:idx}
```

### Integracion con el loop LED

```
update_led_ui():
  if critical_no_gps_no_wifi:
    show_critical_state()
    return

  mode = select_mode(cfg)
  result = run_mode(ctx, mode)

  if !result.ok:
    show_fallback(result.fallback_kind)
    return

  range_idx = apply_hysteresis(result.range_idx)
  apply_range_effects(range_idx)
```

### Ventajas
- Agregar un modo nuevo es crear un `ModeStrategy` sin tocar el loop principal.
- El loop solo conoce `ModeResult`.
- Las validaciones de cada modo viven en su propio bloque.

---

## Config (propuesta, no implementada)

Version de config: `3`

```
{
  "version": 3,
  "mode": "speed",                // "speed" | "geofence"
  "led": { "brightness": 77 },
  "speed_ranges_kph": [2,4,6,8,12,16,22,28,34],
  "fence_max_m": 300,
  "effects": { ...range1..range10... },
  "wifi": { ... }
}

Extensible (opcional) para futuros modos:
```
{
  "mode_params": {
    "geofence": { "fence_max_m": 300 },
    "speed": { "speed_ranges_kph": [ ... ] },
    "activity": { "imu_threshold": 0.45 }
  }
}
```
```

Home (separado, en NVS):
```
{
  "home_set": true,
  "home_lat": -34.6037,
  "home_lon": -58.3816,
  "home_set_utc": 1735862400
}
```

---

## Defaults sugeridos para geofence

Parametro:
- `fence_max_m = 300` (default)

Derivacion de rangos (metros):
- `step = fence_max_m / 10`
- R1: 0..step
- R2: step..2*step
- ...
- R9: 8*step..9*step
- R10: >9*step

Colores: reusar el gradiente actual (cian -> rojo), interpretado como "mas lejos = mas caliente".

---

## UI / Portal (propuesta)

En `/config`:
- Selector de **modo**: Speed / Geofence.
- Si modo = Speed: mostrar `speed_ranges_kph`.
- Si modo = Geofence: mostrar `fence_max_m` (input numerico).
- Mostrar debajo la tabla de 10 rangos **calculados** (solo lectura).
- Seccion Home:
  - Boton "Set Home = GPS actual" (solo si hay fix).
  - Mostrar lat/lon actual y distancia a home.
  - Boton "Clear Home".

Presets:
- Preset **default = "Velocidad"** (Speed).
- Al cargar defaults, el modo queda en `speed`.

En Presets:
- Preset incluye `mode` y el array de rangos correspondiente.
- `home` no se modifica al aplicar preset.

---

## Pseudocodigo (alto nivel)

```
tick():
  update_gps()
  update_wifi()
  update_led_ui()

update_mode_defaults():
  // defaults en cold boot o reset
  mode = "speed"
  fence_max_m = 300

update_led_ui():
  if critical_no_gps_no_wifi:
    show_critical_state()
    return

  if mode == "speed":
    range_idx = pick_speed_range(current_speed_kph)
  else: // geofence
    if !gps_fix:
      show_no_gps_effect()
      return
    if !home_set:
      show_home_missing_effect()
      return
    dist_m = haversine_m(cur_lat, cur_lon, home_lat, home_lon)
    range_idx = pick_fence_range(dist_m)

  range_idx = apply_hysteresis(range_idx)
  apply_range_effects(range_idx)

pick_fence_range(dist_m):
  step = fence_max_m / 10
  // 10 rangos definidos por 9 umbrales derivados
  for i in 0..8:
    if dist_m <= step * (i + 1):
      return i
  return 9

apply_hysteresis(next_idx):
  if next_idx == last_idx:
    return last_idx
  // margen fijo o relativo
  margin_m = max(5, (fence_max_m / 10) * 0.03)
  if moving_to_higher_range and dist_m < threshold + margin_m:
    return last_idx
  if moving_to_lower_range and dist_m > threshold - margin_m:
    return last_idx
  return next_idx

// auto-home: 10 s despues de fix estable
auto_set_home():
  if home_set:
    return
  if gps_fix:
    if fix_stable_ms >= 10000:
      home_lat, home_lon = current_fix
      home_set = true

fix_stable_logic():
  if gps_fix:
    if was_fix:
      fix_stable_ms += dt
    else:
      fix_stable_ms = 0
  else:
    fix_stable_ms = 0
  was_fix = gps_fix

handle_home_api():
  if POST /api/home/set:
    if gps_fix:
      home_lat, home_lon = current_fix
      home_set = true
      home_set_utc = gps_utc_now
      home_source = "manual"
  if POST /api/home/clear:
    home_set = false
    home_source = "none"

config_validate_v3(cfg):
  assert cfg.mode in ["speed","geofence"]
  assert cfg.fence_max_m >= 50 and cfg.fence_max_m <= 5000
  // keep existing validations

config_migrate_v2_to_v3(cfg_v2):
  cfg_v3.mode = "speed"
  cfg_v3.fence_max_m = 300
  copy cfg_v2.led, speed_ranges_kph, effects, wifi

apply_mode_and_ranges():
  if mode == "speed":
    use speed_ranges_kph
  else:
    use fence_max_m (derived thresholds)
```

Notas:
- `show_home_missing_effect()` puede ser blanco suave pulsante o azul/ambar.
- En modo geofence, el Segmento A sigue mostrando estados (Wi-Fi/GPS).

---

## Plan de implementacion (sin codigo)

1) Definir esquema v3 y como migrar v2 -> v3 (defaults).
2) Agregar storage de `home_*` y endpoints:
   - `POST /api/home/set` (usa GPS actual)
   - `POST /api/home/clear`
   - `GET /api/home`
3) Agregar `mode` al config y al UI en `/config`.
4) Implementar `fence_max_m` y seleccion de rango derivada.
5) Agregar histeresis para evitar flicker.
6) Ajustar docs: `manual_de_colores.md`, `portal_config.md`, `portal_config_presets.md`.
