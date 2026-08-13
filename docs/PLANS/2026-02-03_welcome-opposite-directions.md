# Plan: Bienvenida con direcciones opuestas (cintas A/B)

> **Document status:** Historical LED plan (Spanish). Treat the current `led_ui` implementation and [LED UI reference](../led_ui_spec.md) as authoritative.

## Resumen

Se propone una nueva rutina de bienvenida que mantiene el efecto actual (CHASE), pero con movimiento en direcciones contrarias entre la cinta A y la cinta B. No hay cambios de UI ni de configuración; el cambio es solo en la animación de bienvenida.

## Estado actual (repo)

- La bienvenida se ejecuta con `start_welcome()` / `update_welcome()` en `Platformio/Dog-RGB/src/main.cpp`.
- La animación usa el efecto **CHASE** (`apply_effect(3, ...)`) con `WELCOME_SPEED`, `WELCOME_INTENSITY` y `WELCOME_COLORS`.
- Ambas cintas (A y B) avanzan en el **mismo** sentido porque `apply_effect` incrementa `state.pos` hacia adelante.
- El conteo de vueltas (`WELCOME_LAPS`) se basa en el wrap de `welcome_state_a.pos`.

## Objetivo

- Mantener la bienvenida igual en color, brillo, velocidad, laps y duración.
- **Solo** invertir la dirección en cinta B: A avanza en sentido normal, B avanza en sentido contrario.
- En `LED_STRIP_MODE != 2` (una sola cinta), el comportamiento actual no cambia.

## Diseño propuesto (sin código)

### 1) Implementar un “chase con dirección” exclusivo para la bienvenida

Opciones viables:

1. **Helper específico para welcome** (recomendado, mínimo impacto):
   - Nueva función local (o inline en `update_welcome`) que haga lo mismo que el case CHASE, pero con un parámetro `reverse`.
   - Mecanismo: `state.pos` siempre incrementa hacia adelante; el índice que se pinta se invierte si `reverse`.
   - Fórmula: `write_pos = reverse ? (count - 1 - state.pos) : state.pos`.

2. **Extender `apply_effect` con dirección** (mayor alcance):
   - Agregar un parámetro opcional de dirección para CHASE/COMET.
   - Riesgo: toca el API y afecta a otros usos del motor de efectos.

**Recomendación:** usar la opción 1 para limitar el cambio al flujo de bienvenida.

### 2) Actualizar `update_welcome()`

- Cinta A: aplicar CHASE en sentido normal con `welcome_state_a`.
- Cinta B: aplicar CHASE en sentido inverso con `welcome_state_b`.
- Mantener `WELCOME_COLORS`, `WELCOME_SPEED`, `WELCOME_INTENSITY`, y el gating por `LED_UPDATE_MS`.
- Mantener el conteo de vueltas basado en **A** (no requiere cambios).

### 3) Mantener compatibilidad con modos existentes

- `start_welcome()` sigue limpiando las tiras y fuerza brillo 255 solo durante la bienvenida.
- El fin de bienvenida sigue restaurando `g_cfg.brightness` y limpiando tiras.
- No toca `show` ni `simple` ni el motor de efectos en modo normal.

## Pseudocódigo de referencia

```text
function update_welcome(now):
  if now - last_led_update_ms < LED_UPDATE_MS:
    return
  last_led_update_ms = now

  base = WELCOME_COLORS[welcome.color_index]
  prev_pos = welcome_state_a.pos

  // A: forward
  apply_welcome_chase(leds_a, base, welcome_state_a, reverse=false)

  // B: reverse (solo si hay 2 tiras)
  if LED_STRIP_MODE == 2:
    apply_welcome_chase(leds_b, base, welcome_state_b, reverse=true)

  show_leds()

  if welcome_state_a.pos < prev_pos:
    welcome.laps_done++
    if welcome.laps_done >= WELCOME_LAPS:
      finish_welcome()
    else:
      welcome.color_index++

function apply_welcome_chase(leds, base, state, reverse):
  fade_range(...)
  state.pos = (state.pos + step_from_speed(WELCOME_SPEED, 32)) % count
  write_pos = reverse ? (count - 1 - state.pos) : state.pos
  leds[start + write_pos] = base
```

## Criterios de aceptación

- En boot, la bienvenida muestra un “punto en carrera” (CHASE) en ambas cintas.
- La cinta A avanza en un sentido; la cinta B avanza en el sentido contrario.
- Colores, brillo, velocidad y número de vueltas se mantienen iguales a la bienvenida actual.
- En modo de una sola tira (`LED_STRIP_MODE != 2`), la bienvenida se comporta igual que antes.

## Riesgos y notas

- “Dirección opuesta” se define por índice: A incrementa, B decrementa. Si el montaje físico invierte alguna cinta, la percepción visual podría requerir ajustar la fórmula (se revisa con prueba en hardware).
- No se propone modificar el catálogo de efectos ni la configuración runtime.
