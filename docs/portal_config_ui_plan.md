# Portal Config UI Plan (Pantalla de Parametros)

Este documento define la pantalla de configuracion para parametros runtime.

---

## Pantalla: Configuracion

### Seccion 1: Brillo
- Slider 1-255
- Texto: "Brillo LED"
- Hint: "Recomendado 30%"

### Seccion 2: Rangos de velocidad
- 5 inputs numericos (km/h)
- Validacion: valores ascendentes
- Texto: "Rangos de velocidad"

### Seccion 3: Efectos por rango
- Para cada rango (1-6):
  - Dropdown efecto A
  - Dropdown efecto B
  - Slider velocidad (0-255)
  - Slider intensidad (0-255)

### Seccion 4: Wi-Fi AP
- SSID
- Password
- mDNS
 - Aviso: cambiar AP puede desconectar la sesion

### Acciones
- Boton "Guardar"
- Boton "Restaurar defaults"

---

## Validaciones UI

- Rango 1 < Rango 2 < Rango 3 < Rango 4 < Rango 5
- Brillo 1..255
- Effect id 0..11
- Speed/intensity 0..255
- Password >= 8

---

## Estados

- Guardando...
- Guardado OK
- Error (mostrar mensaje simple)

---

## Notas

- Mostrar ayuda corta para cada seccion.
- Mantener dise?o simple para celular.
