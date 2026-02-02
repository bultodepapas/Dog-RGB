# LED UI Spec (Fase 1)

Esta especificacion define el uso de la tira LED como interfaz de estado del sistema.

---

## Objetivo

- La tira LED comunica el estado del collar sin abrir el portal.
- Estados claros, simples y consistentes.
- Brillo bajo para notificaciones (30%).

---

## Segmentos

- Segmento A (LED 0-2): indicadores de estado.
- Segmento B (LED 3-fin): modo normal o idle.
- Aplica por tira si se usan dos tiras independientes.

---

## Configuracion base

- LED_STRIP_COUNT (por tira): 20 (min 10, max 50)
- LED_STRIP_MODE: 1 (tira unica) o 2 (doble tira)

---

## Tabla de estados

Segmento A (LED 0-1) por tira:
- LED0 = Wi-Fi/AP
  - STA conectado: verde fijo
  - STA intentando: verde pulsante (ciclo 1.5 s)
  - AP activo sin clientes: amarillo fijo
  - AP activo con clientes: amarillo pulsante suave
  - STA fallo (fallback a AP): rojo fijo
  - Wi-Fi apagado por ahorro: ambar doble pulso (cada ~3 s)
- LED1 = GPS
  - GPS OK: azul fijo
  - GPS buscando: azul pulsante (ciclo 1.5 s)

Override critico:
- Sin GPS y sin Wi-Fi por >10 min: LED0 y LED1 rojo parpadeo rapido (200 ms)

Modo normal (Segmento B):
- Si no hay GPS fix: rainbow animado
- Con GPS OK: efecto por rango de velocidad (FastLED, configurable en `config.h`)

Homogeneo:
- Si Wi-Fi esta OFF y GPS OK por >5 min, LED0 y LED1 replican el color/efecto del Segmento B

---

## Prioridad de estados

1) Error critico (LED0/LED1 rojo rapido)
2) Homogeneo (Wi-Fi OFF + GPS OK >5 min)
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

---

## Condiciones

- GPS OK: has_gps_fix = true
- GPS buscando: has_gps_fix = false
- STA conectado: wifi_sta_connected = true y WL_CONNECTED
- STA intentando: wifi_sta_connecting = true
- AP activo: ap_enabled = true
- AP con clientes: softAPgetStationNum() > 0
- Wi-Fi OFF: modo WIFI_OFF (sin STA ni AP)
- Error critico: sin GPS y sin Wi-Fi por > 10 min
- AP auto: sin GPS fix mantiene AP on; velocidad < 2 km/h por 2 min enciende AP; sin clientes por 5 min apaga AP; sin STA ni AP apaga Wi-Fi

---

## Mapeo velocidad -> color (Segmento B)

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

## Notas

- Un solo estado activo a la vez.
- Segmento A siempre reservado a estados.
- Segmento B no se sobreescribe por estados; si no hay GPS fix usa rainbow, con GPS OK usa rangos.
- La prioridad evita estados confusos.
