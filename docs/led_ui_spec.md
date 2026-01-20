# LED UI Spec (Fase 1)

Esta especificacion define el uso de la tira LED como interfaz de estado del sistema.

---

## Objetivo

- La tira LED comunica el estado del collar sin abrir el portal.
- Estados claros, simples y consistentes.

---

## Tabla de estados

1) Arranque
- Color: Blanco suave
- Modo: fijo 1-2 s

2) GPS
- GPS OK: Azul fijo
- GPS buscando: Azul pulsante (ciclo 1.5 s)

3) Wi-Fi
- STA conectado: Verde fijo
- STA intentando: Verde pulsante (ciclo 1.5 s)
- AP activo: Amarillo fijo
- Error Wi-Fi: Rojo fijo

4) Error critico
- Color: Rojo
- Modo: parpadeo rapido (200 ms)

---

## Prioridad de estados

1) Error critico (rojo rapido)
2) Error Wi-Fi (rojo fijo)
3) STA conectado (verde fijo)
4) STA intentando (verde pulsante)
5) AP activo (amarillo fijo)
6) GPS OK (azul fijo)
7) GPS buscando (azul pulsante)

---

## Colores base (RGB)

- Blanco suave: 80, 80, 80
- Azul: 0, 0, 200
- Verde: 0, 200, 0
- Amarillo: 200, 160, 0
- Rojo: 200, 0, 0

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
- Error critico: sin GPS y sin Wi-Fi por > 5-10 min

---

## Notas

- Toda la tira muestra un solo estado a la vez.
- No usar patrones complejos en Fase 1.
- La prioridad evita estados confusos.
