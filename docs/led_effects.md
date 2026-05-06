# LED Effects (Runtime Engine)

Este documento describe los efectos disponibles y su uso actual en el firmware.

---

## Motor actual

- Libreria: Adafruit NeoPixel
- Implementacion: efectos custom en `Platformio/Dog-RGB/src/led/led_ui.cpp`
- Cada rango de velocidad define:
  - effect_A (tira A)
  - effect_B (tira B)
  - speed (0-255)
  - intensity (0-255)
- SHOW usa el mismo motor de efectos con constantes internas (`SHOW_SPEED`, `SHOW_INTENSITY`, `SHOW_EFFECT_MS`).

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

Defaults actuales (ver `Platformio/Dog-RGB/include/config.h`):
- R1 (<=2.0 km/h): JUGGLE / JUGGLE (speed 40, intensity 80)
- R2 (<=4.0 km/h): JUGGLE / JUGGLE (58, 95)
- R3 (<=6.0 km/h): JUGGLE / JUGGLE (76, 110)
- R4 (<=8.0 km/h): JUGGLE / JUGGLE (94, 125)
- R5 (<=10.0 km/h): JUGGLE / JUGGLE (112, 140)
- R6 (<=12.0 km/h): JUGGLE / JUGGLE (130, 155)
- R7 (<=14.0 km/h): JUGGLE / JUGGLE (148, 170)
- R8 (<=16.0 km/h): JUGGLE / JUGGLE (166, 180)
- R9 (<=18.0 km/h): JUGGLE / JUGGLE (184, 190)
- R10 (>18.0 km/h): JUGGLE / JUGGLE (200, 200)

---

## Modo SHOW

- Recorre los 12 efectos disponibles.
- Duracion por efecto: `SHOW_EFFECT_MS` (default 30 s).
- Usa un color base aleatorio por efecto cuando el efecto lo permite.
- El orden actual usa una bolsa barajada interna: no repite efectos hasta recorrer los 12.
- Al reiniciar la bolsa, evita que el primer efecto nuevo sea igual al ultimo efecto mostrado.
- Al entrar a SHOW, se baraja la bolsa y el primer efecto ya no esta fijado a SOLID.
- El color base sale de una paleta interna curada con variacion leve por canal.
- Durante cada efecto, el color base se interpola hacia un segundo color interno.
- Cada cambio de efecto usa fade-in/fade-out corto (`SHOW_TRANSITION_MS`, 500 ms).
- `speed` e `intensity` tienen variacion interna segura por efecto; FIRE usa una ventana propia.
- Ambas cintas reciben siempre el mismo efecto SHOW y los mismos parametros internos.
- Segmento B muestra la demo; Segmento A conserva Wi-Fi/GPS salvo modo homogeneo.
- En modo homogeneo, SHOW se aplica a toda la tira.

---

## Parametros

- `speed`: controla la velocidad del efecto (0-255)
- `intensity`: controla brillo/energia interna del efecto (0-255)
- `base`: color RGB base del efecto cuando aplica.

Notas sobre color base:
- SOLID, PULSE, BREATH, CHASE, COMET, SINELON, CONFETTI, JUGGLE y BPM usan el color base.
- RAINBOW y GRADIENT_WAVE generan color desde HSV interno y no respetan directamente el color base.
- FIRE usa `heat_color()` y no respeta directamente el color base.

---

## Configuracion runtime

- UI: `/config`
- API: `GET /api/config` y `POST /api/config`
- Validacion: effect id 0..11, speed/intensity 0..255

---

## Notas

- Segmento A (estado) es independiente del Segmento B.
- En modo homogeneo, el efecto se aplica a toda la tira.
- Para auditar SHOW en vivo, usar `/dev`; actualmente expone el efecto actual, pero no el color base ni el temporizador.
