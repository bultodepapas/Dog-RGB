# Plan Final — Modo SIMPLE (Un Solo Efecto) + Tema Extra

Este documento propone un modo **SIMPLE** para el firmware LED: un solo efecto configurable que se aplica de forma uniforme, con control total desde el portal AP y un **tema extra** como preset visual. **No implementa cambios**.

---

## 1) Estado actual (repo)

**Firmware (LED):**
- `update_led_ui()` decide entre `speed` y `geofence` según `g_cfg.mode`.
- El motor de efectos usa `apply_effect(effect_id, base, speed, intensity)`.
- `RAINBOW`, `GRADIENT_WAVE` y `FIRE` **ignoran** `base`.
- Segmento A (LED0/LED1) se usa para estados Wi‑Fi/GPS, salvo modo homogéneo.

**Portal/AP (UI + API):**
- `/config` ya expone `mode` (speed/geofence).
- `GET/POST /api/config` incluye `mode`, `effects` y `speed_ranges`.
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

## 3) Tema extra (con lo que ya existe)

Se agrega un **preset de tema** en la UI del modo SIMPLE, sin cambios en el motor.
- El tema es **solo un atajo UI** que rellena `effect_id/speed/intensity/RGB`.
- Persistencia: **no** requiere un campo nuevo; se guarda el resultado en `single.*`.
- Al recargar, el portal puede detectar si los valores coinciden con un tema y mostrarlo; si no, muestra `Manual`.

### Tema extra propuesto (ejemplo)
- **Tema “Aurora”**
  - `effect_id = GRADIENT_WAVE` (11)
  - `speed = 120`
  - `intensity = 180`
  - `RGB = (0, 180, 120)`

Nota: puedes cambiar el nombre o los valores sin tocar firmware.

---

## 4) Diseño propuesto (config y persistencia)

### 4.1 Config runtime

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

### 4.2 JSON API (GET/POST)

Agregar sección `single`:

```
"single": {
  "effect": 3,
  "speed": 120,
  "intensity": 180,
  "rgb": {"r": 0, "g": 60, "b": 60}
}
```

### 4.3 NVS (persistencia)

- Subir `CONFIG_VERSION` (p.ej. 4).
- Guardar campos individuales:
  - `single_eff`, `single_speed`, `single_intensity`, `single_r`, `single_g`, `single_b`.

### 4.4 Defaults sugeridos

- `effect_id = 0` (SOLID)
- `speed = 80`
- `intensity = 140`
- `base = (0, 60, 60)`

---

## 5) Lógica LED propuesta

### 5.1 Nuevo modo

En `config.h`:
- `MODE_SIMPLE = 2` (o siguiente ID libre)

En `update_led_ui()`:
```
if (g_cfg.mode == MODE_SIMPLE) {
  update_simple_mode(now_ms);
  return;
}
```

### 5.2 update_simple_mode()

- Aplica el mismo efecto en ambas tiras.
- Usa `apply_effect(effect_id, ..., base, speed, intensity, state)`.
- Siempre `start=0`, `count=LED_STRIP_COUNT`.

### 5.3 Estado y reseteo

- Usar `EffectState simple_state_a/b` o reutilizar `state_a/b`.
- Resetear estado cuando:
  - se cambia a modo SIMPLE,
  - se cambia `effect_id`, `speed`, `intensity` o `RGB`.
- Para `FIRE`, limpiar `heat_a/heat_b` al entrar al modo o al cambiar `effect_id`.

---

## 6) UI del portal (AP)

### 6.1 Selector de modo

Agregar opción `simple` en el `<select id='mode'>`:
- `Velocidad`, `Geocerca`, `Simple`.

### 6.2 Bloque SIMPLE

Visible solo en modo SIMPLE:
- `effect_id` (0..11) — `<select>` con nombres (`SOLID`, `PULSE`, …)
- `speed` (0..255)
- `intensity` (0..255)
- `color`:
  - inputs `R/G/B` (0..255)
  - opcional `<input type='color'>` sincronizado
- `tema` (dropdown): `Manual`, `Calm`, `Active`, `Sport`, **`Aurora` (tema extra)**
  - Al elegir un tema, se rellena `effect_id/speed/intensity/RGB`.
  - Si el usuario cambia manualmente valores, el tema pasa a `Manual`.

Notas UI:
- Indicar que `RAINBOW`, `GRADIENT_WAVE` y `FIRE` **no** respetan el color base.

### 6.3 JSON JS

- `load`: poblar `single.*` desde API y resolver si coincide con un tema.
- `save`: enviar `single` en el JSON.
- Validación front mínima (0..255).

---

## 7) Validaciones (backend)

En `handle_config_post()`:
- `single.effect`: 0..11
- `single.speed`: 0..255
- `single.intensity`: 0..255
- `single.rgb`: 0..255

---

## 8) Pseudocódigo (nivel senior)

```
const MODE_SIMPLE = 2

function update_led_ui():
  if !LED_UI_ENABLED:
    return
  now = millis()

  if welcome.active:
    update_welcome(now)
    return

  if g_cfg.mode == MODE_SIMPLE:
    update_simple_mode(now)
    return

  // lógica actual (speed / geofence)
  ...

function update_simple_mode(now):
  if now - last_led_update_ms < LED_UPDATE_MS:
    return
  last_led_update_ms = now

  start = 0
  count = LED_STRIP_COUNT

  base = rgb(single_r, single_g, single_b)

  apply_effect(single_effect_id, leds_a, heat_a, start, count,
               base, single_speed, single_intensity, simple_state_a)

  if LED_STRIP_MODE == 2:
    apply_effect(single_effect_id, leds_b, heat_b, start, count,
                 base, single_speed, single_intensity, simple_state_b)

  show_leds()
```

---

## 9) Pruebas sugeridas

1) Seleccionar modo SIMPLE en portal y guardar.
2) Verificar que el efecto elegido se aplica correctamente en **toda la tira**.
3) Cambiar `speed/intensity` y confirmar respuesta visual.
4) Cambiar RGB y confirmar color base.
5) Probar `RAINBOW/GRADIENT_WAVE/FIRE` y validar nota de “color ignorado”.
6) Probar `LED_STRIP_MODE = 1` y `2`.

---

## 10) Próximos pasos

- Confirmar nombre y valores del **tema extra** (ej. “Aurora”).
- Si ok, implemento cambios en firmware + portal + docs.

