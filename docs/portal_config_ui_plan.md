# Portal Config UI Plan (Pantalla de Parametros)

Este documento define la pantalla de configuracion para parametros runtime.

---

## Pantalla: Configuracion

### Seccion 1: Brillo
- Input numerico 1-255
- Texto: "Brightness"
- Hint: "Recomendado ~30%"

### Seccion 2: Rangos de velocidad
- 9 inputs numericos (km/h)
- Validacion: valores ascendentes
- Texto: "Speed ranges (kph)"

### Seccion 3: Efectos por rango
- Para cada rango (1-10):
  - effect A (0..11)
  - effect B (0..11)
  - speed (0..255)
  - intensity (0..255)

### Seccion 4: Wi-Fi AP
- SSID
- Password
- Checkbox: "AP abierto (sin password)"
- mDNS
- Aviso: cambiar AP puede desconectar la sesion

### Acciones
- Boton "Guardar"
- Boton "Restaurar defaults"

---

## Validaciones UI

- Brillo 1..255
- Rangos estrictamente ascendentes
- Effect id 0..11
- Speed/intensity 0..255
- Password >= 8 (si no se marca AP abierto)

---

## Estados

- Guardando...
- Guardado OK
- Error (mostrar mensaje simple)

---

## Notas

- Mantener diseno simple para celular.
- El firmware vuelve a validar todo en backend.
