# Portal Config Validation Flow

Este documento define el flujo de validacion para el formulario de configuracion.

---

## 1) Validacion en frontend

1) Brillo: 1..255
2) Rangos: 5 valores numericos y ascendentes
3) Efectos: id 0..11
4) Speed/intensity: 0..255
5) Password AP: >= 8 (si no se marca AP abierto)
6) mDNS: solo letras, numeros y guiones

Si falla: mostrar mensaje y bloquear envio.

---

## 2) Validacion en backend (ESP32)

1) Re-validar todos los campos
2) Si falla -> 400 "invalid config"
3) Si OK -> guardar en NVS

---

## 3) Aplicacion

- Brillo: aplicar inmediato
- Rangos y efectos: aplicar inmediato
- Wi-Fi: reiniciar AP con nuevo SSID/PASS
- mDNS: reiniciar si STA activo

---

## 4) Respuestas

- OK: {"status":"ok"}
- Error: {"status":"error","reason":"..."}

---

## 5) Restaurar defaults

- Boton "Restaurar defaults" -> POST /api/config/reset
- Respuesta OK
- Recarga de pagina
