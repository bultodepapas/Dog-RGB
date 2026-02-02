# Config Parameters (Centralized)

Este documento centraliza los parametros que se definen al inicio del proyecto para facilitar ajustes y futura configuracion via portal.

---

## 1) LED Hardware

- LED_STRIP_MODE: 1 (tira unica) o 2 (doble tira)
  - Controla si se usan una o dos tiras independientes.
- LED_STRIP_COUNT: LEDs por tira (min 10, max 50)
  - Se usa para dimensionar todos los bucles y efectos.
- LED_STATUS_COUNT: LEDs reservados para estados (default 2)
  - Segmento A (estado) siempre tiene prioridad.
- LED_BRIGHTNESS: brillo global (0-255)
  - Recomendado ~30% para bateria y calor.
- Tipo de LED: SK6812 (single-wire, 5V)
  - Implica uso de timing preciso y posible level shifting.

---

## Defaults vs Recommended

Valores actuales (default) y recomendados para inicio.
Estos valores viven en `Platformio/Dog-RGB/include/config.h` y el firmware los usa en compilacion.
Los parametros runtime pueden ser sobrescritos desde el portal y se guardan en NVS.

| Parametro | Default | Recomendado | Nota |
| --- | --- | --- | --- |
| LED_STRIP_MODE | 2 | 2 | Cambiar a 1 si solo hay una tira |
| LED_STRIP_COUNT | 24 | 24 | Ajustar segun largo real |
| LED_STATUS_COUNT | 2 | 2 | Mantener corto para estados |
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
  1) Error critico (rojo rapido, segmento A)
  2) Error Wi-Fi (rojo fijo, segmento A)
  3) Arranque (blanco suave, toda la tira)
  4) Estados Wi-Fi/GPS (segmento A)
  5) Modo normal (segmento B)
- Animaciones:
  - Pulso lento: 1.5 s
  - Parpadeo rapido: 200 ms
- Error critico: sin GPS y sin Wi-Fi por > 10 min
- Segmento B: si no hay GPS fix, mostrar rainbow animado; con GPS OK, usar rangos de velocidad
- Segmento A (LED_STATUS_COUNT=2): LED0 Wi-Fi/AP, LED1 GPS; en Wi-Fi OFF + GPS OK estable, ambos siguen el segmento B

---

## 3) Velocidad -> Color (Segmento B)

- SPEED_RANGE_1_KPH: 2.0
- SPEED_RANGE_2_KPH: 4.0
- SPEED_RANGE_3_KPH: 6.0
- SPEED_RANGE_4_KPH: 8.0
- SPEED_RANGE_5_KPH: 12.0
- SPEED_RANGE_6_KPH: 16.0
- SPEED_RANGE_7_KPH: 22.0
- SPEED_RANGE_8_KPH: 28.0
- SPEED_RANGE_9_KPH: 34.0

Efectos por rango (motor actual, ver `docs/led_effects_plan.md`):
- RANGE_1_EFFECT_A / RANGE_1_EFFECT_B
- RANGE_2_EFFECT_A / RANGE_2_EFFECT_B
- RANGE_3_EFFECT_A / RANGE_3_EFFECT_B
- RANGE_4_EFFECT_A / RANGE_4_EFFECT_B
- RANGE_5_EFFECT_A / RANGE_5_EFFECT_B
- RANGE_6_EFFECT_A / RANGE_6_EFFECT_B
- RANGE_7_EFFECT_A / RANGE_7_EFFECT_B
- RANGE_8_EFFECT_A / RANGE_8_EFFECT_B
- RANGE_9_EFFECT_A / RANGE_9_EFFECT_B
- RANGE_10_EFFECT_A / RANGE_10_EFFECT_B
  - Defaults: A y B usan el mismo efecto por rango.

Velocidad e intensidad por rango:
- RANGE_1_SPEED / RANGE_1_INTENSITY
- RANGE_2_SPEED / RANGE_2_INTENSITY
- RANGE_3_SPEED / RANGE_3_INTENSITY
- RANGE_4_SPEED / RANGE_4_INTENSITY
- RANGE_5_SPEED / RANGE_5_INTENSITY
- RANGE_6_SPEED / RANGE_6_INTENSITY
- RANGE_7_SPEED / RANGE_7_INTENSITY
- RANGE_8_SPEED / RANGE_8_INTENSITY
- RANGE_9_SPEED / RANGE_9_INTENSITY
- RANGE_10_SPEED / RANGE_10_INTENSITY

Mapeo de color:
- 0.0 - 2.0 km/h: Cian (muy baja) (0, 60, 60)
- 2.0 - 4.0 km/h: Verde-cian (0, 60, 35)
- 4.0 - 6.0 km/h: Verde (0, 60, 0)
- 6.0 - 8.0 km/h: Verde-lima (25, 60, 0)
- 8.0 - 12.0 km/h: Amarillo (60, 60, 0)
- 12.0 - 16.0 km/h: Ambar (60, 45, 0)
- 16.0 - 22.0 km/h: Naranja (60, 30, 0)
- 22.0 - 28.0 km/h: Naranja intenso (60, 20, 0)
- 28.0 - 34.0 km/h: Rojo-naranja (60, 10, 0)
- > 34.0 km/h: Rojo (critico) (60, 0, 0)

---

## 4) Wi-Fi

- AP_SSID: dog
- AP_PASS: Dog123456789
- MDNS_NAME: dog-collar
- STA_CONNECT_TIMEOUT_MS: 10000
- WIFI_RETRY_INTERVAL_MS: 10000
- AP_IDLE_TIMEOUT_MS: 300000 (AP off si no hay clientes por 5 min)
- AP_STATIONARY_MS: 120000 (AP on si velocidad baja por 2 min)
- AP_CLIENT_POLL_MS: 1000
- AP_STATIONARY_ON_KPH: 2.0
- AP_STATIONARY_OFF_KPH: 2.5
- WIFI_OFF_GPS_FIX_MS: 300000 (modo homogeneo tras GPS OK estable)
- AP_OFF_PULSE_PERIOD_MS: 3000
- AP_OFF_PULSE_MS: 200

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

- Este documento debe mantenerse sincronizado con `Platformio/Dog-RGB/src/main.cpp`.
- En fase futura, estos parametros se exponen en el portal web.
- Detalles de estados y prioridades: `docs/led_ui_spec.md`.
- Portal Wi-Fi: `docs/wifi_portal_spec.md` y `docs/wifi_portal_plan.md`.
