# Plan Modo SHOW (Demo de Efectos)

> **Document status:** Historical implementation plan (Spanish). Show mode is implemented and has evolved beyond this proposal; notably, its current cadence and shuffle behavior are documented in [LED UI](led_ui_spec.md).

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
- **Los LEDs de estado están activos** (Segmento A), **salvo** cuando homogeneous mode está activo (pisará toda la tira).

---

## 3) Decisiones confirmadas (por usuario)

- Activación **solo desde el portal** `/config` (sin atajos físicos por ahora).
- Modo por defecto al boot: **speed**.
- **Welcome** siempre corre antes de cualquier modo.
- Brillo respeta `g_cfg.brightness` (no forzar brillo).
- SHOW usa **el mismo color base** en ambas tiras.
- Segmento A muestra estado Wi‑Fi/GPS con la lógica actual **excepto** cuando homogeneous está activo.
- **Homogeneous mode debe seguir funcionando** incluso cuando SHOW está activo y **puede pisar status**.
- Si `LED_STATUS_COUNT == 0`, SHOW usa toda la tira (fallback defensivo).
- Si `POST /api/config` no incluye `mode`, se conserva el valor actual.
- Se actualizan docs (`docs/led_ui_spec.md` y `docs/portal_config.md`).
- Selector de modo en portal `/config` y soporte en `/api/config`.
- Subir payload de config a **version 3**.

Confirmación adicional (recibida):
- Se permiten resets **solo de estado LED** (buffers/efectos), **sin tocar** métricas GPS (distancia, velocidad, etc.).

---

## 4) Diseño propuesto (sin implementar)

### 4.1 Nuevas constantes

- En `config.h`:
  - `MODE_SHOW = 2` (o siguiente ID libre).
  - `EFFECT_COUNT = 12` (IDs 0..11) para evitar “magic numbers”.
  - `SHOW_EFFECT_MS = 15000`.
  - `SHOW_SPEED = 150` (balance entre fluidez y legibilidad).
  - `SHOW_INTENSITY = 200` (vivo sin saturar consumo).

### 4.2 Estado runtime adicional

Variables sugeridas en `main.cpp`:

- `static uint8_t show_effect_id = 0;`
- `static unsigned long show_effect_since_ms = 0;`
- `static Rgb show_base = make_rgb(...);`
- `static EffectState show_state_a = {};`
- `static EffectState show_state_b = {};`
- `static bool show_first_tick = true;`
- `static uint8_t last_mode = MODE_SPEED;` (para detectar entrada/salida de SHOW)

Notas:
- `show_state_*` para mantener `hue`/`pos` por efecto.
- `show_first_tick` permite inicializar en la primera entrada al modo.
- Se reutilizan los arrays `heat_a` y `heat_b` para el efecto FIRE.
- `last_mode` permite re‑inicializar SHOW cuando el usuario cambia de modo desde el portal.

### 4.3 Selección de color aleatorio

Se recomienda generar color en HSV para evitar colores muy oscuros:

- `hue = random8(0, 255)`
- `sat = random8(200, 255)`
- `val = random8(180, 255)`
- `show_base = hsv_to_rgb(hue, sat, val)`

Opcional (calidad): sembrar PRNG en `setup()` si se desea mayor variación
de colores en cada boot (p.ej. `randomSeed(esp_random())` o `randomSeed(micros())`).

Para efectos que **ignoran base** (RAINBOW, GRADIENT_WAVE, FIRE):
- Si se mantiene “no reset”, los efectos continuarán su fase/hue previa; esto es aceptable.
- Opcional (solo en primer arranque de SHOW): randomizar `show_state_a.hue` y `show_state_b.hue` para variar el inicio.
- FIRE usa su propia paleta; si se necesita color random real, habría que extender `apply_fire` (fuera del alcance actual).

---

## 5) Integración en la lógica LED

### 5.1 Entrada al modo SHOW

Agregar un early‑return en `update_led_ui()`:

```
if (g_cfg.mode == MODE_SHOW) {
  update_show_mode(now_ms);
  return;
}
```

Esto evita que el modo normal sobreescriba la demo. **Los LEDs de estado (Segmento A) se siguen pintando** dentro de `update_show_mode()` para mantener visibilidad de Wi‑Fi/GPS, **salvo cuando homogeneous mode pisa toda la tira**.

Detección de cambio de modo (para re‑inicializar SHOW):
- Si `g_cfg.mode != last_mode` y `g_cfg.mode == MODE_SHOW`, setear `show_first_tick = true`.
- Actualizar `last_mode = g_cfg.mode` al final de `update_led_ui()` (o en `loop()`).

### 5.2 Reseteo entre efectos

Cada 15s:
- Avanzar `show_effect_id` (0..`EFFECT_COUNT-1`, wrap).
- Generar un nuevo `show_base`.
- **No resetear** `show_state_a` / `show_state_b` (estado continuo entre efectos).
- Limpiar `heat_a`/`heat_b` al entrar a FIRE para evitar “arrastre” visual.
- Opcional: limpiar `leds_a`/`leds_b` para “corte limpio”.

Notas de seguridad:
- Estos resets son **solo de estado LED**.
- **No** deben tocar métricas GPS (`total_distance_m`, `max_speed_kph`, `active_time_ms`, etc.).

### 5.3 Refactor mínimo recomendado

Para reutilizar lógica existente:
- Extraer el cálculo de **status LEDs** a `paint_status_leds(now)` desde `update_led_ui()`.
- Extraer el cálculo de `gps_fix_ms` a `update_gps_fix_timer(now)` para que homogeneous funcione igual en SHOW.

---

## 6) Pseudocódigo (nivel senior)

```
const SHOW_EFFECT_MS = 15000
const SHOW_SPEED = 150
const SHOW_INTENSITY = 200
const EFFECT_COUNT = 12

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
    last_mode = g_cfg.mode

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
    show_state_a = {}
    show_state_b = {}

  // cambiar de efecto cada 15s
  if now - show_effect_since_ms >= SHOW_EFFECT_MS:
    show_effect_id = (show_effect_id + 1) % EFFECT_COUNT
    show_effect_since_ms = now
    show_base = random_color()
    maybe_reset_show_state()

  // refresco LED por tick
  if now - last_led_update_ms < LED_UPDATE_MS:
    return
  last_led_update_ms = now

  // actualizar temporizador de GPS fix (para homogeneous)
  update_gps_fix_timer(now)

  // 1) Homogeneous mode: pisa toda la tira
  if wifi_off && gps_fix_ms >= WIFI_OFF_GPS_FIX_MS:
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT,
                 show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_a)
    if LED_STRIP_MODE == 2:
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT,
                   show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_b)
    show_leds()
    return

  // 2) Segmento B: demo de efectos (desde LED_STATUS_COUNT hasta fin)
  seg_start = LED_STATUS_COUNT
  seg_count = LED_STRIP_COUNT - LED_STATUS_COUNT
  if seg_count > 0:
    apply_effect(show_effect_id, leds_a, heat_a, seg_start, seg_count,
                 show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_a)
    if LED_STRIP_MODE == 2:
      apply_effect(show_effect_id, leds_b, heat_b, seg_start, seg_count,
                   show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_b)
  else:
    // fallback defensivo si no hay LEDs de estado
    apply_effect(show_effect_id, leds_a, heat_a, 0, LED_STRIP_COUNT,
                 show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_a)
    if LED_STRIP_MODE == 2:
      apply_effect(show_effect_id, leds_b, heat_b, 0, LED_STRIP_COUNT,
                   show_base, SHOW_SPEED, SHOW_INTENSITY, show_state_b)

  // 3) Segmento A: siempre status Wi‑Fi/GPS (misma lógica actual)
  paint_status_leds(now)

  show_leds()

function random_color():
  hue = random8(0, 255)
  sat = random8(200, 255)
  val = random8(180, 255)
  return hsv_to_rgb(hue, sat, val)

function maybe_reset_show_state():
  // Usuario pidió “no reset” entre efectos; se deja el estado vivo.
  // Reset mínimo para asegurar FIRE estable:
  if show_effect_id == FIRE:
    clear(heat_a)
    clear(heat_b)
  // Para efectos con hue interno, opcionalmente randomizar arranque:
  // show_state_a.hue = random8()
  // show_state_b.hue = random8()

function update_gps_fix_timer(now):
  if last_gps_fix_ms == 0:
    last_gps_fix_ms = now
  dt = now - last_gps_fix_ms
  last_gps_fix_ms = now
  if has_gps_fix:
    gps_fix_ms = min(gps_fix_ms + dt, WIFI_OFF_GPS_FIX_MS)
  else:
    gps_fix_ms = 0

function paint_status_leds(now):
  // Extraer la lógica de estado Wi‑Fi/GPS de update_led_ui()
  // (incluye last_ok_ms, critical_error, colores y el pinteo del Segmento A).
  ...
```

---

## 7) Configuración y control (portal/AP)

### 7.1 API JSON (version 3)

**GET /api/config** debe incluir `mode` y reportar versión 3:

```
{
  "version": 3,
  "mode": "speed", // "speed" | "geofence" | "show"
  "led": {"brightness": 77},
  "speed_ranges_kph": [...],
  "effects": {...},
  "wifi": {...}
}
```

**POST /api/config** acepta `mode` (opcional):

```
{
  "version": 3,
  "mode": "show",
  ...
}
```

Reglas:
- Si `mode` no viene, **conservar** `g_cfg.mode`.
- `mode` válido: `speed`, `geofence`, `show`.
- Si `mode` es inválido: `{"status":"error","reason":"mode"}`.

Nota técnica: hoy `doc["version"]` usa `CONFIG_VERSION` (NVS) y la UI manda `version:2`.
En este plan se **alinea** a `version:3` en API/UI. Si se quiere separar versiones,
definir un `API_VERSION = 3`.

### 7.2 UI `/config` (selector de modo)

Agregar un selector simple:
- Label: “Modo”
- `<select id="mode">` con opciones `speed`, `geofence`, `show`
- En `load`:
  - `mode.value = c.mode || "speed"`
- En `saveCfg()`:
  - incluir `mode: mode.value` en el JSON

### 7.3 Server‑side (main.cpp)

Cambios propuestos:
- `MODE_SHOW = 2` en `config.h`.
- `mode_name()` agrega `"show"`.
- `parse_mode()` acepta `"show"`.
- `validate_mode()` acepta `MODE_SHOW`.
- `handle_config_get()` añade `doc["mode"] = mode_name(g_cfg.mode)`.
- `handle_config_post()`:
  - leer `mode` si existe y validarlo.
  - si falta `mode`, mantener valor actual.

### 7.4 Compatibilidad

- `POST /api/config` sin `mode` debe seguir funcionando (compat).
- Si se recibe `"version":2`, se ignora y se procesa igual (no bloquear).

---

## 8) Estado actual del sistema de modos (lectura del repo)

### 8.1 Qué existe hoy

- En `config.h` existen dos modos definidos:
  - `MODE_SPEED = 0`
  - `MODE_GEOFENCE = 1`
- En `RuntimeConfig` existe el campo `mode` y se persiste en NVS (`dogrgb_cfg`).
- Hay funciones utilitarias:
  - `mode_name(mode)` devuelve `\"speed\"` o `\"geofence\"`.
  - `parse_mode(value, mode_out)` parsea los strings `speed` y `geofence`.
  - `validate_mode(mode)` valida si está en {0,1}.
- `load_config()` lee `mode` desde NVS y lo valida, pero **en la lógica LED actual no se utiliza**.

### 8.2 Qué NO existe hoy

- No hay UI ni API para seleccionar `mode` en `/config` ni en `/api/config`.
- No hay ramas en `update_led_ui()` que dependan de `g_cfg.mode`.
- Por lo tanto, **el modo no cambia el comportamiento real** en el firmware actual.

### 8.3 Implicación para SHOW

Para activar SHOW, hay que **usar de verdad** `g_cfg.mode` en `update_led_ui()` e incorporar la opción en el portal o un método alterno (p.ej. hardcode temporal). El plan asume que se usa `g_cfg.mode` como switch central.

---

## 9) Consideraciones de calidad y UX

- **Status LEDs:** en SHOW se mantienen activos en Segmento A, **excepto** cuando homogeneous mode está activo (pisará toda la tira).
- **Brillo:** mantener `g_cfg.brightness` para evitar picos de consumo.
- **Random color:** evitar colores muy oscuros o casi blancos para claridad visual.
- **Efectos especiales:**
  - RAINBOW y GRADIENT_WAVE no usan `base`; inicializar `hue` aleatorio para variar.
  - FIRE usa su propio mapa de calor; la variación aleatoria se logra por estado inicial (opcional). Si se necesita “fire color random”, se requerirá extender `apply_fire`.

---

## 10) Pruebas sugeridas (manual)

1. Activar modo SHOW desde portal.
2. Verificar que cada 15s cambia el efecto en **ambas** tiras.
3. Validar que el color base cambia cada cambio de efecto.
4. Confirmar que `LED_STATUS_COUNT` no sobreescribe la demo (salvo homogeneous).
5. Verificar comportamiento con `LED_STRIP_MODE = 1` y `2`.
6. Confirmar que el resto del sistema (GPS/Wi‑Fi) sigue funcionando sin interferencias.
7. Forzar condición de homogeneous (Wi‑Fi off + GPS fix estable) y verificar que **pisan** status en SHOW.

---

## 11) Próximos pasos

Si estás de acuerdo con este diseño, puedo:
- Implementar el modo SHOW en firmware.
- Extender el portal `/config` y el JSON runtime para seleccionar el modo.
- Actualizar docs (`docs/led_ui_spec.md` y `docs/portal_config.md`).
