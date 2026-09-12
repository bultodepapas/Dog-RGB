# Identidad y diseño visual para RGB Dog

La siguiente evolución debe convertir el collar en una placa de identificación
legible y después enriquecer su interfaz. La recomendación es **nombre dominante,
QR WhatsApp, teléfono completo, negro puro y movimiento localizado**. La identificación debe
poder consultarse sin red; el portal conserva la edición y los detalles extensos.

Este informe sustenta el [subplan visual](PLANS/2026-09-12_display-visual-identity.md).
La [ampliación GitHub/MCP: 28 investigaciones](display-github-research-2026-09-12.md)
añade selección de herramientas, compatibilidad 8.4 y un ensayo host del encoder;
R01–R04 precisan la ruta QR y sus límites de memoria/recuperación.
Consulta de fuentes: 2026-09-12. Las propuestas de diseño se distinguen de las
capacidades observadas. No se ejecutaron ni midieron los repositorios externos.

## Referencias de uso e identificación

| Referencia primaria | Hallazgo | Aplicación al collar |
| --- | --- | --- |
| [Pawfit: Audio ID](https://support.pawfit.com/hc/en-gb/articles/360019554899-What-is-audio-ID), actualización indicada 2021-02-10 | Una persona puede pulsar el botón y escuchar información de identificación; el fabricante describe su utilidad fuera de cobertura | Adoptar acceso local a nombre/contacto. Nuestro equivalente inicial es texto; no supone altavoz, voz o conectividad Pawfit |
| [Samsung: SmartTag2 Lost Mode](https://news.samsung.com/global/user-guide-guard-your-bike-gear-with-the-galaxy-smarttag2), 2023 | Comparte contacto y mensaje mediante un teléfono con NFC | Contacto y mensaje corto son suficientes para la tarea. No copiar el requisito NFC: no forma parte del hardware integrado en este proyecto |
| [Apple: Medical ID en Apple Watch](https://support.apple.com/en-ie/guide/watch/apd58cd0b3f4/watchos), guía vigente consultada | Hay un acceso específico desde el botón lateral y una pantalla dedicada | Dar a Identificación una ruta clara. El gesto de deslizar no se traslada a la placa No Touch |
| [Google: principios Wear, Material 2.5](https://developer.android.com/design/ui/wear/guides/m2-5/foundations/design-principles), 2025-05-22 | Prioriza pocas tareas, consultas de segundos, trabajo complementario con teléfono y uso offline | Nombre/teléfono en collar; configuración en portal. Es una referencia de uso, no una norma para este LCD ni el sistema Material más reciente |

La inferencia para RGB Dog es directa: una identificación visible no debería
depender de que el propietario haya activado previamente «perdido», de que haya
fix GPS ni de que quien lo encuentre descargue una aplicación. El mensaje normal
puede ser **«Si estoy solo, llama a mi familia»**. No afirma que una pérdida se
haya detectado automáticamente.

El teléfono debe seguir siendo legible aunque no se pueda escanear un QR. Una
placa física conserva utilidad con el dispositivo apagado o sin energía; el LCD
no ofrece identificación pasiva. Esto afecta la elección de producto y montaje,
no exige agregar una plataforma cloud al proyecto.

## Dirección visual

| Dirección | Composición y personalidad | Decisión |
| --- | --- | --- |
| **Placa esencial** | Negro, nombre grande, teléfono blanco, pequeño acento cálido; contacto inmóvil | Base recomendada para identificación |
| **Paseo deportivo** | Una métrica dominante, cifras estables, estado textual con acento verde cuando corresponde | Evolución de Actividad, conservando fecha y validez |
| **Mascota expresiva** | Pequeña huella o retrato simplificado, una secuencia breve al entrar | Personalización opcional; no sustituye nombre/contacto ni reproduce movimiento constantemente |

La tipografía Wear distingue funciones de texto y recomienda cifras tabulares
cuando los números cambian, para evitar saltos de alineación. Adoptar jerarquía y
estabilidad; no trasladar tamaños en sp/dp ni textos curvos a nuestros píxeles.
[Google: Apply typography](https://developer.android.com/design/ui/wear/guides/styles/typography/apply).

La inspección de las capturas actuales muestra una base coherente: negro,
márgenes y separación clara. Sus oportunidades son el encabezado genérico
repetido, ausencia de identidad, exceso de texto técnico en relación con la
personalidad del collar y falta de una escala compartida explícita para iconos,
espaciado y movimiento. No hace falta sustituir las tres vistas para resolverlo.

El [tablero conceptual](assets/display-visual-identity/concepts.svg) presenta la
familia propuesta. Es un boceto vectorial, con tipografía aproximada y datos
ficticios; no es una captura LVGL, una prueba óptica ni una interfaz ya cargada.

## Repositorios inspeccionados

| Proyecto y pieza leída | Qué aprender | Qué no trasladar |
| --- | --- | --- |
| [InfiniTime: WatchFaceDigital.cpp](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/src/displayapp/screens/WatchFaceDigital.cpp) | Jerarquía de un dato central, indicadores secundarios; `Refresh()` comprueba cambios antes de modificar varios labels | Usa APIs LVGL de otra generación y controladores PineTime. Inspiración de estructura, no código compatible para pegar |
| [InfiniTime: README/licencias](https://github.com/InfiniTimeOrg/InfiniTime) | Producto completo con pantallas y componentes separados | Firmware GPL-3.0; copiar código tiene condiciones distintas de observar un patrón. No se importó código |
| [InfiniSim](https://github.com/InfiniTimeOrg/InfiniSim) | Simular la UI del firmware en PC acorta iteraciones | Su CMake/SDL y firmware no reemplazan el renderer que ya tenemos; SDL sigue siendo opcional |
| [LVGL: smartwatch demo, código actual](https://github.com/lvgl/lv_demos/blob/master/src/smartwatch/lv_demo_smartwatch.c) | Composición expresiva, grupos animados y transiciones con intención | Recomienda 384×384, usa `lv_display_t`/`lv_screen_active` y contempla Lottie. No es una demo lista para nuestro LVGL 8.4/240×280 |
| [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) y [T-Watch-S3](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-watch-s3.md) | Comparador de un wearable ESP32-S3 con ST7789 SPI y pantalla pequeña | Pantalla 240×240, táctil, PMIC y periféricos diferentes. Sus cifras de consumo, pines y comportamiento de botones no validan Waveshare |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) | Recortar rangos/glifos, elegir tamaño/bpp y generar fuentes para el dispositivo | No convertir todas las fuentes y alfabetos por defecto; registrar fuente, receta y versión |

La demo smartwatch actual merece atención especial: una animación atractiva en
su vídeo no demuestra que entre en el presupuesto de este collar. La inspección
del código confirma diferencias de resolución y API. Mantener LVGL 8.4.0 evita
que un cambio visual se convierta en una migración de plataforma.
[Código de la demo](https://raw.githubusercontent.com/lvgl/lv_demos/master/src/smartwatch/lv_demo_smartwatch.c).

## Pantalla y rendimiento: evidencia aplicable

La wiki oficial describe 240×280, ESP32-S3, 16 MB de flash y 8 MB PSRAM, y publica
ejemplos Arduino con LVGL 8.4.0. Su redacción combina variantes; no convierte la
placa **No Touch** disponible en táctil ni confirma su revisión física.
[Waveshare: ESP32-S3-LCD-1.69](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69).

El proyecto usa Arduino_GFX 1.6.7, LVGL 8.4.0, RGB565, SPI a 40 MHz, un buffer
parcial de 9.600 bytes y pool LVGL de 48 KiB. I6d midió 12,340 ms de máximo en
una ventana de refresco normal de Estado y 45,914 ms con navegación, sin GPS ni
tiras. Esos resultados limitan cualquier promesa de fluidez y no equivalen a
20/60 FPS. [Baseline local I6d](baselines/display-i6d-2026-09-12.md).

Cálculo propio de transferencia ideal, sin comandos ni renderizado:

| Región RGB565 | Bytes | Tiempo a 40 Mbit/s |
| --- | ---: | ---: |
| Pantalla 240×280 | 134.400 | 26,880 ms |
| Contenido 192×244 | 93.696 | 18,739 ms |
| Sprite 48×48 | 4.608 | 0,922 ms |

Un repintado completo a 60 FPS necesitaría 64,512 Mbit/s solo en píxeles, por
encima de 40 Mbit/s. Una animación pequeña puede requerir mucho menos tráfico;
el área invalidada real, el coste de composición y el resto del loop determinan
el resultado. No confundir estos cálculos ideales con mediciones del driver.

LVGL 8.4 permite buffers parciales y documenta que dos buffers solapan trabajo
cuando la transferencia ocurre en segundo plano; añadir memoria por sí solo no
crea paralelismo en nuestro envío síncrono. Su guía desaconseja tratar un
controlador externo por enlace serie como un framebuffer de refresco completo.
[Display interface 8.4](https://lvgl.io/docs/open/8.4/porting/display.html).

## Movimiento y recursos adecuados

**Primero una transición corta del indicador de página.** El contenido puede
cambiar de inmediato mientras un subrayado se desplaza 120–160 ms. Después
comparar un desplazamiento de contenido de 8–12 px durante 160–200 ms. Son
candidatos propios de diseño, no tiempos certificados por LVGL. No animar el
teléfono ni usar un fundido de toda la placa para ocultarlo durante la lectura.

LVGL 8.4 tiene animaciones de propiedades, curvas, cancelación y timelines.
Usarlas conserva un único dueño de la UI y permite resolver una pulsación nueva
o el apagado sin esperar a que termine un efecto.
[Animations 8.4](https://lvgl.io/docs/open/8.4/overview/animation.html).

Para una huella o mascota breve, `lv_animimg` reproduce una secuencia de imágenes
con duración/repetición configurable. Cuatro frames opacos de 32×32 RGB565 son
8.192 bytes de píxeles; ocho de 48×48 son 36.864, sin descriptores ni alfa. Usar
una secuencia pequeña almacenada en flash si aporta personalidad; GIF, Lottie,
vídeo y fondos animados quedan fuera de la primera entrega.
[Animation Image 8.4](https://lvgl.io/docs/open/8.4/widgets/extra/animimg.html).

Para nombres españoles, conservar una familia y convertir los glifos necesarios:
acentos, ñ, ü y signos usados. Montserrat se distribuye bajo SIL OFL; las fuentes
compiladas requieren guardar licencia y receta. Para iconos, comparar dibujos
propios mínimos con un subconjunto de Lucide: su licencia distingue las piezas
derivadas de Feather. Los SVG se convierten antes de compilar; no se presupone
un decodificador SVG en LVGL 8.4.
[Montserrat OFL](https://raw.githubusercontent.com/google/fonts/main/ofl/montserrat/OFL.txt),
[Lucide: licencia](https://lucide.dev/license),
[LVGL: fuentes 8.4](https://lvgl.io/docs/open/8.4/overview/font.html).

## QR WhatsApp: prioridad de la placa digital

El canal elegido para el primer prototipo es `https://wa.me/<número>`; WhatsApp
documenta el uso del teléfono internacional. Normalizarlo a dígitos en el enlace,
conservar el teléfono visible y no añadir mensaje prellenado inicialmente. No
requiere backend propio ni WhatsApp Business API. Abrir el enlace necesita
conectividad del teléfono del visitante y una cuenta de destino utilizable.
[Ayuda oficial de WhatsApp](https://faq.whatsapp.com/5913398998672934).

`tel:+<número>` queda como alternativa configurable: la acción que ofrece la
cámara depende de la aplicación y la llamada requiere telefonía. El collar
puede generar ambos QR offline. Una página pública propia añadiría alojamiento
y mantenimiento; `192.168.4.1` exigiría conectarse al AP y no es un identificador
universal. El QR se incorpora a la primera familia de diseños, manteniendo
siempre el teléfono como alternativa visible.

DENSO exige una zona libre de cuatro módulos por lado. Para N módulos y escala
entera s, reservar **(N + 8) × s** píxeles por lado: 29 módulos a 4 px requieren
148×148 px con margen. El N real se obtiene del contenido/encoder; no se fija
porque el teléfono de ejemplo quepa. No decorar, invertir ni animar el código.
[DENSO: área del QR](https://www.qrcode.com/en/howto/code.html).

LVGL 8.4 implementa QR sobre canvas indexado de un bit y realiza asignaciones
temporales al codificar. Hay que habilitar las dependencias de canvas/imagen,
reservar explícitamente el margen; el borde de cinco píxeles
del ejemplo no demuestra cuatro módulos para cualquier escala. Generar al
cambiar el contacto, nunca cada frame. El tamaño se mide con el payload final.
[QR 8.4](https://lvgl.io/docs/open/8.4/libs/qrcode.html),
[implementación v8.4.0](https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/src/extra/libs/qrcode/lv_qrcode.c).

La ampliación encontró que comprobar `LV_RES_INV` no basta para recuperar fallos
de asignación del wrapper. El plan elige encoder acotado y canvas propio del
componente, con fallback textual precreado; falta verificarlo en el renderer real.

## Foros: pistas, no resultados transferibles

| Caso | Observación publicada | Consecuencia para nuestro ensayo |
| --- | --- | --- |
| [ST7789 Watch, marzo 2021](https://forum.lvgl.io/t/esp32-pico-d4-st7789-watch/5048) | Autores reportan distintas velocidades SPI; una intervención describe artefactos al elevarla | No subir a 80/120 MHz para compensar un diseño pesado. Medir regiones antes de tocar el transporte |
| [LVGL v8 / CPU alta, agosto 2023](https://forum.lvgl.io/t/lvgl-v8-on-esp32-esp-idf-v4-4-cpu-usage-in-100-always-please-help/12762) | Discusión incompleta sobre imágenes grandes y cargas/decodificación repetidas | Ensayar assets residentes y pequeños. El hilo no ofrece un diagnóstico verificable para nuestra placa |
| [Origen de la demo smartwatch, julio 2024](https://forum.lvgl.io/t/how-to-obtain-the-source-code-for-the-official-perfect-smartwatch-gui-demo/17160) | Se remite a plantillas y otros repos para una demo concreta | Registrar la versión y procedencia exactas; una galería no equivale a un ejemplo compatible con 8.4 |

Estas conversaciones son testimonios de sus participantes, con hardware y
versiones distintos. Las decisiones técnicas se apoyan en documentación/código
oficial y en la baseline local, no en una velocidad publicada en un comentario.

## Selección final de investigación

Adoptar placa textual offline, nombre primero, jerarquía Wear adaptada, fuentes
recortadas y componentes LVGL pequeños. Experimentar después con indicador
animado, una entrada breve y un sprite opcional. Incorporar QR WhatsApp con
pruebas de escaneo desde la primera familia. Modo de ayuda explícito, tendencias
y batería conservan contratos y ensayos separados.

Las fuentes enlazadas no prueban legibilidad en el cuello del perro, facilidad
para encontrar BOOT, escaneo con teléfonos concretos ni consumo del montaje.
Las fichas comerciales describen funciones, no tasas de recuperación de mascotas.
Enlaces consultados sin contenido recuperable (PawTag, una ficha de Pawfit y
Red Dingo) no se utilizan como evidencia de conclusiones.
