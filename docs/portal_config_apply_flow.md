# Config Apply Flow (Runtime)

Este documento define como aplicar cambios del portal sin reiniciar todo el firmware.

---

## Clasificacion de cambios

### Aplicacion inmediata
- LED_BRIGHTNESS
- SPEED_RANGE_*_KPH
- RANGE_*_EFFECT_*
- RANGE_*_SPEED / RANGE_*_INTENSITY

### Requiere reinicio de Wi-Fi
- AP_SSID
- AP_PASS
- MDNS_NAME

---

## Flujo de aplicacion

1) Validar JSON
2) Guardar en NVS
3) Aplicar cambios inmediatos en RAM
4) Si hay cambios de Wi-Fi:
   - Reiniciar AP con nuevos valores
   - Si ap_open=true, limpiar password y dejar AP abierto
   - Reiniciar mDNS si STA activo

---

## Respuesta del API

- OK: {"status":"ok","wifi_restart":true/false}
- Error: {"status":"error","reason":"..."}

---

## Notas

- No reiniciar el MCU completo.
- Guardar timestamp de ultimo cambio (opcional).
