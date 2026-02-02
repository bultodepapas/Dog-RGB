# Portal Config Validation Flow

Este documento define el flujo de validacion para el formulario de configuracion.

---

## 1) Validacion en frontend

- Inputs numericos para brillo, rangos, efectos, speed/intensity.
- Checkbox para AP abierto.
- Confirmacion si se cambia SSID/password/mDNS.

Nota: el frontend actual no bloquea errores complejos; el backend valida todo.

---

## 2) Validacion en backend (ESP32)

- Brillo: 1..255
- Rangos: 9 valores > 0 y estrictamente ascendentes
- Efectos: 10 rangos presentes
- Effect id: 0..11
- Speed/intensity: 0..255
- AP SSID: 1..32
- AP password: >= 8 (si no es AP abierto)
- mDNS: 1..32 (solo letras, numeros y guiones)

Si falla -> 400 con `{"status":"error","reason":"..."}`

Motivos actuales:
- `no body`
- `bad json`
- `brightness`
- `ranges`
- `ranges value`
- `ranges order`
- `effects`
- `effect values`
- `effect id`
- `ssid`
- `pass`
- `mdns`

---

## 3) Aplicacion

- Brillo, rangos y efectos: aplicar inmediato.
- Wi-Fi: reiniciar AP si cambian SSID/password.
- mDNS: reiniciar si STA activo y cambia.

---

## 4) Respuestas

- OK: `{"status":"ok","wifi_restart":true/false}`
- Error: `{"status":"error","reason":"..."}`

---

## 5) Restaurar defaults

- Boton "Restaurar defaults" -> `POST /api/config/reset`
- Respuesta OK y recarga opcional
