# Config Runtime - Plan de Persistencia (NVS)

Este documento define como guardar y aplicar parametros editables desde el portal.

---

## Objetivo

- Guardar parametros runtime en NVS.
- Aplicar cambios sin recompilar.
- Mantener defaults desde `config.h` si no hay datos.

---

## Estructura en NVS

Namespace: `dogrgb_cfg`

Claves sugeridas:
- `ver` (uint8) -> version de config
- `brightness` (uint8)
- `ranges` (blob de 5 floats)
- `effects` (blob de 6 structs)
- `ap_ssid` (string)
- `ap_pass` (string, puede estar vacio para AP abierto)
- `mdns` (string)

---

## Carga en arranque

1) Leer `ver`.
2) Si no existe o es incompatible -> usar defaults de `config.h`.
3) Si existe -> cargar valores y validar.

---

## Aplicacion en caliente

- Brillo: aplicar inmediato.
- Rangos: aplicar inmediato.
- Efectos: aplicar inmediato.
- Wi-Fi: reiniciar AP con nuevo SSID/PASS.
- Si se solicita AP abierto, guardar `ap_pass` vacio.
- mDNS: reiniciar si STA activo.

---

## Validacion

- Si algun valor invalido -> descartar y usar default.
- Log simple por Serial.

---

## Reset a defaults

- Endpoint `/api/config/reset`
- Borra namespace `dogrgb_cfg`
- Reinicia configuracion a defaults.

---

## Notas

- Limitar escrituras para no desgastar flash.
- Guardar solo cuando el usuario haga "Guardar".
