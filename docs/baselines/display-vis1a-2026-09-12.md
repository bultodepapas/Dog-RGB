# VIS-1a — QR de contacto compartido

Fecha: 2026-09-12. **Implementado y aceptado en host; sin carga a COM6.**
Continuación del [subplan visual](../PLANS/2026-09-12_display-visual-identity.md).
La imagen física sigue siendo I6d; VIS-1b, persistencia y cuarta página no están
implementados. La captura local solicitada usa FREYA y su contacto proporcionado
en la conversación; los assets versionados usan números sintéticos no marcables.

## Implementación

- `display/contact.{h,cpp}`: una representación canónica `+` + 7–15 dígitos,
  primer dígito no cero; acepta espacios/guiones/paréntesis y rechaza letras,
  extensiones, prefijo ausente, desbordes o entrada de 64 bytes. Es sintaxis de
  producto, no verificación de asignación telefónica ni de cuenta WhatsApp.
- `display/contact_qr.h`, `display/ui/contact_qr.cpp`: Nayuki de LVGL 8.4.0,
  `encodeBinary`, versiones 1–3, ECC M con aumento permitido, escala 4.
  Canvas indexado de 1 bit con quiet zone incluida de 16 px por lado;
  dimensiones 116/132/148 según matriz 21/25/29. Negro/blanco sin interpolación.
- Buffers persistentes propios; sin malloc del encoder ni modificación de
  `.pio/libdeps`. IMG/CANVAS habilitados; wrapper `lv_qrcode` no utilizado.
  La instancia no se copia y debe sobrevivir al canvas: `end()` antes de
  destruir su padre; en MCU usar almacenamiento persistente, no temporal del loop.
- Número y enlace salen del mismo formatter. Actualización idéntica normalizada
  no regenera ni invalida. Datos inválidos/QR desactivado ocultan el código previo;
  el consumidor conserva el teléfono válido y precrea su alternativa textual.
  El fixture incluye esa alternativa. `EncodeFailed` queda como guarda, pero no
  se fuerza: el contrato actual cabe en versión 3. No se promete recuperar OOM
  global de LVGL, cuyas asignaciones mantienen assertions.

## Evidencia ejecutada

| Comprobación | Resultado |
| --- | --- |
| Suite Python existente | 147 tests OK |
| CTest del simulador | 6/6 OK; incluye los cinco contratos I6d |
| Capturas I6d | 14 regeneradas con renderer compartido |
| Nuevas capturas públicas | 8 PNG de LVGL real, no un dibujo del QR en Python |
| ZXing 2.3.0 independiente | 6 códigos con payload exacto, 2 fallbacks con cero códigos |
| Captura local de FREYA | Un QR; enlace exacto del número solicitado, lectura automática OK |
| Geometría | Todos los píxeles del margen blancos; cada módulo 4×4 uniforme; límites seguros |
| Estabilidad | 100 updates equivalentes: cero generaciones/flush adicionales; 30 ciclos válido/máximo/inválido/desactivado/recuperado sin pérdida de pool |
| Ciclo de vida | Destrucción explícita, recreación y generación posterior OK |
| Compilaciones | Classic, Display producto y Display diagnóstico OK |

El lector verifica **cantidad, formato QR y payload completo**, no solo que
detecte una forma. Sigue la [API oficial ZXing Python](https://github.com/zxing-cpp/zxing-cpp/blob/master/wrappers/python/README.md).
No abre WhatsApp ni envía mensajes. El contexto tipográfico de esta fixture es
del banco del componente: nombre ASCII corto, Montserrat 20, teléfono 14;
no representa la aceptación de IdentityView, nombres largos o Unicode.

Assets: [manifest](../assets/display-vis1a/manifest.json),
[QR WhatsApp](../assets/display-vis1a/qr-whatsapp.png),
[número máximo](../assets/display-vis1a/qr-whatsapp-max.png),
[alternativa sin QR](../assets/display-vis1a/qr-disabled.png).

## Memoria y límites de medición

| Recurso | Medición |
| --- | --- |
| Canvas reservado, incluida paleta/padding | 2.820 bytes dentro de la instancia |
| Dos arrays de encoder | 214 bytes dentro de la instancia |
| `sizeof(ContactQr)` host 64 bits | 3.104 bytes, incluye buffers/metadatos/puntero |
| `sizeof(ContactQr)` Xtensa ESP32-S3 | 3.100 bytes; símbolo objeto `0x0c1c` compilado con toolchain actual |
| Pool libre antes / creación / primer QR / estable | 43.864 / 43.736 / 43.632 / 43.632 bytes en fixture host |
| Delta de pool observado | 232 bytes; se suma al almacenamiento propio, no lo sustituye |
| Stack estático del compilador Xtensa `-Os` | `ContactQr::update` 144 bytes, `encodeBinary` 80, `encodeSegmentsAdvanced` 144, **por función**, sin sumar callees |

[Memoria host](../assets/display-vis1a/qr-memory.txt) y reportes de stack
[componente](../assets/display-vis1a/qr-component-stack.txt) /
[encoder](../assets/display-vis1a/qr-encoder-stack.txt).
Los `.su` son estimaciones por función en compilación aislada; no son un
high-water mark del task ni el máximo total de la cadena LVGL/encoder/libc.
Pool MCU, stack efectivo, heap mínimo/bloque mayor, primer render y tiempos
permanecen como mediciones de integración VIS-3. El test solo acredita
estabilidad y presupuesto de pool del componente en host.

| Target | RAM / flash reportados por PlatformIO |
| --- | --- |
| `seeed_xiao_esp32s3` | 57.636 / 1.151.879 bytes |
| `waveshare_lcd169` | 119.644 / 1.435.675 bytes |
| `waveshare_lcd169_displaycheck` | 119.652 / 1.437.399 bytes |

Estos tamaños permanecen iguales a I6d porque aún no hay una instancia de
`ContactQr` en el port y el linker descarta sus funciones no referenciadas.
La compilación valida compatibilidad del módulo; **no mide el coste final de
flash/RAM de identidad activada**. Presupuesto +96 KiB e integración de cuatro
vistas deben comprobarse al enlazarlas. Classic sigue excluyendo `display/`.

## Reproducción y continuación

Desde la raíz, ejecutar `python tools/display-simulator/render.py` y el flujo
de [render/decoder QR](../../tools/display-simulator/README.md#contact-qr-vis-1a).
El runner QR fija versiones en `requirements-qr.txt`, genera manifest y falla
ante error de contrato/decoder. Los datos locales se pasan mediante `--name`
y `--phone`; permanecen en `tools/display-simulator/output/`, ignorado por Git.

Para reproducir sizeof/stack, usar `xtensa-esp32s3-elf-g++`/`gcc` de la toolchain
PlatformIO actual, `-Os`, `-DLV_CONF_INCLUDE_SIMPLE` y los includes absolutos de
`Platformio/Dog-RGB/include` y su LVGL 8.4.0. Compilar una TU con
`char contact_qr_size[sizeof(display::ContactQr)];` y leer el tamaño con `nm -S`.
Compilar `contact_qr.cpp` y `qrcodegen.c` con `-fstack-usage` produce los `.su`.

**Siguiente: VIS-1b.** Extraer el contexto de nombre/teléfono a `IdentityView`,
fijar fuente/glifos, validar NFC/UTF-8/ancho y comparar layouts A/B. Después
VIS-2 configura identidad por portal y VIS-3 incorpora la página y el escaneo
físico. El QR host no cierra Android/iPhone, luz/ángulos, BOOT/despertar, consumo
portátil ni convivencia con GPS y tiras ausentes.
