# LED Effects (Runtime Engine)

Este documento describe los efectos disponibles y su uso actual en el firmware.

---

## Motor actual

- Libreria: Adafruit NeoPixel
- Implementacion: efectos custom en `Platformio/Dog-RGB/src/main.cpp`
- Cada rango de velocidad define:
  - effect_A (tira A)
  - effect_B (tira B)
  - speed (0-255)
  - intensity (0-255)

---

## Catalogo de efectos (IDs)

- 0: SOLID (color fijo)
- 1: PULSE (pulso suave)
- 2: BREATH (respiracion)
- 3: CHASE (carrera de un pixel)
- 4: COMET (cometa con cola)
- 5: SINELON (barra oscilante)
- 6: CONFETTI (destellos aleatorios)
- 7: JUGGLE (varios puntos en movimiento)
- 8: BPM (latido con brillo)
- 9: RAINBOW (arco iris animado)
- 10: FIRE (fuego procedimental)
- 11: GRADIENT_WAVE (onda con gradiente)

---

## Rango de velocidad -> efecto

- Se usan 10 rangos (1..10) definidos por 9 umbrales en km/h.
- Cada rango tiene efecto A/B + speed/intensity.
- El color base por rango esta en `docs/manual_de_colores.md`.

Defaults actuales (ver `config.h`):
- R1: SOLID / SOLID
- R2: PULSE / PULSE
- R3: BREATH / BREATH
- R4: CHASE / CHASE
- R5: SINELON / SINELON
- R6: JUGGLE / JUGGLE
- R7: BPM / BPM
- R8: RAINBOW / RAINBOW
- R9: GRADIENT_WAVE / GRADIENT_WAVE
- R10: FIRE / FIRE

---

## Parametros

- `speed`: controla la velocidad del efecto (0-255)
- `intensity`: controla brillo/energia interna del efecto (0-255)

---

## Configuracion runtime

- UI: `/config`
- API: `GET /api/config` y `POST /api/config`
- Validacion: effect id 0..11, speed/intensity 0..255

---

## Notas

- Segmento A (estado) es independiente del Segmento B.
- En modo homogeneo, el efecto se aplica a toda la tira.
