# Manual de colores (configuracion default)

Este manual explica que significa cada color segun el firmware activo en `Platformio/Dog-RGB` con la configuracion default.

---

## 1) Resumen rapido (como leer el collar)

- Segmento Estado (LEDs de estado): primeros 2 LEDs de cada tira (LED 0-1).
- Segmento Resto (el resto de la tira): del LED 2 al final (LED 2-23).
- Brillo global default: 77 (~30%).
- El estado (Segmento Estado) siempre tiene prioridad.
- El Segmento Resto muestra rainbow si no hay GPS fix, y usa rangos cuando hay GPS OK.

---

## 2) Estados del sistema (Segmento Estado)

Si ves estos colores en los primeros LEDs, el estado es:

- Azul fijo: GPS OK, sin Wi-Fi conectado.
- Azul pulsante (~1.5 s): GPS buscando (sin fix).
- Verde fijo: Wi-Fi STA conectado.
- Verde pulsante (~1.5 s): Wi-Fi STA intentando conectar.
- Amarillo fijo: AP activo (sin STA conectado).
- Rojo fijo (Segmento Estado): credenciales guardadas, STA fallo y quedo en AP (fallback).
- Rojo parpadeo rapido (Segmento Estado, 200 ms): error critico (sin GPS ni Wi-Fi por >10 min).

Notas:
- Durante intento STA con AP+STA activo, no hay indicador exclusivo de "conectando"; puede verse el estado GPS.
- El modo AP abierto (sin password) usa el mismo color que AP normal (amarillo).

---

## 3) Velocidad -> color (Segmento Resto)

Si no hay GPS fix, el Segmento Resto muestra un rainbow animado.

Con GPS OK, se usan los rangos por defecto (km/h) y color base (RGB):

- 0.0 - 2.0  : Azul            (0, 0, 60)
- 2.0 - 4.0  : Azul/Violeta    (10, 0, 60)
- 4.0 - 6.0  : Violeta         (20, 0, 60)
- 6.0 - 8.0  : Violeta intenso (30, 0, 60)
- 8.0 - 12.0 : Magenta frio    (40, 0, 60)
- 12.0 - 16.0: Magenta         (50, 0, 50)
- 16.0 - 22.0: Magenta/Naranja (60, 0, 40)
- 22.0 - 28.0: Naranja tenue   (60, 0, 30)
- 28.0 - 34.0: Naranja         (60, 0, 20)
- > 34.0     : Rojo            (60, 0, 0)

El color base define el "tono" general por rango; algunos efectos (RAINBOW, FIRE) no mantienen un color fijo.
Nota: el firmware descarta picos por encima de 40 km/h (SPEED_MAX_VALID_KPH).

---

## 4) Efectos default por rango (tira A / tira B)

- R1 (<=2.0): SOLID / PULSE (speed 40, intensity 80)
- R2 (<=4.0): PULSE / BREATH (58, 95)
- R3 (<=6.0): BREATH / CHASE (76, 110)
- R4 (<=8.0): CHASE / COMET (94, 125)
- R5 (<=12.0): SINELON / CONFETTI (112, 140)
- R6 (<=16.0): JUGGLE / BPM (130, 155)
- R7 (<=22.0): BPM / RAINBOW (148, 170)
- R8 (<=28.0): RAINBOW / GRADIENT_WAVE (166, 180)
- R9 (<=34.0): GRADIENT_WAVE / COMET (184, 190)
- R10 (>34.0): FIRE / CHASE (200, 200)

---

## 5) Defaults relevantes

- LED_STRIP_MODE = 2 (doble tira)
- LED_STRIP_COUNT = 24 (LEDs por tira)
- LED_STATUS_COUNT = 2 (LEDs reservados para estado)
- LED_BRIGHTNESS = 77 (~30%)
- SPEED_RANGE_1..9 = 2.0 / 4.0 / 6.0 / 8.0 / 12.0 / 16.0 / 22.0 / 28.0 / 34.0 km/h

---

## 6) Si cambias la configuracion

Si ajustas `LED_STRIP_COUNT`, `LED_STATUS_COUNT` o los rangos de velocidad en `Platformio/Dog-RGB/include/config.h`, este manual deja de ser exacto. Recomiendo actualizar esta tabla cuando cambies defaults.
