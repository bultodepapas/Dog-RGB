# RGB Dog Display: desarrollo incremental

Estado: **I6d Estado implementado, ensayado por USB y lectura/ciclo BOOT confirmados. I6a e I6c confirmados por el propietario; timeout de producto desactivado. V2/V3 y aceptación portátil pendientes**.
Fecha: 2026-09-12.

## Punto de reanudación

El propietario confirmó que la pantalla se ve mejor y BOOT cambia de página;
tras apagar manualmente el backlight confirmó que el primer clic despierta la
misma página y el segundo navega. Son observaciones físicas del usuario, además
de los eventos USB previos; no cierran barras/borde, radio con cliente ni V3.

La pausa documental terminó con su instrucción de continuar. COM6 volvió a
responder con las dos páginas I6a, GPS sin datos y cero bytes RX; no se informó
conexión de GPS o tiras. Se entregó I6c como opción de diagnóstico desactivada
al arrancar, sin adoptarla todavía como política automática del producto.
Esta preparación independiente no cierra las condiciones de aceptación V3.
I6b animación permanece sin iniciar.

Referencia actual: [guía I6d](../../Platformio/Dog-RGB/docs/display-i6d.md),
[baseline del incremento](../baselines/display-i6d-2026-09-12.md)
y [catorce capturas LVGL](../assets/display-i6d/manifest.json). No reinstalar I5
ni reconstruir el simulador como si fueran trabajo pendiente. Se conservaron
las imágenes de diagnóstico y el respaldo original. El HEAD cambió durante el
trabajo y después registró la implementación; al retomar, revisar también el
estado local. Hash del binario y manifest identifican exactamente lo probado.

| Entregado | Evidencia disponible | Lo que todavía no demuestra |
| --- | --- | --- |
| Perfiles Classic/Display y diagnósticos I0–I3 | Siete entornos compilados; aislamiento gráfico verificado | Revisión física/pinout completo ni funcionamiento de periféricos no conectados |
| Actividad y Wi-Fi, negro `#000000`, BOOT corto/despertar | 146 pruebas host, cuatro CTest y nueve PNG revisados | Lectura en el montaje final y comprobación completa de barras/borde; BOOT/despertar ya confirmados |
| Navegación optimizada, restauración de márgenes en dos pasos | Ventana final USB de 120,078 s: 30 cambios y 10 despertares; máximo 44,768 ms por llamada | Latencia botón→píxel, 20 FPS, autonomía o carga conjunta |
| Datos reales de radio y demo solo de GPS | AP observado; adaptador AP/STA probado con stubs | HTTP físico, asociación de teléfono o acceso a Internet |
| Preparación I4 de portal/persistencia | Pruebas de software y banco parcial | NVS/ruta/portal bajo GPS y ambas tiras reales durante 30 minutos |

### Prioridad visual solicitada después de I6d

El [subplan de identidad y evolución visual](2026-09-12_display-visual-identity.md)
detalla nombre del perro, placa digital con QR WhatsApp/teléfono y componentes.
La [investigación](../display-visual-identity-research.md) aporta fuentes y límites.
La [ampliación de 28 investigaciones GitHub/MCP](../display-github-research-2026-09-12.md)
incluye un ensayo host y selección por paquete: VIS-1a resuelve encoder/margen/
decoder independiente; VIS-1b integra tipografía y layouts A/B.
[VIS-1a ya está implementado y verificado en host](../baselines/display-vis1a-2026-09-12.md).
[VIS-1b ya está implementado y verificado en host](../baselines/display-vis1b-2026-09-12.md).
[VIS-2, persistencia/editor, está implementado en software](../baselines/display-vis2-2026-09-12.md).
Sigue VIS-3, integración y escaneo físico. Drivers alternativos y editores siguen opcionales.
La imagen I6d cargada se mantiene; aún no se integró identidad en su navegación.

Secuencia visual activa: VIS-1 prototipo LVGL de identificación/QR → VIS-2 datos,
portal y persistencia → VIS-3 integración estática y escaneo físico → VIS-4 pulido
común → VIS-5 transición localizada del I6b existente. VIS-6 reúne extensiones
opcionales individuales. Esta prioridad permite preparar/ensayar UI sin los
periféricos ausentes; no cierra V2/V3 ni aceptación portátil. I7-identidad queda
priorizado; las demás extensiones I7 no se activan en bloque.

### Secuencia para retomar

1. Revisar estado local, imagen preservada, conexiones actuales y confirmaciones
   del propietario; cerrar lo que pueda observarse de I6a en la placa USB.
2. Completar identificación/alimentación I0 antes de cablear. Con periféricos
   disponibles: I1 LEDs → I2 GPS+LEDs → I3 datos reales+LCD → I4 carga conjunta.
3. I6c ya está preparado y confirmado como experimento opt-in; I6d añade Estado
   con GPS y política LED; lectura y ciclo BOOT ya confirmados. Completar V1/V2
   restantes y después V3.
4. Con la base conjunta comprobada, evaluar I6b transición breve. I6e pausa
   estimada exige primero un contrato de observaciones del dominio. I7 se elige
   una extensión a la vez.

Sin periféricos se pueden preparar fixtures, contratos y casos de prueba al
reanudar; la siguiente entrega funcional preferida es cerrar la base física.
Una nueva función de UI no cierra ni desplaza automáticamente I1–I4.

Actualización I5: LVGL compartido y simulador verificados; comparación USB de diez
minutos completada. El propietario vio la primera página y pidió negro al percibir
el fondo verde como gris brillante. La revisión `#000000` está cargada y tiene
smoke propio de 90 s; aceptación óptica pendiente. Véanse la [baseline I5](../baselines/display-i5-2026-09-12.md)
y la [investigación técnica del panel](../waveshare-lcd169-technical-research.md).
Esta última contrasta demo oficial, esquema, datasheet, fallas reportadas y repositorios.

Orden de aceptación de pantalla: observar negro/RGBW/borde; si hace falta, diagnóstico
PWM acotado; verificar el repintado antes de animar. El smoke negro histórico I5
registró 50,485 ms de máximo agregado frente al presupuesto de 50 ms. I6a reduce
el área de cambio de página y restaura diagnósticos en dos iteraciones del loop:
su prueba USB final registró 44,768 ms de máximo por llamada y p95 normal con
cota superior de 20 ms. Cumple el presupuesto en esa ventana de banco; falta
la aceptación bajo carga conjunta. Mediciones, alcance y binario en la
[baseline I6](../baselines/display-i6-2026-09-12.md).
Conservar swap 0 y 40 MHz mientras no exista evidencia para cambiarlos. Confirmar
revisión física antes de usar botón de alimentación o pines que cambian V1/V2.
Esto no reemplaza las pruebas conjuntas I1/I2/I4 cuando se conecten periféricos.

Actualización I3: página de texto, adaptador de lectura, actualización parcial y controles de banco implementados; evidencia en la [baseline I3](../baselines/display-i3-2026-09-12.md) y procedimiento en la [guía LCD](../../Platformio/Dog-RGB/docs/display-i3.md). No se cierra aceptación física por compilar o generar una vista previa host.

Actualización I2: preparación software desde `4293f4c72cdfbf26816d7cd21f6e5c1686e421a3`, con seis builds y 137 pruebas host aprobados. Véase la [baseline I2](../baselines/display-i2-2026-09-12.md). La aceptación física conserva el orden I0 → I1 → I2.

Revisión de preparación: contrastado con firmware, pruebas y CI en `dc6789b1c66921ca3b29a1f78410a879aa699ccd`, más los cambios documentales locales. Implementación iniciada desde `3fa9e7bff27b0cc6e10c78eea0a68d966c7294b8`: perfiles y diagnóstico I0, seguidos por preparación software I1 autorizada al continuar el plan. Evidencia en las baselines [I0](../baselines/display-i0-2026-09-12.md) e [I1](../baselines/display-i1-2026-09-12.md); procedimiento en [boards.md](../../Platformio/Dog-RGB/docs/boards.md). I0/I1 físicos siguen abiertos; preparar el software no reemplaza su orden de aceptación.

Este documento gobierna la secuencia de trabajo de la variante Display. Ante diferencias de prioridad con el [plan de investigación Waveshare](2026-09-12_waveshare-display-variant.md) o el [flujo visual con IA](2026-09-12_display-ai-workflow.md), prevalece este orden. Esos documentos conservan valor como referencias técnicas; no convierten sus propuestas avanzadas en requisitos del primer incremento.

## Autoridad y hallazgos de la revisión

La separación entre placas, la reutilización del núcleo actual y la distinción entre pruebas de PC y placa son adecuadas. Hay que corregir cuatro problemas de planificación:

1. No había un hito explícito para demostrar ambas tiras en Waveshare antes de la interfaz.
2. GPS quedaba incluido en una integración tardía, sin prueba propia de recepción, fix y recuperación junto a LEDs.
3. El simulador completo y las cuatro vistas podían consumir el primer ciclo de desarrollo sin demostrar un collar funcional.
4. Batería, sensores, navegación, animaciones y herramientas estaban demasiado cerca del alcance inicial. Su presencia en la placa no obliga a implementarlos ahora.

La primera entrega útil será **LEDs + GPS + una página de texto con datos reales**, conservando la versión Classic. La calidad visual crecerá sobre esa base.

Selección técnica de apoyo: [bibliotecas, animaciones y repositorios revisados](../display-library-research.md). Arduino_GFX 1.6.7 y LVGL 8.4.0 ya están fijados y probados; el simulador actual usa framebuffer sin ventana. I6b evaluará animaciones nativas. LovyanGFX/TFT_eSPI/esp_lvgl_port, SDL y DMA siguen condicionados a una necesidad demostrada, no son tareas de instalación pendientes.

Este plan decide **orden, contratos, tareas y aceptación**. El plan Waveshare conserva evidencia eléctrica y arquitectura; el flujo IA describe el trabajo visual futuro; la investigación de bibliotecas conserva fuentes y alternativas. Los índices, roadmap y cola de tareas remiten aquí: no mantienen otro calendario Display. El contrato cloud conserva su alcance independiente y no es dependencia de esta integración.

| Hallazgo en el repositorio | Consecuencia de implementación |
| --- | --- |
| `pins.h` asigna GPIO1 a tira A; en el esquema Waveshare V2 es ADC batería | Perfil obligatorio antes de flashear/cablear; no reutilizar el binario XIAO |
| `main.cpp` configura heartbeat sin comprobar disponibilidad, también en la rama de diagnóstico AP | Encapsular todas sus escrituras; la ausencia de LED debe ser una operación vacía, sin convertir `-1` a GPIO |
| `led_ui.cpp` construye `LedBus` globalmente con los pines | Selección de perfil en compilación, disponible antes de `setup()` |
| `start_welcome()` solicita brillo 255; `LED_UI_ENABLED` es una constante, no un flag `-D` directamente sustituible | Diagnóstico omite welcome y limita explícitamente brillo; no asumir que bajar la configuración limita toda animación |
| `set_transport_enabled(false)` solo evita envíos siguientes | Enviar un frame negro antes de suspender salida; los píxeles retienen el último estado |
| `gps::has_current_fix()` puede indicar posición raw aunque falle la calidad; `total_distance_m()` es acumulado diario | Usar confianza/validez del dominio; no rotular distancia como sesión ni llamar fix confiable a cualquier posición |
| CI compila Classic y tests dependen de su nombre, libdeps y literales en `pins.h` | Preservar entorno; ampliar matriz y adaptar contratos afectados conservando comprobación de valores reales |
| El índice de planes calificaba todo excepto cloud como histórico | Corregir índice para reconocer este contrato Display sin alterar la autoridad cloud |

## Reglas de ejecución

- Un solo incremento funcional Display activo. Classic continúa recibiendo correcciones y mejoras en paralelo; esta regla evita anticipar funciones de Display, no congela Classic.
- Reutilizar drivers y lógica del firmware actual; portar no significa reescribir GPS, escenas, métricas o persistencia.
- Un repositorio, perfiles por placa y ramas cortas. Las mejoras comunes llegan a ambas variantes; Display agrega solo sus diferencias.
- Cada entrega registra alcance, evidencia y pendientes. Estados: pendiente, en desarrollo, verificado en software, verificado en placa. Compilar no equivale a funcionar físicamente.
- Cerrar una etapa requiere sus pruebas aplicables. Si falta hardware, avanzar en trabajo independiente y dejar la aceptación física abierta; no sustituirla por una afirmación basada en simulación.
- Reversión sencilla por commit/target y binario identificado por placa. No cambiar particiones, core o protocolos salvo necesidad demostrada del incremento.
- Las comprobaciones rutinarias no requieren una nueva autorización. Revisar con el propietario decisiones que cambien el alcance, no cada compilación.
- Los tiempos de banco siguientes son criterios iniciales de ingeniería, no certificación ni garantía de autonomía.

## Contrato técnico para el desarrollo paralelo

| Target | Función | Dependencias y límites |
| --- | --- | --- |
| `seeed_xiao_esp32s3` existente | Producto Classic | Pines/defaults actuales, sin LCD/LVGL ni código de diagnóstico Display |
| `wokwi` existente | Regresión simulada Classic | Conservar `extends = env:seeed_xiao_esp32s3`, UART y limitación de transporte actuales |
| `waveshare_lcd169` implementado en I0 | Producto Display experimental, núcleo compartido | Mismo core/ArduinoJson/NeoPixel; LCD de texto implementada en I3, LVGL desde I5; sin patrones/fixtures de banco |
| `waveshare_lcd169_bringup` implementado en I0 | Diagnóstico de la misma placa | Etapa 0; sin publicar como firmware de uso normal |
| `waveshare_lcd169_ledcheck` implementado en I1 | Mismo diagnóstico, compilado con etapa 1 | Un píxel y después ambas tiras; brillo 16, parada por comando/timeout/desconexión |
| `waveshare_lcd169_gpscheck` implementado en I2 | Núcleo normal, etapa 2 | GPS/LEDs/portal/persistencia existentes; welcome omitido, brillo máximo 16, diagnóstico en cola |
| `waveshare_lcd169_displaycheck` implementado en I3 | Núcleo normal con LCD, etapa 3 | Límites de banco I2; controles USB de barras/backlight/pausa; etapas 4+ rechazadas |

Mantener un único `main.cpp` y una única implementación de GPS, LED bus/policy, escenas, portal y persistencia. El diagnóstico añade rutinas pequeñas, excluidas del producto mediante compilación. Un selector **solo del target bringup**, `DOG_RGB_BRINGUP_STAGE`, comienza en 0: I0 consola/alimentación; I1 patrones del bus; I2 núcleo real con GPS/LEDs; I3 añade pruebas LCD. Valores desconocidos deben fallar al compilar. En I0/I1 la ruta de diagnóstico termina antes del arranque normal; en I2/I3 se reutilizan `setup/loop` normales con overrides de banco en RAM. No emplear `DEBUG_AP_ONLY_MINIMAL` como sustituto: omite GPS/LEDs. Documentar el valor usado en cada binario y recompilar al cambiarlo.

No añadir cuatro aplicaciones ni copiar el demo del fabricante. En I1 ejercitar el `LedBus`/conversión/limitador existentes desde la rutina de diagnóstico, sin abrir una segunda salida NeoPixel activa sobre los mismos pines. En I2 volver a la política LED normal. El apagado envía negro; la limitación de brillo de banco debe cubrir todos los emisores habilitados y no modificar NVS. Los valores de usuario de Classic conservan su comportamiento.

En `include/pins.h` conservar la fachada de nombres actuales y delegar en `include/board/board_profile.h`; los perfiles físicos son `xiao_s3.h` y `waveshare_lcd169_v2.h`. Fallar ante selección múltiple/inválida. Usar capacidades para distinguir hardware presente, función habilitada y driver inicializado. No definir una clase abstracta por cada periférico: constantes por placa y unas pocas funciones para alimentación/heartbeat bastan.

Comprobar que las dos salidas LED no colisionan entre sí ni con UART, USB o señales integradas reservadas del perfil; excluir el sentinel de pin ausente de operaciones GPIO. Probar también la selección Classic/Wokwi para que la extracción no cambie sus valores. No convertir el perfil V2 candidato en confirmación de revisión física.

La definición PlatformIO Waveshare debe fijar flash, modo PSRAM y USB conforme al módulo identificado. Reutilizar un manifest compatible solo tras comprobar esos campos; si falta, añadir uno local bajo `boards/` con procedencia documentada. Conservar inicialmente core fijado y particiones de 8 MiB, incluido `tracknvs`; ninguna migración NVS ni ampliación OTA pertenece a I0–I4. Separar tamaño de flash física de espacio de aplicación disponible. El perfil Display podrá revisar su consumo base estimado después de medir; los 200 mA actuales no son una medición de esta placa.

Excluir fuentes y bibliotecas de pantalla del build Classic, no solo omitir `display::begin()`. Confirmarlo en dependencias resueltas y mapa del binario. En Display, `has_display` no obliga a habilitar el servicio antes de I3. No mover configuración persistida a perfiles: brillo/modo siguen siendo elecciones del usuario; GPIO y capacidades son decisiones físicas.

## I0 — Base reproducible y perfil de placa

**Resultado:** poder compilar Classic y un target Waveshare mínimo sin ambigüedad de pines.

Tareas: registrar commit y estado de trabajo; ejecutar build y suite host existentes; identificar PCB/revisión y demo; extraer únicamente pines/capacidades necesarios; definir consola USB, flash/PSRAM y control de alimentación. No incorporar aún LVGL, IMU, RTC o buzzer. Tratar explícitamente la ausencia del LED de heartbeat externo.

La identificación física es requisito para cablear la nueva placa, pero la revisión y baseline de Classic pueden realizarse antes. Empezar en banco con alimentación conocida por USB/fuente y rama de LEDs dimensionada. La batería y carga conjunta tienen validación separada; el circuito de alimentación que la placa necesita para arrancar sí pertenece a I0.

Aceptación I0-software: comandos y resultados de Classic registrados; builds de los cuatro targets; perfiles y ausencia de heartbeat comprobados; Wokwi conserva su cableado y flags. Aceptación I0-placa: revisión identificada, consola y arranque repetibles durante cinco reinicios y diez minutos, sin resets inesperados; flash/PSRAM detectadas y comportamiento de alimentación registrado. Un build de perfil candidato V2 puede avanzar antes de identificar hardware, pero no autoriza tratar ese cableado como confirmado.

Dividir I0 en tres cambios revisables: **I0a**, baseline y evidencia; **I0b**, perfiles, heartbeat y targets; **I0c**, diagnóstico mínimo, matriz CI y arranque de banco. No mezclar aquí extracción masiva de módulos, actualizaciones de dependencias o herramientas gráficas. Falta de hardware deja I0-placa abierto; permite preparar pruebas y revisar I1, sin declarar que el collar ya funciona.

## I1 — LEDs en Waveshare

**Preparación software entregada:** target `waveshare_lcd169_ledcheck`, reutilizando bus/conversión/limitador. Arranca en negro y admite `1` (un píxel, 30 s), `f` (longitud configurada, hasta 15 min), `0` (apagado) y `?` (diagnóstico). `f` requiere completar antes los pasos de `1`; desconectar USB apaga sin reanudar al reconectar. El código no prueba por sí solo colores, corriente ni cableado. Véanse evidencia y protocolo enlazados al inicio.

**Resultado:** controlar las dos tiras desde la nueva placa.

Tareas: adaptar el bus LED existente al perfil; comenzar con un píxel y después la longitud configurada de cada tira, a brillo de banco reducido. Ejercitar rojo, verde, azul, blanco dedicado y apagado; comprobar tira A/B por separado. Integrar un modo simple existente y mantener el limitador actual. LCD apagada; diagnóstico por consola.

Un píxel significa encender solo el índice de prueba y enviar negro al resto, manteniendo la longitud del bus; no reducir `LED_STRIP_COUNT` a 1 si rompe los contratos del layout. Valor inicial propuesto de brillo de diagnóstico: **16/255**, con fuente limitada y presupuesto acordes al montaje; no es una garantía eléctrica. El blanco se prueba a través de `rgb_to_rgbw` y su canal W, sin saltarse el limitador. Omitir welcome en bringup; los patrones de prueba no entran en el producto y la animación welcome normal de Classic se conserva.

Aceptación física: orden RGBW correcto, ambas salidas independientes, apagado controlado y funcionamiento de quince minutos con cambios periódicos sin parpadeos espurios ni resets. Registrar alimentación, número de píxeles, brillo y corriente medida si se dispone del instrumento. No atribuir una corriente medida al limitador estimado. Ejecutar las pruebas LED existentes afectadas y build Classic.

No entra: nuevos efectos, editor de escenas, interfaz LCD o calibración completa de autonomía.

## I2 — GPS junto con LEDs

**Preparación software entregada:** `waveshare_lcd169_gpscheck` mantiene arranque y loop normales, parser/filtros, política LED y persistencia. Añade estados tipados de recepción y reporte `[I2]` por la cola serial existente, cada 600 ms. El bus limita brillo a 16/255 y mantiene el estimador activado con presupuesto máximo de 1000 mA, respetando valores inferiores y calibración. Estos límites no se guardan; las operaciones normales de configuración y GPS sí conservan su persistencia. No fuerza modo, Day Mode ni escenas: seleccionar Speed por el portal para la prueba correspondiente. I2 continúa sin USB y no incorpora comandos ni parada automática de I1. LCD apagada. Pruebas nativas verifican clasificación/caducidad/recuperación, formato y bus con entradas simuladas; no prueban recepción UART física.

**Resultado:** recibir GNSS y mantener LEDs operativos simultáneamente.

Tareas: conectar UART al perfil, conservar parser y filtros actuales, verificar recepción NMEA y distinguir recepción de fix válido. Exponer por consola estado, satélites/calidad, velocidad, distancia y antigüedad. Probar un comportamiento LED ya existente alimentado por GPS, sin modificar sus reglas para facilitar la demostración.

Cruce esperado: TX del GNSS → RX44 del ESP; RX del GNSS ← TX43 del ESP, condicionado al cableado confirmado. Mantener consola USB separada y buffer RX de 16 KiB. Para pérdida/recuperación interrumpir datos de forma controlada en banco; no hacer cortocircuitos ni manipular alimentación en carga. La recepción de bytes sin RMC válido no cierra I2.

Aceptación física: NMEA válido; adquisición de fix en una prueba exterior con condiciones registradas, sin prometer un tiempo máximo universal; quince minutos de GPS+LEDs; una pérdida controlada de datos y recuperación. Los datos caducados deben identificarse como tales. Registrar contadores/diagnósticos disponibles y resets. Ejecutar pruebas GPS afectadas y build Classic.

No entra: IMU, fusión de sensores, nuevas métricas o mapas.

## I3 — Pantalla sencilla con datos reales

**Preparación software entregada:** Arduino_GFX 1.6.7/ST7789 en producto Waveshare y diagnóstico etapa 3; Classic y diagnósticos I0–I2 excluyen fuentes y dependencia. `DisplaySnapshot` separado de formato/dibujo, GPS tipado de I2, valores inválidos explícitos y distancia diaria con fecha. Muestras a máximo 1 Hz, filas iguales sin repintar y una fila cambiada por loop. Diagnóstico con barras/borde, backlight, pausa de UI y tiempos máximos/percentil por buckets. Sin LVGL ni animaciones. La prueba nativa usa transporte/grabador falso; las vistas previas host no validan panel, SPI ni rendimiento.

**Resultado:** una página de lectura que sirve para usar y diagnosticar el collar.

Contenido inicial: estado GPS, velocidad, distancia y modo LED. Mostrar `--` o aviso explícito cuando el dato no sea válido. Sin menú, animaciones, porcentaje de batería o múltiples páginas. Encabezado, cuatro líneas legibles y colores simples son suficientes.

Tareas: validar primero barras/borde/orientación; usar driver mínimo compatible con el core fijado; encapsular `begin/update` en un servicio Display y tomar un snapshot pequeño del dominio. Actualizar valores cambiados a una cadencia máxima inicial de 1 Hz, sin esperas largas. Apagar/encender backlight por diagnóstico para verificar independencia del núcleo.

Conservar orden del núcleo: alimentación de placa al principio; almacenamiento/configuración, GPS/geofence/LEDs y orden BLE→Wi-Fi existentes; inicialización Display acotada al final del arranque normal. En el loop, servir GPS primero y añadir Display después del trabajo existente, sin `delay` periódico ni espera a fix/USB. En I3 se usa `begin()` una vez y `tick()`, que llama al adaptador de lectura solo cuando vence el muestreo para capturar un instante fresco después del trabajo del núcleo; backlight se enciende tras dibujar el primer frame. Ante error conocido de driver/asignación, registrar y deshabilitar UI, sin reiniciar todo el collar.

**Elección deliberada:** Arduino_GFX u otro driver mínimo ya validado puede dibujar texto directamente. LVGL no es requisito de I3. Si introducir LVGL añade trabajo de integración, se pospone a I5; conservar servicio y modelo para reutilizarlos. No construir el simulador avanzado como prerrequisito.

Ensayo inicial: Arduino_GFX/ST7789, con dimensiones, offsets y versión registrados. Una fuente legible, unidades alineadas y áreas que se actualicen solo al cambiar aportan calidad desde esta pantalla básica. Probar una alternativa únicamente si falla compilación, inicialización o presupuesto de actualización y el problema queda identificado.

Aceptación física: texto completo y colores correctos; datos coinciden con el dominio/consola para la misma muestra y período; búsqueda, fix y dato caducado distinguibles; quince minutos con GPS+LEDs+LCD; pantalla apagada o servicio deshabilitado no detienen tracking ni LEDs. Una inicialización fallida detectable de LCD tiene salida acotada. **SPI de escritura no confirma que el panel esté conectado ni que muestre píxeles:** un `begin()` exitoso no sustituye inspección física y no se promete autodetección de pantalla ausente. Registrar latencia de loop con y sin display.

### Contrato mínimo de datos I3, reutilizado en I5

`DisplaySnapshot` es una estructura C++ acotada, sin `Arduino.h`, punteros a objetos mutables ni acceso a NVS/HTTP. Separar el adaptador del dominio del dibujo. Campos mínimos: instante monotónico de captura, estado GNSS, velocidad/validez, distancia diaria/fecha registrada y modo LED. Sin batería, sesión, AP o nombre del perro hasta que una vista los necesite.

| Campo visible | Fuente y regla |
| --- | --- |
| GPS | Estado derivado del parser: sin observaciones, buscando/calidad insuficiente, fix confiable, datos caducados. No usar solo `has_current_fix()` |
| Velocidad | `gps::speed_usable()` y confianza vigente; `last_speed_kph()` en km/h. Si inválida, `--`; cero solo si es una muestra válida de reposo |
| Distancia | `gps::total_distance_m()` en metros, formateada sin recalcular. Es acumulado diario, no distancia del paseo/sesión; conservar valor registrado al perder fix |
| Fecha de distancia | `gps::current_date()`; usar «Dist. día registrado» y mostrar fecha cuando haga falta. No rotular «Hoy» sin fecha vigente confirmada |
| Modo LED | Estado/modo del dominio LED; no crear otro enum persistido ni copiar políticas en UI |

El parser ya aplica vencimiento RMC de 3.000 ms y UART de 5.000 ms; calidad GGA depende de configuración. I2 incorporó `gps::reception_state()` y helpers de caducidad compartidos con el parser: reutilizarlos en I3 sin duplicar umbrales. Preservar las reglas y probar recuperación/`millis()` con rollover. Nunca inferir caducidad del valor cero ni del número de frames. Que una velocidad caduque no borra distancia persistida. Los fixtures host se convierten al mismo contrato y nunca entran en el producto.

## I4 — Consolidación del collar básico

**Avance actual:** prueba nativa del codec y guardado/recarga A/B real con diez cambios y procesos nuevos para Classic/Display, incluida escritura interrumpida; suite del portal embebido aislada de las pruebas de la aplicación web. El propietario autorizó utilizar la placa USB sin GPS ni tiras: diagnóstico LCD con fixtures explícitos mediante `f`, etiqueta DEMO y sin inyectar datos al dominio/persistencia. Hay evidencia física parcial de arranque/memoria y LCD; no reemplaza la prueba conjunta de 30 minutos. Véanse [guía I4](../../Platformio/Dog-RGB/docs/display-i4.md) y [baseline I4](../baselines/display-i4-2026-09-12.md).

**Resultado:** base utilizable que merece convertirse en referencia para la evolución visual.

Tareas: comprobar comportamiento existente del portal con los tres periféricos, cambios de modo/configuración, persistencia y reinicios. El portal se conserva durante el port; aquí se prueba expresamente bajo carga conjunta. Hacer una sesión de banco de al menos treinta minutos y una prueba exterior breve documentada. Recuperar fallos encontrados antes de ampliar UI.

Secuencia mínima de banco: diez cambios entre modos existentes y tres ciclos de guardar configuración/reiniciar/leerla, con valores de prueba anotados y restaurados al terminar. Incluir una consulta/exportación de ruta mientras GPS, LEDs y LCD están activos. Identificar muestras y reinicios en el log para comparar persistencia y distinguir resets solicitados de inesperados.

Aceptación: GPS, ambas tiras, LCD y portal conviven sin fallos observados durante la prueba definida; cambios esperados sobreviven al reinicio; sin degradación de memoria progresiva observada; builds y suites relevantes de ambas variantes registrados. Informar límites de la observación, no declarar ausencia universal de fallos.

Si se va a utilizar batería, validar antes polaridad, camino único de carga, corriente/capacidad de los módulos, USB+batería y apagado. Si no está listo, la entrega puede cerrarse como **base de banco**, manteniendo uso portátil pendiente. No realizar prueba sobre el perro con un montaje de banco sin validar fijación y alimentación.

## I5 — Mejorar una vista con IA y LVGL

**Entregado en software y banco USB; aceptación óptica/conjunta abierta:** el propietario confirmó que ve funcionar la
demo I4 y autorizó continuar sin GPS/tiras soldados. Se desarrolló una vista
Paseo con LVGL 8.4.0, formato compartido, buffer único de 20 filas, simulador
CMake con tres capturas y página sencilla seleccionable para comparación.
I6a la evolucionó a Actividad y añadió Wi-Fi. La aceptación física conjunta I4 sigue abierta; no se convierte la ausencia de
periféricos en una validación simulada. Véanse [guía I5](../../Platformio/Dog-RGB/docs/display-i5.md)
y [evidencia I5](../baselines/display-i5-2026-09-12.md). Para estas capturas estáticas
el primer port PC usa callback a framebuffer sin SDL; SDL/interacción quedan
para cuando la navegación lo necesite. Se mantiene el mismo código/configuración LVGL.

**Resultado:** sustituir la página básica por Paseo bien diseñada, manteniendo iguales los datos y funciones.

Aplicar gradualmente el [flujo visual](2026-09-12_display-ai-workflow.md): elegir versión LVGL probada; una vista compartida PC/placa; tres escenarios iniciales (sin fix, fix, dato caducado); capturas reproducibles estáticas y revisión. Añadir primero herramientas mínimas para esos escenarios. El runner temporal completo, generador de tema y reportes elaborados solo se incorporan cuando resuelvan una necesidad real.

Aceptación: mejora visual comprobable en capturas y placa, paridad de datos con I4, sin regresión funcional. Mantener temporalmente la pantalla básica como opción de diagnóstico si facilita comparar. Medir memoria y tiempo de actualización antes/después.

Usar el port oficial CMake/SDL como referencia, con submódulos/versiones compatibles. Adoptar actualización por cambios, inspirada en la revisión de InfiniTime. Usar fuentes LVGL existentes al inicio y `lv_font_conv` solo cuando la personalización lo requiera. No incorporar firmware ajeno completo.

## I6 — Navegación y movimiento

**Resultado:** agregar una segunda vista y navegación por botón; después una transición breve.

**I6a software implementado:** Actividad con distancia prioritaria, Conexión con
snapshot Wi-Fi de solo lectura, BOOT GPIO0 con debounce/liberación y despertar
sin avance accidental. Nueve capturas y cuatro contratos CTest; ver [guía](../../Platformio/Dog-RGB/docs/display-i6.md)
y [baseline](../baselines/display-i6-2026-09-12.md). Cambios instantáneos, sin
animaciones ni timeout automático. El circuito de alimentación no se reutiliza
como entrada UI. La observación física del botón y las pruebas de carga real
se registran por separado, no se infieren de la inyección USB del evento.

La segunda vista seleccionada es **Conexión**: explicar AP/STA y cómo abrir el
portal, leyendo el gestor existente. La principal evoluciona hacia **Actividad**,
con distancia del día registrado como dato prioritario y velocidad secundaria.
La [especificación de uso y pantallas](2026-09-12_display-use-and-screens.md)
define contenido, avisos, estados y escenarios. Estado será una tercera página
posterior; pausa estimada será una variante contextual cuando exista evidencia
de quietud. No introducir todos estos estados en el primer cambio de navegación.

El apagado por falta de interacción requiere despertar validado y no pausa GPS,
LEDs ni registro. La clasificación de descanso y el resumen por paseo necesitan
contratos propios: una sesión de arranque no equivale a una salida, y ausencia
de fix no demuestra reposo. Wi-Fi desconectado durante una salida no produce
por sí solo una nueva alerta global. La [guía I6a](../../Platformio/Dog-RGB/docs/display-i6.md)
distingue lo incorporado de las propuestas posteriores.

Tareas secuenciales: botón/despertar compatible con circuito de alimentación; segunda vista útil; pruebas de pulsaciones repetidas; transición; capturas temporales y medición en placa. Optimizar SPI/buffers solo ante evidencia de un cuello de botella. No imponer 20/30 FPS a la pantalla estática de I3.

Aceptación: eventos coherentes, sin acciones involuntarias al despertar, animación sin afectar GPS/LEDs/portal, latencia y consumo documentados. Añadir otras vistas una por incremento.

Ejercicio físico pendiente: treinta cambios de página, diez de ellos con pulsaciones rápidas, y diez ciclos de apagar backlight/despertar. La prueba USB entregada usó treinta eventos separados un segundo y diez despertares; no acredita pulsaciones físicas rápidas. Verificar destino final y memoria tras cada grupo. I6a ignora pulsaciones de al menos 1,5 s; no les asigna apagado. No confundir apagar backlight con cortar alimentación de la placa.

Implementar movimiento con `lv_anim` y, solo para coordinación necesaria, timeline. Evaluar transporte/buffers con condiciones iguales. Sprites `lv_animimg` y GIF quedan como detalle opcional de I7, después de la navegación; no son requisitos para una UI fluida.

## I7 — Extensiones opcionales

Batería calibrada, RTC, IMU, buzzer, más vistas, ahorro avanzado y refinamiento mecánico se priorizan individualmente. Nube, OTA y endurecimiento avanzado siguen fuera de esta iniciativa inicial. Cada extensión necesita utilidad concreta, alcance pequeño y aceptación propia.

## Paquetes pendientes: entrada, trabajo y cierre

Los identificadores siguientes desglosan los incrementos existentes; no crean
otro calendario. Cada paquete se cierra en su baseline con resultado observado,
fallos y pendientes. Las duraciones son ventanas de prueba, no estimaciones de
horas de desarrollo ni fechas prometidas.

### V1 — Cerrar aceptación de la UI I6a disponible en USB

- **Entrada:** reanudación, placa identificada como la usada en banco y binario
  I6a conservado; registrar si cambió hardware o firmware.
- **Trabajo:** observar negro, barras RGBW, borde, orientación y las tres páginas actuales;
  comprobar BOOT corto/liberación, rebote y pulsaciones rápidas. Probar diez
  despertares distribuidos entre las tres páginas; el primero despierta sin
  avanzar y el siguiente cambia. Verificar retorno desde barras/texto sin
  márgenes residuales ni frame incompleto iluminado.
- **Radio:** con teléfono/cliente disponible comprobar AP sin/con cliente,
  STA+AP, conexión fallida y pérdida de IP. Abrir el portal usando la dirección
  mostrada y comparar con la misma muestra de estado. Registrar/restaurar
  cualquier configuración modificada; contar asociaciones no acredita HTTP.
- **Salida:** observación del propietario y log con eventos físicos separados
  de los inyectados; cero cambios involuntarios. Si falta teléfono o persiste
  un recorte, anotar el caso abierto. No declarar Internet a partir de STA.
- **Si el negro sigue molestando:** diagnóstico PWM acotado y separado, con
  brillo/frecuencia, condiciones visuales y consumo si se mide. Conservar
  swap 0, inversión y 40 MHz salvo fallo reproducible de color/transporte.

### V2 — Completar I0 y conectar los periféricos gradualmente

| Paso | Preparación y acción | Salida necesaria para continuar |
| --- | --- | --- |
| I0 pendiente | Identificar revisión/mazo, confirmar alimentación y continuidad de pines; conservar la evidencia previa de cinco reinicios USB y memoria | Revisión y cableado anotados; completar diez minutos/arranques que falten, sin repetir lo ya válido si no cambió la configuración |
| I1 físico | Fuente/masa/nivel lógico adecuados; GPIO17/18 solo tras confirmar perfil; `ledcheck`, un píxel y después ambas longitudes | RGBW y A/B independientes, apagado efectivo, 15 min sin resets ni parpadeo espurio; registrar brillo y alimentación |
| I2 físico | UART cruzada del GNSS, target `gpscheck`, modo Speed existente | NMEA válido, fix confiable y 15 min con LEDs; pérdida controlada/recuperación y deltas RX/overflow |
| I3 físico | `displaycheck` con `v` para datos reales y backend básico `s` como referencia | GPS/LED/LCD coinciden con muestras del dominio; 15 min y ventanas con backlight apagado/UI pausada |

El detalle eléctrico y los comandos existentes están en la
[guía de placas](../../Platformio/Dog-RGB/docs/boards.md) y las secciones I0–I3.
No cargar producto sin límites de banco en un montaje aún no caracterizado.
Si solo llega un periférico, validar únicamente los casos independientes y
mantener abierto el caso conjunto; no simular su cierre.

### V3 — I4 conjunto y comparación final I6a

- **Entrada:** V2 cumplido, ambas tiras y GPS reales, cliente para portal y
  registro de configuración inicial. Confirmar `demo=0`.
- **Protocolo:** seguir la [guía I4](../../Platformio/Dog-RGB/docs/display-i4.md):
  30 minutos, diez cambios de modo, tres ciclos guardar/reiniciar/leer,
  exportación de ruta poblada y transferencia lenta/interrumpida. Los reinicios
  solicitados se marcan; segmentar contadores/tiempos entre arranques.
- **Comparación de UI:** dentro de condiciones equivalentes, medir texto/LVGL,
  páginas, backlight apagado y servicio pausado; repetir navegación física.
  Reservar ventanas separadas para actualización normal, cambio de página y
  retorno desde diagnóstico. No mezclar sus histogramas como un único p95.
- **Cierre:** cero resets inesperados y nuevos overflows UART, sin caída
  sostenida de heap tras calentamiento ni fallos de asignación; p95 normal
  ≤20 ms y máximo por llamada Display ≤50 ms. Registrar latencia del loop y
  HTTP con/sin UI; si hay regresión funcional o margen insuficiente, corregir
  antes de añadir movimiento. La restauración en dos pasos necesita además
  comprobar latencia visible total; 44,768 ms no la mide.
- **Entrega:** base de banco aceptada o lista concreta de fallos reproducibles.
  La prueba exterior breve se documenta con montaje adecuado. Batería,
  autonomía, temperatura y fijación siguen siendo aceptación portátil separada.

### I6b — Una transición visual, opcional

- **Entrada:** V1 y V3 aceptados. Puede prepararse un escenario PC antes, pero
  no declararse fluidez de placa ni desplegarse como siguiente base aceptada.
- **Cambio:** un movimiento pequeño de 160–220 ms con `lv_anim`, conservando
  tipografía, negro y datos I6a. Mantener cambio instantáneo como alternativa.
  No añadir sprites, nueva biblioteca, tarea, DMA ni tamaño de buffer a la vez.
- **Interacción:** botón durante transición sustituye destino, no acumula
  animaciones; prueba de paridad del destino, pérdida de GPS y despertar.
- **Pruebas:** tiempos virtuales 0/50/100/200 ms y evento intermedio; secuencia
  de capturas del renderer real, memoria tras 30 ciclos. En placa medir tiempos
  entre frames, servicio/SPI y botón→primer cambio visible con vídeo o instrumento.
- **Salida:** objetivo 20 FPS solo durante movimiento y respuesta p95 <100 ms,
  sin regresión V3. Si no aporta legibilidad o excede presupuesto, conservar
  I6a instantánea y registrar que se descarta la transición; no migrar el stack
  para sostener un efecto decorativo.

### I6c — Pantalla sin consultar: timeout independiente

**Entregado como experimento opt-in:** `i/o` en etapa 3, plazo 30 s, apagado
solo de backlight y sin persistencia. Cuatro CTest ampliados, 147 pruebas host,
tres builds, secuencia USB de 190 s y confirmación del propietario de apagado
automático/despertar. Ver [guía](../../Platformio/Dog-RGB/docs/display-i6c.md)
y [baseline](../baselines/display-i6c-2026-09-12.md). Producto no lo activa;
V3 sigue siendo condición de aceptación conjunta.

- **Entrada para aceptación:** despertar físico V1 y continuidad V3 probados.
  El despertar ya está confirmado. La reanudación permite preparar y ensayar
  I6c por USB con activación explícita de diagnóstico; V3 sigue abierto y la
  política del producto permanece desactivada. No depende de adoptar I6b.
- **Cambio inicial:** opción desactivable de 30 s sin interacción, con override
  de banco; solo backlight. La opción nace desactivada para comparar, sin nueva
  persistencia o interfaz de configuración extensa en esta primera prueba.
  Muestras GPS, paquetes Wi-Fi o pasos del perro no reinician el contador.
- **Reglas:** pulsación aceptada reinicia la consulta; primera pulsación en
  oscuro prepara la página actual y despierta, sin navegar. Si el timeout vence
  durante transición, cancelar/completar lógicamente el destino y mantenerlo
  oscuro hasta un despertar explícito. Retener página; no guardar cada evento.
- **Pruebas:** justo antes/en/después del límite, `millis()` rollover, botón
  mantenido/rebotado, reinicio, las tres páginas desde I6d, dato que caduca en oscuro y diez
  despertares por página. Comprobar que no cambian GPS/LED/Wi-Fi/NVS por timeout.
- **Salida:** lectura al despertar completa, continuidad real y consumo
  encendido/apagado medido cuando haya instrumento. No prometer autonomía.
  Suspender además refresco, PWM gradual o deep sleep son evaluaciones distintas.

### I6d — Estado: tercera página solo si resuelve una consulta

**Implementado:** dos grupos, GPS y luces/control. Reutiliza clasificación GPS
y copia la política LED efectiva; conserva Modo día y aviso simultáneos, y
distingue transporte pausado. Sin señal fiable se omiten registro, batería,
brillo aplicado y satélites. Cinco CTest, catorce PNG y 147 pruebas host;
recursos, USB y límites en la [baseline I6d](../baselines/display-i6d-2026-09-12.md).
Navegación Actividad → Wi-Fi → Estado → Actividad; sin animación ni cambio NVS.

- **Entrada:** dos páginas aceptadas; elegir hasta tres grupos útiles tras
  observar el uso: GPS, luces efectivas y registro.
- **Datos:** ampliar snapshots acotados con validez/fecha; leer política LED
  efectiva (incluido Modo día), no afirmar que una tira está encendida por su
  configuración. Para registro, identificar señal fiable de fallo actual antes
  de mostrar `Guardado` o un error. No inferir salud de un contador histórico.
- **Cambio:** vista compartida y ciclo de tres páginas; conservar wake-only,
  SSID/IP en Wi-Fi y métricas principales en Actividad. `/dev` conserva detalle
  técnico. Sin acciones de borrar o editar configuración desde BOOT.
- **Salida:** fixtures válidos/desconocidos/fallidos, texto completo, navegación
  circular y recursos/timing comparados contra I6a/I6c. Si no hay señal fiable
  de un grupo, omitirlo y documentarlo; no rellenarlo con una constante favorable.

### I6e — Pausa estimada: primero dominio, después presentación

- **Entrada:** captura real de movimiento/quietud/pérdida de señal que permita
  evaluar la propuesta de [uso del collar](2026-09-12_display-use-and-screens.md).
- **Primer cambio:** contrato tipado `movimiento/quietud/desconocido`, con
  antigüedad y tiempo observado, basado en la política existente. Sin tocar
  integración de distancia ni persistir un nuevo tipo de paseo.
- **Segundo cambio:** variante contextual de Actividad. Ensayar 60 s de quietud
  válida para entrar y 5 s de movimiento para salir; son hipótesis ajustables,
  no valores validados. Un hueco GPS interrumpe la evidencia y exige reunirla
  de nuevo. Pausa breve conserva la vista normal.
- **Salida:** pruebas de ruido cerca del umbral, huecos/reanudación, reinicio y
  reloj; comparación con observación de campo. Nunca `Durmiendo`, salud ni
  descanso calculado como tiempo encendido menos tiempo activo. No alterar
  luces/radio automáticamente.

### I7 — Decisiones que todavía requieren una función propia

| Función | Decisión/dato previo | Primer entregable acotado |
| --- | --- | --- |
| Resumen por paseo | Inicio/fin explícito, reinicios, pausas y relación con sesiones de arranque | Contrato y pruebas de ciclo de vida antes de una nueva página |
| Batería/USB/carga | Revisión eléctrica, divisor/ADC calibrados y fuente real de estado de carga | Voltaje válido/desconocido; porcentaje solo después de validación bajo carga |
| IMU/RTC | Utilidad y consumo medidos; reloj confiable separado de fecha GNSS | Un driver con timeout/fallo recuperable, sin inferir sueño o salud |
| SSID internacional | Glifos y presupuesto de flash; conservar IP y límite de 32 bytes | Ampliar cobertura declarada y fixtures; hoy se muestra `Nombre no compatible` |
| Avisos contextuales | Señal de dominio vigente, prioridad, resolución/deduplicación | Un aviso que no cambie página recordada ni anuncie geofence con posición caducada |
| QR/nombre/recursos animados | Caso de uso y lectura física real | Una mejora por entrega; sin sustituir SSID/IP por un QR no probado |

Cloud, BLE, OTA y herramientas avanzadas siguen siendo opcionales y mantienen
sus propios contratos. La pantalla no los convierte en prerrequisitos.

## Mapa de cambios y verificación

Las rutas I0–I6a, I6c opt-in e I6d ya se incorporaron. I6b/I6e/I7 siguen propuestas. La tabla conserva la división de trabajo, no sustituye la baseline de ejecución.

| Cambio | Archivos o área | Verificación que permite cerrarlo |
| --- | --- | --- |
| I0a | `docs/baselines/` | Baseline real, fallos preexistentes separados de regresiones |
| I0b | `platformio.ini`, `include/pins.h`, `include/board/`, `src/board/`, `src/main.cpp`, manifest local si hace falta | Perfiles/heartbeat, cuatro builds, contratos host afectados |
| I0c | Rutinas pequeñas `include/bringup/` y `src/bringup/`, `.github/workflows/ci.yml`, instrucciones firmware | Solo un `main`, diagnóstico fuera del producto, logs de arranque, artefactos por entorno |
| I1 | Rutina bringup y frontera LED existente | RGBW/apagado/limitador, brillo acotado incluso al reiniciar, independencia A/B |
| I2 | Perfil UART y diagnóstico; módulo GPS solo si existe necesidad de port real | Parser existente, pérdida/recuperación, convivencia con modo LED normal |
| I3 | `include/display/`, `src/display/`, adaptador GPS de lectura si hace falta, hooks en `main.cpp` | Semántica del snapshot y fixture inválido, dependencia ausente en Classic, texto en placa |
| I4 | Correcciones concretas + evidencia | Portal/configuración/persistencia bajo carga conjunta |
| I5 | `src/display/ui/`, `tools/display-simulator/` en raíz, fixtures y tema mínimo | Mismo código LVGL, tres PNG, versiones fijadas, comparativa de recursos |
| I6a entregado | `display.cpp`, `button.h`, `connection*`, `ui/`, `lvgl_port`, simulador/fixtures | Cuatro CTest, nueve PNG, BOOT/despertar y banco; aceptación física restante V1/V3 |
| I6c / I6d implementados | Timer opt-in, `status*`, servicio/port y simulador | Cinco CTest, catorce PNG; evidencia y pendientes en sus baselines |
| I6b / I6e propuestos | UI/servicio y pruebas; adaptador de dominio cuando corresponda | Entrada/salida por paquete; no ampliar simultáneamente |

Desde `Platformio/Dog-RGB`, baseline existente:

```powershell
pio --version
pio pkg install -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3
pio run -e wokwi
python -m unittest discover -s test -p "test_*.py" -v
```

Usar CLI 6.1.19 y requisitos de [testing](../testing.md) para paridad con CI; registrar cualquier diferencia del equipo local. El paso `pkg install` importa: `test_led_phase4.py` busca ArduinoJson bajo `.pio/libdeps/seeed_xiao_esp32s3`. `test_wokwi_assets.py` lee literales de `pins.h`; adaptar sus comprobaciones al perfil extraído y comprobar valores, no suprimirlas. `test_track_retention.py` comprueba particiones. No renombrar Classic para simplificar una plantilla.

Los entornos I0 ya existen; comandos desde `Platformio/Dog-RGB`:

```powershell
pio run -e waveshare_lcd169
pio run -e waveshare_lcd169_bringup
pio run -e waveshare_lcd169_ledcheck
pio run -e waveshare_lcd169_gpscheck
pio run -e waveshare_lcd169_displaycheck
pio pkg list -e waveshare_lcd169
```

| Modificación | Comprobación antes de integrar |
| --- | --- |
| Perfiles, `main`, core compartido, PlatformIO | Siete builds actuales + suite host; Wokwi prepare/suite si cambian comportamiento, UART o assets, con disponibilidad/token documentados |
| Driver/UI exclusivo Display | Builds Waveshare producto/diagnóstico afectados + Classic y pruebas del adaptador; Wokwi adicional si cambian hooks compartidos |
| Portal o contrato API | Verificación anterior + `webui:check`, `webui:unit`, smoke y casos de navegador afectados, según guía existente |
| Solo documentación | Enlaces locales, coherencia de alcance/estados y `git diff --check`; no simular una aceptación física |

Ampliar el job firmware existente a entornos con artefactos y rutas separados; conservar paquete/size/hash y distinguir diagnóstico de producto. Incluir manifest de board y configuración relevante en la clave de caché. La suite host común corre una vez; agregar casos por perfil donde cambie el resultado. Un cambio compartido no se integra con Classic roto. Una dependencia gráfica que impida compilar Classic es regresión aunque Waveshare funcione. Los jobs cloud existentes siguen independientes.

Al cambiar las ramas del selector bringup, compilar las etapas ya introducidas (0 hasta la actual), además del producto sin diagnóstico. No multiplicar esa matriz para cambios documentales o de estilo sin relación. Cuando se llega a I5, los fixtures gráficos de placa son una opción explícita de diagnóstico sobre I3; no convertirlos en otro firmware GPS ni en datos por defecto del producto.

## Presupuestos y condiciones de avance

- **I0–I4:** cero resets inesperados y ningún nuevo overflow UART durante la ventana definida. Comparar contadores por diferencia inicio/fin, no por total histórico. Errores NMEA ambientales se registran con condiciones; no exigir un GPS perfecto para ocultar un problema de recepción.
- **I3:** máximo una actualización de datos por segundo, sin repintar valores iguales. Objetivo inicial de tiempo agregado por servicio Display: p95 ≤20 ms y máximo ≤50 ms por tick después del arranque, medido bajo la misma carga sin/con LCD. Son presupuestos propuestos, no resultados; si no se cumplen, reducir región/cantidad por tick antes de añadir tareas o DMA.
- Registrar duración total del loop y por fase, flash usada, heap interno mínimo/bloque máximo y PSRAM. Si los logs actuales solo dan máximos, no inventar p95: añadir medición acotada por histograma/muestras en diagnóstico cuando corresponda. Medir sin volcar logs por cada frame.
- **I4:** memoria estabilizada tras calentamiento y sin caída sostenida durante los 30 minutos y ciclos definidos; cero fallos de asignación. Anotar mínimos y condiciones, no inferir ausencia de fugas de una captura aislada.
- **I5:** comparación contra I4 con la misma muestra/carga; primera vista estática sin objetivos de animación. Buffers mínimos primero; un frame completo ocupa 134.400 bytes y dos buffers no garantizan concurrencia con un driver síncrono.
- **I6a:** máximo USB observado 44,768 ms por llamada; p95 normal con cota superior 20 ms. No mide latencia visible total ni cierra carga conjunta. Buffer único 9.600 bytes, pool LVGL 48 KiB y contenido de página 51.308 píxeles transferidos: referencia antes de optimizar de nuevo.
- **I6b propuesto:** transición de referencia 200 ms, objetivo 20 FPS durante movimiento y respuesta a botón p95 <100 ms, por confirmar en placa. Si falla, reducir animación/área antes de cambiar stack. Las mediciones del PC no cierran este criterio.

Si una etapa falla, corregirla o reducir su alcance explícitamente; conservar el último binario validado de cada placa. No flashear una imagen de otra variante ni borrar NVS para hacer pasar una prueba. Durante I0–I3 los binarios Display son experimentales. La primera entrega I4 declara **base de banco** o **portátil validada**, junto con revisión/commit y limitaciones reales.

## Decisiones abiertas, con momento de resolución

| Pendiente | Evidencia necesaria | Bloquea |
| --- | --- | --- |
| SKU/revisión física y pinout del mazo | Inscripción/fotos de placa, esquema/demo correspondiente y continuidad | Cableado nuevo/cierre I0; no invalida el banco USB ya autorizado y observado |
| Alimentación/SYS_EN/SYS_OUT y BOOT | Flash 16 MiB/PSRAM 8 MiB ya detectadas; completar revisión y comportamiento de alimentación. BOOT es GPIO0 independiente | Alimentación portátil y cierre del botón físico; no volver a tratar memoria detectada como desconocida |
| Número/orden de píxeles, fuente y presupuesto de banco | Montaje actual, rieles y prueba de un píxel con límite reducido | Encender tiras I1; no compilación |
| Contraste/borde y parámetros ópticos | Arduino_GFX 1.6.7, RGB565/swap 0 y 40 MHz ya usados; inspección óptica pendiente | Cierre visual V1; no selección nueva de driver sin fallo |
| LVGL/port PC | Resuelto: LVGL 8.4.0 compartido, CMake sin ventana, catorce PNG y cinco contratos | No bloquea; SDL/runner de animación solo si I6b lo necesita |
| Carga, conector, celda, boost y montaje | Topología revisada y medidas eléctricas/mecánicas | Uso portátil; no entrega de banco |

Cada pendiente se resuelve en la baseline de su etapa, sin abrir un proceso de aprobación adicional para decisiones rutinarias. No convertir las incertidumbres de I5/I7 en bloqueo artificial de I0.

## Registro mínimo por incremento

Una entrada breve bajo `docs/baselines/` debe indicar: objetivo, commit y board/revisión, archivos o cambios principales, comandos/resultados de software, conexión y condiciones de banco, evidencia física, incidencias y siguiente tarea. Adjuntar solo logs/fotos/capturas útiles. No crear un sistema administrativo adicional.

| Incremento | Estado al revisar estos planes |
| --- | --- |
| I0 | Software implementado y comprobado; CI configurada, ejecución remota no comprobada; I0 físico y escenarios Wokwi abiertos |
| I1 | Diagnóstico implementado y comprobado en software; pruebas de ambas tiras y modo normal en placa pendientes |
| I2 | Diagnóstico GPS con política LED normal verificado en software; recepción/fix, convivencia y pérdida/recuperación físicas pendientes |
| I3 | Pantalla de texto implementada; demo visible confirmada, tiempos de banco registrados; colores/convivencia pendientes |
| I4 | Software y banco LCD sin periféricos registrados; aceptación conjunta con GPS/LEDs/HTTP/persistencia pendiente |
| I5 | Implementado y evolucionado en I6a; comparación USB registrada; óptica y carga conjunta siguen abiertas |
| I6 | I6a/I6c confirmados; I6d implementado con evidencia separada. Producto sin timeout automático; V1 restante/V3 abiertos; I6b/I6e sin iniciar |
| I7 | Identidad/QR priorizados mediante VIS-1a/1b y siguientes; otras extensiones opcionales |

Próximo trabajo: VIS-3 del subplan visual solicitado. Mantener V1 restante y V2 cuando haya periféricos,
seguido de V3. Recuperar Wokwi cuando CLI/token estén disponibles como tarea de
regresión Classic independiente; no acredita el panel Waveshare. Los targets
Display siguen experimentales y el uso portátil permanece sin validar.
