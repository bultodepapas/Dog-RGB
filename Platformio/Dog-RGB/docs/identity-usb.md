# Identidad por USB — diagnóstico etapa 3

Extensión de VIS-3 para el banco USB cuando el PC no puede acceder al AP.
Comparte `identity::save()` con HTTP: validación, A/B+CRC, generación esperada
y lectura posterior. No cambia credenciales de radio, configuración LED/GPS
ni particiones. Disponible únicamente en `waveshare_lcd169_displaycheck`;
ausente del producto Display y de Classic. Acceso físico USB del diagnóstico,
sin PIN HTTP; no es una función remota del producto.

## Uso

Desde `Platformio/Dog-RGB`, usando Python de PlatformIO con pyserial. Cerrar
otros lectores del mismo puerto. Ejemplo deliberadamente sintético:

```powershell
python tools/identity_usb.py --port COM6 --status
python tools/identity_usb.py --port COM6 --name FREYA --phone +100000000000 --channel whatsapp
```

Sustituir por el contacto real local. `--channel call` genera llamada y
`--channel disabled` mantiene nombre/teléfono sin QR. `--clear` borra explícitamente
la identidad mediante el store. `--output ruta.json` guarda solo un recibo sin
nombre/teléfono. No hay valores personales por defecto ni reintentos implícitos.
La herramienta normaliza el nombre a NFC; firmware valida independientemente.
No navega, reinicia, flashea, abre enlaces ni envía mensajes.

## Protocolo de banco v1

- `j` arma una petición y devuelve `[IDENTITY] version=1 status=ready
  generation=N configured=0|1` (una sola línea real).
- Recibir ese handshake antes de transmitir JSON. Cuerpo de hasta 512 bytes,
  terminado por LF, cuatro campos idénticos a POST `/api/identity`: `name`,
  `phone`, `qr_kind`, `expected_generation`. CRLF también válido.
- Solo ocho bytes USB por servicio, plazo total 5.000 ms desde `j`; no espera
  bloqueante. JSON se procesa una vez al terminar. La escritura NVS es síncrona
  y su tiempo se evalúa separado de navegación/refresco normal.
- Línea vacía cancela sin guardar, por eso `--status` es lectura sin NVS.
- Respuesta: `saved`, `unchanged`, `conflict`, `name`, `phone`, `qr_kind`,
  `fields`, `body_size`, `timeout`, `storage` o `cancelled`, junto a generación
  y configured. No imprime cuerpo, nombre, teléfono ni QR.
- Después de exceso, NUL o timeout, descarta hasta LF: nunca interpreta los
  caracteres restantes del JSON como comandos LCD (`n/b/f/d/...`). LF también
  permite recuperar una petición abandonada. Reloj con rollover probado.
- El helper descarta entrada antigua, envía LF+j y espera handshake nuevo.
  Sin protocolo compatible, no transmite datos personales. Conflicto/error
  conserva el resultado del store y termina, sin intentar sobrescribirlo.

El helper `display_bench.py` conserva solo comandos LCD y no acepta `j`.
Editar por USB tampoco navega ni despierta: `p` selecciona la Placa configurada
en un paso separado. BOOT/n mantiene la primera pulsación de despertar.

## Alcance de comprobación

Doce CTest: parser/timeout/drenado reales, errores/conflicto/NVS, servicio con
bytes acotados y edición en oscuro; fuentes existentes de VIS-3 sin rediseño.
Cinco pruebas Python del cliente: handshake, generación, lectura sin cuerpo,
exceso y rechazo sin reintento. Ver [baseline](../../../docs/baselines/display-identity-usb-2026-09-12.md).
La extensión permite configurar y medir el QR físico sin resolver todavía el
AP. No acredita acceso HTTP ni recepción GNSS/tiras reales.
