# VIS-3 — Identidad integrada y firmware USB

Actualización posterior: [contacto USB, reinicio, QR y BOOT confirmados](display-identity-usb-2026-09-12.md). El resto de esta entrada conserva la primera carga sin contacto.

2026-09-12. **Integración implementada y cargada en COM6; configuración del
contacto y aceptación óptica pendientes.** Waveshare USB sin GPS ni tiras.
[Contrato/guía](../../Platformio/Dog-RGB/docs/display-vis3.md),
[plan visual](../PLANS/2026-09-12_display-visual-identity.md).

## Cambio

Store → copia propia → `IdentityView`, en el loop Arduino existente. Arranca
Placa si hay nombre/teléfono válidos; conserva Actividad si no. Ciclo adaptativo
de tres/cuatro páginas con numeración coherente. Los cuatro contenedores se
crean una vez. Editar conserva página y luz; borrar una Placa visible retira
el contacto y pasa a Actividad sin despertar. BOOT, diagnóstico y timeout opt-in
mantienen su contrato. `p` selecciona Placa configurada; QR queda inmóvil.

No cambia radio, persistencia LED/GPS, particiones, bibliotecas ni buffers.
Identidad real no incluida en el binario ni en fixtures públicos. Editor/API
VIS-2 están ahora disponibles en el firmware cargado.

## Software

- **11/11 CTest:** renderer, adaptadores y store/handlers anteriores; servicio
  ampliado y proceso separado para arranque con identidad persistida.
- **147/147 Python**, 53,597 s; después, 6/6 del helper serial al ampliar su
  lista de comandos con `p`. Sin modificación del portal ni regeneración web.
- **11 capturas** del framebuffer del servicio/port real, con SPI/NVS falsos:
  cinco QR con payload exacto leído por ZXing, seis imágenes sin QR.
  [Manifest y hashes](../assets/display-vis3/manifest.json).
- Las **14 PNG anteriores I6d permanecen idénticas** sin identidad configurada.
- Cubre copia sin alias, guardado rechazado, renombrar sin regenerar QR,
  actualización de llamada en oscuro, borrado, retorno desde barras/texto,
  30 ciclos de cuatro páginas y 30 expiraciones/despertares en Placa.
- Pool LVGL host estable al finalizar: **31.608 B libres**, bloque mayor igual.
  No escrituras NVS por navegación/despertar; los no-op no generan ni repintan QR.
  Host no acredita tiempos ni asignaciones reales de ESP32.

| Target | RAM estática B | Flash B | Delta RAM / flash vs VIS-2 |
| --- | ---: | ---: | ---: |
| Classic | 57.636 | 1.154.219 | 0 / 0 |
| Waveshare producto | 123.068 | 1.480.739 | +3.344 / +37.380 |
| Display diagnóstico | 123.068 | 1.482.499 | +3.336 / +37.384 |

Tres compilaciones correctas en 4:14,329. Inventario ELF: Classic sin
`IdentityView`, LVGL, ST7789 ni store de identidad; diagnóstico enlaza vista,
adaptador y store. Buffer 9.600 B, pool 48 KiB, SPI 40 MHz sin cambios.

## Carga y aceptación pendiente

Carga de etapa 3 en COM6 correcta, **30,830 s**, hash de escritura verificado.
Imagen de aplicación 1.511.120 B, SHA-256:
`861064af8835994af2cec8dc1693a87461e8d4032a7a1a03f0bd752abaeeb4c6`.
Conservada localmente en `Platformio/Dog-RGB/artifacts/display-vis3/flashed-display.bin`;
log de carga junto a ella. Sin borrado de NVS. Imagen I6d anterior conservada.

Antes de cargar: COM6 reportaba I6d, AP activo sin STA; no RX ni overflow GPS.
Este PC no está conectado al AP. Se solicitó configurar el nombre/contacto
facilitados desde `/config` en el teléfono; no se presume realizada esa escritura.

Pendiente: confirmar contacto guardado, lectura y QR óptico exacto, ciclo físico
de cuatro páginas y reinicio con identidad retenida. Medir ventana estática de
Placa y navegación por separado, sin confundir p95 mixto con refresco normal.
La prueba de reinicio/persistencia hasta aquí es host; aceptación física abierta.
V2/V3 con GPS/tiras, radio, alimentación y uso portátil siguen separados.

### Ventana USB inicial, sin identidad

45,141 s: 80 reportes LCD, dos SYS separados 30 s. Arranque y ciclo de las tres
páginas anteriores, `p` sin contacto vuelve a Actividad; apagado manual seguido
de `n` despierta sin avanzar. Terminó en Wi-Fi con luz encendida y timer apagado.
Generaciones QR=0, coherente con contacto aún sin configurar.

Máximo de servicio **43.879 µs**, p95 mixto de navegación con cota **50.000 µs**;
no es p95 de refresco estático ni latencia visible. Pool LVGL físico libre
**38.768 B**, bloque mayor igual; heap 156.980→156.976 B (dos muestras), mínimo
156.756 B. Log drops constantes en esas dos muestras, no desde el arranque.
Un registro unido descartado; sin marcador fatal ni regresión del reloj GPS;
RX/overflow=0 sin GNSS conectado. No acredita carga conjunta ni identidad visible.

El propietario reportó que no puede acceder al portal. Diagnóstico USB posterior:
AP activo `192.168.4.1`, cero clientes y cero asociaciones desde el arranque,
sin fallos de arranque AP; servicio HTTP sigue ejecutándose. Falta identificar
si el fallo está en descubrimiento, asociación o navegación antes de cambiar
radio/servidor. La escritura de identidad sigue pendiente.
