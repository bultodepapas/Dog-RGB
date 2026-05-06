# LED UI Spec (Fase 1)

Esta especificacion define el uso de la tira LED como interfaz de estado del sistema.

---

## Objetivo

- La tira LED comunica el estado del collar sin abrir el portal.
- Estados claros, simples y consistentes.
- Brillo bajo para notificaciones (30%).

---

## Segmentos

- Segmento A (estado): LEDs 0..(LED_STATUS_COUNT-1)
- Segmento B (cuerpo): LEDs LED_STATUS_COUNT..fin
- Aplica por tira si se usan dos tiras independientes.

---

## Configuracion base (defaults)

- LED_STRIP_COUNT (por tira): 24 (min 10, max 50)
- LED_STATUS_COUNT: 2
- LED_STRIP_MODE: 1 (tira unica) o 2 (doble tira)

---

## Tabla de estados (Segmento A)

LED0 = Wi-Fi/AP
- STA conectado: verde fijo
- STA intentando: verde pulsante (ciclo 1.5 s)
- AP activo sin clientes: amarillo fijo
- AP activo con clientes: amarillo pulsante suave
- STA fallo con credenciales (fallback a AP): rojo fijo
- Wi-Fi apagado por ahorro: ambar doble pulso (AP_OFF_PULSE_PERIOD_MS)

LED1 = GPS
- GPS OK: azul fijo
- GPS buscando: azul pulsante (ciclo 1.5 s)

Override critico:
- Sin GPS y sin STA por >10 min: LED0 y LED1 rojo parpadeo rapido (200 ms)

---

## Modo normal (Segmento B)

- Si no hay GPS fix: rainbow animado
- Con GPS OK: efecto por rango de velocidad (configurable)

## Modo SHOW (demo)

- Recorre todos los efectos (IDs 0..11) cada 30 s usando una bolsa barajada interna.
- No repite efectos dentro de la misma bolsa y evita repetir el ultimo efecto anterior al iniciar una bolsa nueva.
- Usa paleta interna curada, color base aleatorio por efecto y mezcla gradual hacia un segundo color.
- Aplica fade-in/fade-out corto al cambiar de efecto.
- Ambas cintas mantienen siempre el mismo efecto SHOW.
- Segmento B siempre en demo.
- Segmento A sigue mostrando estado Wi‑Fi/GPS, **excepto** si se activa modo homogeneo.

---

## Homogeneo

- Si Wi-Fi esta OFF y GPS OK estable por >5 min, toda la tira usa el mismo efecto del rango (incluye LEDs de estado).
- En SHOW, el homogeneo **pisa todo** con el efecto SHOW actual.

---

## Modo simple

- Aplica **un solo efecto** configurado por el usuario a toda la tira.
- El modo simple **pisa** LEDs de estado (no muestra Wi‑Fi/GPS mientras está activo).
- RAINBOW, GRADIENT_WAVE y FIRE **ignoran** el color base.

---

## Prioridad de estados

1) Error critico (LED0/LED1 rojo rapido)
2) Homogeneo (Wi-Fi OFF + GPS OK estable)
3) Wi-Fi/AP (LED0)
4) GPS (LED1)
5) Modo normal (Segmento B)

---

## Colores base (RGB) - 30% aprox

- Blanco suave: 60, 60, 60
- Azul: 0, 0, 60
- Verde: 0, 60, 0
- Amarillo: 60, 45, 0
- Rojo: 60, 0, 0

---

## Parametros de animacion

- Pulso lento: 1.5 s (0.75 s subida, 0.75 s bajada)
- Parpadeo rapido: 200 ms on/off
- Rainbow idle: avance de hue cada tick de `LED_UPDATE_MS`

---

## Condiciones (firmware)

- GPS OK: `has_gps_fix = true`
- GPS buscando: `has_gps_fix = false`
- STA conectado: `wifi_sta_connected = true` y `WL_CONNECTED`
- STA intentando: `wifi_sta_connecting = true`
- AP activo: `ap_enabled = true`
- AP con clientes: `softAPgetStationNum() > 0`
- Wi-Fi OFF: `wifi_off = true`
- Error critico: sin GPS y sin STA por `CRITICAL_NO_OK_MS`

---

## Notas

- Un solo estado activo a la vez en Segmento A.
- Segmento A siempre reservado a estados (salvo modo homogeneo).
- Segmento B no se sobreescribe por estados.
