# Config Parameters (Centralized)

Este documento centraliza los parametros que se definen al inicio del proyecto para facilitar ajustes y futura configuracion via portal.

---

## 1) LED Hardware

- LED_STRIP_MODE: 1 (tira unica) o 2 (doble tira)
  - Controla si se usan una o dos tiras independientes.
- LED_STRIP_COUNT: LEDs por tira (min 10, max 50)
  - Se usa para dimensionar todos los bucles y efectos.
- LED_STATUS_COUNT: LEDs reservados para estados (default 3)
  - Segmento A (estado) siempre tiene prioridad.
- LED_BRIGHTNESS: brillo global (0-255)
  - Recomendado ~30% para bateria y calor.
- Tipo de LED: SK6812 (single-wire, 5V)
  - Implica uso de timing preciso y posible level shifting.

---

## Defaults vs Recommended

Valores actuales (default) y recomendados para inicio.
Estos valores viven en `firmware/esp32s3_base/include/config.h` y el firmware los usa en compilacion.
Los parametros runtime pueden ser sobrescritos desde el portal y se guardan en NVS.

| Parametro | Default | Recomendado | Nota |
| --- | --- | --- | --- |
| LED_STRIP_MODE | 2 | 2 | Cambiar a 1 si solo hay una tira |
| LED_STRIP_COUNT | 20 | 20 | Ajustar segun largo real |
| LED_STATUS_COUNT | 3 | 3 | Mantener corto para estados |
| LED_BRIGHTNESS | 77 | 77 | ~30% brillo |
| AP_SSID | dog | dog | Temporal |
| AP_PASS | Dog123456789 | Dog123456789 | Temporal |
| GPS_BAUD | 9600 | 9600 | GNSS E108-GN02 |
| GPS_SAMPLE_MS | 1000 | 1000 | 1 s |
| SPEED_ACTIVE_KPH | 0.7 | 0.7 | Umbral activo |
| SPEED_MAX_VALID_KPH | 40.0 | 40.0 | Filtro de picos |
| SAVE_INTERVAL_MS | 60000 | 60000 | Guardado cada 60 s |

---

## 2) LED UI (Estados)

- Colores base RGB (30% aprox):
  - Blanco suave: 60, 60, 60
  - Azul: 0, 0, 60
  - Verde: 0, 60, 0
  - Amarillo: 60, 45, 0
  - Rojo: 60, 0, 0
- Prioridad de estados (de mayor a menor):
  1) Error critico (rojo rapido, toda la tira)
  2) Error Wi-Fi (rojo fijo, toda la tira)
  3) Arranque (blanco suave, toda la tira)
  4) Estados Wi-Fi/GPS (segmento A)
  5) Modo normal (segmento B)
- Animaciones:
  - Pulso lento: 1.5 s
  - Parpadeo rapido: 200 ms
- Error critico: sin GPS y sin Wi-Fi por > 10 min

---

## 3) Velocidad -> Color (Segmento B)

- SPEED_RANGE_1_KPH: 1.5
- SPEED_RANGE_2_KPH: 3.0
- SPEED_RANGE_3_KPH: 4.5
- SPEED_RANGE_4_KPH: 6.0
- SPEED_RANGE_5_KPH: 7.5

Efectos por rango (FastLED, ver `docs/led_effects_plan.md`):
- RANGE_1_EFFECT_A / RANGE_1_EFFECT_B
- RANGE_2_EFFECT_A / RANGE_2_EFFECT_B
- RANGE_3_EFFECT_A / RANGE_3_EFFECT_B
- RANGE_4_EFFECT_A / RANGE_4_EFFECT_B
- RANGE_5_EFFECT_A / RANGE_5_EFFECT_B
- RANGE_6_EFFECT_A / RANGE_6_EFFECT_B

Velocidad e intensidad por rango:
- RANGE_1_SPEED / RANGE_1_INTENSITY
- RANGE_2_SPEED / RANGE_2_INTENSITY
- RANGE_3_SPEED / RANGE_3_INTENSITY
- RANGE_4_SPEED / RANGE_4_INTENSITY
- RANGE_5_SPEED / RANGE_5_INTENSITY
- RANGE_6_SPEED / RANGE_6_INTENSITY

Mapeo de color:
- 0.0 - 1.5 km/h: Azul (0, 0, 60)
- 1.5 - 3.0 km/h: Azul/Violeta (20, 0, 60)
- 3.0 - 4.5 km/h: Morado (40, 0, 60)
- 4.5 - 6.0 km/h: Magenta/Naranja (60, 0, 40)
- 6.0 - 7.5 km/h: Naranja (60, 0, 20)
- > 7.5 km/h: Rojo (60, 0, 0)

---

## 4) Wi-Fi

- AP_SSID: dog
- AP_PASS: Dog123456789
- MDNS_NAME: dog-collar
- STA_CONNECT_TIMEOUT_MS: 10000
- WIFI_RETRY_INTERVAL_MS: 10000

---

## 5) GNSS

- GPS_BAUD: 9600
- GPS_SAMPLE_MS: 1000
- SPEED_ACTIVE_KPH: 0.7
- SPEED_MAX_VALID_KPH: 40.0

---

## 6) Persistencia

- SAVE_INTERVAL_MS: 60000
  - Guarda metricas a NVS cada 60 s.

---

## Notas

- Este documento debe mantenerse sincronizado con `firmware/esp32s3_base/src/main.cpp`.
- En fase futura, estos parametros se exponen en el portal web.
- Detalles de estados y prioridades: `docs/led_ui_spec.md`.
- Portal Wi-Fi: `docs/wifi_portal_spec.md` y `docs/wifi_portal_plan.md`.
