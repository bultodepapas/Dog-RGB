# Plan Modo SIMPLE (Un Solo Efecto)

Este documento propone un modo **SIMPLE** para el firmware LED: un solo efecto configurable que se aplica de forma uniforme. Incluye plan de cambios y pseudocódigo. **No implementa cambios**.

---

## 1) Estado actual (resumen)

- `update_led_ui()` usa `g_cfg.mode` solo para `speed` y `geofence`.
- Los efectos se configuran **por rango** (10 rangos). Cada rango tiene:
  - `effect_a`, `effect_b`, `speed`, `intensity`.
- El color base se deriva del rango (`base_color_for_range()`), no es configurable desde el portal.
- El portal `/config` ya muestra un selector `mode` con `speed` y `geofence`.

Archivos relevantes:
- `Platformio/Dog-RGB/src/main.cpp` (LED UI, portal y API)
- `Platformio/Dog-RGB/include/config.h` (constantes y defaults)
- `docs/led_effects.md` (catálogo de efectos)
- `docs/portal_config.md` (JSON del portal)

---

## 2) Objetivo del modo SIMPLE

- Modo **SIMPLE** = un solo efecto para todo el LED (misma configuración para A/B).
- Configurable desde el portal AP.
- Se pueden seleccionar **todas las características del efecto**:
  - `effect_id` (0..11)
  - `speed` (0..255)
  - `intensity` (0..255)
  - `base_color` (RGB, 0..255)

Opcional (a confirmar):
- `full_strip`: aplicar el efecto a **toda** la tira (incluye LEDs de estado) o solo al segmento B.

---

## 3) Decisiones abiertas (confirmar)

1) **¿El modo SIMPLE debe ignorar los LEDs de estado?**
   - Opción A (más simple): aplica el efecto a toda la tira (similar a modo homogéneo).
   - Opción B: respeta LED0/LED1 de Wi‑Fi/GPS y aplica el efecto solo al cuerpo.

2) **Color base**
   - Propuesto: seleccionar RGB en la UI.
   - Alternativa mínima: usar el color base de un rango fijo (p.ej. rango 1). Esto reduce UI pero limita el control.

3) **Un solo efecto para ambas tiras**
   - Propuesto: un único `effect_id` para A/B (cumple “un solo efecto”).
   - Alternativa: permitir A/B distintos (no recomendado para “simple”).

---

## 4) Diseño propuesto (config y persistencia)

### 4.1 Config runtime (nuevo bloque)

Extender `RuntimeConfig` con un bloque de modo SIMPLE:

```
struct SingleEffectConfig {
  uint8_t effect_id;
  uint8_t speed;
  uint8_t intensity;
  uint8_t base_r;
  uint8_t base_g;
  uint8_t base_b;
  uint8_t full_strip; // opcional
};
```

### 4.2 JSON API (GET/POST)

Agregar sección `single`:

```
"single": {
  "effect": 3,
  "speed": 120,
  "intensity": 180,
  "rgb": {"r": 0, "g": 60, "b": 60},
  "full_strip": true // opcional
}
```

### 4.3 NVS (persistencia)

- **Subir `CONFIG_VERSION`** (p.ej. 4).
- Guardar campos individuales para evitar padding:
  - `single_eff`, `single_speed`, `single_intensity`, `single_r`, `single_g`, `single_b`, `single_full`.

### 4.4 Defaults sugeridos

- `effect_id = 0` (SOLID)
- `speed = 80`
- `intensity = 140`
- `base = (0, 60, 60)` (azul/verde suave)
- `full_strip = 1` (si se habilita)

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
- Si `full_strip == true`, usar `start=0` y `count=LED_STRIP_COUNT`.
- Si `full_strip == false`, respetar LEDs de estado y usar `start=LED_STATUS_COUNT`.

### 5.3 Estado y reseteo

- Usar `EffectState simple_state_a/b` o reutilizar `state_a/b`.
- Resetear estado cuando se cambia de modo o cuando se cambian parámetros clave.
- Para `FIRE`, limpiar `heat_a/heat_b` al entrar al modo o al cambiar `effect_id`.

---

## 6) UI del portal (AP)

### 6.1 Selector de modo

Agregar opción `simple` en el `<select id='mode'>`:
- `Velocidad`, `Geocerca`, `Simple`.

### 6.2 Bloque SIMPLE

Nuevo bloque visible solo en modo SIMPLE, con inputs:
- `effect_id` (0..11) — ideal: `<select>` con nombres (`SOLID`, `PULSE`, …).
- `speed` (0..255)
- `intensity` (0..255)
- `color`:
  - Opción simple: tres inputs numéricos `R/G/B`.
  - Opción UX: `<input type='color'>` + sincronización con R/G/B.
- `full_strip` (checkbox) si se incluye.

### 6.3 JSON JS

- En `load`, poblar `single.*`.
- En `save`, enviar `single` en el JSON.
- Validación front mínima (range 0..255).

---

## 7) Validaciones (backend)

Agregar validaciones en `handle_config_post()`:
- `single.effect`: 0..11
- `single.speed`: 0..255
- `single.intensity`: 0..255
- `single.rgb`: 0..255
- `single.full_strip`: 0/1 (si existe)

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
  if single_full_strip == false:
    start = LED_STATUS_COUNT
    count = LED_STRIP_COUNT - LED_STATUS_COUNT

  base = rgb(single_r, single_g, single_b)

  apply_effect(single_effect_id, leds_a, heat_a, start, count,
               base, single_speed, single_intensity, simple_state_a)

  if LED_STRIP_MODE == 2:
    apply_effect(single_effect_id, leds_b, heat_b, start, count,
                 base, single_speed, single_intensity, simple_state_b)

  if single_full_strip == false:
    render_status_leds() // misma lógica actual del Segmento A

  show_leds()
```

---

## 9) Pruebas sugeridas

1) Seleccionar modo SIMPLE en portal y guardar.
2) Verificar que el efecto elegido se aplica correctamente.
3) Cambiar `speed/intensity` y confirmar respuesta visual.
4) Cambiar RGB y confirmar color base.
5) Verificar que no interfiere con `speed`/`geofence` al volver a esos modos.
6) Probar con `LED_STRIP_MODE = 1` y `2`.

---

## 10) Próximos pasos

- Confirmar decisiones abiertas (estado LEDs, color base, full_strip).
- Si hay acuerdo, implemento cambios en firmware + portal + docs.

