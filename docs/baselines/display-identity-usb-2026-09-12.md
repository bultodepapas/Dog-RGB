# VIS-3 — Configuración USB, persistencia y QR físico

2026-09-12. **FREYA configurada por USB, identidad retenida tras reiniciar y
lectura del QR y ciclo BOOT de cuatro páginas confirmados por el propietario.** Extensión acotada de VIS-3 tras
el acceso fallido al AP, no un cambio del producto ni una solución del Wi-Fi.
[Guía](../../Platformio/Dog-RGB/docs/identity-usb.md),
[manifest de fuentes/binarios/mediciones](../assets/display-identity-usb/manifest.json).

## Implementación

`IdentityConsole` está compilado solo en etapa 3. Comando `j`, handshake v1,
JSON limitado a 512 B, ocho bytes por servicio y plazo de 5 s; rechazos drenan
hasta LF para que el cuerpo no se convierta en comandos LCD. No imprime contacto.
Comparte validación/NVS con HTTP, con generación esperada y sin reintentos.
El helper Python normaliza NFC, espera el handshake antes de transmitir y genera
un recibo sin datos personales. Producto y Classic excluyen el transporte.

Se autoriza por el acceso USB físico del firmware de banco; no modifica las
guardas/PIN del portal. `display_bench.py` continúa aceptando solo comandos LCD.
El guardado no navega, no despierta LCD y no altera Wi-Fi/LED/GPS/particiones.

## Software

- **12/12 CTest**: nuevo parser real, store compartido, delimitación/timeout/
  rollover, NUL/exceso/malformado, conflictos y fallo de escritura; integración
  con el servicio real, máximo ocho bytes y ausencia de eco de datos personales.
- **152/152 Python**, ejecución final 19,657 s. Incluye cinco contratos nuevos
  del helper. Ejecuciones anteriores tropezaron con WinError 32/267 al borrar
  ejecutables temporales tras terminar. Los dos fixtures afectados ahora usan
  limpieza con reintento exclusivo Windows, máximo 1 s; no suprimen fallos
  persistentes ni errores ajenos. No se modificó su validación funcional.
- Once capturas del port permanecen idénticas a VIS-3, cinco QR decodificados
  exactamente por ZXing. Sin rediseño, fuentes ni dependencia gráfica nueva.
- Tres builds correctos, 3:54,677. Producto/Classic conservan tamaños; inventario
  ELF confirma ausencia de `IdentityConsole` en producto y presencia en etapa 3.

| Target | RAM estática B | Flash B | Delta frente a VIS-3 |
| --- | ---: | ---: | --- |
| Classic | 57.636 | 1.154.219 | Sin cambio |
| Waveshare producto | 123.068 | 1.480.739 | Sin cambio |
| Display diagnóstico | 123.604 | 1.486.179 | +536 RAM / +3.680 flash |

## Placa física

COM6, Waveshare USB sin GPS/tiras. Carga correcta en 33,154 s con verificación
del hash. Imagen preservada en `Platformio/Dog-RGB/artifacts/display-identity-usb/`;
SHA-256 `f5f5b99a1b086921229343f6d0cdce4546fb787ea1d43bd1bbc6bca12a6a626d`.
No borrado de NVS. El contacto facilitado se escribió por la herramienta local,
sin incorporarlo al firmware, fixtures ni capturas públicas.

Recibo `saved`, generación 1, configured=true. Reinicio solicitado por USB,
sin reflashear ni escribir flash: lectura posterior conserva generación 1 y
configured=true. LCD arranca `page=identity`, `clicks=0`, `qr_generations=1`.
Primera imagen completa 67.200 píxeles; inicialización 696.336 µs, separada de
los tiempos de servicio después del arranque.

El propietario confirmó **nombre/teléfono completos y QR con el enlace WhatsApp
correcto**. Esto acredita el escaneo básico de esta composición/contacto en su
teléfono; no consta modelo, distancias, ángulos ni matriz Android/iPhone.
No se envió mensaje ni se hizo llamada para la prueba.

Ventana estática: 60,063 s, 105 reportes LCD y dos SYS. Tras `r`, cero flushes,
cero píxeles y QR generado una sola vez; máximo del servicio 372 µs. No hubo
redibujos con los que calcular p95 de dibujo: el cero reportado no es latencia
visible ni FPS. Pool LVGL libre/bloque mayor 38.708 B; heap 156.456 B en ambas
muestras, mínimo 155.696 B. Drops constantes en esas muestras, un registro unido
descartado; sin marcadores fatales, regresión del reloj GPS ni overflow GNSS.

Pendientes separados: acceso al AP/portal, matriz óptica ampliada, carga
conjunta GPS/tiras V2/V3 y uso portátil. VIS-4 es el
siguiente incremento de software visual; VIS-5 conserva su medición propia.

### Navegación y confirmación BOOT

Ventana USB 90,031 s: 155 reportes LCD, tres SYS. Se verificaron 30 eventos `n`
en orden circular, expiración en Placa, evento 31 de despertar sin avanzar y
32 de avance a Actividad. Termina en Placa, encendida, timer apagado. QR mantiene
una única generación; repetir el guardado devuelve `unchanged`, generación 1.

Máximo de servicio 44.976 µs; p95 mixto de navegación con cota superior 50.000 µs,
no p95 estático ni latencia visible de BOOT. Pool libre/bloque mayor 38.708 B,
heap 156.456 B estable en tres muestras; mínimo 154.008 B. Cero registros unidos,
marcadores fatales, regresiones de reloj GPS o overflow. Drops constantes en la
ventana; no se comparan como continuidad las pausas entre lectores USB.

Después, sin comandos automáticos, el propietario confirmó cuatro pulsaciones
físicas: **Actividad → Wi-Fi → Estado → FREYA**. Esto cierra la aceptación básica
VIS-3 de contacto, reinicio, escaneo y navegación en este banco. No cierra la
matriz óptica ampliada ni las condiciones portátiles/con periféricos.

La captura adicional sin comandos no registró incremento del contador de clics
(permaneció en 32). Acredita el estado final Placa encendida, pero no fecha las
cuatro liberaciones declaradas; la confirmación del ciclo físico se atribuye al
propietario, separada de los 32 eventos inyectados por USB.
