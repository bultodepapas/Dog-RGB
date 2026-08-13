# Plan Final — Modo SIMPLE (Un Solo Efecto) + Tema Extra (Compatibilidad Actual)

> **Document status:** Historical implementation plan (Spanish). Simple mode is implemented; the optional preset system is not. See [LED UI](../led_ui_spec.md) and [preset proposal](../portal_config_presets.md).

Este documento propone un modo **SIMPLE** para el firmware LED: un solo efecto configurable que se aplica de forma uniforme, con control total desde el portal AP y un **tema extra** como preset visual. **No implementa cambios**.

---

## 1) Estado actual (repo — revisión final)

**Firmware (LED):**
- `update_led_ui()` maneja `speed`, `geofence` y **`show`**.
- `MODE_SHOW = 2` ya existe en `config.h`.
- `mode_name/parse_mode/validate_mode` ya incluyen `show`.
- Hay estado para `show` en RAM: `show_state_*`, `show_effect_id`, `show_first_tick`, `last_mode`.
- `update_show_mode()` aplica efectos con `SHOW_SPEED/SHOW_INTENSITY` y respeta `paint_status_leds()` salvo modo homogéneo.

**Portal/AP (UI + API):**
- `/config` **solo** expone `speed` y `geofence`.
- `GET/POST /api/config` incluye `mode` y `effects` por rango.
- `CONFIG_VERSION = 3`, persistencia en NVS (`dogrgb_cfg`).

Archivos clave:
- `Platformio/Dog-RGB/src/main.cpp`
- `Platformio/Dog-RGB/include/config.h`
- `docs/led_effects.md`
- `docs/portal_config.md`

---

## 2) Objetivo del modo SIMPLE

- **Un solo efecto** aplicado a **toda la tira** (incluye LEDs de estado).
- **Mismo efecto y mismo color** para ambas cintas.
- Control desde el portal con los **parámetros existentes** del motor:
  - `effect_id` (0..11)
  - `speed` (0..255)
  - `intensity` (0..255)
  - `base_color` (RGB, 0..255)

---

## 3) Tema extra (sin tocar el motor)

- Tema = **preset UI** que rellena `effect_id/speed/intensity/RGB`.
- Persistencia: **no** requiere un campo nuevo; se guarda en `single.*`.
- Si los valores coinciden con un tema, el portal lo muestra; si no, `Manual`.

### Tema extra aprobado
- **Tema “Aurora”**
  - `effect_id = GRADIENT_WAVE (11)`
  - `speed = 120`
  - `intensity = 180`
  - `RGB = (0, 180, 120)`

---

## 4) Compatibilidad con el estado actual

- **No tocar** el modo `show`; mantener `MODE_SHOW = 2`.
- Agregar **`MODE_SIMPLE = 3`** para evitar colisión con `show`.
- Extender `mode_name/parse_mode/validate_mode` para `simple`.
- Integrar `simple` en el mecanismo de cambio de modo (`last_mode`) igual que `show`.
- El modo SIMPLE **no** altera lógica Wi‑Fi/GPS ni timers; solo reemplaza la salida LED.

---

## 5) Diseño propuesto (config y persistencia)

### 5.1 Config runtime

Extender `RuntimeConfig`:

```
struct SingleEffectConfig {
  uint8_t effect_id;
  uint8_t speed;
  uint8_t intensity;
  uint8_t base_r;
  uint8_t base_g;
  uint8_t base_b;
};
```

### 5.2 JSON API (GET/POST)

Agregar sección `single`:

```
"single": {
  "effect": 3,
  "speed": 120,
  "intensity": 180,
  "rgb": {"r": 0, "g": 60, "b": 60}
}
```

### 5.3 NVS (persistencia)

- Subir `CONFIG_VERSION` a `4`.
- Guardar campos individuales:
  - `single_eff`, `single_speed`, `single_intensity`, `single_r`, `single_g`, `single_b`.

### 5.4 Migración (compatibilidad)

- Si `ver == 3`: cargar config actual, **setear `single` con defaults**, guardar `ver = 4`.
- Si `ver == 4`: cargar `single` desde NVS y validar rangos (0..255, effect 0..11).
- Si faltan claves o valores inválidos: volver a defaults de `single`.

### 5.5 Defaults sugeridos

- `effect_id = 0` (SOLID)
- `speed = 80`
- `intensity = 140`
- `base = (0, 60, 60)`

---

## 6) Lógica LED propuesta

### 6.1 Nuevo modo

En `config.h`:
- `MODE_SIMPLE = 3`

En `update_led_ui()`:
- Mantener el bloque actual de `show`.
- Agregar un bloque equivalente para `simple`:
  - resetear estado si `last_mode` cambia a `MODE_SIMPLE`.
  - `update_simple_mode(now_ms)`.

### 6.2 update_simple_mode()

- Aplica el mismo efecto en ambas tiras.
- Usa `apply_effect(effect_id, ..., base, speed, intensity, state)`.
- Siempre `start=0`, `count=LED_STRIP_COUNT`.
- **No** pinta `paint_status_leds()` (porque el efecto cubre toda la tira).

### 6.3 Estado y reseteo

- Agregar estado propio:
  - `EffectState simple_state_a/b`
  - `bool simple_first_tick`
- Resetear cuando:
  - se entra a modo SIMPLE,
  - cambian `effect_id/speed/intensity/RGB`.
- Si `effect_id == FIRE`, limpiar `heat_a/heat_b`.

---

## 7) UI del portal (AP)

### 7.1 Selector de modo

Agregar `Simple` en el `<select id='mode'>`.
- Mantener `speed` y `geofence`.
- `show` puede permanecer oculto si no se desea exponerlo.

### 7.2 Bloque SIMPLE

Visible solo en modo SIMPLE:
- `effect_id` (0..11) — `<select>` con nombres.
- `speed` (0..255)
- `intensity` (0..255)
- `color`: inputs `R/G/B` (0..255) + opcional `<input type='color'>`.
- `tema` (dropdown): `Manual`, `Calm`, `Active`, `Sport`, **`Aurora`**.
  - Seleccionar tema rellena `effect_id/speed/intensity/RGB`.
  - Editar manualmente cambia a `Manual`.

Notas UI:
- Indicar que `RAINBOW`, `GRADIENT_WAVE` y `FIRE` **no** respetan el color base.

---

## 8) Backend (API + validaciones)

En `handle_config_get()`:
- Incluir `single` en JSON.
- Aumentar el `StaticJsonDocument` si es necesario.

En `handle_config_post()`:
- Aceptar `single` opcional.
- Validar:
  - `single.effect` 0..11
  - `single.speed` 0..255
  - `single.intensity` 0..255
  - `single.rgb` 0..255
- Si `single` no viene, mantener valores actuales (compatibilidad con clientes viejos).

---

## 9) Pseudocódigo (nivel senior)

```
const MODE_SIMPLE = 3

function update_led_ui():
  if !LED_UI_ENABLED:
    return
  now = millis()

  if welcome.active:
    update_welcome(now)
    return

  if g_cfg.mode != last_mode:
    if g_cfg.mode == MODE_SHOW:
      show_first_tick = true
    if g_cfg.mode == MODE_SIMPLE:
      simple_first_tick = true
    last_mode = g_cfg.mode

  if g_cfg.mode == MODE_SHOW:
    update_show_mode(now)
    return

  if g_cfg.mode == MODE_SIMPLE:
    update_simple_mode(now)
    return

  // lógica actual (speed / geofence)
  ...

function update_simple_mode(now):
  if simple_first_tick:
    simple_first_tick = false
    simple_state_a = {}
    simple_state_b = {}
    if single_effect_id == FIRE:
      clear(heat_a); clear(heat_b)

  if now - last_led_update_ms < LED_UPDATE_MS:
    return
  last_led_update_ms = now

  base = rgb(single_r, single_g, single_b)

  apply_effect(single_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT,
               base, single_speed, single_intensity, simple_state_a)

  if LED_STRIP_MODE == 2:
    apply_effect(single_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT,
                 base, single_speed, single_intensity, simple_state_b)

  show_leds()
```

---

## 10) Pruebas sugeridas

1) Activar modo SIMPLE desde portal y guardar.
2) Verificar que el efecto se aplica en **toda la tira**, sin estado Wi‑Fi/GPS.
3) Cambiar `speed/intensity/RGB` y confirmar respuesta visual.
4) Probar `RAINBOW/GRADIENT_WAVE/FIRE` y validar nota de “color ignorado”.
5) Cambiar a `show` y confirmar que sigue funcionando (no regresión).
6) Probar `LED_STRIP_MODE = 1` y `2`.

---

## 11) Próximos pasos

Si confirmas este plan final, implemento cambios en firmware + portal + docs.

