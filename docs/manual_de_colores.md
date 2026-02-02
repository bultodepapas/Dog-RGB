# Manual de colores (configuracion default)

Este manual explica que significa cada color segun el firmware activo en `Platformio/Dog-RGB` con la configuracion default.

---

## 1) Resumen rapido (como leer el collar)

- Segmento Estado (LEDs de estado): primeros 2 LEDs de cada tira (LED 0-1).
- Segmento Resto (el resto de la tira): del LED 2 al final (LED 2-23).
- Brillo global default: 77 (~30%).
- El estado (Segmento Estado) tiene prioridad, excepto en modo homogeneo (Wi-Fi OFF + GPS OK >5 min).
- El Segmento Resto muestra rainbow si no hay GPS fix, y usa rangos cuando hay GPS OK.

---

## 2) Estados del sistema (Segmento Estado)

LED0 = Wi-Fi/AP
- Verde fijo: STA conectado.
- Verde pulsante (~1.5 s): STA intentando conectar.
- Amarillo fijo: AP activo sin clientes.
- Amarillo pulsante suave: AP activo con clientes conectados.
- Rojo fijo: STA fallo y quedo en AP (fallback).
- Ambar doble pulso (cada ~3 s): Wi-Fi apagado por ahorro (sin STA/AP).

LED1 = GPS
- Azul fijo: GPS OK.
- Azul pulsante (~1.5 s): GPS buscando (sin fix).

Override critico:
- LED0 y LED1 rojo parpadeo rapido (200 ms) si no hay GPS ni Wi-Fi por >10 min.

Homogeneo:
- Si Wi-Fi esta OFF y GPS OK por >5 min, LED0 y LED1 toman el mismo color/efecto del Segmento Resto.

Notas:
- El modo AP abierto (sin password) usa el mismo color que AP normal (amarillo).

---

## 3) Velocidad -> color (Segmento Resto)

Si no hay GPS fix, el Segmento Resto muestra un rainbow animado (el tono avanza `LED_GPS_SEARCH_RAINBOW_STEP` por tick de `LED_UPDATE_MS`).

Con GPS OK, se usan los rangos por defecto (km/h) y color base (RGB):

- 0.0 - 2.0  : Cian (muy baja)       (0, 60, 60)
- 2.0 - 4.0  : Verde-cian           (0, 60, 35)
- 4.0 - 6.0  : Verde                (0, 60, 0)
- 6.0 - 8.0  : Verde-lima           (25, 60, 0)
- 8.0 - 12.0 : Amarillo             (60, 60, 0)
- 12.0 - 16.0: Ambar                (60, 45, 0)
- 16.0 - 22.0: Naranja              (60, 30, 0)
- 22.0 - 28.0: Naranja intenso      (60, 20, 0)
- 28.0 - 34.0: Rojo-naranja         (60, 10, 0)
- > 34.0     : Rojo (critico)       (60, 0, 0)

El color base define el "tono" general por rango; algunos efectos (RAINBOW, FIRE) no mantienen un color fijo.
Nota: el firmware descarta picos por encima de 40 km/h (SPEED_MAX_VALID_KPH).

---

## 4) Efectos default por rango (tira A y B iguales)

- R1 (<=2.0): SOLID / SOLID (speed 40, intensity 80)
- R2 (<=4.0): PULSE / PULSE (58, 95)
- R3 (<=6.0): BREATH / BREATH (76, 110)
- R4 (<=8.0): CHASE / CHASE (94, 125)
- R5 (<=12.0): SINELON / SINELON (112, 140)
- R6 (<=16.0): JUGGLE / JUGGLE (130, 155)
- R7 (<=22.0): BPM / BPM (148, 170)
- R8 (<=28.0): RAINBOW / RAINBOW (166, 180)
- R9 (<=34.0): GRADIENT_WAVE / GRADIENT_WAVE (184, 190)
- R10 (>34.0): FIRE / FIRE (200, 200)

---

## 5) Defaults relevantes

- LED_STRIP_MODE = 2 (doble tira)
- LED_STRIP_COUNT = 20 (LEDs por tira)
- LED_STATUS_COUNT = 3 (LEDs reservados para estado)
- LED_BRIGHTNESS = 77 (~30%)
- LED_GPS_SEARCH_RAINBOW_STEP = 6 (avance de tono por tick sin GPS fix)
- SPEED_RANGE_1..9 = 2.0 / 4.0 / 6.0 / 8.0 / 12.0 / 16.0 / 22.0 / 28.0 / 34.0 km/h

---

## 6) Si cambias la configuracion

Si ajustas `LED_STRIP_COUNT`, `LED_STATUS_COUNT` o los rangos de velocidad en `Platformio/Dog-RGB/include/config.h`, este manual deja de ser exacto. Recomiendo actualizar esta tabla cuando cambies defaults.
