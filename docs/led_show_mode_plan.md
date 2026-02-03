# Plan Modo SHOW (Demo de Efectos)

Este documento propone un nuevo modo **SHOW** para el firmware LED, pensado como demo visual: recorre todos los efectos existentes y los muestra en ambas tiras cada 15 segundos, con colores aleatorios. **No implementa cambios**; es un plan con pseudocódigo y puntos de integración.

---

## 1) Estado actual (resumen técnico)

**Entrypoint:** `Platformio/Dog-RGB/src/main.cpp`.

**Flujo LED actual:**
- `update_led_ui()` se ejecuta en cada `loop()` y actualiza LEDs cada `LED_UPDATE_MS`.
- Hay tres “capas” principales:
  - **Welcome**: animación inicial (`start_welcome()` / `update_welcome()`).
  - **Modo normal**: Segmento A = estado Wi‑Fi/GPS; Segmento B = efecto por rango de velocidad.
  - **Homogeneous**: si Wi‑Fi está off y GPS OK estable > `WIFI_OFF_GPS_FIX_MS`, aplica el efecto a **toda la tira**, incluido el segmento A.
- Los efectos se aplican con `apply_effect(effect_id, ...)` y usan `Rgb base` + `speed` + `intensity`.
- Catálogo de efectos definido en `docs/led_effects.md` (IDs 0..11).

**Configuración runtime:**
- `RuntimeConfig g_cfg` persistida en NVS (`dogrgb_cfg`).
- Config actual (portal) **no expone** modo LED. El campo `mode` existe a nivel firmware (`g_cfg.mode`) pero **no se usa** en la lógica LED actual.

---

## 2) Objetivo del nuevo modo SHOW

- **Recorrer todos los efectos** disponibles (IDs `0..11`).
- **Duración por efecto:** 15 segundos.
- **Ambas tiras LED** (A y B) deben mostrar **el mismo efecto** al mismo tiempo.
- **Color aleatorio** por efecto (base RGB aleatoria).
- **Los LEDs de estado SIEMPRE están activos** (Segmento A). La demo afecta solo el **Segmento B**.

---

## 3) Diseño propuesto (sin implementar)

### 3.1 Nuevas constantes

- En `config.h`:
  - `MODE_SHOW = 2` (o siguiente ID libre).
  - `SHOW_EFFECT_MS = 15000`.
  - Opcional: `SHOW_SPEED` y `SHOW_INTENSITY` (valores medios, p.ej. 140 y 200).

### 3.2 Estado runtime adicional

Variables sugeridas en `main.cpp`:

- `static uint8_t show_effect_id = 0;`
- `static unsigned long show_effect_since_ms = 0;`
- `static Rgb show_base = make_rgb(...);`
- `static EffectState show_state_a = {};`
- `static EffectState show_state_b = {};`
- `static bool show_first_tick = true;`

Notas:
- `show_state_*` para mantener `hue`/`pos` por efecto.
- `show_first_tick` permite inicializar en la primera entrada al modo.
- Se reutilizan los arrays `heat_a` y `heat_b` para el efecto FIRE.

### 3.3 Selección de color aleatorio

Se recomienda generar color en HSV para evitar colores muy oscuros:

- `hue = random8(0, 255)`
- `sat = random8(200, 255)`
- `val = random8(200, 255)`
- `show_base = hsv_to_rgb(hue, sat, val)`

Para efectos que **ignoran base** (RAINBOW, GRADIENT_WAVE, FIRE):
- Randomizar `show_state_a.hue` y `show_state_b.hue` al cambiar de efecto para “variar” el inicio.
- FIRE usa su propia paleta; si se necesita color random real, habría que extender `apply_fire` (fuera del alcance actual).

---

## 4) Integración en la lógica LED

### 4.1 Entrada al modo SHOW

Agregar un early‑return en `update_led_ui()`:

```
if (g_cfg.mode == MODE_SHOW) {
  update_show_mode(now_ms);
  return;
}
```

Esto evita que el modo normal sobreescriba la demo. **Los LEDs de estado (Segmento A) se siguen pintando** dentro de `update_show_mode()` para mantener visibilidad de Wi‑Fi/GPS.

### 4.2 Reseteo entre efectos

Cada 15s:
- Avanzar `show_effect_id` (0..11, wrap).
- Generar un nuevo `show_base`.
- Resetear `show_state_a`, `show_state_b`.
- Limpiar `heat_a`/`heat_b` si el próximo efecto es FIRE.
- Opcional: limpiar `leds_a`/`leds_b` para “corte limpio”.

---

## 5) Pseudocódigo (nivel senior)

```
const SHOW_EFFECT_MS = 15000
const SHOW_SPEED = 140
const SHOW_INTENSITY = 200

function update_led_ui():
  if !LED_UI_ENABLED:
    return
  now = millis()

  if welcome.active:
    update_welcome(now)
    return

  if g_cfg.mode == MODE_SHOW:
    update_show_mode(now)
    return

  // lógica existente: status + segment B + homogeneous
  ...

function update_show_mode(now):
  if show_first_tick:
    show_first_tick = false
    show_effect_id = 0
    show_effect_since_ms = now
    show_base = random_color()
    reset_show_state()

  // cambiar de efecto cada 15s
  if now - show_effect_since_ms >= SHOW_EFFECT_MS:
    show_effect_id = (show_effect_id + 1) % 12
    show_effect_since_ms = now
    show_base = random_color()
    reset_show_state()

  // refresco LED por tick
  if now - last_led_update_ms < LED_UPDATE_MS:
    return
  last_led_update_ms = now

  // 1) Segmento B: demo de efectos (desde LED_STATUS_COUNT hasta fin)
  seg_start = LED_STATUS_COUNT
  seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT
  if seg_count > 0:
    apply_effect(show_effect_id, leds_a, heat_a, seg_start, seg_count,
                 show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_a)
    if LED_STRIP_MODE == 2:
      apply_effect(show_effect_id, leds_b, heat_b, seg_start, seg_count,
                   show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_b)

  // 2) Segmento A: siempre status Wi‑Fi/GPS (misma lógica actual)
  paint_status_leds(now)

  show_leds()

function random_color():
  hue = random8(0, 255)
  sat = random8(200, 255)
  val = random8(200, 255)
  return hsv_to_rgb(hue, sat, val)

function reset_show_state():
  show_state_a = {}
  show_state_b = {}
  if show_effect_id == FIRE:
    clear(heat_a)
    clear(heat_b)
  // Para efectos que usan hue interno:
  show_state_a.hue = random8()
  show_state_b.hue = random8()
```

---

## 6) Configuración y control (propuesta)

Actualmente `mode` no está en el portal. Opciones:

1. **Agregar selector de modo en /config**
   - `speed`, `geofence`, `show`
   - Persistir en NVS como `mode` (ya existe `g_cfg.mode`).
   - Actualizar `GET /api/config` y `POST /api/config` para incluir `mode`.

2. **Modo SHOW “hardcodeado”**
   - Útil para demos internas, pero no es ideal para usuario final.

Recomendación: **Opción 1** para control desde el portal.

---

## 7) Estado actual del sistema de modos (lectura del repo)

### 7.1 Qué existe hoy

- En `config.h` existen dos modos definidos:
  - `MODE_SPEED = 0`
  - `MODE_GEOFENCE = 1`
- En `RuntimeConfig` existe el campo `mode` y se persiste en NVS (`dogrgb_cfg`).
- Hay funciones utilitarias:
  - `mode_name(mode)` devuelve `\"speed\"` o `\"geofence\"`.
  - `parse_mode(value, mode_out)` parsea los strings `speed` y `geofence`.
  - `validate_mode(mode)` valida si está en {0,1}.
- `load_config()` lee `mode` desde NVS y lo valida, pero **en la lógica LED actual no se utiliza**.

### 7.2 Qué NO existe hoy

- No hay UI ni API para seleccionar `mode` en `/config` ni en `/api/config`.
- No hay ramas en `update_led_ui()` que dependan de `g_cfg.mode`.
- Por lo tanto, **el modo no cambia el comportamiento real** en el firmware actual.

### 7.3 Implicación para SHOW

Para activar SHOW, hay que **usar de verdad** `g_cfg.mode` en `update_led_ui()` e incorporar la opción en el portal o un método alterno (p.ej. hardcode temporal). El plan asume que se usa `g_cfg.mode` como switch central.

---

## 8) Consideraciones de calidad y UX

- **Status LEDs:** en SHOW **se mantienen siempre activos** (Segmento A). La demo corre en Segmento B para preservar telemetría visible.
- **Brillo:** mantener `g_cfg.brightness` para evitar picos de consumo.
- **Random color:** evitar colores muy oscuros o casi blancos para claridad visual.
- **Efectos especiales:**
  - RAINBOW y GRADIENT_WAVE no usan `base`; inicializar `hue` aleatorio para variar.
  - FIRE usa su propio mapa de calor; la variación aleatoria se logra por estado inicial (opcional). Si se necesita “fire color random”, se requerirá extender `apply_fire`.

---

## 9) Pruebas sugeridas (manual)

1. Activar modo SHOW desde portal.
2. Verificar que cada 15s cambia el efecto en **ambas** tiras.
3. Validar que el color base cambia cada cambio de efecto.
4. Confirmar que `LED_STATUS_COUNT` no sobreescribe la demo.
5. Verificar comportamiento con `LED_STRIP_MODE = 1` y `2`.
6. Confirmar que el resto del sistema (GPS/Wi‑Fi) sigue funcionando sin interferencias.

---

## 10) Próximos pasos

Si estás de acuerdo con este diseño, puedo:
- Implementar el modo SHOW en firmware.
- Extender el portal `/config` y el JSON runtime para seleccionar el modo.
- Actualizar docs (`docs/led_ui_spec.md` y `docs/portal_config.md`).
