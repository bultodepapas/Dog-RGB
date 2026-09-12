# RGB Dog Display: desarrollo incremental

Estado: **Dirección y orden acordados con el propietario; implementación pendiente**.
Fecha: 2026-09-12.

Este documento gobierna la secuencia de trabajo de la variante Display. Ante diferencias de prioridad con el [plan de investigación Waveshare](2026-09-12_waveshare-display-variant.md) o el [flujo visual con IA](2026-09-12_display-ai-workflow.md), prevalece este orden. Esos documentos conservan valor como referencias técnicas; no convierten sus propuestas avanzadas en requisitos del primer incremento.

## Análisis de los planes anteriores

La separación entre placas, la reutilización del núcleo actual y la distinción entre pruebas de PC y placa son adecuadas. Hay que corregir cuatro problemas de planificación:

1. No había un hito explícito para demostrar ambas tiras en Waveshare antes de la interfaz.
2. GPS quedaba incluido en una integración tardía, sin prueba propia de recepción, fix y recuperación junto a LEDs.
3. El simulador completo y las cuatro vistas podían consumir el primer ciclo de desarrollo sin demostrar un collar funcional.
4. Batería, sensores, navegación, animaciones y herramientas estaban demasiado cerca del alcance inicial. Su presencia en la placa no obliga a implementarlos ahora.

La primera entrega útil será **LEDs + GPS + una página de texto con datos reales**, conservando la versión Classic. La calidad visual crecerá sobre esa base.

## Reglas de ejecución

- Un solo incremento funcional activo. Cada cambio debe responder al objetivo de ese incremento.
- Reutilizar drivers y lógica del firmware actual; portar no significa reescribir GPS, escenas, métricas o persistencia.
- Un repositorio, perfiles por placa y ramas cortas. Las mejoras comunes llegan a ambas variantes; Display agrega solo sus diferencias.
- Cada entrega registra alcance, evidencia y pendientes. Estados: pendiente, en desarrollo, verificado en software, verificado en placa. Compilar no equivale a funcionar físicamente.
- Cerrar una etapa requiere sus pruebas aplicables. Si falta hardware, avanzar en trabajo independiente y dejar la aceptación física abierta; no sustituirla por una afirmación basada en simulación.
- Reversión sencilla por commit/target y binario identificado por placa. No cambiar particiones, core o protocolos salvo necesidad demostrada del incremento.
- Las comprobaciones rutinarias no requieren una nueva autorización. Revisar con el propietario decisiones que cambien el alcance, no cada compilación.
- Los tiempos de banco siguientes son criterios iniciales de ingeniería, no certificación ni garantía de autonomía.

## I0 — Base reproducible y perfil de placa

**Resultado:** poder compilar Classic y un target Waveshare mínimo sin ambigüedad de pines.

Tareas: registrar commit y estado de trabajo; ejecutar build y suite host existentes; identificar PCB/revisión y demo; extraer únicamente pines/capacidades necesarios; definir consola USB, flash/PSRAM y control de alimentación. No incorporar aún LVGL, IMU, RTC o buzzer. Tratar explícitamente la ausencia del LED de heartbeat externo.

La identificación física es requisito para cablear la nueva placa, pero la revisión y baseline de Classic pueden realizarse antes. Empezar en banco con alimentación conocida por USB/fuente y rama de LEDs dimensionada. La batería y carga conjunta tienen validación separada; el circuito de alimentación que la placa necesita para arrancar sí pertenece a I0.

Aceptación: comandos y resultados de Classic registrados; target Waveshare compilable; en placa, consola y arranque repetibles durante cinco reinicios y diez minutos, sin resets inesperados. Perfil documentado con pines confirmados y pendientes señalados. Verificar Wokwi cuando cambien sus rutas o configuración compartida.

## I1 — LEDs en Waveshare

**Resultado:** controlar las dos tiras desde la nueva placa.

Tareas: adaptar el bus LED existente al perfil; comenzar con un píxel y después la longitud configurada de cada tira, a brillo de banco reducido. Ejercitar rojo, verde, azul, blanco dedicado y apagado; comprobar tira A/B por separado. Integrar un modo simple existente y mantener el limitador actual. LCD apagada; diagnóstico por consola.

Aceptación física: orden RGBW correcto, ambas salidas independientes, apagado controlado y funcionamiento de quince minutos con cambios periódicos sin parpadeos espurios ni resets. Registrar alimentación, número de píxeles, brillo y corriente medida si se dispone del instrumento. No atribuir una corriente medida al limitador estimado. Ejecutar las pruebas LED existentes afectadas y build Classic.

No entra: nuevos efectos, editor de escenas, interfaz LCD o calibración completa de autonomía.

## I2 — GPS junto con LEDs

**Resultado:** recibir GNSS y mantener LEDs operativos simultáneamente.

Tareas: conectar UART al perfil, conservar parser y filtros actuales, verificar recepción NMEA y distinguir recepción de fix válido. Exponer por consola estado, satélites/calidad, velocidad, distancia y antigüedad. Probar un comportamiento LED ya existente alimentado por GPS, sin modificar sus reglas para facilitar la demostración.

Aceptación física: NMEA válido; adquisición de fix en una prueba exterior con condiciones registradas, sin prometer un tiempo máximo universal; quince minutos de GPS+LEDs; una pérdida controlada de datos y recuperación. Los datos caducados deben identificarse como tales. Registrar contadores/diagnósticos disponibles y resets. Ejecutar pruebas GPS afectadas y build Classic.

No entra: IMU, fusión de sensores, nuevas métricas o mapas.

## I3 — Pantalla sencilla con datos reales

**Resultado:** una página de lectura que sirve para usar y diagnosticar el collar.

Contenido inicial: estado GPS, velocidad, distancia y modo LED. Mostrar `--` o aviso explícito cuando el dato no sea válido. Sin menú, animaciones, porcentaje de batería o múltiples páginas. Encabezado, cuatro líneas legibles y colores simples son suficientes.

Tareas: validar primero barras/borde/orientación; usar driver mínimo compatible con el core fijado; encapsular `begin/update` en un servicio Display y tomar un snapshot pequeño del dominio. Actualizar valores cambiados a una cadencia máxima inicial de 1 Hz, sin esperas largas. Apagar/encender backlight por diagnóstico para verificar independencia del núcleo.

**Elección deliberada:** Arduino_GFX u otro driver mínimo ya validado puede dibujar texto directamente. LVGL no es requisito de I3. Si introducir LVGL añade trabajo de integración, se pospone a I5; conservar servicio y modelo para reutilizarlos. No construir el simulador avanzado como prerrequisito.

Aceptación física: texto completo y colores correctos; datos coinciden con el dominio/consola para la misma muestra y período; búsqueda, fix y dato caducado distinguibles; quince minutos con GPS+LEDs+LCD; pantalla apagada o servicio deshabilitado no detienen tracking ni LEDs. Una inicialización fallida de LCD tiene salida acotada. Registrar latencia de loop con y sin display.

## I4 — Consolidación del collar básico

**Resultado:** base utilizable que merece convertirse en referencia para la evolución visual.

Tareas: comprobar comportamiento existente del portal con los tres periféricos, cambios de modo/configuración, persistencia y reinicios. El portal se conserva durante el port; aquí se prueba expresamente bajo carga conjunta. Hacer una sesión de banco de al menos treinta minutos y una prueba exterior breve documentada. Recuperar fallos encontrados antes de ampliar UI.

Aceptación: GPS, ambas tiras, LCD y portal conviven sin fallos observados durante la prueba definida; cambios esperados sobreviven al reinicio; sin degradación de memoria progresiva observada; builds y suites relevantes de ambas variantes registrados. Informar límites de la observación, no declarar ausencia universal de fallos.

Si se va a utilizar batería, validar antes polaridad, camino único de carga, corriente/capacidad de los módulos, USB+batería y apagado. Si no está listo, la entrega puede cerrarse como **base de banco**, manteniendo uso portátil pendiente. No realizar prueba sobre el perro con un montaje de banco sin validar fijación y alimentación.

## I5 — Mejorar una vista con IA y LVGL

**Resultado:** sustituir la página básica por Paseo bien diseñada, manteniendo iguales los datos y funciones.

Aplicar gradualmente el [flujo visual](2026-09-12_display-ai-workflow.md): elegir versión LVGL probada; una vista compartida PC/placa; tres escenarios iniciales (sin fix, fix, dato caducado); capturas reproducibles estáticas y revisión. Añadir primero herramientas mínimas para esos escenarios. El runner temporal completo, generador de tema y reportes elaborados solo se incorporan cuando resuelvan una necesidad real.

Aceptación: mejora visual comprobable en capturas y placa, paridad de datos con I4, sin regresión funcional. Mantener temporalmente la pantalla básica como opción de diagnóstico si facilita comparar. Medir memoria y tiempo de actualización antes/después.

## I6 — Navegación y movimiento

**Resultado:** agregar una segunda vista y navegación por botón; después una transición breve.

Tareas secuenciales: botón/despertar compatible con circuito de alimentación; segunda vista útil; pruebas de pulsaciones repetidas; transición; capturas temporales y medición en placa. Optimizar SPI/buffers solo ante evidencia de un cuello de botella. No imponer 20/30 FPS a la pantalla estática de I3.

Aceptación: eventos coherentes, sin acciones involuntarias al despertar, animación sin afectar GPS/LEDs/portal, latencia y consumo documentados. Añadir otras vistas una por incremento.

## I7 — Extensiones opcionales

Batería calibrada, RTC, IMU, buzzer, más vistas, ahorro avanzado y refinamiento mecánico se priorizan individualmente. Nube, OTA y endurecimiento avanzado siguen fuera de esta iniciativa inicial. Cada extensión necesita utilidad concreta, alcance pequeño y aceptación propia.

## Registro mínimo por incremento

Una entrada breve bajo `docs/baselines/` debe indicar: objetivo, commit y board/revisión, archivos o cambios principales, comandos/resultados de software, conexión y condiciones de banco, evidencia física, incidencias y siguiente tarea. Adjuntar solo logs/fotos/capturas útiles. No crear un sistema administrativo adicional.

| Incremento | Estado al revisar estos planes |
| --- | --- |
| I0 | Pendiente; aún no se ejecuta baseline en esta revisión |
| I1–I4 | Pendientes; primera entrega funcional objetivo |
| I5–I6 | Planificados para después de la base funcional |
| I7 | Opcional, sin priorización de implementación |

Próximo trabajo concreto: ejecutar I0 y preparar la adaptación mínima del bus LED para I1. El orden aceptado no implica que ya exista firmware Waveshare ni validación física. Esta revisión modifica documentación únicamente.
