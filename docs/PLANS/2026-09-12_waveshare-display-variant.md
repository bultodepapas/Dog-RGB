# RGB Dog: variante con Waveshare ESP32-S3-LCD-1.69

Fecha: 2026-09-12. Estado: **Investigación de apoyo reconciliada con I6d e I6c experimental; aceptación conjunta pendiente**.

Actualización: existen siete entornos, diagnósticos I0–I3, texto/LVGL y tres páginas
con BOOT. I6d añade GPS/política LED en Estado; véanse su
[baseline](../baselines/display-i6d-2026-09-12.md) y
[guía de placas](../../Platformio/Dog-RGB/docs/boards.md). Arduino_GFX 1.6.7,
LVGL 8.4.0 y renderer sin ventana ya están probados; catorce PNG y cinco CTest.
La investigación original siguiente conserva alternativas y cálculos de diseño,
no tareas pendientes por defecto. Sensores y animación siguen propuestos; I6c prepara timeout opt-in de diagnóstico.

Revisión de coherencia 2026-09-12: contratos de targets, diagnóstico, datos y pruebas contrastados con código/CI en el plan incremental. Este documento aporta evidencia y opciones; no define un segundo backlog.

**Orden de ejecución actualizado por el propietario:** [Desarrollo incremental I0–I7](2026-09-12_display-incremental-delivery.md). Primero placa y LEDs, después GPS, pantalla sencilla y consolidación. LVGL, simulador y refinamiento visual se incorporan posteriormente. Este documento conserva la investigación técnica y la arquitectura objetivo.

La versión XIAO sin pantalla continúa activa. La segunda variante Display ya
usa el mismo núcleo de firmware con perfil propio. Este documento está en
español por solicitud del propietario; el estado actual prevalece sobre las
propuestas históricas de las secciones de investigación.

## Decisión recomendada

Desarrollo del ciclo de diseño y verificación: [Flujo de interfaces con IA](2026-09-12_display-ai-workflow.md), con componentes compartidos, simulador determinista, capturas temporales y aceptación en placa.

Usar la Waveshare como MCU principal de la variante Display. Mantener XIAO en Classic. Ambas conservan GNSS externo, dos tiras RGBW y portal local. Desarrollarlas en el mismo repositorio, con dos targets de compilación y controladores de placa separados. No hace falta instalar IA en el collar: Codex sirve para diseñar, programar y verificar la UI durante el desarrollo.

La fotografía muestra “No Touch” y un título compatible con **ESP32-S3-LCD-1.69**. La identificación es probable; no confirma SKU, autenticidad ni revisión PCB. La wiki mezcla algunas referencias táctiles en texto genérico. Para este plan se asume **sin táctil**; no se configura CST816T ni navegación por gestos sobre el cristal.

## Hardware investigado

| Característica | Especificación publicada |
| --- | --- |
| Pantalla | IPS de 1,69 pulgadas, 240 × 280, esquinas redondeadas |
| Controlador | ST7789V2, SPI de cuatro hilos; RGB565 utilizable |
| Procesador | ESP32-S3, doble núcleo LX7, hasta 240 MHz |
| Memoria | SRAM 512 KB, PSRAM 8 MB, flash 16 MB |
| Radio | Wi-Fi 2,4 GHz y Bluetooth 5 LE |
| Periféricos | IMU QMI8658, RTC PCF85063, buzzer, botones y USB-C |
| Batería | Cargador ETA6098, conector de batería y lectura ADC |

Fuentes: [ficha oficial](https://www.waveshare.com/product/esp32-s3-lcd-1.69.htm) y [wiki del modelo sin táctil](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69). No incorpora GNSS ni módem celular. El E108-GN02 actual sigue siendo necesario. Brillo en nits, consumo real, masa, dimensiones exteriores exactas, estanqueidad y autonomía quedan sin confirmar; 1,69 pulgadas es la diagonal del panel, no la dimensión de la carcasa.

### Pines: evidencia V2 y propuesta de cableado

Se extrajo y revisó visualmente la única página del [esquema oficial V2](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf). La siguiente asignación describe ese esquema, no una placa física ya identificada.

| Función integrada V2 | GPIO |
| --- | --- |
| LCD DC / CS / SCLK / MOSI / RESET | 4 / 5 / 6 / 7 / 8 |
| Backlight | 15 |
| I²C SCL / SDA | 10 / 11 |
| ADC batería | 1 |
| IMU INT / RTC INT | 38 / 39 |
| SYS_OUT, lectura de botón / SYS_EN, control de alimentación | 40 / 41 |
| Buzzer | 42 |
| USB D− / D+ | 19 / 20 |
| BOOT, entrada UI reservada desde I6a | 0 |

Propuesta de asignación externa, condicionada a identificar PCB y verificar continuidad del cable:

| Señal RGB Dog | Classic actual | Display candidato V2 |
| --- | ---: | ---: |
| Datos tira A | GPIO1 | GPIO17 |
| Datos tira B | GPIO2 | GPIO18 |
| GNSS TX → ESP RX | GPIO44 | GPIO44 |
| ESP TX → GNSS RX | GPIO43 | GPIO43 |
| LED externo de estado | GPIO3 | Ausente; representar estado en LCD y píxeles de estado |

GPIO17/18 y UART43/44 están expuestos en P2 del esquema V2. No usar colores del mazo como autoridad. Mantener GNSS en UART1 remapeado y consola por USB CDC. Desactivar correctamente el heartbeat físico cuando el perfil no tenga LED: nunca ejecutar `pinMode(-1)` o `digitalWrite(-1)`.

El conflicto GPIO1 es real: [pins.h](../../Platformio/Dog-RGB/include/pins.h) lo dedica a tira A, mientras la placa lo conecta al divisor de batería. Cargar el firmware XIAO sin adaptar pines no constituye un bring-up válido. Los nombres `SYS_OUT` y `SYS_EN` tampoco se deben interpretar como dos botones independientes. Confirmar polaridad y secuencia con el demo correspondiente a la revisión recibida.

## Encaje con el código existente

Revisión de configuración y código local, apoyada en el grafo del proyecto:

- [platformio.ini](../../Platformio/Dog-RGB/platformio.ini): producción `seeed_xiao_esp32s3` y simulación `wokwi`; Arduino sobre pioarduino 55.03.311, ArduinoJson 7.4.3 y NeoPixel 1.15.5.
- [main.cpp](../../Platformio/Dog-RGB/src/main.cpp): `setup()` inicializa almacenamiento/configuración, escenas, GPS, geofence, LEDs y radio/portal. `loop()` usa ticks cooperativos y mide duración por fase. La pantalla debe respetar ese modelo.
- [config.h](../../Platformio/Dog-RGB/include/config.h): GNSS a 9.600 baud, muestreo 1 Hz y buffer RX de 16 KB. No reducir ese margen para alojar la UI.
- [tabla de particiones](../../Platformio/Dog-RGB/partitions_dog_rgb.csv): ocupa 8 MiB e incluye `tracknvs`. Mantenerla inicialmente en Display si el binario cabe; disponer de flash adicional no obliga a migrar almacenamiento.
- GPS, escenas, LED policy/bus, Wi-Fi y persistencia ya están separados. Aprovechar sus lecturas de estado; la LCD no debe recalcular distancia, geofence, fechas o escenas.

## Arquitectura para dos variantes en paralelo

```mermaid
flowchart TD
    CORE[GPS · métricas · escenas · almacenamiento · Wi-Fi] --> PORTAL[Portal local compartido]
    CORE --> LED[Política y salida RGBW compartidas]
    CORE --> SNAP[Snapshot de presentación]
    SNAP --> UI[UI LVGL]
    XIAO[Perfil Classic XIAO] --> LED
    WS[Perfil Display Waveshare] --> LED
    WS --> BSP[LCD · alimentación · botón · ADC]
    BSP --> UI
    FIX[Datos de prueba deterministas] --> UI
    UI --> SIM[Simulador PC]
```

Rutas actuales de las fronteras principales (herramienta PC en la raíz):

```text
include/board/board_profile.h
include/board/xiao_s3.h
include/board/waveshare_lcd169_v2.h
include/display/display.h
include/display/snapshot.h
include/display/connection.h
include/display/button.h
src/display/display.cpp
src/display/snapshot.cpp
src/display/connection_snapshot.cpp
src/display/lvgl_port.cpp
src/display/ui/walk_view.cpp
src/display/ui/connection_view.cpp
tools/display-simulator/            # en la raíz del repositorio
```

Un perfil de compilación selecciona pines/capacidades; página e iluminación son
estado de UI. Tiempo de apagado y brillo LCD configurables siguen propuestos,
no campos persistidos entregados. Que exista un chip no prueba que su driver
esté implementado ni que su lectura sea válida.

Ya existen `waveshare_lcd169` y cuatro diagnósticos de etapas 0–3, junto a
Classic/Wokwi. Conservar dependencias comunes y exclusión real de gráficos en
Classic e I0–I2. Flash 16 MiB y PSRAM 8 MiB fueron detectadas por USB; confirmar
revisión física sigue siendo una tarea distinta. No rehacer la selección de
targets como requisito para continuar I6a.

Mantener una rama principal común y ramas cortas por tarea. Cada cambio compartido se compila para ambos dispositivos. Publicar binarios con nombre de placa, revisión, versión y commit. La disponibilidad de dos particiones OTA no significa que exista actualización OTA: esa función sigue fuera del primer alcance.

El contrato mínimo `DisplaySnapshot` de I3 está definido en el [plan incremental](2026-09-12_display-incremental-delivery.md): estado GNSS, velocidad/validez, distancia diaria/fecha, modo LED e instante de captura. `total_distance_m()` representa el día registrado; no es distancia de sesión y `has_current_fix()` no basta para declarar calidad confiable. AP/STA ya se incorporaron mediante `ConnectionSnapshot` en I6a. Tiempo activo, sesión y batería se ampliarán solo cuando una función los necesite. Sin HTTP contra el propio ESP32, punteros a estado mutable o escrituras NVS desde callbacks de dibujo. Las acciones futuras de UI entrarán por comandos comunes al portal y al dispositivo.

## Stack de interfaz y rendimiento

I3 incorporó texto directo con Arduino_GFX 1.6.7. I5 añadió LVGL 8.4.0 y I6a
entregó dos páginas. La combinación está compilada y observada en USB; la
referencia antigua Arduino_GFX 1.4.9 de la wiki no es la dependencia actual.
Evaluar v9 solo ante necesidad concreta, cambiando PC y placa juntos.

Para una pantalla de estado simple Arduino_GFX solo sería suficiente. LVGL aporta valor aquí por el crecimiento previsto: varias vistas, estilos, navegación y simulación reutilizable. Un editor visual queda opcional; no hace falta una suscripción de diseño para empezar.

El [simulador oficial LVGL 8.4](https://docs.lvgl.io/8.4/get-started/platforms/pc-simulator.html) permite ejecutar UI real en PC. Usar la misma versión de LVGL y los mismos archivos `ui/` que el firmware, con backend SDL/Windows y entradas de teclado equivalentes al botón físico. Una maqueta HTML puede ayudar a elegir estética, pero no valida memoria, SPI ni el resultado LVGL.

Los cálculos iniciales siguientes no sustituyen la medición I6a: SPI 40 MHz,
un buffer de 20 filas (9.600 bytes), pool de 48 KiB y máximo USB final de
44,768 ms por llamada. Dos buffers siguen siendo una alternativa no adoptada.
La comparación de carga conjunta permanece pendiente.

- Un frame RGB565: `240 × 280 × 2 = 134.400 bytes` (131,25 KiB).
- Dos buffers parciales de 20 filas: `2 × 240 × 20 × 2 = 19.200 bytes` (18,75 KiB).
- A SPI hipotético de 40 MHz, un frame completo necesita al menos 26,88 ms solo de datos. No prometer 60 FPS; comandos y software agregan tiempo.
- I3 actualiza datos cambiados a un máximo de 1 Hz; I5 conserva esa semántica. I6 propone 20 FPS durante una transición breve, con refresco por regiones y sin repintado continuo en reposo. Una cadencia superior de presentación necesita un caso de uso; no aumenta por sí sola la frecuencia de muestras GNSS. Son objetivos de ingeniería, no especificaciones del panel.

Usar buffers parciales en memoria compatible con el transporte elegido y PSRAM para recursos cuando corresponda. Medir heap interno libre/mínimo, bloque máximo, PSRAM, latencia de UI y máximos del loop. LVGL tiene soporte de [renderizado parcial](https://lvgl.io/docs/open/9.1/porting/display); ese enlace describe API v9 y no debe copiarse literalmente al target v8.

Empezar con un único dueño de LVGL en el loop y transferencias acotadas. Si las mediciones exigen una tarea dedicada, comunicar snapshots por cola y mantener todas las operaciones LVGL en esa tarea. No compartir objetos LVGL entre callbacks Wi-Fi y renderizado.

## Inicialización propuesta en dispositivo

Esta secuencia describe el destino ampliado: I3 incorpora solo alimentación, LCD y texto; pasos LVGL corresponden a I5, y RTC/IMU permanecen opcionales en I7. No activar todos los periféricos desde el primer arranque.

1. Seleccionar perfil al compilar, antes de la construcción global del bus LED. Establecer enseguida control de alimentación según revisión y demo; mantener backlight apagado. Para apagar tiras alimentadas, enviar negro mediante el bus: suspender el transporte no borra el último frame.
2. Inicializar consola, comprobar flash/PSRAM y registrar board ID/revisión. Sin esperas indefinidas a un monitor USB.
3. Cargar NVS, configuración y escenas; iniciar GNSS, geofence, LEDs y resto del núcleo en el orden actual, incluido BLE antes de Wi-Fi si está habilitado. El diagnóstico I0/I1 tiene una ruta mínima que termina antes; en I2/I3 usa el núcleo real con overrides de banco y sin welcome a brillo elevado. No alterar defaults Classic.
4. Configurar bus SPI, reset ST7789 y ventana visible 240 × 280. Verificar offsets, rotación, orden RGB/BGR e inversión con barras de color y marco de un píxel. No asumir que la memoria 240 × 320 del controlador es toda visible.
5. En I3 dibujar texto directo; en I5 crear buffers, LVGL, tema y primera vista. Encender backlight tras el primer frame; la rampa es un refinamiento posterior. El servicio Display se inicia al final del arranque normal, con trabajo acotado.
6. Solo en una extensión I7 iniciar I²C y sondear RTC/IMU con timeout. Los errores detectables de sensor/driver deben permitir continuar GPS/LEDs/portal. Un ST7789 conectado por SPI de escritura no ofrece por ello detección fiable de presencia: verificar imagen físicamente.
7. Servir GPS primero en el loop y publicar snapshots con cadencia limitada después del trabajo existente. Desde I6 muestrear botón sin bloquear.
8. Propuesta I6c: timeout opcional que apaga solo backlight, tras validar despertar.
   Suspender además refresco es una optimización distinta. Seguir recibiendo
   GNSS y ejecutando el collar. Deep sleep no pertenece a ese timeout.

El RTC puede mantener hora entre arranques, pero no se convierte automáticamente en fecha GNSS confiable. Conservar las reglas existentes de rollover y validez. Un fallo de PSRAM debe tener salida explícita: degradación si caben buffers mínimos o UI deshabilitada y diagnóstico.

## Diseño de producto y trabajo con Codex

La pantalla sirve para consultar el collar al ponerlo, retirarlo o detenerse.
El [contrato de uso](2026-09-12_display-use-and-screens.md) reemplaza el catálogo
inicial Paseo/Luces/Conexión/Resumen por el siguiente:

| Vista | Contenido y estados |
| --- | --- |
| Actividad, implementada | Distancia registrada/fecha principal, GPS, velocidad y modo LED secundarios |
| Wi-Fi, implementada | AP/STA separados con nombres/IP reales; solo lectura |
| Estado, I6d implementado | GPS y luces/control efectivos; registro omitido sin señal de salud actual |
| Pausa, I6e propuesta | Variante de Actividad tras evidencia válida; sin señal no significa reposo |
| Resumen por paseo, I7 opcional | Requiere ciclo de vida propio; sesión de arranque no equivale a paseo |

Fondo oscuro, números de 36–48 px, etiquetas de 16–20 px, márgenes iniciales de 16 px ajustados al recorte real, unidades visibles y color acompañado de texto/icono. No reutilizar el portal completo en 240 × 280. El fondo negro mejora contraste; el ahorro principal del LCD se obtiene regulando backlight.

Botón corto: despertar si está apagada; avanzar página si está encendida. Primera iteración solo lectura, sin acciones de borrado o cambios accidentales. Reservar pulsación larga para la política real de alimentación una vez ensayada. No asignar una función incompatible con el circuito de apagado.

Batería: comenzar mostrando voltaje calibrado. Un porcentaje basado en tensión bajo carga puede ser engañoso; si se incorpora, etiquetarlo como estimado y probar reposo/carga. IMU y buzzer son extensiones posteriores. Despertar por movimiento en un collar puede mantenerlo encendido todo el paseo; debe ser opcional y medido. IMU no equivale a diagnóstico de salud ni reemplaza GPS.

Flujo de diseño con IA:

1. Escribir un brief con 240 × 280, no táctil, estados, tipografía, colores y límites de memoria. Elegir una propuesta visual con capturas a escala 1:1 y ampliadas.
2. Pedir a Codex componentes C/C++ LVGL y un tema central; recursos compactos y tipografía con `áéíóúñ`, sin texto incrustado en imágenes decorativas.
3. En I5 alimentar el simulador con tres fixtures estáticos: sin fix, fix válido y dato caducado. I6 añade eventos de botón y secuencias temporales. Batería, AP y nombre se prueban cuando se incorporen sus funciones, no antes.
4. Exportar PNG de cada estado desde el renderizador real. Codex compara referencia/resultado y corrige recorte, jerarquía y navegación. Guardar baselines revisados, sin aceptarlos automáticamente.
5. Compilar para ambos targets y verificar en placa lectura exterior, colores, consumo, botón y continuidad GNSS. El simulador no sustituye esas mediciones.

La [documentación oficial de entradas de imagen](https://learn.chatgpt.com/docs/image-inputs) confirma capturas/referencias como contexto y el uso de `codex --image`. Ejemplo histórico del encargo I5, ya ejecutado y sustituido por el encargo de reanudación del flujo vigente:

```text
Al llegar a I5, implementa la vista Paseo con la versión LVGL fijada
en el ensayo de compatibilidad. Resolución 240x280 RGB565, sin táctil.
Usa DisplaySnapshot; no recalcules métricas ni accedas a Wi-Fi/NVS desde UI.
Comparte ui/ entre simulador PC y firmware. Incluye estados sin fix,
fix válido y dato caducado. Conserva el contrato de datos de I3.
Genera tres capturas estáticas; navegación queda en I6 y batería en I7.
Verifica márgenes de esquinas y texto español. Compila también Classic.
```

## Alimentación y montaje

La propuesta conserva una celda 1S protegida y una rama boost 5 V dimensionada para LEDs. La placa y GNSS tendrán su alimentación adecuada; mantener masa común y adaptador de nivel lógico para las tiras. No alimentar las tiras desde un GPIO o desde la salida 3,3 V de la Waveshare.

El cargador integrado cambia la topología: elegir un único camino de carga de la celda. No conectar en paralelo cargador externo e integrado sin diseñar explícitamente reparto y aislamiento. Comprobar polaridad y paso del conector físico, corriente de carga configurada, protección de la celda, comportamiento USB+batería y corriente admisible de conector/pistas. La corriente anunciada de un chip no determina la capacidad térmica de la placa montada.

Actualizar el presupuesto de corriente del collar por perfil y medir pantalla apagada, brillo mínimo/medio/máximo, Wi-Fi transmitiendo y LEDs limitados. El limitador LED actual estima consumo; no mide toda la batería. Autonomía se calcula con energía útil y potencia media medidas, incluyendo eficiencia del boost, no sumando directamente corrientes de rieles a distintas tensiones.

Enclosure independiente para Display: medir PCB/pantalla, altura de conectores y radio de cables; ventana protegida, acceso al botón/USB, alivio de tensión y espacio de antena. Evaluar masa y balance sobre el collar. La ilustración comercial no acredita resistencia al agua. Ensayar primero en banco temperatura, consumo y mecánica.

## Ejecución por incrementos y criterios de aceptación

| Incremento | Entregable | Aceptación resumida |
| --- | --- | --- |
| I0 | Baseline Classic y perfil Waveshare | Builds registrados, placa identificada y arranque verificable |
| I1 | Dos tiras LED | RGBW, independencia y apagado probados en banco |
| I2 | GPS junto a LEDs | NMEA, fix, pérdida/recuperación y convivencia verificados |
| I3 | Una página de texto | Datos reales, validez y funcionamiento con LEDs/GPS |
| I4 | Base funcional consolidada | Portal, persistencia y carga conjunta comprobados |
| I5 | Paseo con IA/LVGL | Una vista mejorada y simulación mínima, sin regresión |
| I6 | Navegación y animación gradual | Botón, segunda vista y después transición medida |
| I7 | Extensiones opcionales | Una función y aceptación específica por entrega |

Los criterios detallados y dependencias están en el [plan incremental](2026-09-12_display-incremental-delivery.md). Un incremento funcional Display activo, con Classic evolucionando en paralelo; si falta placa, avanzar trabajo independiente sin declarar cerrada su validación física. No mantener dos copias divergentes de GPS, escenas o portal.

Pruebas específicas futuras: selección de perfiles sin pines duplicados, rutas de LED de estado ausente, transiciones del botón y timeout con rollover de `millis()`, snapshots inválidos, ADC fallido, OOM de UI, largas respuestas HTTP con GNSS continuo, cambios de escena durante animación y apagado/despertar repetido. Ejecutar la suite host existente y Wokwi Classic; no anunciar simulación de la placa Waveshare sin verificar soporte de sus periféricos.

## Alcance y pendientes de esta investigación

### Ampliación: interfaces bonitas y fluidas (investigación web, 2026-09-12)

La ruta LVGL propia con renderer reproducible ya se aplicó hasta I6a. Los
editores alternativos de la tabla siguen opcionales y no fueron instalados o
evaluados localmente. Sus características/licencias son referencias de la
consulta fechada, por verificar otra vez solo si se decide adoptar alguno.

| Camino | Evidencia y aplicación propuesta |
| --- | --- |
| LVGL C/C++ + Codex + simulador | Máximo control del código y las pruebas; mantener tema, vistas y fixtures separados. Es la ruta base sin editor propietario. |
| EEZ Studio | Editor gratuito y abierto con soporte LVGL 8.x/9.x. Buena opción para ajustar composición visual; mantener lógica del collar en C++ y una única fuente editable para lo generado. [Repositorio oficial](https://github.com/eez-open/studio) |
| SquareLine Studio | Edición visual y exportación de UI a C. Alternativa si resulta más cómodo su flujo; comprobar versión exacta del exportador y condiciones antes de adoptarlo. [Documentación del editor](https://docs.squareline.io/docs/layout/) |
| LVGL Pro Editor | XML, edición visual, previsualización LVGL y generación C; adecuado para que una IA edite componentes declarativos. La versión LVGL del proyecto debe concordar con el exportador, sin asumir compatibilidad con el demo v8. [Editor oficial](https://lvgl.io/docs/pro/editor/overview) |

Distinción de licencia relevante para automatización: el **Editor** LVGL Pro permite uso personal/no comercial gratuito, según su [licencia publicada](https://lvgl.io/docs/pro/editor/license). Su **CLI**, que valida, compila, genera C y captura PNG sin GUI, exige licencia Professional/Product/Platform y no está disponible con Community/Evaluation. Por tanto, no presupuestar automatización gratuita con `lvglpro screenshot`. [Documentación CLI](https://lvgl.io/docs/pro/cli)

LVGL publica un flujo de IA basado en documentación MCP, ejemplos XML y validación/capturas. Esto respalda un ciclo concreto de editar → compilar → renderizar → comparar, pero no prueba que el MCP esté configurado aquí. La alternativa gratuita sigue siendo un pequeño ejecutable de simulación propio que guarde PNG. [Integración de IA oficial](https://lvgl.io/docs/pro/ai)

#### Dirección visual específica

Dirección revisada tras I6a: negro puro, texto blanco/gris, color acotado a
estados y distancia registrada principal. La propuesta inicial casi negra con
fondo verde fue descartada por preferencia del propietario. Mantener unidades
estables y estados sin fix/caducado deliberados, con geometría probada.

La propuesta original centraba Paseo en velocidad y añadía Luces/Resumen; I6a la sustituyó por Actividad con distancia principal y Wi-Fi. Estado/pausa tendrán sus propios incrementos. Diseñar pensando en consulta breve: Google recomienda información interpretable de un vistazo y pruebas con distracción para wearables. Aquí se toman esos principios visuales, no el runtime Android ni sus gestos táctiles. [Guía de wearables](https://developer.android.com/design/ui/wear/guides/get-started/design-for-wearables?hl=en)

Referencia de implementación: el [demo smartwatch de LVGL](https://github.com/lvgl/lv_demos/blob/master/src/smartwatch/lv_demo_smartwatch.c) permite estudiar composición y transiciones. Su código recomienda 384 × 384; no es un ejemplo listo para 240 × 280 ni un benchmark de esta placa. Adaptar jerarquía y densidad en vez de reducir toda la pantalla proporcionalmente.

#### Movimiento deliberado

Valores iniciales propuestos para experimentar:

| Interacción | Tratamiento |
| --- | --- |
| Pulsar botón | Respuesta visual inmediata; objetivo inicial de latencia p95 menor de 100 ms |
| Cambiar página | Desplazamiento corto de elementos centrales, 160–220 ms, desaceleración suave |
| Cambio de estado | Transición breve de icono/color, 100–160 ms, texto explícito |
| Despertar | Rampa de backlight de 150–250 ms tras preparar el frame |
| GPS esperando | Indicador discreto de área pequeña; detener animación al apagar pantalla |

Evitar animaciones que persigan datos falsos: mostrar la velocidad válida recibida; una transición decorativa no modifica métricas ni predice movimiento. Las alertas no esperan a que termine una animación. Pulsaciones repetidas interrumpen o sustituyen una transición; no se acumulan en una cola larga. LVGL dispone de animaciones y curvas temporales para implementarlo sin bucles bloqueantes. [Animaciones v8.4](https://lvgl.io/docs/open/8.4/overview/animation.html)

#### Fluidez en esta pantalla SPI

1. Reutilizar widgets; actualizar etiquetas únicamente cuando cambia su representación visible. Mantener fondo y barra de estado quietos durante transiciones centrales.
2. Refrescar regiones modificadas. Ensayar dos buffers de 20, 40 y 70 filas y comparar tiempo de transferencia, frame time y heap. Los dos buffers solapan dibujo y envío solamente con transporte realmente asíncrono, por ejemplo DMA. Notificar `lv_disp_flush_ready` cuando el buffer pueda reutilizarse, nunca antes de terminar el envío. [Interfaz de display v8.4](https://lvgl.io/docs/open/8.4/porting/display.html)
3. Evitar zoom/rotación de contenedores grandes, transparencias de pantalla completa y sombras animadas extensas. LVGL documenta capas intermedias y asignaciones adicionales para algunas transformaciones y opacidades. [Estilos v8.4](https://lvgl.io/docs/open/8.4/overview/style.html)
4. Preconvertir recursos y limitar glifos necesarios; conservar antialiasing del texto. Empezar con geometría simple y un icono de mascota pequeño. Fondos animados y GIF de pantalla completa quedan fuera del MVP por su área de refresco y decodificación.
5. Considerar `esp_lcd`/adaptadores Espressif si el driver inicial resulta bloqueante. No asumir que una migración o PSRAM adicional elimina el cuello de botella SPI. Los modos antitearing RGB/MIPI del adaptador no están soportados para su ruta SPI. Dos buffers de dibujo no garantizan ausencia de tearing en el panel. [ESP LVGL Adapter](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html)

Desde I6, objetivo base de 20 FPS estables durante movimiento; explorar 30 FPS para regiones pequeñas solo si una necesidad y las mediciones lo justifican. I3/I5 estáticos no tienen ese requisito. A 40 MHz hipotéticos, 30 frames completos consumen 32,256 Mbit/s antes de overhead; 60 requieren 64,512 Mbit/s. La frecuencia soportada y estable debe verificarse en el panel concreto.

Medir frame times p50/p95/máximo, tiempo SPI, área actualizada, latencia botón→frame, heap mínimo y continuidad GNSS. Repetir con Wi-Fi y ambas tiras activas. Capturas sirven para composición; usar una secuencia temporal o vídeo y medición en dispositivo para juzgar fluidez. Criterio de elección de herramienta: producir la misma vista, exportarla, compilarla y capturar sus estados; elegir la que mantenga mejor el ciclo reproducible con menos trabajo manual.

Registro histórico: la investigación anterior a I0 solo modificó documentos.
Después se implementaron perfiles, diagnósticos y UI hasta I6a; las baselines
separan esa evidencia de la investigación. Esta reconciliación también cambia
solo documentos y conserva fuentes consultadas y resultados históricos.

Pendientes físicos: identificación completa, aceptación óptica/BOOT, periféricos,
carga conjunta y uso portátil. Polaridad de batería, corriente de carga,
dimensiones/masa y consumo no se deducen de la compra ni de la demo USB.
Al reanudar, seguir V1–V3 e I6b–I7 del plan incremental; el simulador ya existe.
