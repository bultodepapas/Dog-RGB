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
- Error Wi-Fi: toda la tira rojo fijo

4) Error critico
- Segmento: toda la tira
- Color: rojo
- Modo: parpadeo rapido (200 ms)

5) Modo normal
- Segmento: B (LED 3-fin)
- Modo: respiracion suave o color estable (definir luego)

---

## Prioridad de estados

1) Error critico (rojo rapido, toda la tira)
2) Error Wi-Fi (rojo fijo, toda la tira)
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

## Notas

- Un solo estado activo a la vez.
- Segmento A siempre reservado a estados.
- Segmento B se apaga si hay error critico o error Wi-Fi.
- La prioridad evita estados confusos.
