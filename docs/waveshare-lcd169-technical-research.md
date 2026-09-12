# Waveshare LCD 1.69: investigación técnica aplicada a RGB Dog

Fecha de consulta y banco: **12 de septiembre de 2026**. Alcance: **ESP32-S3-LCD-1.69 No Touch**, documentación V2, desarrollo paralelo con Classic. La placa USB de COM6 funciona y el propietario ve la interfaz; faltan GPS y tiras soldados. La identificación física definitiva de revisión sigue pendiente. Este informe diferencia especificaciones, relatos de otros desarrolladores, mediciones propias y propuestas.

## Decisión para la implementación actual

**Nota de reconciliación tras I6a:** las mediciones siguientes pertenecen a la
investigación I5 y se conservan como historia. El estado actual tiene dos vistas,
Actividad/Wi-Fi, distancia principal, nueve capturas, BOOT y máximo USB final de
44,768 ms por llamada. Véanse [baseline I6a](baselines/display-i6-2026-09-12.md)
y [flujo vigente](PLANS/2026-09-12_display-ai-workflow.md). Tras esa pausa se reanudó el desarrollo de I6c opt-in;
el plan incremental contiene los pendientes V1–V3/I6b–I7. Esta revisión no
añade consultas web ni nuevas mediciones del panel.

Conservar Arduino_GFX 1.6.7 y LVGL 8.4.0, cambiar la superficie a **negro `#000000`** y mantener una sola página estática. La primera versión utilizaba `#101918`: el propietario la percibió como gris brillante. Ese comentario justifica corregir la paleta, pero por sí solo no demuestra inversión de color, fallo del panel o problema eléctrico. El cambio a negro ya se compiló y cargó; su apariencia física necesita observación del propietario.

No hace falta reemplazar el stack para mejorar esta pantalla. La comparación USB de diez minutos conservó el heap observado en 160.856 bytes, sin reinicios detectados. En ventanas de actualización normal, el máximo agregado fue 18,479 ms con LVGL y 4,528 ms con texto básico. La interfaz más elaborada tiene un coste medible, aceptable para seguir evaluando una página a 1 Hz, sin demostrar todavía animación fluida ni convivencia con recepción GNSS real. La [baseline I5](baselines/display-i5-2026-09-12.md) conserva imágenes, versiones, condiciones y resultados.

## 1. Qué placa y pantalla estamos integrando

El fabricante identifica la variante No Touch como SKU 27932. Combina ESP32-S3R8 de doble núcleo hasta 240 MHz, flash de 16 MB y PSRAM de 8 MB con un IPS de 1,69 pulgadas y 240×280 píxeles. También incorpora RTC PCF85063, IMU QMI8658, circuito de carga y conexión para batería de 3,7 V. El controlador es ST7789V2; su memoria de imagen es de 240×320, aunque la ventana visible tiene 280 filas. La ficha anuncia capacidad de 262K colores, que no obliga a transportarlos todos en nuestra aplicación. [Ficha oficial No Touch](https://docs.waveshare.net/ESP32-S3-LCD-1.69/).

La integración debe identificar **SoC, variante táctil y revisión** en cada ejemplo reutilizado. Los proyectos ESP32-C5/C6 de 1,69 pulgadas y los Touch no son intercambiables con esta placa. La FAQ sitúa CST816T únicamente en la variante táctil y propone buscar la etiqueta **V2QC** para distinguir V2. Ni el tamaño de flash leído por esptool ni que la pantalla encienda prueban esa revisión. [FAQ oficial](https://docs.waveshare.net/ESP32-S3-LCD-1.69/FAQ/).

No encontramos en las fuentes revisadas una especificación suficiente de luminancia en nits, contraste óptico del módulo completo o calibración de gamma que permita prometer un negro determinado. Debemos observar la unidad real, su iluminación ambiente y el ángulo de visión. Tampoco traducimos capacidad de batería o memoria en autonomía sin una medida del sistema.

## 2. Negro, retroiluminación y color son controles distintos

La primera intervención cambia únicamente la paleta: negro de fondo, blanco para el dato principal, gris neutro para texto secundario y colores de estado conservados. Esto elimina el tinte deliberado y facilita comparar la pantalla con una referencia simple. Una captura del simulador verifica el valor del píxel y la composición; no mide luz emitida por el panel.

En el esquema V2, GPIO15 controla el backlight mediante Q1 S8050. Se observan R16 de 1 kΩ hacia la base, R11 de 10 kΩ hacia 3,3 V y R10 de 10 Ω en la rama del cátodo; el ánodo del LED va a 3,3 V. La lectura del circuito concuerda con el ejemplo oficial que activa BL con nivel alto. El pull-up también explica por qué no debemos equiparar un pin flotante durante reset con un apagado garantizado. Es una interpretación del esquema, no una medida de esta placa. [Esquema V2, página 1](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf).

Por tanto, dibujar negro no apaga el LED del backlight. Si el negro nuevo todavía se percibe gris luminoso, el siguiente experimento útil es variar brillo conservando exactamente la imagen y la posición de observación. No modificaríamos simultáneamente gamma, inversión, frecuencia SPI y color: impediría identificar qué produjo la mejora.

Protocolo propuesto, todavía sin resultado óptico:

1. Observar la página negra con el brillo actual y verificar legibilidad del texto pequeño.
2. Usar `b` para comparar retroiluminación encendida/apagada y volver a encenderla. Este comando no detiene el GPS ni el renderizador.
3. Revisar `t`, las barras y el borde existentes: rojo, verde, azul, blanco, posición y recorte. Volver con `l` y `f` a Paseo DEMO.
4. Si sigue haciendo falta, añadir un diagnóstico PWM de pocos niveles, con el mismo patrón y sin guardar preferencias todavía.
5. Registrar apariencia y, cuando haya instrumentos, corriente y nivel eléctrico. No inferir consumo a partir del duty programado.

Para PWM se evaluaría LEDC según la versión del core que realmente compila el proyecto. La API actual documenta `ledcAttach` y `ledcWrite`; muchos tutoriales antiguos utilizan funciones de configuración diferentes. Una primera prueba de 5 kHz y 8 bits sería una elección experimental nuestra, no una especificación de Waveshare. Habría que comprobar niveles extremos, parpadeo perceptible y comportamiento al reiniciar. [Arduino-ESP32: LEDC](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html).

## 3. Inicialización y trampas de color

Se descargó y examinó el archivo oficial de ejemplos, además de la documentación web. En `Arduino-v3.0.5/example/10_LVGL_Arduino/10_LVGL_Arduino.ino` aparecen DC4, CS5, SCK6, MOSI7, reset8, rotación 0, opción IPS activa, geometría 240×280 y offsets `0,20,0,0`. La configuración Arduino fija `LV_COLOR_16_SWAP=0` y selecciona `draw16bitRGBBitmap`; la rama con swap usa `draw16bitBeRGBBitmap`. El ejemplo ESP-IDF del mismo archivo utiliza swap 1. Son contratos de transporte distintos. [Archivo oficial de demos](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_DemoCode.zip).

Nuestro port conserva la ruta Arduino. La revisión local de Arduino_GFX 1.6.7 confirmó que `draw16bitRGBBitmap` acaba en la escritura SPI que prepara los bytes para el bus. También se comprobó que la implementación ST7789 combina el indicador IPS con la petición de inversión. Copiar un `invertDisplay` de otro panel sin revisar esa combinación puede deshacer una inicialización correcta. Estos hallazgos se obtuvieron del paquete realmente instalado, no de una rama flotante de Internet.

Conviene separar tres diagnósticos:

| Síntoma | Comprobación que lo distingue | Acción condicionada al resultado |
| --- | --- | --- |
| Rojo y azul intercambiados | Barras primarias, incluido verde | Revisar RGB/BGR y rotación del driver |
| Varios colores deformados | Mismo patrón nativo y por LVGL | Revisar RGB565 y orden de bytes en el límite de transferencia |
| Aspecto invertido en ambos renderizadores | Blanco/negro y primarios con driver aislado | Comparar secuencia de inicialización e indicador IPS con el demo |
| Solo molesta el tono de fondo | Captura y observación de negro puro | Paleta y después backlight |
| Imagen desplazada o borde incompleto | Borde y coordenadas conocidas | Geometría, offset y orientación antes de rediseñar la UI |

El datasheet distingue el orden RGB/BGR mediante MADCTL `0x36`, bit D3, del formato de píxel configurado por COLMOD `0x3A`; documenta `0x55` para escritura de 16 bits. Ninguno equivale a intercambiar los dos bytes de un búfer en RAM. [ST7789V2, páginas 217 y 226](https://files.waveshare.com/wiki/common/ST7789V2.pdf).

## 4. Presupuesto de fluidez y arquitectura de renderizado

Los siguientes números son cálculos propios de carga útil, sin comandos, CPU ni intervalos entre transferencias:

| Operación a 40 MHz SPI | Bytes | Tiempo ideal mínimo |
| --- | ---: | ---: |
| Frame 240×280 RGB565 | 134.400 | 26,88 ms |
| Bloque de 20 filas RGB565 | 9.600 | 1,92 ms |
| Frame transportado a 3 bytes por píxel | 201.600 | 40,32 ms |

En RGB565, 37,2 frames completos por segundo es solo el techo matemático de ese transporte. No es una medición de la placa. Un objetivo futuro de 20 FPS deja 50 ms por frame para todo el trabajo; el loop también debe atender GPS, LEDs y portal. Reducir el área sucia suele ser más útil que repintar toda la pantalla con una frecuencia mayor.

La tabla de interfaz serie de cuatro hilos especifica un ciclo mínimo de escritura de **16 ns** bajo sus condiciones de ensayo. 40 MHz equivale a 25 ns; 80 MHz a 12,5 ns. Por ello no adoptaremos 80 MHz por copiar un tutorial. El inverso de 16 ns, 62,5 MHz, tampoco constituye una garantía de funcionamiento del módulo y su circuito. [ST7789V2, página 44, tabla 6](https://files.waveshare.com/wiki/common/ST7789V2.pdf).

La implementación I5 toma decisiones sencillas y medibles: un propietario SPI, un buffer de 20 filas, pool LVGL de 48 KiB, etiquetas reutilizadas y actualización de datos a 1 Hz. No hay animación ni tarea adicional. La callback marca el flush listo después de terminar la escritura síncrona, siguiendo el contrato del [port de referencia LVGL 8.4](https://github.com/lvgl/lvgl/blob/v8.4.0/examples/porting/lv_port_disp_template.c).

Un buffer pequeño limita cada transferencia, pero una llamada al servicio puede procesar varias. Por eso la métrica de aceptación es tiempo agregado de `display::tick`, además de tiempo de flush. El repintado manual completo alcanzó 49,997 ms en la comparación inicial. El smoke posterior del firmware negro registró 50,485 ms antes de reiniciar contadores: excede el presupuesto de 50 ms. Este criterio queda abierto y requiere atender el repintado completo antes de añadir transiciones. La variación entre ensayos no demuestra que el cambio de paleta sea su causa.

La PSRAM disponible ofrece capacidad, pero no elimina la latencia SPI. Antes de introducir DMA o doble buffer exigiríamos una comparación con la misma escena y carga. DMA añade condiciones de memoria, alineación y vida del buffer hasta terminar la transferencia; el código actual no implementa esa ruta. [Espressif SPI master, ESP32-S3/IDF 5.5.2](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/spi_master.html).

## 5. Fallas y experiencias de otros desarrolladores

Los foros aportan hipótesis y pruebas que repetir, no un diagnóstico remoto de nuestra placa. Se revisaron relatos de primera mano y se registró cuándo el hardware o stack era diferente.

| Fuente | Caso observado por su autor | Aplicación y límite para RGB Dog |
| --- | --- | --- |
| [Waveshare issue #9](https://github.com/waveshareteam/ESP32-display-support/issues/9), marzo de 2025 | V1 con Tasmota: GPIO15 cambiaba poco el brillo y no conseguía apagarlo como esperaba | Justifica comprobar BL físicamente. El issue figura cerrado, pero el contenido consultado no establece una solución transferible a V2 |
| [LVGL: Wrong quality on display ST7789](https://forum.lvgl.io/t/wrong-quality-on-display-st7789/21007), abril de 2025 | El autor reportó corregir el resultado intercambiando bytes | Verificar formato si falla el patrón. No copiar swap 1 a nuestro backend Arduino |
| [LVGL: Inverse colors](https://forum.lvgl.io/t/inverse-colors-on-cheap-yellow-display-and-lvgl-9/17198) | Discusión con distintos paneles y LVGL 9 sobre inversión y orden de color | Separar ambas causas. Las APIs y soluciones citadas no son directamente las de LVGL 8.4 |
| [LVGL: Porting issue with ST7789](https://forum.lvgl.io/t/lvgl-porting-issue-with-st7789-display/2753), 2020 | Port en K210 con comportamiento temporal incorrecto; se recomienda aislar el driver | Medir tiempo transcurrido real y verificar el transporte primero; otro MCU |
| [TFT_eSPI issue #3172](https://github.com/Bodmer/TFT_eSPI/issues/3172), febrero de 2024 | ESP32-S3 DevKit y módulo Waveshare externo con dudas de configuración | Cambiar de librería no elimina la necesidad de identificar pines y variante; no es la placa integrada |
| [Reddit: pantalla Waveshare 1.69 Touch](https://www.reddit.com/r/esp32/comments/1h1a4zl/cant_get_waveshare_169_touch_lcd_to_power_on/) | El backlight encendía al activar GPIO15, pero faltaba resolver la imagen | Luz y comunicación LCD se prueban por separado; el relato corresponde a Touch |
| [Arduino-ESP32 discusión #12339](https://github.com/espressif/arduino-esp32/discussions/12339) | Tearing al escribir NVS en un Waveshare de 4,3 pulgadas con bus RGB | No extrapolar ese problema de bus RGB a este ST7789 por SPI con memoria de imagen interna |

La FAQ oficial además menciona calentamiento cuando el buzzer queda habilitado y recomienda desactivarlo; también señala consumo de radio y memoria externa. Eso orienta una revisión de configuración y una medida, sin declarar normal cualquier temperatura. La misma FAQ documenta puertos serie ocupados, entrada manual a descarga y configuración USB CDC como causas frecuentes de problemas de programación/logs. [FAQ Waveshare](https://docs.waveshare.net/ESP32-S3-LCD-1.69/FAQ/).

## 6. Repositorios que sí aportan al proyecto

**Demo oficial de Waveshare:** referencia principal para pinout, offset e inicialización. Su guía Arduino lista GFX 1.4.9 y LVGL 8.4.0 entre las versiones de los ejemplos. Nuestro GFX 1.6.7 ya compila y funciona, por lo que no lo degradamos solo para igualar un tutorial. [Guía Arduino oficial](https://docs.waveshare.net/ESP32-S3-LCD-1.69/Arduino/).

**KAST:** proyecto público de un contador portátil sobre la misma No Touch, con ESP-IDF y LVGL. Su README describe botones físicos, ajuste de brillo y tiempo de suspensión; es una referencia concreta de producto con pantalla pequeña. Los botones propuestos usan GPIO2/16/17: GPIO17 ya tiene otro destino en nuestro perfil LED, por lo que no copiaríamos su cableado. Revisamos alcance y documentación del repositorio, no certificamos su firmware ni su autonomía. [Repositorio KAST](https://github.com/Nikolay-Tyulkin/KAST). La [publicación del autor](https://www.reddit.com/r/esp32/comments/1ug2dpb/i_made_a_wearable_knitting_row_counter_for_my/) permite ver el uso real; sus horas de batería no son una estimación para RGB Dog.

**Repositorios oficiales de soporte:** el [listado ESP32-display-support](https://github.com/waveshareteam/ESP32-display-support) incluye No Touch como soportada, mientras que [Waveshare-ESP32-components](https://github.com/waveshareteam/Waveshare-ESP32-components) la mostraba pendiente en su propia matriz al consultar. Son alcances distintos. No interpretaremos el primer indicador como garantía de un BSP administrado y listo para nuestro build.

**LVGL PC:** el [port oficial con VS Code/CMake/SDL](https://github.com/lvgl/lv_port_pc_vscode) orienta la estructura del simulador. Para esta página estática implementamos primero un renderer sin ventana, con el mismo LVGL/configuración/UI y salida PNG. SDL y controles interactivos pueden incorporarse cuando exista navegación que probar. Esto mantiene corto el ciclo de revisión y evita simular otra UI que luego haya que traducir.

## 7. Cambios de revisión y periféricos que condicionan el plan

Waveshare documenta estos cambios V1→V2: buzzer GPIO33→42, interrupción RTC 41→39, SYS_EN 35→41 y SYS_OUT 36→40. Su ejemplo mantiene SYS_EN alto para la alimentación por batería y trata SYS_OUT bajo como detección del botón. Se verificará la revisión antes de incorporar pulsación larga, latch o apagado. [Migración en la guía Arduino](https://docs.waveshare.net/ESP32-S3-LCD-1.69/Arduino/).

En el esquema V2, la lectura de batería en GPIO1 utiliza divisor de 200 kΩ/100 kΩ, relación nominal de reconstrucción ×3. Eso no calibra el ADC ni convierte voltaje en porcentaje. Tampoco se observa una señal TE expuesta en el conector LCD revisado: no planificaremos sincronización con TE suponiendo que basta activar una opción de software. Ambas son lecturas del [esquema V2](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf), pendientes de cotejar con la unidad física.

La pantalla no cambia por sí misma el presupuesto eléctrico de las tiras ni valida alimentación portátil. Conservamos las pruebas incrementales de un píxel, dos tiras, GNSS y carga conjunta existentes. RTC, IMU, buzzer y batería siguen como ampliaciones individuales, no dependencias de la primera UI.

## 8. Flujo con IA y criterios para una interfaz cuidada

La unidad de avance será una mejora verificable. Para cada cambio visual, la instrucción a Codex debe incluir resolución, versión LVGL, estados del dato, componentes permitidos y criterio de aceptación. Ejemplo de contrato: «Paseo 240×280; fondo negro; velocidad central; conservar distancia si se pierde fix; sin datos ficticios en producto; mantener todas las etiquetas dentro del margen; no añadir animaciones». La IA puede modificar componentes y regenerar capturas; los resultados deben revisarse contra ese contrato.

El ciclo ya implementado es **referencia → componentes compartidos → renderer LVGL → tres capturas → revisión → placa**. Los fixtures cubren búsqueda, fix y dato caducado. Las pruebas adicionales incluyen cero válido, texto largo, cambios repetidos y memoria. Las imágenes provienen del código real, no de una reinterpretación gráfica independiente. Véase [herramienta reproducible](../tools/display-simulator/README.md).

Para este tamaño proponemos tipografía jerárquica, márgenes constantes, pocas líneas y estados expresados con texto además de color. La velocidad ocupa el área dominante; fecha y distancia quedan subordinadas. El negro evita un fondo cromático que compita con el dato. No añadiremos fotos, degradados o tarjetas por defecto. En I6, una transición breve y una segunda vista deberán justificar su coste con una acción útil.

Un simulador puede demostrar composición y lógica, pero no contraste óptico, PWM, latencia SPI, interferencia de radio o recepción UART. La prueba en placa cierra esas diferencias. Las cifras de frames del PC nunca se trasladarán al collar como medición.

## 9. Orden de trabajo histórico de I5

Esta tabla documenta la secuencia propuesta al investigar I5; segunda vista,
BOOT y optimización de regiones ya se implementaron en I6a. No usar sus filas
«Ahora/Siguiente» como cola vigente. El plan incremental conserva la autoridad.

| Prioridad | Incremento concreto | Evidencia para avanzar |
| --- | --- | --- |
| Ahora, I5 | Negro puro, capturas regeneradas y firmware cargado | Build, contratos del renderer y smoke serie; apariencia física por confirmar |
| Siguiente, I5 | Revisar negro, RGBW, borde y orientación | Observación en el panel; no cambiar swap si el patrón es correcto |
| Si el brillo sigue molestando | Diagnóstico PWM pequeño con niveles conocidos | Legibilidad, apagado/encendido y reinicio; corriente cuando haya instrumentos |
| Cuando estén conectados | Completar I1/I2 y carga conjunta I4 | LEDs/GNSS/portal reales, sin resets ni nuevos overflows; memoria y tiempo de servicio |
| Después, I6 | Botón de revisión confirmada, segunda vista y transición de referencia de 200 ms | Eventos, treinta cambios, diez ciclos de despertar, objetivo 20 FPS y respuesta p95 <100 ms medidos |
| Solo ante un cuello de botella demostrado | Evaluar buffer/regiones y después transporte alternativo | Comparativa idéntica, mejora del tiempo agregado y ausencia de regresiones Classic |

El [plan incremental](PLANS/2026-09-12_display-incremental-delivery.md) sigue siendo la fuente de estado. Esta investigación añade decisiones y experimentos; no cierra los criterios físicos que faltan ni abre otra arquitectura paralela al plan.

## Trazabilidad y límites de la investigación

Se contrastaron ficha, FAQ, guía Arduino, archivo de ejemplos, esquema de una página y datasheet de 319 páginas, además de repositorios y relatos originales enlazados en cada apartado. Se extrajo texto nativo de los PDF y se inspeccionaron visualmente el esquema y las páginas 44, 217 y 226. Las fuentes web se consultaron en la fecha indicada; las matrices de soporte y ramas de GitHub pueden cambiar.

Copias de análisis, fuera de Git, en `Platformio/Dog-RGB/artifacts/display-i5/`:

| Fuente conservada | SHA-256 |
| --- | --- |
| `official-demo.zip` | `b5dcaa0f524fa17dbf670c071e11e777a5d450aaf8eb506f2ed27e912e74a90e` |
| `v2-schematic.pdf` | `3a6a0d35e6ff104246e865789b9bcc63993249eaa0a4d9dec9d39127b2c46204` |
| `st7789v2.pdf` | `8937678475e20598c8deb17c3cdd41403da6317a3b6518f86144b8a609de9f46` |

La evidencia actual no determina la causa óptica del gris percibido, una tasa de fallos del producto, la calidad de todas sus revisiones ni la autonomía del collar. Sí permite hacer la siguiente prueba con una paleta inequívoca, una inicialización contrastada y un presupuesto de renderizado medido.
