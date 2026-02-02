# Manual de colores (configuracion default)

Este manual explica que significa cada color segun el firmware activo en `Platformio/Dog-RGB` con la configuracion default.

---

## 1) Resumen rapido (como leer el collar)

- Segmento Estado (LEDs de estado): primeros 2 LEDs de cada tira (LED 0-1).
- Segmento Resto (el resto de la tira): del LED 2 al final (LED 2-23).
- Brillo global default: 77 (~30%).
- El estado (Segmento Estado) siempre tiene prioridad.
- El Segmento Resto solo se enciende si hay GPS OK (fix).

---

## 2) Estados del sistema (Segmento Estado o toda la tira)

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

Rangos por defecto (km/h) y color base (RGB):

- 0.0 - 2.0  : Azul           (0, 0, 60)
- 2.0 - 6.0  : Azul/Violeta   (20, 0, 60)
- 6.0 - 12.0 : Morado         (40, 0, 60)
- 12.0 - 20.0: Magenta/Naranja(60, 0, 40)
- 20.0 - 30.0: Naranja        (60, 0, 20)
- > 30.0     : Rojo           (60, 0, 0)

El color base define el "tono" general por rango; algunos efectos (RAINBOW, FIRE) no mantienen un color fijo.

---

## 4) Efectos default por rango (tira A / tira B)

- R1 (<=2.0): SOLID / PULSE (speed 40, intensity 80)
- R2 (<=6.0): PULSE / CHASE (60, 100)
- R3 (<=12.0): CONFETTI / SINELON (80, 120)
- R4 (<=20.0): JUGGLE / BPM (110, 150)
- R5 (<=30.0): RAINBOW / COMET (140, 180)
- R6 (>30.0): FIRE / CHASE (170, 200)

---

## 5) Defaults relevantes

- LED_STRIP_MODE = 2 (doble tira)
- LED_STRIP_COUNT = 24 (LEDs por tira)
- LED_STATUS_COUNT = 2 (LEDs reservados para estado)
- LED_BRIGHTNESS = 77 (~30%)
- SPEED_RANGE_1..5 = 2.0 / 6.0 / 12.0 / 20.0 / 30.0 km/h

---

## 6) Si cambias la configuracion

Si ajustas `LED_STRIP_COUNT`, `LED_STATUS_COUNT` o los rangos de velocidad en `Platformio/Dog-RGB/include/config.h`, este manual deja de ser exacto. Recomiendo actualizar esta tabla cuando cambies defaults.
