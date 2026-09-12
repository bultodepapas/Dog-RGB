# Display RGB Dog: 28 investigaciones de herramientas y técnicas

Fecha de consulta: **2026-09-12**. Resultado: selección técnica para el
[subplan visual](PLANS/2026-09-12_display-visual-identity.md), sobre I6d.
Placa objetivo: Waveshare LCD 1.69 No Touch, 240×280, SPI RGB565 a 40 MHz;
LVGL 8.4.0 y Arduino_GFX 1.6.7. Classic continúa independiente.

Se investigaron **28 preguntas distintas**, agrupando alternativas cuando
resuelven la misma necesidad. Se leyeron repositorios, código y documentación
primaria; no se cuentan búsquedas repetidas ni páginas inaccesibles como hallazgos.
**Compatibilidad documental no significa compilación o rendimiento comprobados.**
Solo se ejecutó el ensayo host del encoder descrito al final; no se cargó firmware.

MCP usados: **codebase-memory-mcp** para descubrimiento local, **Context7** para
consultar LVGL 8.4 y **Semble** para explorar el QR del repositorio remoto.
La búsqueda local resultó insuficiente para piezas nuevas del display y se
completó leyendo archivos conocidos. Semble encontró APIs de `master` que no
existen en 8.4; incluso una respuesta de Context7 mezcló una referencia de
`master`. Se contrastaron las decisiones con el tag `v8.4.0` y la dependencia local.
Los enlaces a ramas móviles documentan lo consultado, no fijan dependencias;
cualquier herramienta adoptada deberá registrar versión/commit y receta.

## Selección que cambia el plan

| Aplicación | Selección | Paquete |
| --- | --- | --- |
| Placa digital | Nombre + teléfono estáticos, QR WhatsApp con margen explícito | VIS-1–3 |
| QR comprobable | Nayuki ya incluido; encoder acotado y lectura independiente con ZXing | VIS-1 |
| Nombre expresivo | Montserrat recortada, glifos españoles, ancho real y normalización | VIS-1–2 |
| Iconos | Subconjunto coherente; SVG convertido fuera del ESP32 | VIS-4 |
| Fluidez | Animación nativa localizada; invalidaciones y cancelación medidas | VIS-5 / I6b |
| Personalidad | Huella de cuatro frames, opcional y breve | VIS-6 |
| Revisión con IA | Renderer actual, fixtures, capturas y pruebas semánticas | Todos |
| Herramientas opcionales | resvg, EEZ/SquareLine, WASM; cada una exige demostrar utilidad | Sin bloquear VIS-1 |
| Transporte | Conservar Arduino_GFX; DMA solo ante un cuello medido | Experimento posterior |

## Identificación y QR

### R01 — Nayuki: generación local y acotada

**Pregunta:** ¿podemos generar el contacto sin servidor ni otra dependencia pesada?
Nayuki ofrece C, módulos crudos, límites de versión y selección de corrección;
licencia MIT. LVGL ya incluye una copia. La propuesta es reutilizar esa copia
con buffers dimensionados para el contrato del teléfono, conservando su licencia.
[Repositorio y funciones](https://github.com/nayuki/QR-Code-generator).

**Decisión:** adoptar en VIS-1. El ensayo local admite los tres payloads ficticios
con versiones 1–3 y 214 bytes para los dos arrays del encoder. Esa cifra excluye
stack interno, canvas, objetos y metadatos. Verificar todos los límites del
formatter; devolver fallo explícito si el payload excede el contrato. No asumir
que `encodeText` y `encodeBinary` producen la misma matriz o nivel efectivo.

### R02 — Wrapper QR de LVGL 8.4: geometría y asignaciones

**Pregunta:** ¿basta con crear un widget de 132×132? El wrapper calcula escala,
puede ampliar versión y centra el símbolo; ese centrado no garantiza quiet zone.
También usa asignaciones temporales con assertions, sin recuperación completa
de punteros nulos en `update`.
[Implementación v8.4.0](https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/src/extra/libs/qrcode/lv_qrcode.c).

**Decisión:** VIS-1 comienza por resolver este contrato. Preferencia: encoder
acotado y canvas indexado controlado por `ContactQr`; alternativa nativa solo
si cumple geometría y manejo de fallo. No editar `.pio/libdeps`. La prueba debe
inspeccionar módulos, borde y retorno de error del componente real, no solo
comparar el boceto SVG. El ensayo de abajo demuestra el problema geométrico.

### R03 — Área del QR: tamaño útil frente a decoración

**Pregunta:** ¿cuánto espacio reservar sin sacrificar lectura? DENSO establece
cuatro módulos libres por lado. El total es `(N+8)×s`; el panel necesita su
propia validación óptica, pues la guía explica impresión.
[Guía del creador del QR](https://www.qrcode.com/en/howto/code.html).

**Decisión:** adoptar margen blanco real, escala entera inicialmente 4, sin logo,
antialiasing ni esquinas redondeadas que invadan el código. N=25 exige 132 px
totales; N=29, 148 px. Ensayar luz, ángulo y distancia con el collar montado;
registrar aciertos sobre diez intentos por condición. Si no cabe, usar la
composición textual; reducir el margen para acomodar decoración no es aceptable.

### R04 — ZXing y quirc: verificar el QR con un lector independiente

**Pregunta:** ¿cómo detectar un QR que parece correcto pero codifica mal?
ZXing-C++ ofrece bindings Python con lectura de imágenes y acceso al contenido.
Quirc es una alternativa C con identificación desde escala de grises y
decodificación separadas; su núcleo tiene licencia ISC.
[ZXing Python](https://github.com/zxing-cpp/zxing-cpp/blob/master/wrappers/python/README.md),
[quirc](https://github.com/dlbeer/quirc).

**Decisión:** seleccionar ZXing como herramienta host de VIS-1; quirc queda como
alternativa, sin instalar ambos por defecto. Decodificar el recorte de la captura
LVGL y exigir un único payload idéntico al derivado del teléfono canónico.
Rotación/desenfoque sintéticos son diagnóstico adicional; no reemplazan cámara
física. Los decoders no se incorporan al collar y la prueba no abre ni envía mensajes.

## Tipografía, iconos y assets

### R05 — lv_font_conv: legibilidad con coste controlado

**Pregunta:** ¿cómo tener un nombre bonito sin cargar todas las fuentes?
El conversor oficial exporta C para LVGL, permite rangos/símbolos, tamaño y bpp,
preserva kerning y ofrece salida de inspección.
[README y opciones](https://github.com/lvgl/lv_font_conv/blob/master/README.md).

**Decisión:** adoptar receta fijada para un tamaño principal y el conjunto
realmente usado. Comparar 2 y 4 bpp en nombres/acentos; habilitar compresión solo
tras comprobar el decoder/configuración de 8.4. El formato generado debe compilar
en ambos renderers. Aceptación: glifos completos, nombre largo sin recorte,
teléfono íntegro y delta de flash registrado. No dar por idéntico el alto nominal
de fuente y la caja real del texto ni reemplazar el número por una imagen.

### R06 — utf8proc: nombres con tildes y representaciones equivalentes

**Pregunta:** ¿por qué un mismo nombre puede ocupar bytes diferentes?
utf8proc documenta normalización NFC/NFD y operaciones Unicode, con implementación
C y licencias MIT/datos Unicode.
[Documentación del proyecto](https://github.com/JuliaStrings/utf8proc).

**Decisión:** adoptar el contrato de normalización, sin añadir toda la biblioteca
al MCU inicialmente. El portal normaliza NFC; el firmware valida UTF-8 y el
subconjunto explícito, independientemente del navegador. Definir el límite sobre
texto normalizado: 48 bytes/24 puntos de código admitidos más ancho real.
Probar `Niño`, `René` y sus formas descompuestas; entrada directa no canónica debe
tener respuesta explícita. No confundir bytes, puntos de código y grafemas ni
eliminar tildes silenciosamente. Ampliar alfabetos exige otro contrato de fuentes.

### R07 — Lucide: una familia pequeña de símbolos

**Pregunta:** ¿cómo unificar Wi-Fi, teléfono, huella y estados?
Lucide proporciona una biblioteca de iconos; su licencia distingue ISC y piezas
derivadas de Feather bajo MIT.
[Repositorio](https://github.com/lucide-icons/lucide),
[licencia](https://github.com/lucide-icons/lucide/blob/main/LICENSE).

**Decisión:** candidato VIS-4: escoger 6–8 iconos o dibujar equivalentes propios
con una única geometría. Comparar tamaños 16/20/24 px y trazo visible en RGB565.
Es una propuesta visual, no una propiedad certificada del panel. Convertir solo
los elegidos; registrar licencia por asset. GPS y Wi-Fi mantienen etiqueta textual:
el símbolo no puede sugerir Internet, localización válida o salida LED medida
cuando esos estados no se conocen.

### R08 — resvg: SVG a imagen reproducible fuera del dispositivo

**Pregunta:** ¿cómo convertir iconos sin depender de capturas del navegador?
resvg dispone de CLI y biblioteca para SVG estático; no reproduce animaciones
ni scripts. Documenta renderizado reproducible y licencias MIT/Apache-2.0.
[README](https://github.com/linebender/resvg).

**Decisión:** candidato host para VIS-4, especialmente útil ante la dependencia
Cairo/DLL encontrada en Windows. Fijar versión, tamaño y fuentes de entrada;
después convertir al formato LVGL 8.4. Comparar PNG de referencia con captura
real del asset convertido. El SVG nunca se interpreta en la placa por esta vía.
El negro y los bordes con alfa deben comprobarse después de RGB565; la promesa
del renderer no cubre una segunda conversión configurada incorrectamente.

### R09 — LibreSprite: diseñar una mascota de pocos frames

**Pregunta:** ¿qué herramienta sirve para una huella animada pequeña?
LibreSprite ofrece edición por capas/frames, paletas, previsualización y onion
skinning. Es un editor GPLv2 derivado de la etapa libre de Aseprite.
[Proyecto y funciones](https://github.com/LibreSprite/LibreSprite).

**Decisión:** opcional VIS-6. Crear cuatro frames propios de 32×32 sobre negro,
revisados a escala 1:1; exportar imágenes y convertirlas previamente. Guardar
fuente editable, paleta, orden y duración. La licencia del editor y la procedencia
de imágenes importadas son asuntos distintos. Aceptación: silueta reconocible,
ausencia de bordes residuales y una sola reproducción; no retrasar la placa de
contacto ni presentar la animación como movimiento detectado del perro.

### R10 — lv_animimg: reproducir frames sin un decoder de vídeo

**Pregunta:** ¿cómo llevar ese sprite al firmware existente?
El widget de LVGL 8.4 acepta un array de imágenes y duración/repeticiones.
[Documentación fijada](https://github.com/lvgl/lvgl/blob/v8.4.0/docs/widgets/extra/animimg.md).

**Decisión:** preferirlo para el experimento VIS-6; primero comprobar sus
dependencias en `lv_conf.h`. Cuatro frames opacos RGB565 de 32×32 representan
8.192 bytes de píxeles, cálculo propio que excluye descriptores/alfa. El array y
las imágenes deben vivir durante la reproducción. Detener al ocultar/apagar;
capturar cada frame y diez ciclos de navegación. El pequeño tamaño almacenado
no demuestra por sí solo un repintado pequeño: medir el área enviada.

### R11 — PNGdec: coste de decodificar en ejecución

**Pregunta:** ¿conviene guardar PNG para ahorrar flash?
PNGdec documenta decodificación por callback, sin malloc/free internos, y un
objetivo MCU con al menos 48K de RAM libre; no admite PNG interlazado.
[README del autor](https://github.com/bitbank2/PNGdec).

**Decisión:** aplazar el decoder en el collar. Para los primeros iconos, usar
assets convertidos a C; PNG queda como formato de autoría/capturas. La cifra del
autor no equivale al consumo adicional de nuestra integración. Si se propone
una foto configurable, comparar flash comprimida, RAM de trabajo, composición
alfa y latencia de primera apertura con la alternativa preconvertida; debe
pasar el mismo presupuesto de UI y no decodificarse en cada refresco.

### R12 — AnimatedGIF: transparencia y disposición de frames

**Pregunta:** ¿por qué un GIF pequeño puede dejar rastros o ir lento?
El autor documenta compromisos entre canvas, paletas locales, transparencia,
disposición de frames y RAM. Turbo añade memoria; las cifras de velocidad
dependen de la imagen y plataforma.
[Limitaciones técnicas](https://github.com/bitbank2/AnimatedGIF).

**Decisión:** diferir. Una animación breve propia se entrega como frames
precompuestos. Si más adelante se acepta GIF importado, el ensayo debe incluir
disposal, paletas locales y transparencia alternada, y pasar por el dueño LVGL
del display. No introducir un segundo callback que dibuje directamente por SPI
mientras LVGL mantiene una versión distinta de los píxeles.

### R13 — ThorVG/Lottie: atractivo visual frente a migración

**Pregunta:** ¿podemos copiar las animaciones vectoriales de demos recientes?
El widget actual de LVGL integra ThorVG y renderiza sobre un buffer ARGB8888
de `ancho×alto×4`. Esa API no es la de nuestro stack 8.4.
[Código/documentación actual](https://github.com/lvgl/lvgl/blob/master/docs/src/widgets/lottie.mdx),
[ThorVG](https://github.com/thorvg/thorvg).

**Decisión:** no incluirlo en VIS-1–5. Una región de 96×96 requiere 36.864 bytes
solo para ese buffer; 240×280, 268.800, antes del motor. Son cálculos, no
mediciones. Mantenerlo como referencia de diseño: una secuencia atractiva puede
reinterpretarse con pocos frames originales. Migrar LVGL o el motor por Lottie
sería un experimento separado con su propia comparación de recursos.

## Movimiento, interacción y transporte

### R14 — lv_anim: curvas y sustitución de transiciones

**Pregunta:** ¿cómo lograr movimiento fluido sin una cola tras varios clics?
LVGL 8.4 incluye curvas, cancelación y callbacks; una nueva animación sustituye
la anterior para el mismo par variable/función.
[Animaciones 8.4](https://github.com/lvgl/lvgl/blob/v8.4.0/docs/overview/animation.md).

**Decisión:** adoptar en VIS-5. Indicador 120–160 ms con ease-out; entrada corta
de título después, si aporta valor. Mantener identidad de variable/callback,
cancelar al ocultar y resolver el estado final al apagar. Evitar crear callbacks
diferentes que eludan la sustitución. Capturar t=0/40/80/120/160/200 ms, clic
intermedio y timeout. QR/teléfono quedan inmóviles; el modo instantáneo sigue disponible.

### R15 — AceButton: contrato de gesto antes de cambiar biblioteca

**Pregunta:** ¿cómo añadir un acceso largo sin un clic extra al soltar?
AceButton separa pressed/released/clicked/long-pressed/long-released y permite
ajustar temporización.
[API y eventos](https://github.com/bxparks/AceButton).

**Decisión:** aprender del modelo de eventos, conservar el botón ya validado.
Un atajo VIS-6 necesita definir cuándo dispara, cómo consume la liberación y
qué hace en oscuro. No habilitar doble clic/repetición por disponibilidad de la
librería. Ensayar rebotes, umbral, pulsación sostenida y primera pulsación de
despertar. El control No Touch gana claridad con una acción predecible y una
indicación física del botón; una biblioteca no resuelve el descubrimiento del gesto.

### R16 — Invalidación parcial y doble buffer

**Pregunta:** ¿qué mejora más la fluidez del SPI actual?
LVGL permite render parcial; dos buffers solapan trabajo cuando el transporte
ocurre en segundo plano. `flush_ready` indica que el buffer puede reutilizarse.
[Port de display 8.4](https://github.com/lvgl/lvgl/blob/v8.4.0/docs/porting/display.md).

**Decisión:** conservar buffer/pool y medir píxeles enviados por efecto. Evitar
opacidad de toda la pantalla, sombras extensas y transformaciones del padre que
amplíen el repintado. Los 26,880 ms ideales de un frame completo RGB565 a
40 Mbit/s ya excluyen 60 FPS de repintado completo. Doble buffer o PSRAM solos
no corrigen el límite de enlace. Mantener snapshots de datos separados de
framebuffers gráficos; no reservar una imagen completa por cada página.

### R17 — Arduino_GFX 1.6.7: entender el envío actual

**Pregunta:** ¿el driver actual deja libre la CPU mientras transmite?
La implementación ESP32SPI inspeccionada transmite bloques y espera mediante
`POLL`; no equivale a una cola DMA asíncrona.
[Código de la versión usada](https://github.com/moononournation/Arduino_GFX/blob/v1.6.7/src/databus/Arduino_ESP32SPI.cpp).

**Decisión:** mantenerlo como referencia de banco. Medir render, transferencia,
servicio y botón→píxel como magnitudes distintas. No adelantar `flush_ready`
para aparentar velocidad. Si un efecto falla, reducir primero área y frecuencia.
La pantalla negra ya aprobada, offsets, orden de bytes y backlight forman parte
de la regresión ante cualquier cambio de transporte.

### R18 — LovyanGFX: candidato DMA y sprites

**Pregunta:** ¿qué alternativa merece un benchmark si SPI bloquea demasiado?
LovyanGFX documenta ESP32-S3, ST7789, DMA y sprites con transformaciones.
[Repositorio y matriz de soporte](https://github.com/lovyan03/LovyanGFX).

**Decisión:** candidato posterior, sin añadir otro driver al producto ahora.
El soporte del controlador no confirma pines/offsets/revisión de nuestra placa.
Un ensayo debe reemplazar únicamente el transporte tras la misma UI LVGL y
comparar igual payload, reloj, carga y memoria. Medir si libera tiempo para
GPS/LED/HTTP. Sus sprites son una alternativa de composición, no una razón para
dibujar por fuera de LVGL ni trasladar sus FPS de demo al collar.

### R19 — TFT_eSPI: antialiasing y paletas como referencia

**Pregunta:** ¿qué aprender de sus medidores e imágenes compactas?
TFT_eSPI contiene gráficos suavizados, ejemplos y soporte SPI/DMA para S3;
requiere configuración concreta de controlador y placa.
[README técnico](https://github.com/Bodmer/TFT_eSPI).

**Decisión:** observar terminaciones de arcos, contraste y economía de regiones;
recrear solo lo útil con widgets LVGL. Un arco necesita una magnitud/meta válida,
no un porcentaje decorativo. Mantener TFT_eSPI como comparador, sin importar su
stack junto a Arduino_GFX. Una paleta reduce almacenamiento del asset, pero el
LCD sigue recibiendo RGB565 con nuestro transporte: no atribuirle una reducción
automática equivalente del tráfico SPI.

### R20 — esp_lvgl_port: tareas, DMA y propiedad de la UI

**Pregunta:** ¿qué aporta el port oficial de Espressif?
Integra `esp_lcd`, tareas/timers, buffers y locks. Declara soporte LVGL8/9 con
advertencias de compatibilidad; la selección predeterminada puede traer otra versión.
[README oficial](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/README.md).

**Decisión:** referencia arquitectónica para un eventual port DMA. No pegar sus
ejemplos sobre el scheduler actual: duplicaría responsabilidades. Exigir versión
fijada, un único dueño LVGL, devolución de buffers al completar transmisión y
pruebas de apagado/cancelación. PSRAM y buffer DMA no se tratan como intercambiables.
Ese trabajo es independiente de diseñar la placa digital y no bloquea VIS-1.

### R21 — ESP32_Display_Panel: ST7789 no equivale a placa soportada

**Pregunta:** ¿podemos tomar una configuración Waveshare ya hecha?
La librería integra bus, LCD, backlight y otros periféricos, con placas propias
y personalizadas. La tabla consultada enumera otras Waveshare, pero no la
LCD-1.69 No Touch; sí incluye el controlador ST7789.
[Tabla oficial](https://github.com/esp-arduino-libs/ESP32_Display_Panel).

**Decisión:** referencia, no sustitución inmediata. No copiar pines de una
Touch-1.85/2.1/4.3. Cualquier port necesita mapa y revisión reales, orientación,
offsets, inversión y BOOT/backlight verificados. Los problemas de paneles con bus
RGB paralelo no se diagnostican como si fueran nuestro SPI por compartir ESP32-S3.

## Herramientas de autoría, simulación y utilidades

### R22 — EEZ Studio: autoría visual opcional

**Pregunta:** ¿vale la pena un editor visual para acelerar layouts?
EEZ Studio declara soporte LVGL8/9 y distingue la licencia GPLv3 del editor de
las condiciones del código generado; EEZ Flow añade su propio alcance.
[README y propiedad del código](https://github.com/eez-open/studio).

**Decisión:** opcional; comparar un componente de Placa contra el flujo C++
actual. Exportar para 8.4, sin Flow ni control instrumental. Aceptar solo si
compila, permite regenerar sin pisar lógica manual y produce capturas equivalentes
con menor esfuerzo. Registrar archivos generados y hooks de integración. No
convertir la herramienta en dependencia obligatoria para compilar o contribuir.

### R23 — SquareLine Studio: frontera de exportación

**Pregunta:** ¿cómo evitar que un editor reemplace el firmware existente?
Su guía separa exportar proyecto y exportar archivos de UI; permite integrar
estos últimos en un proyecto preparado. La selección de LVGL depende del proyecto.
[Flujo oficial](https://docs.squareline.io/docs/introduction/typical_dev/),
[ajustes](https://docs.squareline.io/docs/dev_env/project_settings/).

**Decisión:** alternativa a EEZ, no ambos a la vez. La versión/exportador exacto
para 8.4 queda por comprobar con una compilación, sin prometer compatibilidad
por la etiqueta «LVGL». Mantener inicialización y eventos bajo el código del
collar. Evaluar licencia vigente si se elige; esta investigación no requiere
comprar ni instalar. La captura compartida decide equivalencia, no el preview del editor.

### R24 — lv_web_emscripten: UI real revisable en navegador

**Pregunta:** ¿podemos revisar componentes LVGL sin flashear?
El port oficial compila mediante Emscripten/CMake y genera HTML/JavaScript;
documenta restricciones al abrir archivos locales.
[Port y construcción](https://github.com/lvgl/lv_web_emscripten).

**Decisión:** opción host posterior para una galería interactiva. Adaptar nuestras
vistas y fijar LVGL8.4; el `master` del port no certifica esa combinación. Servir
en localhost, mapear clic a BOOT y simular tiempo/datos sin hardware. Conservar
el runner nativo para CI; WASM se adopta si mejora las revisiones. No entregar
una réplica HTML/CSS como prueba del layout LVGL ni exigir publicación externa.

### R25 — lv_snapshot: captura de componente y coste oculto

**Pregunta:** ¿conviene convertir cada página en imagen para transiciones?
Snapshot de 8.4 captura objeto/hijos y ofrece asignación dinámica o buffer del
llamador; documenta liberación y formatos soportados.
[API fijada](https://github.com/lvgl/lvgl/blob/v8.4.0/docs/others/snapshot.md).

**Decisión:** aprovechar conceptualmente para inspección host de componentes;
el framebuffer del simulador actual sigue siendo suficiente. No cachear cuatro
pantallas en RAM ni confundir snapshot de datos con una captura gráfica. Si se
ensaya una miniatura, calcular memoria del formato real y vida del descriptor,
desvincularla antes de liberar y comprobar caché. La regresión temporal debe
avanzar reloj virtual y verificar contenido, no solo la existencia de un PNG.

### R26 — InfiniTime e InfiniSim: aprender de un wearable completo

**Pregunta:** ¿cómo organizar un dato dominante y estados secundarios?
`WatchFaceDigital::Refresh` compara cambios antes de actualizar varios labels y
separa ciclo de vida del refresco. InfiniSim permite ejecutar la UI en PC con
CMake/SDL y capturas opcionales.
[Vista real](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/src/displayapp/screens/WatchFaceDigital.cpp),
[simulador](https://github.com/InfiniTimeOrg/InfiniSim).

**Decisión:** adoptar el patrón de actualización por cambio y jerarquía, no copiar
APIs antiguas ni sensores/gestos del reloj. El nombre sustituye protagonismo de
marca; una métrica domina Actividad. No añadir pasos, pulso o sueño que el collar
no mide. Cualquier reutilización de código requiere revisar licencia; aquí se
usa como referencia de arquitectura y diseño.

### R27 — lvgl-tool: inspección útil, exportación todavía no confiable

**Pregunta:** ¿qué herramienta emergente merece seguir investigando?
Una pista comunitaria llevó a este conversor web. Su README ofrece inspección
de fuentes/imágenes C y formatos v7/v8/v9, pero reconoce estructuras no
verificadas contra un checkout concreto y compresión v9 experimental propia.
[Funciones y limitaciones declaradas](https://github.com/exendahal/lvgl-tool).

**Decisión:** exploración opcional para mirar assets, no generador de producción
en VIS-1. Mantener `lv_font_conv` como receta de fuentes. Si se prueba su salida,
compilar y renderizar en nuestra versión, validar bytes/colores/alfa y comparar
con la referencia. El hallazgo refuerza una regla: «compatible con LVGL» en la
portada no basta para confiar en structs, compresión o kerning exportados.

### R28 — lv_chart: una tendencia que respete huecos GPS

**Pregunta:** ¿qué widget agrega utilidad real después de identidad?
LVGL8.4 ofrece arrays externos, actualización circular/desplazada y
`LV_CHART_POINT_NONE` para omitir puntos o segmentos.
[Contrato de chart](https://github.com/lvgl/lvgl/blob/v8.4.0/docs/widgets/extra/chart.md).

**Decisión:** candidato VIS-6 posterior al contrato de muestras. Prototipo propio:
30 muestras, una por segundo, región 160×36, sin grid denso ni desplazamiento
continuo. Ventana temporal explícita; huecos conservados, muestra caducada no
convertida en cero. Array persistente y escala estable con unidades. Una ausencia
de señal no es descanso y una gráfica no habilita un modo salud. Aceptación:
hueco/reanudación correctos y coste de refresco dentro del presupuesto.

## Ensayo host: geometría y memoria del encoder

Ejecutado con Clang y la copia local de `qrcodegen.c` usada por el target Display.
SHA-256 de esa entrada:
`9f1f23b8f800784740da16b9fff08ad0b75072dedb00caed2ae0d4475419cd34`.
[Fuente del ensayo](assets/display-github-research/qr_geometry_probe.c),
[doce casos geométricos](assets/display-github-research/qr-geometry.csv),
[tres casos con buffers acotados](assets/display-github-research/qr-workspace.txt).

El programa ejecuta el encoder y reproduce el cálculo de tamaño del wrapper;
**no ejecuta el renderer LVGL, no decodifica imágenes y no prueba escaneo físico**.
Todos los contactos son ficticios, intencionadamente no utilizables. No se abrieron.

| Payload ficticio | Canvas solicitado | N resultante | px/módulo | Margen izquierdo interno | Margen necesario |
| --- | ---: | ---: | ---: | ---: | ---: |
| WhatsApp, 26 bytes | 132 | 25 | 5 | 3 | 20 |
| WhatsApp, 29 bytes | 132 | 33 | 4 | 0 | 16 |
| WhatsApp, 29 bytes | 116 | 29 | 4 | 0 | 16 |

**Consecuencia de diseño:** reservar 132/148 px totales y un símbolo interior
100/116 px cuando N=25/29 a escala 4. El margen externo es 16 px por lado;
no pasar el tamaño total como si fuera el tamaño de la matriz. La ruta acotada
codificó 26/29/20 bytes con N=25/29/25, respectivamente, usando dos arrays de
107 bytes. Son tres ejemplos; VIS-1 todavía debe cubrir el dominio completo,
render, decodificación y fallo del componente.

Reproducción desde la raíz, con Clang y dependencias Display ya presentes:

```powershell
$qrDir = 'Platformio/Dog-RGB/.pio/libdeps/waveshare_lcd169/lvgl/src/extra/libs/qrcode'
$qrOut = 'Platformio/Dog-RGB/artifacts/display-visual-plan'
New-Item -ItemType Directory -Force $qrOut | Out-Null
clang -DLV_CONF_SKIP -I $qrDir docs/assets/display-github-research/qr_geometry_probe.c "$qrDir/qrcodegen.c" -o "$qrOut/qr_geometry_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'No compila el ensayo QR' }
& "$qrOut/qr_geometry_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'Fallo del ensayo QR' }
```

## Traducción a implementación gradual

| Orden | Trabajo acotado | Criterio de salida |
| --- | --- | --- |
| VIS-1a | Contacto canónico ficticio + encoder/render QR acotado | Tamaño/margen correctos; payload exacto leído por decoder independiente; error recuperable |
| VIS-1b | Nombre, teléfono y dos layouts con fuentes finales | Capturas 1:1; límites Unicode/ancho; todos los dígitos visibles; memoria/flash comparadas |
| VIS-2 | Guardado/editor local | NFC/validación alineadas; cambio de contacto coherente; recuperación y reinicio |
| VIS-3 | Cuarta página en placa | BOOT/despertar conservados; QR físico y lectura aprobados |
| VIS-4 | Tokens e iconos mínimos | Misma familia visual en las cuatro vistas, estados preservados |
| VIS-5 | Un efecto localizado | Capturas temporales, cancelación, regiones y latencia medidas; alternativa instantánea |
| VIS-6 | Una utilidad opcional | Sprite, atajo o tendencia con contrato y evidencia propios |

No se incorporan 28 dependencias. La selección inmediata reutiliza LVGL/Nayuki,
añade herramientas host solo donde verifican un requisito y conserva los
presupuestos de memoria y rendimiento del subplan. V2/V3 físicos siguen abiertos.
No se ejecutaron las demos externas ni se certificaron sus FPS, energía o
compatibilidad con la PCB. Las fuentes inaccesibles y testimonios de foros no
se usan para acreditar esas propiedades.
