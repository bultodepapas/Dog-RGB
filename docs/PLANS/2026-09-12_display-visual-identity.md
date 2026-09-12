# RGB Dog Display: identidad, QR y evolución visual

**Estado: VIS-1a/1b y VIS-2 implementados y verificados en software; sigue VIS-3 (cuarta página y aceptación física). Sin nueva carga a placa.**
Fecha: 2026-09-12. Base: I6d, con lectura y ciclo físico de tres páginas confirmados.
Prioridad solicitada: diseño visual, nombre del perro y placa digital de contacto;
QR de WhatsApp incorporado a la primera familia de prototipos.

Evidencia actual: [baseline VIS-1a](../baselines/display-vis1a-2026-09-12.md).
Identidad, fuentes y layouts A/B: [baseline VIS-1b](../baselines/display-vis1b-2026-09-12.md).
Persistencia/editor y contrato: [baseline VIS-2](../baselines/display-vis2-2026-09-12.md) /
[API de identidad](../display-identity-api.md).
`ContactQr` ya comparte encoder/render entre firmware y PC. El prototipo local
usa el nombre/contacto facilitados por el propietario; no son valores por defecto.

Depende del [plan incremental](2026-09-12_display-incremental-delivery.md) para
integración/aceptación y amplía el [contrato de pantallas](2026-09-12_display-use-and-screens.md).
Fuentes y comparación: [investigación visual](../display-visual-identity-research.md).
Ampliación técnica: [28 investigaciones GitHub/MCP y ensayo QR](../display-github-research-2026-09-12.md).
Composición propuesta: [tablero visual](../assets/display-visual-identity/concepts.svg).

## Decisión de producto

**El perro es la identidad principal.** Nombre, QR y teléfono forman una placa
útil para quien lo encuentre; Actividad, Wi-Fi y Estado sirven al propietario.
`RGB DOG` pasa a firma secundaria, sin quitar protagonismo al nombre ni al contacto.

La primera entrega visual debe resolver estas consultas:

| Momento | Información | Regla |
| --- | --- | --- |
| Al colocarlo | Nombre, contacto configurado y acceso a Actividad | No esperar GPS para identificar al perro |
| Al encontrarlo solo | Nombre + QR WhatsApp + teléfono visible | Sin app del collar, cuenta cloud ni conexión al AP |
| Durante una parada | Distancia registrada, fecha, velocidad válida | Nada se desliza mientras se lee |
| Al abrir el portal | AP/STA, SSID e IP actuales | Sin escaneo/reconexión provocados por la pantalla |
| Ante un problema | GPS y política LED efectiva | Mantener los significados comprobados de I6d |

La placa muestra identificación normal aunque no exista un «modo perdido».
Mensaje posible: «Si estoy solo, llama a mi familia». GPS ausente, Wi-Fi caído
o salida de zona no activan automáticamente una afirmación de pérdida.

## Inspección de la base

Inspección local sobre HEAD `adc3a91bf39cc8ef1e05ad71e6dc989a9fd765ef` y archivos
presentes. Se revisaron capturas I6d, configuración, fuentes, botón, port y runner.

| Pieza actual | Hallazgo | Trabajo necesario |
| --- | --- | --- |
| `src/display/ui/{walk,connection,status}_view.cpp` | Tres vistas, negro y rectángulo de contenido 192×244 en (24,20) | Mantener geometría compartida; añadir `IdentityView` |
| `include/lv_conf.h` | Montserrat 12/14/20/48; widgets gráficos extra desactivados | Añadir solo los tamaños/glifos y dependencias QR necesarios |
| `src/display/lvgl_port.cpp` | Contenedores creados una vez, invalidación parcial, retorno de diagnóstico en dos fases | Ciclo de cuatro páginas; medir coste del cuarto contenedor y QR |
| `include/display/button.h` | Debounce 30 ms; liberación corta <1.500 ms; largas descartadas | Conservar corto/despertar; un atajo largo requeriría contrato separado |
| `include/config/runtime_config.h` y `ConfigRecord` | No existen nombre, teléfono ni canal de contacto | Persistencia de identidad acotada, sin mezclarla con métricas GPS |
| `src/web/portal_http.cpp`, `webui/src/pages/config.html` | API/configuración local y guardas de escritura existentes | Editor pequeño de identidad y vista previa; reutilizar reglas de acceso |
| `tools/display-simulator/` | Cinco CTest y catorce capturas reales; sin captura temporal de animaciones | Fixtures de identidad/QR y reloj virtual para movimiento |

**Límites preservados:** LVGL 8.4.0, Arduino_GFX 1.6.7, 40 MHz, RGB565, buffer
9.600 bytes, pool 48 KiB. Classic sigue sin gráficos. Sin GPS/tiras conectados,
pueden prepararse y ensayarse incrementos visuales; V2/V3 continúan pendientes.

## Placa digital y QR

### Contenido recomendado

**Composición A, principal:** nombre arriba, QR centrado, teléfono debajo.
Una etiqueta breve indica `WhatsApp` o `Llamar`, según lo que realmente codifica.
Nombre y teléfono no se desplazan ni parpadean. El teléfono permanece visible
incluso si falla el QR. El QR nunca lleva logo superpuesto, transparencia ni
colores invertidos.

**Composición B, alternativa de legibilidad:** nombre y teléfono dominantes,
sin QR. Se usa si el propietario desactiva QR, hay error de generación o el
contenido no cabe al tamaño mínimo aceptado. Para nombres extremos evaluar una
subvista de contacto explícita antes que recortar el nombre o los dígitos.
No añadir una quinta página por defecto solo para decoración.

La placa de 240×280 no puede alojar simultáneamente un nombre de dos líneas,
QR grande, teléfono grande y varias instrucciones. Por eso se comparan layouts
reales con datos largos antes de prometer una composición universal.

### Enlace y número

Canal inicial recomendado: **WhatsApp**. Alternativa configurable: llamada.

```text
Número almacenado: +<país><número nacional>
QR WhatsApp:       https://wa.me/<solo dígitos internacionales>
QR llamada:        tel:+<país><número nacional>
```

WhatsApp documenta el enlace con el número internacional. El QR abre la acción
de contacto; no envía mensajes automáticamente ni demuestra que la cuenta exista.
No se necesita WhatsApp Business API, token, backend ni generador QR externo.
[Ayuda oficial de WhatsApp](https://faq.whatsapp.com/5913398998672934).

Reglas propuestas de implementación:

- Una fuente canónica para teléfono visible, QR y vista previa. Sin segunda
  copia editable que pueda divergir. No inferir el prefijo por el idioma o GPS.
- Campo internacional explícito. Normalizar separadores de presentación; no
  borrar ceros interiores ni «corregir» números locales adivinando su país.
  Rechazar letras/extensiones en esta primera versión.
- Al generar `wa.me`, quitar el `+` y los separadores; conservar los dígitos.
  El límite de almacenamiento propuesto es 15 dígitos más `+` y terminador;
  es un límite del producto, no validación exhaustiva del plan telefónico mundial.
  VIS-1a fija sintaxis de 7–15 dígitos, primer dígito distinto de cero, `+`
  explícito y entrada menor de 64 bytes; acepta espacios, guiones y paréntesis.
- No añadir `?text=` inicialmente: aumenta densidad y longitud del QR. Un
  mensaje prellenado como «Encontré a tu perro» puede ensayarse después con
  escape URL correcto; abrirlo nunca envía el mensaje por sí solo.
- Generación y lectura visual offline. Abrir `wa.me` depende de conectividad y
  del teléfono del visitante; la llamada requiere servicio telefónico. Mostrar
  el número permite continuar si falla la cámara, el navegador o WhatsApp.
- Contacto público por diseño de la placa; el portal explicará qué se verá.
  No mostrar domicilio, ubicación del dueño ni datos adicionales por defecto.

### Geometría del QR

Probar QR negro sobre un cuadrado blanco, módulos enteros, quiet zone de cuatro
módulos en cada lado. Objetivo inicial **4 píxeles por módulo**; no aceptar un
tamaño físico mínimo hasta escanear el panel real. El tamaño total es
`(N + 8) * escala`: 25 módulos → 132 px, 29 → 148 px a escala 4.
[DENSO](https://www.qrcode.com/en/howto/code.html).

El encoder real determina N; la implementación 8.4 puede elegir/extender versión
según el tamaño. Comprobar matriz final y margen, no deducir capacidad solo de
la longitud del ejemplo. El wrapper nativo no ofrece recuperación completa de
sus asignaciones temporales. Ver [R01–R04 y ensayo host](../display-github-research-2026-09-12.md).
[Código oficial v8.4.0](https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/src/extra/libs/qrcode/lv_qrcode.c).

**Decisión VIS-1a:** reutilizar el encoder Nayuki incluido, con buffers acotados,
y un `ContactQr` que controle el canvas indexado y el margen. Para el contrato
actual, ensayar versiones 1–3, ECC M con aumento dentro de la misma versión,
escala 4 y matriz máxima 29×29. Separar interior de hasta 116×116 y blanco
externo de 16 px por lado; obtener N del resultado. No pasar 132/148 como tamaño
de matriz. Mantener buffers/descriptores vivos y precrear el fallback textual;
no modificar archivos de `.pio/libdeps` ni regenerar por tick. El ensayo del
encoder quedó ampliado por los renders/decodificación reales de VIS-1a/1b;
el escaneo físico sigue pendiente.

Error de datos/capacidad se resuelve mostrando texto; la asignación global de
objetos LVGL conserva sus assertions. Evitar prometer recuperación general de
OOM por comprobar únicamente el retorno del encoder. Reservar recursos una vez,
medir su máximo y comprobar errores recuperables del componente sin agotamiento
artificial de todo el sistema.

Nombre corto: probar 28–32 px arriba, QR de hasta 148 px en el centro y teléfono
18–20 px debajo, dentro del área segura actual. Con nombre largo, eliminar
instrucción secundaria, medir dos líneas y recalcular; si no cabe, composición B.
El layout elegido debe mostrar todos los dígitos a la vez. Un QR no cuenta como
aceptado por verse correcto en una captura: hay que decodificar el payload exacto
y comprobar apertura con teléfonos reales.

## Navegación y descubrimiento

Ciclo propuesto al configurar identidad:

`Placa → Actividad → Wi-Fi → Estado → Placa`

- Arranque: Placa si hay identidad válida; de lo contrario Actividad, conservando
  el comportamiento útil actual. No mostrar el nombre ficticio en producto.
- Clic corto con pantalla encendida: página siguiente.
- Primera pulsación en oscuro: despierta **la misma página**, como ya se validó.
  No convertir silenciosamente el despertar en salto a Identificación.
- Sin cambio automático de página ni animación mientras alguien lee/escanea.
  En el ciclo base, Placa queda a un máximo de tres clics desde otra página
  encendida; en oscuro se añade el clic de despertar.
- Acceso rápido opcional posterior: liberación larga de aproximadamente 2 s
  abre Placa cuando está encendida. Debe consumir el evento sin clic corto
  adicional; en oscuro solo despierta. No usar PWR ni cambiar arranque ROM.
- Evaluar una marca en la carcasa que identifique el botón de pantalla. Una
  persona que encuentra al perro no conoce la palabra BOOT ni un gesto oculto.

I6c sigue desactivado al arrancar. El tiempo de lectura/escaneo merece ensayo
propio (30 s frente a una ventana mayor), pero este subplan no cambia la política
automática del producto ni supone ahorro medido. No crear «always-on» por dibujar
un fondo negro: este IPS necesita backlight. El comportamiento con batería y
pantalla apagada forma parte de la aceptación portátil pendiente.

## Sistema visual y widgets

Tesis visual: placa negra y sobria con nombre expresivo, alto contraste y un
acento contenido. La personalidad aparece en la tipografía y una huella pequeña,
no detrás del teléfono o del QR. Una familia tipográfica, cifras estables y
espaciado base de 4 px; se conservan márgenes 24/20 hasta comprobar el montaje.

| Componente | Presentación y utilidad | Fuente de verdad / condición |
| --- | --- | --- |
| `PetHeader` | Nombre corto en vistas del dueño; firma RGB DOG discreta | Identidad validada; fallback RGB DOG |
| `ContactBlock` | Número completo y canal; composición A/B | Un solo teléfono canónico |
| `ContactQr` | Cuadro blanco quieto con margen; generación solo al editar | Enlace derivado, resultado del encoder comprobado |
| `MetricValue` | Distancia y velocidad sin saltos de ancho; unidad clara | Valores/validez de Actividad; no animar cifras falsas |
| `StatusLabel` | Texto + icono opcional, color redundante | GPS/LED/Wi-Fi existentes; nunca solo un punto de color |
| `PageIndicator` | Cuatro posiciones discretas, alto contraste | Página seleccionada; sin apariencia de botón táctil |
| `ConnectionBlock` | AP/STA y dirección agrupados | Adaptador actual; no indicar Internet solo por STA |
| `LightModeBadge` | Modo día, efectos o salida pausada | Política efectiva; sin representar LED físico medido |
| `PetMark` | Huella propia de 24–32 px; sprite breve opcional | Identidad decorativa; no implica pasos, sueño ni salud |
| `TrendLine`, después | Tendencia corta de velocidad con huecos visibles | Requiere muestras tipadas/timestamps; no fabricar una gráfica a partir del último valor |
| Batería, reloj, progreso, después | Solo si resuelven una consulta concreta | Telemetría calibrada/hora válida/meta explícita; no porcentajes decorativos |

Tokens iniciales: fondo `#000000`, texto `#FFFFFF`, secundario `#B8B8B8`,
divisor `#303030`, válido `#50EFAB`, atención `#FFB547`. Elegir un acento de
identidad neutro o verde suave que no convierta toda la página en una señal GPS.
VIS-1b seleccionó Montserrat 600, nombre 28 px y teléfono 18 px, ambos 4 bpp;
secundarios 12/14. La receta fijada y comparación 2/4 bpp están en
[display-fonts](../../tools/display-fonts/README.md). Los acentos españoles se
verifican contra los glifos reales. A muestra un nombre de una línea y QR;
B centra hasta cinco líneas de nombre sobre el teléfono completo, sin QR.

El editor mostrará error antes de guardar un nombre que no se pueda representar;
no sustituir letras silenciosamente. Propuesta inicial: UTF-8, máximo 48 bytes
y 24 puntos de código admitidos después de normalización NFC, conjunto latino
explícito; además prueba de ancho con la fuente final. El portal normaliza y el
firmware valida por separado; una escritura directa no canónica obtiene error
explícito inicialmente. No confundir puntos de código con grafemas ni cargar un
motor Unicode completo. Probar `Niño`, `René`, formas descompuestas, nombres
compuestos y mayúsculas anchas. Ver R05–R08 de la investigación ampliada.

Receta de assets: fuente/SVG original → conversor y versión fijados → C para
LVGL 8.4 → captura real. Comparar 2/4 bpp de fuente; un tamaño principal y 6–8
iconos como máximo en el primer pulido. Registrar bytes, glifos, licencia y hash.
resvg es candidato host; no hay decoder SVG/PNG nuevo en placa por esta decisión.
Los colores/alfa se aceptan tras la conversión RGB565, no solo en el preview.

## Efectos y transiciones: selección acotada

| Prioridad | Efecto | Parámetros de prototipo | Condición de aceptación |
| --- | --- | --- | --- |
| 1 | Indicador de página que acompaña al clic | 120–160 ms, región pequeña, ease-out | Texto cambia sin demora; no acumula eventos ni repinta toda la pantalla |
| 2 | Entrada de título/dato al navegar | Desplazamiento 8–12 px, 160–200 ms | Sin mover QR/teléfono ni invadir margen; comparar contra cambio instantáneo |
| 3 | Confirmación breve de guardado en portal/placa | Texto/icono 600–1.000 ms, sin tapar contacto | Solo después de persistencia correcta |
| 4 | Huella/mascota de bienvenida | 4 frames de 32×32, una sola secuencia | No retrasar nombre/contacto; no repetir al actualizar GPS |
| Opcional | Señal discreta de búsqueda GPS | Elemento pequeño, frecuencia baja | No simular porcentaje ni recepción; detener al ocultar/apagar |
| Descartado inicialmente | Carrusel automático, teléfono desplazándose, QR animado, fondos de partículas, GIF/Lottie/vídeo, grandes fundidos | — | Coste/distracción sin utilidad suficiente |

Solo una transición principal a la vez. Una pulsación nueva sustituye el destino
pendiente; no crea una cola ilimitada. Al vencer el timeout, resolver el destino
lógico y mantener backlight apagado. Cancelar callbacks al destruir/ocultar una
vista. El primer despertar dibuja el contenido final antes de encender; no espera
una bienvenida. Los cambios de estado inválido son inmediatos, sin interpolar
una velocidad que el GPS ya no respalda.

Usar `lv_anim_t` y cancelación nativa, no tareas adicionales. Medir antes de
evaluar timelines compuestos. La animación es opcional y el cambio instantáneo
se conserva como alternativa. [LVGL 8.4: animaciones](https://lvgl.io/docs/open/8.4/overview/animation.html).

## Datos, persistencia y desarrollo paralelo

Propuesta: `IdentityConfig` con nombre, teléfono y enum `QrContactKind`
(`Disabled`, `WhatsApp`, `Call`). Sin URL arbitraria, domicilio ni mensaje libre
en la primera versión. `IdentitySnapshot` copia datos acotados y validez; la
vista nunca lee NVS, consulta Internet ni realiza escrituras.

Preferir un registro de identidad pequeño, versionado y separado de la
configuración LED/GPS: aplicar el patrón A/B + CRC ya usado en el proyecto,
sin cambiar particiones ni el formato `ConfigRecord` existente. Identidad ausente
es un estado normal; fallo de escritura conserva el contacto anterior completo.
Un error de QR conserva nombre/teléfono visibles y no destruye el dato guardado.

API propuesta local `GET/POST /api/identity`, documentada antes de implementarla;
sincronizar lectura, validación, guardado y vista previa con el editor de `/config`.
Reutilizar las guardas existentes; PIN avanzado sigue opcional. Mostrar
capacidad de identidad/QR para que Classic no reciba controles gráficos vacíos.
No añadir identidad a BLE, exportaciones GPS o cloud incidentalmente. Si se
habilita perfil del perro en Classic más adelante, comparte el mismo dominio.

La primera puesta en marcha usa fixtures en simulador/diagnóstico. No insertar
el teléfono real en código fuente, capturas versionadas, fixtures o logs de CI.
Configurarlo por el portal al llegar a la entrega persistente. Es una separación
práctica de datos de configuración, sin nuevo servicio ni flujo de aprobación.

## Secuencia de implementación

Estos paquetes detallan I7-identidad y el I6b existente; no crean otra hoja de
ruta paralela. Tamaño S: un cambio acotado; M: varios pasos verificables. No son
estimaciones de calendario ni autorización para cerrar pruebas sin hardware.

| Paquete | Cambio y archivos propuestos | Salida verificable | Tamaño |
| --- | --- | --- | --- |
| **VIS-0, completo** | Investigación, tablero, subplan y enlaces | Fuentes revisadas; límites y prioridad QR explícitos | S |
| **VIS-1a, implementado en host** | Contrato/formatter puro, encoder acotado, `ContactQr` y fixtures | QR y quiet zone comprobados; payload exacto leído por ZXing; errores, estabilidad y memoria host documentados | S–M |
| **VIS-1b, implementado en host** | `identity.h`, `ui/identity_view.cpp`, fuentes y fixtures | 13 capturas A/B; UTF-8/glifos/límites, QR independiente, memoria con las tres vistas I6d; contacto local separado | S–M |
| **VIS-2, implementado en software** | `display/identity_store.*`, API local, editor `/config` y capability | Store/handlers reales con transportes de prueba; NFC, conflictos, errores/reinicio; Classic sin gráficos | M |
| **VIS-3, siguiente** | Adaptador desde store e integración en `lvgl_port`, cuatro páginas y arranque condicionado | Firmware experimental USB con identidad real configurada; BOOT/wake y QR físico aceptados | M |
| **VIS-4** | Tokens/componentes mínimos y pulido de Actividad/Wi-Fi/Estado | Capturas coherentes; información conservada; recursos comparados | S–M |
| **VIS-5 / I6b** | Captura temporal y una transición localizada | Instantes 0/40/80/120/160/200 ms; nueva pulsación/timeout; medición USB; alternativa instantánea | M |
| **VIS-6, opcional** | Una utilidad elegida: atajo de identidad, sprite, ayuda explícita o tendencia | Contrato y fixtures propios; no implementar todas juntas | S–M |
| **Aceptación conjunta** | V2 → V3 del plan principal | GPS, tiras, portal y persistencia reales; montaje/energía según su protocolo | Según hardware |

VIS-1 puede hacerse sin GPS/LEDs y sin alterar todavía el firmware cargado.
VIS-1a y VIS-1b son dos pasos del mismo paquete, no una nueva hoja de ruta.
La investigación ampliada cerró VIS-0. VIS-1a añade renderer real y decoder
independiente; su aceptación host no acredita lectura, stack ni tiempos en placa.
Una transición VIS-5 puede prototiparse/medirse en banco por la prioridad visual
actual; adoptarla en producto requiere la comparación V3. Los siete targets se
compilan si VIS-2 modifica core/portal/perfiles compartidos; un cambio exclusivo
de UI sigue la matriz Display + Classic ya definida. No migrar LVGL por estilo.

## Pruebas de aceptación y presupuestos

**Identidad y QR:** datos vacíos/parciales, nombre largo, tildes/ñ, caracteres no
soportados, teléfono internacional máximo, normalización, fallo NVS, reinicio y
fallo de generación. Decodificar la captura y comparar con el string exacto;
probar QR real con un teléfono Android y un iPhone cuando estén disponibles,
primero cámara y después apertura de la acción. Registrar distancia, luz, ángulo,
tamaño de módulo y 10 intentos por condición elegida; no declarar ambas plataformas
validadas si solo hay una. No enviar mensajes ni hacer llamadas para verificar
el enlace: basta abrir el destino y comprobar el número.

**Botón y lectura:** Placa al arrancar con perfil válido, fallback sin perfil,
ciclo completo, 30 cambios y diez despertares por página; identidad accesible sin
conocer el menú. Nombre/teléfono legibles sin depender de QR. Comprobar que la
carcasa no tape el botón ni las esquinas. No sustituir esta observación por PNG.

**Rendimiento:** conservar p95 normal ≤20 ms y máximo ≤50 ms por servicio, después
del arranque y separando navegación/QR inicial/refresco normal. Las cifras I6d
(12,340/45,914 ms) son referencia de banco sin periféricos. Para movimiento,
objetivo inicial 20 FPS mientras dura y botón→primer cambio visible p95 <100 ms,
medidos por vídeo/instrumento; el histograma del loop no acredita esa latencia.

Presupuesto inicial propio para VIS-1–4: mantener buffer/pool actuales, no más de
8 KiB adicionales de uso de pool frente a una fixture I6d equivalente y +96 KiB
de flash para fuentes/iconos/QR. Medir heap mínimo y bloque mayor con el QR más
largo; 30 ciclos sin caída sostenida ni fallos. Si excede, recortar glifos/assets
o simplificar layout antes de ampliar memoria. Estas cotas son objetivos nuevos,
no resultados medidos. VIS-5 añade una sola animación simultánea y evita trabajo
cuando está oculto/oscuro; un QR estable no se regenera cada segundo.

Contabilizar además RAM estática, stack y buffers del componente fuera del pool;
los 214 bytes del ensayo son únicamente dos arrays del encoder. Reportar por
separado pico de construcción, primer QR y estado estable. ZXing es herramienta
de prueba host, nunca dependencia de firmware. La captura debe mostrar un solo
QR y devolver exactamente el enlace del mismo teléfono que aparece en pantalla.

En VIS-5 añadir registro de regiones/píxeles enviados, fotogramas por segundo
durante el efecto y página lógica final tras clic intermedio/timeout. Mantener
estable el par variable/callback de animación; cancelar el trabajo al ocultar.
Una etiqueta sin cambios no dispara setters por tick. Si solo cambia el
indicador, la identidad no debe invalidarse como región animada.

**Flujo con IA:** referencia → contrato del componente → LVGL 8.4 compartido →
fixtures/capturas → inspección de texto y escaneo → corrección → placa → evidencia.
Un boceto SVG sirve para decidir; únicamente el renderer compartido acredita
geometría del firmware. Para fuentes/iconos, conservar licencia, origen, versión,
receta y hashes. No instalar editores comerciales, SDL o generadores externos
si el flujo actual cubre la necesidad.

EEZ/SquareLine y WASM son opciones de autoría/revisión: se evalúa un componente
contra el renderer actual antes de adoptarlos. El exportador no controla drivers,
persistencia ni scheduler. `master` y las respuestas MCP son pistas; contrastar
APIs/formatos con 8.4 antes de generar código. GIF, ThorVG/Lottie y cambios a
LovyanGFX/TFT_eSPI/esp_lvgl_port conservan experimentos separados (R11–R27).
Una tendencia posterior usa muestras temporales reales y huecos explícitos
(`LV_CHART_POINT_NONE`); no convierte falta de GPS en descanso (R28).

## Decisiones pendientes en su momento

| Decisión | Por defecto para avanzar | Cuándo resolver |
| --- | --- | --- |
| Nombre y teléfono reales | Fixtures inequívocamente ficticios | VIS-2, configuración local |
| Canal del QR | WhatsApp; llamada como alternativa | Prototipo A/B y configuración |
| Nombre largo y QR simultáneos | Medir layouts; fallback textual sin recortar dígitos | VIS-1 |
| Atajo largo | No modificar el gesto ya validado | VIS-6 si la prueba de descubrimiento lo justifica |
| Tiempo para escanear | Política actual, ensayo explícito de una ventana mayor | VIS-3/I6c, sin activar producto silenciosamente |
| Foto/sprite/tema personal | Huella mínima, ningún bitmap grande | VIS-6, con presupuesto |
| Mensaje perdido / retener identidad al apagar | Sin activación automática ni cambio del despertar | Contrato posterior explícito |

El siguiente cambio concreto es **VIS-3: integrar IdentityView con la identidad
persistida y el ciclo de cuatro páginas**, conservando BOOT/despertar. La carga
y el escaneo físico cierran esa aceptación; efectos permanecen VIS-5.
