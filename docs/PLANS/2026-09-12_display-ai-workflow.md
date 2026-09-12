# Flujo de diseño de interfaces con IA para RGB Dog Display

Estado: **Proposed**, 2026-09-12. Especificación de trabajo; simulador, comandos y componentes todavía no implementados. Complementa el [plan Waveshare](2026-09-12_waveshare-display-variant.md).

**Prioridad revisada:** este flujo se aplica desde I5 del [plan incremental acordado](2026-09-12_display-incremental-delivery.md), después de LEDs, GPS, pantalla de texto y consolidación. Sus herramientas se incorporan por necesidad; no son prerrequisitos del collar básico. En I5 comenzar con una vista estática y tres escenarios; navegación, capturas temporales y animaciones pertenecen a I6.

## Resultado que buscamos

Un ciclo reproducible que permita describir una mejora, implementarla en LVGL, verla con datos representativos, corregirla y llevar el mismo código al ESP32. La primera entrega visual, I5, será Paseo estática con estados GNSS. La navegación mediante botón se agrega en I6. Classic sigue sin depender de LVGL.

Decisión de base: C/C++ LVGL y simulador propio, sin dependencia obligatoria de un editor de pago. Mantener LVGL 8.4.0 como candidato de reproducción del demo Waveshare; fijar definitivamente la versión tras un smoke test de compilación/placa, antes de multiplicar componentes. Si se selecciona v9, migrar deliberadamente ambos backends y sus referencias API.

## 1. Referencia visual: convertir gusto en requisitos verificables

Primero preparar dos o tres propuestas de una sola vista, con los mismos datos. Evaluar jerarquía, contraste, identidad y lectura a 240 × 280. Las imágenes conceptuales, incluidas las generadas por IA, sirven como dirección artística: su tipografía y efectos deben traducirse a componentes ejecutables.

El brief debe contener resolución, esquinas, uso sin táctil, información principal/secundaria, estados, textos, colores, fuentes, espacios y movimiento. La dirección inicial es instrumento deportivo: velocidad grande, distancia y tiempo secundarios, estado GNSS discreto pero inequívoco. No decidir una estética solo con una imagen de datos ideales.

Entregables propuestos:

- `docs/display/brief.md`: objetivos de uso y restricciones.
- `docs/display/design-decisions.md`: dirección elegida, alternativas descartadas y motivo cuando afecte al desarrollo.
- `design/display/references/`: referencias con origen y permiso de reutilización cuando aplique.
- `design/display/theme.json`: colores, tamaños, espaciados y duraciones; fuente de un header generado, sin editar ambos manualmente.

Contratos iniciales: márgenes de 16 px por ajustar a la placa; velocidad 40–48 px, secundarios 20–24 px y etiquetas 16 px. Son valores de partida. Usar números de ancho estable, incluir glifos españoles y dimensionar con la cadena máxima esperada. Determinar qué se abrevia y qué pasa a otra línea.

La aceptación estética puede expresarse con comentarios sobre una captura: “conserva composición, aumenta la distancia y reduce el encabezado”. No requiere aprobar cada compilación; una vez elegida la dirección, Codex itera dentro de ella y documenta cambios relevantes.

## 2. Componentes: separar datos, comportamiento y dibujo

La UI comparte código fuente, fuentes y recursos entre PC y ESP32. Solo cambian reloj, entradas, transporte de píxeles y alimentación. No implementar una réplica HTML como prueba de aceptación LVGL.

```mermaid
flowchart LR
    FIX[Escenarios y reloj virtual] --> MODEL[Modelo de presentación]
    GPS[Datos reales del collar] --> MODEL
    MODEL --> UI[Componentes LVGL compartidos]
    UI --> HOST[Framebuffer PC y ventana SDL]
    UI --> DEVICE[Driver SPI y LCD]
```

Componentes iniciales: `StatusBar`, `PrimaryMetric`, `MetricPair`, `PageIndicator` y `GpsStateNotice`. Una sola pantalla coordinadora, `WalkScreen`, crea y actualiza esos objetos. Evitar un sistema genérico de plugins o widgets antes de necesitarlo.

Contratos propuestos, sin compromiso todavía sobre nombres de API:

- `DisplaySnapshot`: copia acotada de valores, unidades, validez y antigüedad; incluye secuencia/tiempo de muestra cuando sea necesario.
- `UiEvent`: pulsación corta, solicitud de despertar y cambios de disponibilidad; la interpretación eléctrica del botón queda en el backend.
- `UiController`: página seleccionada, transición activa y política ante pulsaciones repetidas. No es dueño del GPS ni de NVS.
- `UiView`: presenta el modelo y actualiza solo valores visuales modificados.

Los ejemplos JSON de simulación se convierten a las mismas estructuras que los adaptadores de dominio. No exigen añadir un parser de fixtures al firmware. Un dato inválido se representa explícitamente: `--` y “Buscando GPS”, no cero ni el último valor sin identificación. Caducidad deriva de tiempo de muestra y reloj actual; no de contar frames.

Una transición interrumpida termina en un estado coherente. Las alertas se presentan con prioridad. Despertar no avanza también la página por accidente. Para navegación simple, las pulsaciones repetidas pueden sustituir la transición hacia la página destino más reciente; esa regla se debe probar.

## 3. Simulador: dos modos con la misma UI

**Interactivo:** ventana SDL a resolución lógica 240 × 280, zoom entero 1×/2×/3× sin alterar layout, botón mediante teclado, selector de escenarios y métricas de desarrollo fuera del área del panel. En un monitor, 1× significa correspondencia de píxeles, no tamaño físico de 1,69 pulgadas; la legibilidad física se confirma en dispositivo.

**Reproducible:** backend de memoria sin ventana, tiempo virtual, eventos fechados y salida PNG. Avanza en pasos fijos, inyecta eventos en orden definido, ejecuta temporizadores y completa el refresco antes de capturar. No usa el reloj de Windows, Wi-Fi real ni números aleatorios sin semilla.

LVGL necesita una fuente de tiempo y ejecución periódica de su handler. El simulador controlará ese tiempo; en placa se alimentará del tiempo monotónico real. No incrementar ficticiamente siempre 5 ms si el loop real tarda más. [Tick v8.4](https://lvgl.io/docs/open/8.4/porting/tick.html), [Timer handler](https://lvgl.io/docs/open/8.4/porting/timer-handler.html).

Propuesta de carpetas adicionales:

```text
tools/display-simulator/
  CMakeLists.txt
  host_main.cpp
  framebuffer_backend.cpp
  scenario_runner.cpp
tests/display/scenarios/
tests/display/baselines/
output/display/<run-id>/
```

El ejecutable host enlaza las fuentes `src/display/ui/` del firmware; no las copia. Compartir configuración visual relevante de LVGL, RGB565, fuentes y assets. Las diferencias de plataforma se mantienen explícitas y pequeñas.

## 4. Capturas: obtener evidencia de lo que realmente dibujamos

El backend copiará cada región enviada por LVGL a un framebuffer completo RGB565 de 240 × 280. Al finalizar un refresco, convertirá sus píxeles a RGB888 y codificará PNG. Debe respetar offsets, límites, stride y orden de bytes. En PC puede guardar muchas imágenes sin cargar de trabajo al collar.

LVGL también ofrece snapshots de objetos. Son útiles para componentes aislados; no prueban por sí solos el montaje de regiones en el backend ni equivalen automáticamente a un archivo PNG. [Snapshot v8.4](https://lvgl.io/docs/open/8.4/others/snapshot.html).

Por escenario producir:

- PNG nativo y ampliación sin suavizado.
- Hoja de contacto de momentos significativos de animación.
- Imagen de diferencia contra baseline y reporte de geometría/estados.
- `manifest.json`: commit, versión LVGL, hash de tema/fuentes, escenario, instante y configuración relevante.

Una máscara de esquinas será una previsualización complementaria basada en medición; guardar también el frame completo. No esconder recortes con la máscara. Para automatización visual estable, fijar entorno de compilación y renderizado; una diferencia de antialiasing no debe confundirse con un cambio semántico.

La documentación de OpenAI permite usar imágenes como contexto y comparar estados con Codex. Nuestra propuesta es entregarle capturas más brief y cambios esperados; la imagen sola no define el comportamiento correcto. [Entradas de imagen](https://learn.chatgpt.com/docs/image-inputs).

## 5. Correcciones: cerrar el ciclo sin perder el diseño

Codex revisa primero errores de contenido, luego recortes/alineación, después jerarquía visual y finalmente movimiento. Cada observación debe indicar escenario, zona, problema y corrección concreta. Ejemplo: “En `speed-width`, a 1.000 ms, `10.0` desplaza la unidad; reservar una columna fija para unidad”.

Aplicar una familia de cambios por iteración: espaciado, tipografía o transición. Recompilar y capturar los escenarios afectados; correr la matriz completa antes de consolidar. Las nuevas capturas van a `actual/`; las baselines no se sobrescriben para silenciar diferencias. Actualizarlas en un cambio explícito después de revisar que representan el resultado deseado.

La comparación de píxeles detecta cambios, no decide calidad. Acompañarla de verificaciones sobre texto esperado, visibilidad, bounds y estado del controlador. Evitar reglas rígidas que rechacen solapamientos deliberados; identificar regiones de layout y overlays permitidos.

Matriz inicial:

| Escenario | Qué comprueba |
| --- | --- |
| Arranque y búsqueda GNSS | Sin métricas inventadas, aviso legible |
| Primera muestra válida | Cambio de estado sin salto de composición |
| Velocidad 9,9 → 10,0 | Ancho estable y unidad alineada |
| GNSS caducado | El valor anterior no aparenta estar actualizado |
| Batería desconocida/baja | Semántica explícita, sin porcentaje inventado |
| Nombre largo y español | Límites, abreviación y glifos |
| Botón repetido durante transición | Destino coherente, sin cola de animaciones |
| Apagar/despertar | No avanza página involuntariamente ni muestra frame incompleto |

Ejemplo temporal propuesto: estado inicial a 0 ms, muestra GPS a 1.000 ms, botón a 2.000 ms; capturas a 2.000, 2.050, 2.100 y 2.200 ms. Añadir una segunda pulsación a 2.075 ms en otro escenario. La caducidad GNSS se toma del contrato del producto, no de estos instantes ilustrativos.

Las secuencias virtuales demuestran la forma prevista del movimiento. Los FPS del PC o de un vídeo exportado no son mediciones del ESP32.

## 6. Prueba en placa: trasladar diseño y medir la ejecución

Primero ejecutar la misma vista con fixtures locales de diagnóstico y sin periféricos externos; después conectar los snapshots reales. Los fixtures de placa quedan solo en el target de bring-up. Verificar colores, orientación, bordes y glifos antes de atribuir diferencias a la UI.

Comparar fotografía de LCD con captura host para encontrar discrepancias de color/recorte; no exigir igualdad de píxeles a una foto, afectada por cámara, exposición y backlight. Medir pulsación→primer cambio visible y regularidad de frames con vídeo o instrumentación adecuada. El final de DMA es fin del envío, no necesariamente el instante exacto en que el usuario ve el píxel.

Aceptar provisionalmente una transición de 200 ms con objetivo de 20 FPS estables y respuesta p95 menor de 100 ms, sujeto a medición. Registrar periodos entre actualizaciones, tiempo SPI, duración UI, heap mínimo y bloqueo del loop bajo carga. Comprobar GNSS continuo, ambas tiras y portal activo. No usar promedio FPS como único criterio: puede ocultar pausas largas.

Repetir cambio de páginas/apagado durante un periodo definido, buscando crecimiento de memoria y fallos. Medir consumo con backlight apagado y en varios niveles; documentar condiciones. Reducir área animada, assets o cadencia si el resultado compromete tareas del collar.

## Evolución completa del flujo visual (I5–I6)

1. Brief y tema inicial de Paseo, con referencia identificable.
2. Smoke test de versión LVGL y backend mínimo de placa cuando esté disponible.
3. `WalkScreen` compartida y simulador interactivo/headless.
4. Ocho escenarios anteriores, capturas temporales y manifest.
5. Revisión visual con correcciones focalizadas y baseline explícita.
6. Compilación Classic y Display; informe separado de validación física pendiente o realizada.

Esta lista es el destino del flujo, no una tarea indivisible: I5 toma una vista y tres escenarios estáticos; I6 añade eventos, escenarios restantes aplicables y secuencias temporales. Generadores, manifest detallado y reportes avanzados son mejoras de herramientas cuando se justifiquen. No ampliar a todas las vistas hasta poder repetir el ciclo básico desde un checkout limpio.

Encargo sugerido para la futura implementación:

```text
Aplica este encargo solo al llegar a I5 del plan incremental.
Empieza por WalkScreen estática y conserva Classic sin dependencia LVGL.
Comparte componentes entre firmware y simulador con tres escenarios:
sin fix, fix válido y dato caducado. Genera capturas reales de LVGL.
Verifica estados, recortes y unidades. Deja navegación y animaciones para I6.
Entrega diferencias visuales, builds y limitaciones físicas explícitas.
No declares rendimiento del ESP32 a partir de mediciones del PC.
```

Documentación completada en esta iteración. No se generaron referencias gráficas, componentes ejecutables ni baselines reales todavía.
