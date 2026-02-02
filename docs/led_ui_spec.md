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

1) Arranque
- Segmento: toda la tira
- Color: Blanco suave
- Modo: fijo 1-2 s

2) GPS
- GPS OK: Segmento A en azul fijo
- GPS buscando: Segmento A azul pulsante (ciclo 1.5 s)

3) Wi-Fi
- STA conectado: Segmento A verde fijo
- STA intentando: Segmento A verde pulsante (ciclo 1.5 s)
- AP activo: Segmento A amarillo fijo
- Error Wi-Fi: Segmento A rojo fijo

4) Error critico
- Segmento: A
- Color: rojo
- Modo: parpadeo rapido (200 ms)

5) Modo normal
- Segmento: B (LED 3-fin)
- Modo: efecto por rango de velocidad (FastLED, configurable en `config.h`)

---

## Prioridad de estados

1) Error critico (rojo rapido, Segmento A)
2) Error Wi-Fi (rojo fijo, Segmento A)
3) Arranque (blanco suave, toda la tira)
4) Wi-Fi / GPS (Segmento A)
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
- STA intentando: wifi_ssid definido y WL_CONNECTED = false
- AP activo: modo AP
- Error critico: sin GPS y sin Wi-Fi por > 10 min

---

## Mapeo velocidad -> color (Segmento B)

- 0.0 - 2.0 km/h: Azul (0, 0, 60)
- 2.0 - 6.0 km/h: Azul/Violeta (20, 0, 60)
- 6.0 - 12.0 km/h: Morado (40, 0, 60)
- 12.0 - 20.0 km/h: Magenta/Naranja (60, 0, 40)
- 20.0 - 30.0 km/h: Naranja (60, 0, 20)
- > 30.0 km/h: Rojo (60, 0, 0)

---

## Notas

- Un solo estado activo a la vez.
- Segmento A siempre reservado a estados.
- Segmento B no se sobreescribe por estados; solo se enciende si hay GPS OK.
- La prioridad evita estados confusos.
