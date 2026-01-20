# LED Effects Plan (FastLED)

Este documento define el plan para usar FastLED y seleccionar efectos por rango de velocidad. No incluye codigo.

---

## Objetivo

- Usar FastLED por su variedad y calidad de efectos.
- Asignar un efecto por rango de velocidad.
- Permitir efectos distintos en tira A y tira B.
- Mantener Segmento A (estado) separado del Segmento B (modo normal).

---

## Libreria elegida

- FastLED (mas efectos y control que Adafruit NeoPixel).

---

## Catalogo de efectos (base)

Lista inicial de efectos disponibles para asignar:

- SOLID
- PULSE
- BREATH
- CHASE
- COMET (gusanito)
- SINELON
- CONFETTI
- JUGGLE
- BPM
- RAINBOW
- FIRE
- GRADIENT_WAVE

---

## Rango de velocidad -> efecto

Se define un mapeo para cada rango. Cada rango tiene:

- effect_A: efecto para tira A
- effect_B: efecto para tira B
- palette/color: paleta base
- speed: velocidad del efecto
- intensity: intensidad del efecto

Ejemplo de estructura (no definitivo):

- Rango 1:
  - A=SOLID, B=BREATH
  - palette=Blue
  - speed=low, intensity=low
- Rango 2:
  - A=PULSE, B=CHASE
  - palette=Blue/Violet
  - speed=low, intensity=mid
- Rango 3:
  - A=CONFETTI, B=SINELON
  - palette=Purple
  - speed=mid, intensity=mid
- Rango 4:
  - A=JUGGLE, B=BPM
  - palette=Magenta/Orange
  - speed=mid, intensity=mid-high
- Rango 5:
  - A=RAINBOW, B=COMET
  - palette=Orange
  - speed=high, intensity=high
- Rango 6:
  - A=FIRE, B=CHASE
  - palette=Red
  - speed=high, intensity=high

---

## Parametros configurables

Para cada rango:
- effect_A
- effect_B
- palette
- speed
- intensity

Parametros globales:
- FPS objetivo (ej. 50-60)
- limite de brillo
- limites de potencia

---

## Logica de prioridad

1) Estados criticos (Segmento A o override total)
2) Modo normal (Segmento B con efectos)

Segmento A siempre conserva estados GPS/Wi-Fi.

---

## Notas de implementacion

- FastLED permite paletas y control fino por frame.
- Cada tira mantiene su propio estado (fase, offset, etc.).
- Segmento B puede dividirse en dos mitades para simetria.

---

## Siguiente paso

- Definir el mapeo final de efectos por rango.
- Elegir paletas definitivas.
- Migrar firmware de Adafruit NeoPixel a FastLED.
