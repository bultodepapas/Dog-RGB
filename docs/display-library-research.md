# Bibliotecas y referencias para RGB Dog Display

Investigación consultada el 2026-09-12. **Actualización de implementación: Arduino_GFX 1.6.7 probado por USB en I4; LVGL 8.4.0 fijado para PC/placa en [I5](../Platformio/Dog-RGB/docs/display-i5.md).** La demo I4 visible fue confirmada por el propietario; la aceptación conjunta sigue abierta. Complementa el [plan incremental](PLANS/2026-09-12_display-incremental-delivery.md), que conserva autoridad sobre el orden de ejecución.

## Selección por capa

No confundir transporte LCD, composición de interfaz y recursos animados: resuelven problemas distintos. Usar un solo dueño del panel y un solo motor de UI activo; no mezclar drivers que inicialicen/controlen el mismo SPI por su cuenta.

| Capa | Selección inicial | Cuándo entra |
| --- | --- | --- |
| LCD ST7789 y texto | Arduino_GFX, condicionado a smoke test del core actual | I3 |
| Componentes y estilo | LVGL; versión exacta común a PC y placa | I5 |
| Simulación | CMake sin ventana implementado; SDL opcional según necesidad de interacción | I5, tres escenarios estáticos |
| Tipografía | Fuentes LVGL existentes primero; `lv_font_conv` para personalizarlas | I5, cuando se elija fuente |
| Movimiento de componentes | Animaciones nativas LVGL y curvas ease-out | I6 |
| Pequeño sprite de mascota | `lv_animimg`, si aporta identidad sin distraer | I7 opcional |
| GIF | Evaluar AnimatedGIF únicamente si se necesita realmente ese formato | I7 opcional |

I0–I2 no incorporan dependencias gráficas. La pantalla sencilla puede verse cuidada con alineación, tamaños y colores consistentes, sin anticipar el motor de animación.

Reconciliación de implementación 2026-09-12: el [contrato incremental](PLANS/2026-09-12_display-incremental-delivery.md) fija targets, aislamiento Classic, semántica GPS y presupuestos. I5 ya genera tres PNG con LVGL compartido mediante CMake sin ventana; interacción y runner temporal se incorporan según necesidad en I6. No convertir el catálogo de referencias siguiente en una lista de dependencias a instalar. La [investigación técnica del panel](waveshare-lcd169-technical-research.md) añade esquema/datasheet, experiencias de foros, diagnóstico de negro/backlight y seguimiento del máximo de repintado observado en placa.

Actualización de ejecución: [guía I3](../Platformio/Dog-RGB/docs/display-i3.md), [baseline I3](baselines/display-i3-2026-09-12.md). Se revisaron el demo No Touch y el tag 1.6.7; las alternativas siguientes permanecen referencias, sin instalar.

## Drivers comparados

**Arduino_GFX — primera elección para I3.** Su driver `Arduino_ST7789` permite configurar dimensiones, offsets, rotación e inicialización. El proyecto ofrece `PDQgraphicstest` como prueba inicial. Eso permite validar el panel antes de dibujar datos. Fijar 240 × 280 y los parámetros confirmados para nuestra revisión; el tamaño por defecto del controlador no es prueba de tamaño visible. [Repositorio](https://github.com/moononournation/Arduino_GFX), [header revisado](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST7789.h).

La [wiki Waveshare](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69) referencia Arduino_GFX y LVGL 8.4.0 en sus demos. Son antecedentes para reproducir, no una garantía sobre el core fijado en RGB Dog. No importar el demo entero ni su entrada táctil al modelo No Touch.

**LovyanGFX — alternativa de transporte.** Revisado `examples/HowToUse/2_user_setting/2_user_setting.ino`: separa panel, bus SPI y backlight PWM, con configuración de DMA, frecuencia y GPIO. El ejemplo incluye opciones ST7789 pero usa otro panel y pines ilustrativos. Adoptar su estructura, no copiar ese cableado. Evaluarlo si Arduino_GFX presenta incompatibilidad concreta o no cumple los tiempos medidos. [Repositorio](https://github.com/lovyan03/LovyanGFX), [configuración revisada](https://github.com/lovyan03/LovyanGFX/blob/master/examples/HowToUse/2_user_setting/2_user_setting.ino).

**TFT_eSPI — reserva, sin introducirlo junto a los anteriores.** Documenta ST7789, sprites, fuentes y DMA SPI para ESP32-S3. Su configuración puede vivir por proyecto en PlatformIO. La referencia del README a un paquete ESP32 antiguo no demuestra compatibilidad con nuestro core actual; hace falta el mismo ensayo de compilación/placa. No hay evidencia en esta revisión para declararlo más rápido o más lento que los otros. [Repositorio y README](https://github.com/Bodmer/TFT_eSPI).

**`esp_lcd` + `esp_lvgl_port` — opción de integración más cercana a ESP-IDF.** Revisado el README del componente en `espressif/esp-bsp`: contiene ejemplo ST7789 mediante SPI, buffers, RGB565, tareas, temporizadores y entradas. Soporta LVGL 8/9 con diferencias de API; su selección por defecto de LVGL no sustituye un pin exacto. La integración con nuestro proyecto Arduino requiere trabajo específico, por lo que no justifica migrar todo el firmware en I3. [README del componente](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/README.md).

`esp_lvgl_port` y `esp_lvgl_adapter` son componentes distintos. Evaluar uno si hace falta; no apilarlos como dos schedulers LVGL. La ruta SPI del adapter no ofrece los modos antitearing reservados para RGB/MIPI. [Guía de ESP LVGL Adapter](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html).

## Animaciones y recursos: elegir el mecanismo más pequeño

**Movimiento de UI:** usar `lv_anim` para desplazar un indicador o componente, cambiar un valor de estilo y definir duración/curva. `lv_anim_timeline` permite coordinar varios movimientos. Revisado el ejemplo de timeline en el tag v8.4.0: organiza animaciones y permite controlar progreso/dirección; sus controles táctiles de demostración se sustituyen por nuestro evento de botón. No necesitamos otra biblioteca de tweening. [Guía v8.4](https://lvgl.io/docs/open/8.4/overview/animation.html), [ejemplo versionado](https://github.com/lvgl/lvgl/blob/v8.4.0/examples/anim/lv_example_anim_timeline_1.c).

**Sprite corto:** `lv_animimg` reproduce una secuencia de imágenes; no es un decodificador GIF. Revisados el header y ejemplo v8.4.0, que usan imágenes declaradas como recursos y parámetros de duración/repetición. Para una pequeña huella o perro animado, una secuencia corta preconvertida permite conocer de antemano el coste de almacenamiento. [Ejemplo](https://github.com/lvgl/lvgl/blob/v8.4.0/examples/widgets/animimg/lv_example_animimg_1.c), [API](https://github.com/lvgl/lvgl/blob/v8.4.0/src/extra/widgets/animimg/lv_animimg.h).

Cálculo de planificación: ocho frames opacos de 48 × 48 RGB565 requieren 36.864 bytes de píxeles, sin metadatos; alfa, formato y compresión cambian ese presupuesto. No reservar ocho pantallas completas para lograr el mismo detalle decorativo.

**GIF:** AnimatedGIF separa decodificación y salida con callbacks y documenta compromisos de memoria, transparencia y disposal. Su rendimiento de decodificación no incluye necesariamente el cuello de botella de nuestra pantalla. Si se adopta con LVGL, entregar los píxeles a un recurso/buffer administrado por la UI; no escribir directamente al LCD saltándose su composición. Probarlo solo después de demostrar que `lv_animimg` no cubre la necesidad. [Repositorio y limitaciones](https://github.com/bitbank2/AnimatedGIF).

**Fuentes:** `lv_font_conv` convierte fuentes a recursos LVGL, permite elegir glifos y bits por píxel. Usar una familia con números de ancho estable, conservar acentos españoles y exportar tamaños reales; evitar escalar un texto pequeño para producir el dato principal. Comparar 2/4 bpp y tamaño final cuando se personalice la tipografía. Mantener fuente original, licencia, versión del conversor y receta reproducible. [Conversor oficial](https://github.com/lvgl/lv_font_conv).

## Repositorios de producto y simulación revisados

| Repositorio y material leído | Hallazgo observable | Aplicación y límite |
| --- | --- | --- |
| [InfiniTime — WatchFaceDigital.cpp](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/src/displayapp/screens/WatchFaceDigital.cpp) | Crea labels al construir la vista; `Refresh()` usa comprobaciones `IsUpdated()` y limpia la tarea al destruirla | Reutilizar la idea de actualizar solo cambios y gestionar vida de componentes. Sus llamadas LVGL son de otra generación: no pegarlas en v8/v9 |
| [InfiniSim — README](https://github.com/InfiniTimeOrg/InfiniSim) | Ejecuta UI de InfiniTime en PC con dependencias del firmware | Referencia de separación firmware/simulación; no arrastrar el firmware PineTime ni su stack al collar |
| [lv_port_pc_vscode — README](https://github.com/lvgl/lv_port_pc_vscode) | CMake/SDL y soporte descrito para Windows/Linux/macOS; FreeRTOS opcional | Partir del patrón de port, sin RTOS host inicial. Fijar revisión y submódulos compatibles con nuestra versión LVGL |
| [lv_demos — README](https://github.com/lvgl/lv_demos) | Catálogo de interfaces y demostraciones LVGL | Usar como referencia de widgets/estilos; no importar todo el catálogo a firmware |

Se leyeron archivos fuente concretos además de documentación. No se ejecutaron estos repositorios ni se reprodujeron sus benchmarks. Los enlaces `master/main` son referencias móviles consultadas en esta fecha; al implementar se debe registrar tag/commit exacto. Conservar licencias y atribuciones del material que efectivamente se reutilice; las ideas de arquitectura no requieren copiar un proyecto completo.

## Guías a usar durante cada incremento

- I3: demo correcto de Waveshare, `Arduino_ST7789` y prueba gráfica; verificar primero ventana visible y colores.
- I5: [port de display LVGL 8.4](https://lvgl.io/docs/open/8.4/porting/display.html), port PC, conversor de fuentes y nuestro [flujo IA](PLANS/2026-09-12_display-ai-workflow.md). Si se elige LVGL 9, actualizar deliberadamente las referencias API.
- I6: guía y ejemplo de animación versionados, [estilos/capas LVGL](https://lvgl.io/docs/open/8.4/overview/style.html) y mediciones del driver. El coste de mover un contenedor depende del área invalidada, no solo del número de widgets.

## Plan de evaluación acotado

**En I3:** probar únicamente Arduino_GFX primero. Registrar versión exacta, core, placa, pines, frecuencia, formato, dimensiones y offsets. Verificar barras, texto, actualización parcial y quince minutos con LEDs/GPS. Evaluar una alternativa solo ante un fallo identificado; no convertir esta selección en un benchmark permanente de cuatro bibliotecas.

**En I5:** incorporar LVGL manteniendo el driver si cumple. Comparar una vista con los mismos datos y condiciones antes/después: flash, heap interno mínimo, uso PSRAM, duración de actualización y latencia del loop. Tres escenarios estáticos y capturas; esta etapa no exige animaciones ni FPS objetivo.

**En I6:** una transición de 160–220 ms, interrupción por botón y dos vistas. Comparar 20/40/70 filas de buffer solo si el transporte lo necesita. Verificar si el envío es realmente asíncrono antes de atribuir beneficio a dos buffers. Guardar p50/p95/máximo, área enviada y duración de transferencia; objetivo inicial de 20 FPS durante movimiento, medido en placa bajo carga conjunta.

**Criterios visuales propuestos:** un dato principal por vista, texto legible con unidades fijas, alineaciones estables, esquinas libres, estado inválido explícito, una curva temporal consistente y una sola animación de énfasis. En I3 basta aplicar tipografía/espaciado sin nuevos assets; en I5 se define el estilo y en I6 su movimiento. Aportan calidad desde el inicio sin cambiar el orden funcional.

**Decisión de salida:** si la primera opción satisface aceptación, fijarla y continuar. Cambiar driver exige demostrar el problema, conservar el modelo de presentación y repetir pruebas conjuntas. Cualquier dependencia de pantalla queda excluida del target Classic.
