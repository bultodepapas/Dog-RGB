# RGB Dog × WLED: análisis técnico y plan de implementación

> Fecha del análisis: 2026-08-12
>
> RGB Dog analizado en: `eb8ab3e622a142422baeaaf32bf8cb5fcb4a0e30` (`main`)
>
> WLED analizado en: [`v16.0.1`](https://github.com/wled/WLED/releases/tag/v16.0.1), commit `29b389df1c1aaec6ff53aea742d17063b985906c`
>
> Alcance: firmware activo, portal local, persistencia, pruebas, CI y patrones transferibles desde WLED.

## Resumen ejecutivo

RGB Dog ya tiene una base mucho más seria de lo que aparenta un proyecto DIY: separa GPS, geocerca, sesiones, almacenamiento, Wi-Fi, BLE, portal y UI LED; mantiene configuración crítica con dos slots, CRC y generaciones; tiene telemetría de desarrollo; y prueba tanto lógica host como el portal y Wokwi.

La recomendación no es reemplazar el firmware por WLED ni copiar su arquitectura. WLED es excelente como laboratorio de iluminación, compatibilidad y operación en ESP, pero su núcleo también acumula archivos gigantes, estado global y funciones muy complejas. RGB Dog debe aprender sus conceptos, no heredar su tamaño.

Las ideas de mayor retorno para este collar son, en este orden:

1. **Límite de corriente estimada por frame**, para proteger batería, regulador, pistas y LEDs.
2. **Registro estable de efectos con metadatos**, para que firmware y portal compartan capacidades reales.
3. **Estado LED separado de la política del producto**, evitando que GPS, Wi-Fi, geocerca y renderizado sigan creciendo dentro de una sola función.
4. **Segmentos semánticos pequeños**, no el editor arbitrario de WLED: estado, cuerpo A, cuerpo B y alerta.
5. **Paletas RGBW y transiciones cruzadas**, para producir cambios más bonitos sin agregar docenas de efectos casi iguales.
6. **Escenas/presets seguros y pocos**, almacenados con la misma disciplina transaccional que ya usa RGB Dog.
7. **UI web editable y compilada a assets comprimidos**, en vez de continuar ampliando grandes strings HTML dentro de C++.

No recomiendo priorizar MQTT, Alexa, DMX, Art-Net, 250 presets, cientos de paletas, segmentos arbitrarios, un sistema completo de plugins o compatibilidad con el JSON de WLED. Son buenas capacidades para WLED, pero distraen del producto y aumentan superficie de prueba, consumo y mantenimiento.

## Cómo se hizo el análisis

- Se indexaron ambos repositorios como grafos de código y se siguieron llamadas, módulos, complejidad y dependencias.
- Para WLED se usó el tag estable exacto `v16.0.1`; no se tomaron decisiones desde `main` ni desde builds nocturnos.
- Se contrastó el código con la documentación y las páginas de release oficiales.
- Se ejecutó la suite host disponible de RGB Dog y el smoke test de las páginas.
- En el análisis inicial PlatformIO no estaba disponible localmente. Durante la Fase 0 se instaló el entorno fijado, se reparó una extracción parcial del framework y se completaron un build limpio y otro incremental con `SUCCESS`.

Este documento distingue entre hechos observados, propuestas y trabajo opcional. Las estimaciones son rangos de esfuerzo de ingeniería, no fechas prometidas.

## 1. Estado actual de RGB Dog

### 1.1 Arquitectura recuperada

El firmware activo está en [`Platformio/Dog-RGB`](../Platformio/Dog-RGB). La secuencia real de arranque y el bucle principal viven en [`src/main.cpp`](../Platformio/Dog-RGB/src/main.cpp).

```mermaid
flowchart LR
    GNSS["GNSS E108-GN02"] --> GPS["gps.cpp<br/>fix, métricas, sesiones"]
    GPS --> GEOF["geofence.cpp"]
    GPS --> STORE["storage / track NVS"]
    GEOF --> POLICY["main + led_ui<br/>política visual actual"]
    WIFI["wifi_mgr.cpp<br/>AP/STA y reintentos"] --> POLICY
    BLE["ble.cpp opcional"] --> POLICY
    POLICY --> LED["led_ui.cpp<br/>12 efectos RGBW"]
    LED --> A["SK6812 A · 24 px"]
    LED --> B["SK6812 B · 24 px"]
    GPS --> HTTP[portal_http.cpp]
    WIFI --> HTTP
    STORE --> HTTP
    HTTP --> UI["pages.cpp<br/>portal local"]
    CFG["runtime_config.cpp<br/>A/B + CRC + generación"] --> POLICY
    CFG --> HTTP
```

Características confirmadas:

- XIAO ESP32-S3, GNSS, dos tiras SK6812 RGBW de 24 LEDs y batería 21700.
- Portal local en AP/STA, con BLE opcional y lógica de coexistencia.
- Doce efectos actuales: sólido, pulso, respiración, chase, comet, sinelon, confetti, juggle, BPM, rainbow, fire y gradient wave.
- Los dos primeros píxeles se usan como estado; los restantes funcionan como cuerpo.
- El modo show baraja efectos, evita repeticiones inmediatas e interpola colores.
- Endpoints para resumen, estado, desarrollo, configuración, Wi-Fi, home/geocerca, tracks CSV/GeoJSON y portal cautivo.
- Configuración persistente versionada con dos registros, CRC, generación y migraciones.
- Particiones OTA A/B ya reservadas, aunque no se encontró un flujo OTA implementado.

### 1.2 Lo que el repositorio ya hace especialmente bien

#### Persistencia resistente

[`runtime_config.cpp`](../Platformio/Dog-RGB/src/config/runtime_config.cpp) valida dos copias, selecciona la generación válida más reciente y contempla migraciones. La actualización HTTP intenta persistir y restaura el estado anterior si falla. Para un dispositivo que puede perder alimentación en cualquier momento, esto es mejor fundamento que guardar JSON de configuración con escrituras aleatorias.

Esta propiedad debe conservarse al agregar escenas, calibración eléctrica o nuevos parámetros. WLED usa archivos para varias de estas tareas; en RGB Dog no hay razón para abandonar el mecanismo robusto que ya existe.

#### Dominio propio, no firmware LED genérico

GPS, calidad del fix, geocerca, sesiones, almacenamiento de recorrido y exportación son capacidades de producto reales. La política de AP sensible al estado del dispositivo también es más apropiada que una política Wi-Fi genérica. WLED no reemplaza ninguna de estas piezas.

#### Observabilidad útil

`/api/dev` expone heap, uptime, build, slots y fallos de almacenamiento, estado y eventos Wi-Fi, métricas GPS, edades de datos, geocerca, efecto actual y modo diurno. Es una base excelente para sumar corriente estimada, factor del limitador, tiempo de frame y memoria del motor visual.

#### Pruebas variadas

Hay pruebas host de contratos y lógica, smoke test HTML, Playwright, regresión visual, escenarios Wokwi y compilación de firmware en CI. El proyecto ya posee los lugares correctos para verificar una refactorización incremental.

### 1.3 Deuda y riesgos observados

| Hallazgo | Evidencia | Riesgo | Decisión sugerida |
|---|---|---|---|
| La política y el render LED están mezclados | `update_led_ui()` consulta GPS, Wi-Fi, geocerca y modo, selecciona efecto, renderiza y aplica estado | Cada nueva regla multiplica combinaciones y regresiones | Separar `LedPolicyEngine`, `LedState`, transición y renderer |
| Un switch concentra los 12 efectos | `apply_effect()` recibe 9 parámetros y conoce todos los modos | Difícil describir capacidades en la UI o probar efectos aisladamente | Registro de efectos con ID estable, función y metadatos |
| No se limita corriente por frame | `show_leds()` convierte RGB a RGBW y escribe; brillo acepta 1–255 | Picos de corriente, caída de tensión, calor, resets o menor autonomía | Estimador y limitador global/per-bus calibrado en banco |
| Segmentación implícita | Dos píxeles de estado y cuerpo se deducen dentro del código | Orientación, simetría y overlays terminan hardcodeados | Layout semántico fijo y pequeño |
| Transición show en dos tiempos | Fade-out y luego fade-in | Se ve como apagado entre escenas; no preserva continuidad | Crossfade entre frame anterior y nuevo |
| HTML/JS grande incrustado en C++ | `pages.cpp` tiene unas 2.387 líneas; la página de configuración ronda 879 | Edición frágil, builds ruidosos y crecimiento de `String` | Fuente web separada, minificada y comprimida al compilar |
| Archivos centrales muy grandes | `gps.cpp` ~2.944, `pages.cpp` ~2.387, `portal_http.cpp` ~1.293, `wifi_mgr.cpp` ~1.127, `led_ui.cpp` ~1.056 líneas | Alta carga cognitiva y más conflictos de edición | Cortes por responsabilidad, sin reescritura total |
| Contrato documentado desalineado | `architecture.md` describe BLE después del portal; el código inicia BLE antes de Wi-Fi por coexistencia | Un mantenedor puede cambiar el orden y reintroducir un fallo | Corregir documentación en la primera fase |
| Suite host no está en CI | Los jobs cubren portal, visuales y firmware, pero no ejecutan `unittest discover` | Una regresión lógica puede entrar aunque el build pase | Añadir job obligatorio y rápido |
| OTA particionado, pero no implementado | `app0`/`app1` existen; no se encontró updater | Capacidad tentadora pero seguridad/rollback sin cerrar | Mantener como fase opcional, local y con desbloqueo físico |
| Licencia del proyecto resuelta después de este análisis | Se añadió la licencia [MIT](../LICENSE), documentada en [ADR-0002](adr/0002-project-license-mit.md) | La licencia propia no concede derechos sobre material externo | Mantener trazabilidad y revisar compatibilidad antes de copiar código de WLED |

Los cinco archivos C++ activos más grandes concentran aproximadamente 8.807 de 11.205 líneas, cerca del 79 %. Esto no exige partir todo de inmediato: sí indica dónde evitar agregar la próxima capa de comportamiento.

### 1.4 Línea base de verificación

Se ejecutó desde `Platformio/Dog-RGB`:

```text
python -m unittest discover -s test -p "test_*.py" -v
```

Resultado: **114 pruebas, 112 pasan y 2 fallan**.

Las dos fallas parecen contratos estáticos desactualizados que deben revisarse, no una conclusión automática de que el firmware esté roto:

1. Una prueba espera cuatro apariciones de `persist_config_or_restore(previous)` y el código actual contiene tres.
2. Una prueba de credenciales espera `{"ok":false,"reason":"storage"}`, mientras el handler actual responde `{"status":"error","reason":"storage"}`.

También se ejecutó `python tools/web_pages_smoke.py` con resultado satisfactorio. La Fase 0 produjo después la línea base local: 56.644 bytes de RAM (17,3 %) y 1.171.295 bytes de flash de aplicación (35,0 %); CI repetirá y archivará la medición por commit.

## 2. Qué hace bien WLED y por qué importa aquí

WLED es un firmware maduro para control de iluminación en ESP8266/ESP32. La release estable usada en este análisis, [`v16.0.1`](https://github.com/wled/WLED/releases/tag/v16.0.1), ofrece segmentos, efectos, paletas, presets, playlists, límites de corriente, JSON/HTTP, actualizaciones OTA, múltiples tipos de salida y una UI embebida. Su repositorio oficial es [Aircoookie/WLED](https://github.com/wled/WLED).

Su mayor lección no es la cantidad de efectos. Es que modela la iluminación como un pequeño sistema:

```text
estado deseado
  → segmento/layout
  → efecto + paleta + parámetros
  → transición/composición
  → bus físico y orden de color
  → estimación de potencia
  → frame mostrado
```

### 2.0 Matriz de transferencia

| Idea de WLED | Estado en RGB Dog | Decisión | Fase |
|---|---|---|---|
| Límite automático de brillo/corriente | No existe por frame | Adoptar y calibrar para dos buses RGBW | 1 |
| Registro + metadatos de efectos | Enum/switch y formulario acoplado | Adaptar en una tabla estática con IDs existentes | 2 |
| Estado declarativo/capabilities JSON | APIs parciales y catálogo duplicable | Adaptar con endpoints propios versionados | 2 |
| Segmentos, reverse y mirror | Dos regiones implícitas | Reducir a layout semántico fijo | 3 |
| Paletas independientes del efecto | Colores A/B por rango | Adoptar un catálogo curado RGBW | 3 |
| Transición que mezcla efecto viejo/nuevo | Fade-out seguido de fade-in | Adoptar crossfade por buffers | 3 |
| Presets y playlists | Show aleatorio; perfiles diseñados pero pendientes | Adaptar a 4 built-ins + 4 slots A/B | 4 |
| Assets web generados y comprimidos | HTML/JS como strings C++ | Adoptar pipeline fuente → gzip → header | 5 |
| Usermods | BLE opcional, futuros sensores | Reducir a hooks estáticos mínimos | 6, opcional |
| OTA con particiones A/B | Particiones existen, updater no | Adaptar solo local, protegido y con rollback | 7, opcional |
| WebSocket/live preview | No existe | Posponer; preview cliente primero | Backlog |
| Protocolos e integraciones masivas | No son parte del producto | No adoptar sin caso de uso | Fuera |

### 2.1 Servicio no bloqueante y estado por segmento

[`WS2812FX::service()`](https://github.com/wled/WLED/blob/v16.0.1/wled00/FX_fcn.cpp#L1318-L1386) recorre segmentos activos, respeta el tiempo de siguiente frame y mantiene runtime por segmento. Durante una transición puede renderizar el efecto viejo y el nuevo y mezclarlos antes de mostrar.

Aplicación en RGB Dog:

- Mantener `loop()` no bloqueante.
- Dar a cada efecto un runtime explícito en vez de depender de estado disperso.
- Renderizar hacia buffers, no directamente a los LEDs.
- Hacer crossfade real al cambiar de escena, modo o efecto.

Con 48 píxeles, dos buffers RGBW son pequeños; el costo de memoria es razonable y debe medirse, no suponerse.

### 2.2 Registro de efectos y metadatos

WLED relaciona un ID, una función de render y metadatos que describen sliders, colores, paleta y valores por defecto. El endpoint `fxdata` permite que la UI muestre controles relevantes en lugar de un formulario genérico para todo. Véanse [`FX.h`](https://github.com/wled/WLED/blob/v16.0.1/wled00/FX.h) y la [documentación de efectos](https://kno.wled.ge/features/effects/).

Aplicación en RGB Dog:

- Mantener IDs numéricos estables porque ya se persisten.
- Añadir una clave textual estable, por ejemplo `breath` o `gradient_wave`.
- Declarar si un efecto usa colores A/B, paleta, velocidad o intensidad.
- Declarar defaults y rango útil, no solo 0–255.
- Etiquetar efectos de alto parpadeo como `advanced`/`strobe-risk`; nunca hacerlos default en un wearable para un animal.

Un registro mínimo puede ser estático, sin `std::function`, RTTI ni asignación dinámica:

```cpp
struct EffectDescriptor {
  uint8_t id;
  const char* key;
  const char* label;
  EffectRenderFn render;
  uint8_t controls;
  EffectDefaults defaults;
  EffectSafetyClass safety;
};
```

### 2.3 Segmentos como layout, no como editor infinito

Los [segmentos de WLED](https://kno.wled.ge/features/segments/) soportan inicio/fin, grouping, spacing, offset, reverse, mirror y, en v16, composición de capas. Es potentísimo, pero trasladarlo completo sería excesivo.

RGB Dog necesita una versión semántica:

- `status`: los LEDs reservados para estado operativo.
- `body_left` y `body_right`: las dos ramas físicas, cada una con orientación declarada.
- `body_all`: vista virtual continua para un efecto simétrico.
- `alert`: overlay lógico de máxima prioridad, no un tercer cable físico.

Esto resuelve una necesidad concreta: mantener visible una advertencia de geocerca o GPS sin destruir por completo el efecto del cuerpo, y representar correctamente una tira instalada en sentido inverso.

No se propone permitir al usuario crear segmentos arbitrarios en el portal.

### 2.4 Límite automático de brillo/corriente

[`BusManager::applyABL()`](https://github.com/wled/WLED/blob/v16.0.1/wled00/bus_manager.cpp#L1483-L1524) estima el consumo y reduce el brillo cuando el frame superaría el presupuesto configurado. WLED expone límites globales y por salida; su [documentación de ajustes](https://kno.wled.ge/features/settings/) explica el presupuesto de corriente y la estimación por píxel.

Esta es la idea más importante para RGB Dog porque es un dispositivo de batería y va puesto sobre un animal. El objetivo no es prometer una medición eléctrica exacta desde software, sino impedir que un frame blanco o un efecto denso solicite más corriente de la que el sistema fue diseñado para entregar.

Propuesta:

1. Estimar corriente desde R, G, B, W y corriente base por píxel.
2. Sumar ambos buses y el consumo fijo configurable del dispositivo.
3. Calcular `scale = min(1, budget / estimate)`.
4. Aplicar el factor al frame final, después de overlays y antes de escribir.
5. Filtrar cambios bruscos del factor para evitar bombeo visible.
6. Exponer estimado, presupuesto, factor y contador de frames limitados en `/api/dev`.
7. Calibrar los coeficientes con una fuente/medidor en banco y guardar un perfil conservador por defecto.

El límite de software complementa fusible, cableado, regulador, ventilación y protección de batería; no los sustituye.

### 2.5 Paletas separadas del movimiento

Las [paletas de WLED](https://kno.wled.ge/features/palettes/) desacoplan la geometría temporal del efecto y la selección de color. Esto multiplica variedad sin multiplicar el código.

RGB Dog no necesita el catálogo enorme de WLED. Una primera biblioteca de seis a ocho paletas RGBW pensadas para el collar sería suficiente:

- `Safety Amber`: ámbar/blanco cálido, alta legibilidad.
- `Night Red`: rojo tenue, baja perturbación nocturna.
- `Ocean`: azul/cian/blanco.
- `Forest`: verde/menta/blanco.
- `Pride`: arcoíris curado.
- `Heat`: rojo/naranja/amarillo.
- `Ice`: blanco frío/azul.
- `Custom A-B`: gradiente desde los colores actuales del usuario.

El canal blanco debe tratarse intencionalmente. La conversión actual `W = min(R,G,B)` es una buena primera extracción RGBW, pero paletas y estimador eléctrico deben compartir exactamente la misma transformación para que la vista previa y el cálculo sean coherentes.

### 2.6 Presets y playlists

Los [presets de WLED](https://kno.wled.ge/features/presets/) guardan estados nombrados y pueden encadenarse en playlists. Es una gran idea de producto, con una adaptación importante: la propia documentación advierte que escrituras aleatorias de LittleFS pueden bloquear temporalmente el dispositivo. RGB Dog ya posee un patrón A/B con CRC que encaja mejor.

Propuesta inicial:

- Cuatro escenas integradas, no editables: `Seguridad`, `Calmado`, `Activo`, `Fiesta`.
- Hasta cuatro escenas del usuario.
- Una escena contiene efecto, paleta, brillo objetivo, velocidad, intensidad, transición y política de estado.
- Nunca contiene Wi-Fi, PIN, home/geocerca ni secretos.
- Guardado transaccional A/B y esquema versionado.
- Export/import JSON validado como comodidad, no como almacenamiento primario.

Una playlist compleja no es necesaria al principio. El modo show existente puede migrar para consumir escenas y mantener su bolsa barajada.

### 2.7 API de estado y capacidades

La [JSON API de WLED](https://kno.wled.ge/interfaces/json-api/) separa estado mutable, información del dispositivo y catálogos de efectos/paletas. No conviene copiar su esquema, pero sí el principio de descubrimiento de capacidades.

Agregar endpoints versionados y aditivos:

```text
GET   /api/v1/led/state
PATCH /api/v1/led/state
GET   /api/v1/led/capabilities
GET   /api/v1/scenes
POST  /api/v1/scenes/:slot/apply
PUT   /api/v1/scenes/:slot
```

`/api/v1/led/capabilities` debería publicar:

- versión del esquema;
- layout y orientación;
- efectos con ID, clave, nombre, controles, defaults y clase de seguridad;
- paletas disponibles;
- límites válidos y soporte de transición;
- presupuesto eléctrico configurado, sin información sensible.

Los endpoints actuales deben mantenerse durante una ventana de compatibilidad. El portal puede migrar primero; clientes externos después.

### 2.8 UI fuente → bundle comprimido

WLED mantiene HTML/CSS/JS como fuentes editables y usa herramientas de build para minificar, comprimir e incrustar assets, por ejemplo [`tools/cdata.js`](https://github.com/wled/WLED/blob/v16.0.1/tools/cdata.js) y [`pio-scripts/build_ui.py`](https://github.com/wled/WLED/blob/v16.0.1/pio-scripts/build_ui.py).

RGB Dog ya usa Node y Playwright, así que puede adoptar un pipeline sencillo sin framework:

```text
webui/src/*.html, *.css, *.js
  → lint/test
  → minify
  → gzip determinista
  → include/web/generated_assets.h
  → Async/HTTP response con Content-Encoding: gzip
```

El archivo generado debe verificarse en CI o generarse dentro del build de forma reproducible. El portal debe seguir funcionando sin internet y conservar las pruebas visuales actuales.

### 2.9 Extensiones opcionales pequeñas

Los [usermods de WLED](https://kno.wled.ge/advanced/custom-features/) ofrecen hooks amplios para setup, loop, JSON, configuración, red y eventos. Copiar esa interfaz sería prematuro. Sí resulta útil una interfaz estática mínima para futuras capacidades como IMU o sensor cardiaco:

```cpp
struct OptionalModule {
  void (*setup)();
  void (*tick)(uint32_t now_ms);
  void (*append_dev_json)(JsonObject out);
};
```

Debe compilarse por flags, sin carga dinámica ni gestor de plugins. La funcionalidad avanzada seguirá siendo opcional, coherente con el carácter DIY del proyecto.

## 3. Qué no copiar de WLED

### 3.1 No copiar su forma monolítica

En el tag analizado, `FX.cpp` supera 11.000 líneas y otros archivos centrales también son muy grandes. Funciones de deserialización y el loop coordinan numerosas responsabilidades y dependen de mucho estado global. Es deuda asumible para un proyecto con años de compatibilidad, pero no una plantilla deseable para RGB Dog.

### 3.2 No copiar código externo sin resolver procedencia y compatibilidad

WLED se distribuye bajo [EUPL-1.2](https://github.com/wled/WLED/blob/v16.0.1/LICENSE). RGB Dog adoptó posteriormente la licencia [MIT](../LICENSE), pero esa decisión no concede permiso para relicenciar material de WLED. Se mantiene una implementación **clean-room de los patrones**, apoyada en comportamiento y documentación, sin copiar y pegar `FX.cpp`, `bus_manager.cpp`, assets o efectos.

Antes de reutilizar código literal se deben revisar procedencia, compatibilidad, atribución y obligaciones de distribución. Este punto es una alerta de ingeniería, no asesoría legal.

### 3.3 No adoptar toda su superficie de integraciones

Por ahora quedan fuera:

- MQTT, Alexa, Hue, Home Assistant y automatizaciones externas.
- DMX, Art-Net, DDP y protocolos de live streaming.
- Múltiples redes Wi-Fi y un editor de red avanzado.
- Cientos de paletas y efectos.
- Editor arbitrario de segmentos/capas.
- 250 presets y playlists recursivas.
- Sistema completo de usermods.
- Compatibilidad binaria o de API con WLED.

Cada una puede reabrirse si aparece un caso de uso real. No debe entrar solo porque WLED la tenga.

## 4. Arquitectura objetivo propuesta

```mermaid
flowchart LR
    DOMAIN["GPS · geocerca · Wi-Fi · BLE · portal"] --> POLICY[LedPolicyEngine]
    POLICY --> TARGET["LedState objetivo"]
    SCENES["SceneStore A/B"] --> TARGET
    TARGET --> TRANS[TransitionEngine]
    REG["EffectRegistry + metadata"] --> RENDER[EffectRenderer]
    PAL["PaletteRegistry RGBW"] --> RENDER
    TRANS --> RENDER
    LAYOUT["LedLayout<br/>status/body A/body B/orientation"] --> RENDER
    RENDER --> COMP["Compositor<br/>body + status + alert"]
    COMP --> LIMIT[PowerLimiter]
    LIMIT --> BUS["LedBus A/B"]
    BUS --> PIXELS["48 × SK6812 RGBW"]
    LIMIT --> DEV["/api/dev"]
    REG --> CAPS["/api/v1/led/capabilities"]
    PAL --> CAPS
    LAYOUT --> CAPS
```

### Responsabilidades

| Pieza | Responsabilidad | No debe conocer |
|---|---|---|
| `LedPolicyEngine` | Traducir GPS/Wi-Fi/geocerca/modo a intención visual y prioridad | Buffers, pines o protocolo SK6812 |
| `LedState` | Estado serializable: escena, efecto, paleta, parámetros, brillo, transición | Reglas de GPS |
| `EffectRegistry` | Descriptor estático y lookup por ID/clave | HTTP o NVS |
| `EffectRenderer` | Generar colores virtuales determinísticamente desde tiempo/runtime | Estado Wi-Fi |
| `LedLayout` | Mapear índices virtuales a buses, reverse y regiones semánticas | Efectos concretos |
| `Compositor` | Mezclar cuerpo, estado y alerta según prioridad | GPIO |
| `PowerLimiter` | Estimar y escalar el frame final | Motivo semántico del color |
| `LedBus` | Conversión RGB→RGBW, orden de color y escritura física | Geocerca, escenas o portal |
| `SceneStore` | Validar/versionar/persistir escenas A/B | Render en tiempo real |

### Reglas de diseño

- Cero asignaciones dinámicas en el hot path LED.
- IDs persistidos nunca cambian de significado; un efecto eliminado deja un slot reservado.
- El estado operativo crítico siempre gana sobre decoración.
- Las transiciones nunca ocultan una alerta nueva esperando terminar un fade.
- El renderer recibe tiempo explícito para permitir pruebas deterministas.
- API, UI y firmware leen el mismo registro de capacidades.
- La configuración simple sigue siendo simple; calibración y tuning quedan detrás de “Avanzado”.

## 5. Plan de implementación

### Fase 0 — Recuperar una línea base confiable

**Esfuerzo:** 1–2 días. **Prioridad:** inmediata.

**Estado 2026-08-12:** ejecutada en software. La suite host queda en 114/114; el build local de producción termina con `SUCCESS`, 17,3 % de RAM y 35,0 % de flash; y CI captura tests, entorno, tamaño y artefactos. Las mediciones eléctricas siguen pendientes de hardware/BOM/instrumentos; véanse el [registro de baseline](baselines/fase-0-2026-08-12.md) y la [ADR de procedencia/licencia](adr/0001-wled-clean-room-y-licencia-del-proyecto.md).

Trabajo:

- Revisar las dos pruebas host fallidas y decidir cuál contrato es correcto en cada caso.
- Dejar las 114 pruebas verdes.
- Agregar `python -m unittest discover ...` como job requerido de CI.
- Capturar desde PlatformIO/CI tamaño de firmware, RAM estática y entorno exacto.
- Documentar el orden real BLE → Wi-Fi y la razón de coexistencia.
- Crear un ADR de licencia antes de importar cualquier fuente de WLED.
- Registrar mediciones físicas actuales: corriente en idle, blanco máximo, efecto típico y temperatura del regulador/batería.

Criterio de salida:

- Suite host, smoke web, Playwright, visuales, Wokwi y build verdes.
- Métricas de flash/RAM archivadas como baseline.
- Presupuesto eléctrico provisional documentado.

### Fase 1 — Seguridad eléctrica y bus medible

**Esfuerzo:** 3–5 días. **Prioridad:** alta.

**Estado 2026-08-12:** implementación de software completa. `LedFrame`, `LedBus`, conversión RGBW única, `PowerLimiter` global, esquema 6 con migración v5, controles avanzados, telemetría y pruebas están integrados. Los builds de producción/Wokwi, 121 pruebas host, smoke y 75 pruebas Playwright quedan verdes; los 17 baselines visuales se regeneraron y compararon en el contenedor fijado. La calibración de corriente/temperatura en el collar físico sigue pendiente; véase el [registro de Fase 1](baselines/fase-1-2026-08-12.md).

Trabajo:

- Introducir `LedFrame` y una frontera `LedBus` alrededor de Adafruit NeoPixel sin cambiar el aspecto visual.
- Centralizar la conversión RGB→RGBW.
- Implementar `PowerLimiter` global para los dos buses.
- Añadir configuración avanzada: presupuesto mA, corriente base y perfil de píxel.
- Aplicar defaults conservadores si la calibración no existe.
- Publicar corriente estimada, factor, pico y frames limitados en `/api/dev`.
- Añadir prueba determinista para negro, primarios, blanco, saturación del presupuesto y dos buses.

Archivos probables:

```text
include/led/led_frame.h
include/led/led_bus.h
src/led/led_bus.cpp
include/led/power_limiter.h
src/led/power_limiter.cpp
```

Criterio de salida:

- Ningún frame supera el presupuesto estimado configurado.
- La comparación en banco cae dentro de una tolerancia acordada y conservadora.
- Sin parpadeo visible por oscilación del factor.
- Sin regresión funcional en estados GPS/Wi-Fi/geocerca.

### Fase 2 — Registro de efectos, metadatos y separación de política

**Esfuerzo:** 4–7 días. **Prioridad:** alta.

**Estado 2026-08-13:** implementación de software completa. Los 12 IDs persistidos se conservan en un `EffectRegistry` versionado; el renderer puro recibe tiempo y PRNG explícitos; `LedPolicyEngine` produce un `LedState` serializable con prioridades caracterizadas; `/api/v1/led/state` y `/api/v1/led/capabilities` son aditivos; y el portal construye opciones/controles desde capabilities. El esquema NVS permanece en 6 y el registro binario en 2. Build de producción, 124 pruebas host, smoke y 76 pruebas Playwright quedan verdes; véase el [registro de Fase 2](baselines/fase-2-2026-08-13.md).

Trabajo:

- Crear pruebas de caracterización de los 12 efectos actuales con tiempo y semilla fijos.
- Extraer `EffectRegistry` conservando los IDs existentes.
- Declarar controles, defaults, uso de color/paleta y clase de seguridad.
- Introducir `LedState` y mover la selección de prioridad a `LedPolicyEngine`.
- Mantener un adaptador temporal desde `config::RangeEffect` para no migrar NVS todavía.
- Añadir `/api/v1/led/state` y `/api/v1/led/capabilities`.
- Mantener `/api/config` compatible.

Archivos probables:

```text
include/led/led_state.h
include/led/effect_registry.h
src/led/effect_registry.cpp
include/led/led_policy.h
src/led/led_policy.cpp
src/led/effects/*.cpp
```

Criterio de salida:

- Los 12 efectos producen el mismo resultado caracterizado antes y después.
- El portal puede construir controles desde capabilities.
- GPS, Wi-Fi y geocerca no son dependencias del renderer.
- No cambia el significado de datos persistidos existentes.

### Fase 3 — Layout semántico, paletas y transiciones bonitas

**Esfuerzo:** 4–7 días. **Prioridad:** media-alta.

Trabajo:

- Declarar orientación y mapeo de los dos buses en `LedLayout`.
- Implementar regiones `status`, `body_left`, `body_right`, `body_all` y overlay `alert`.
- Crear seis a ocho paletas RGBW curadas.
- Adaptar primero breath, chase, comet, gradient wave y rainbow a paletas; el resto puede mantener A/B.
- Implementar crossfade por buffers con interrupción inmediata para alertas.
- Preservar los LEDs de estado durante cualquier transición.
- Añadir modo simétrico/mirror como propiedad del layout, no como duplicación dentro de cada efecto.

Criterio de salida:

- No hay flash negro entre efectos normales.
- Una alerta de geocerca aparece en el siguiente frame y conserva prioridad.
- Ambas ramas se ven orientadas correctamente sobre el collar real.
- El tiempo máximo de fase LED no empeora de forma material frente al baseline.

### Fase 4 — Escenas/presets pequeños y resistentes

**Esfuerzo:** 3–6 días. **Prioridad:** media.

Trabajo:

- Definir esquema `SceneV1` con IDs estables y validación estricta.
- Implementar cuatro escenas integradas y cuatro slots de usuario.
- Persistir slots de usuario con A/B, CRC y generación.
- Migrar el modo show para operar sobre escenas o descriptores, manteniendo bolsa barajada.
- Agregar listar, aplicar, guardar, borrar, exportar e importar.
- Excluir secretos y configuración de geocerca de cualquier export.
- Recuperar siempre el registro válido anterior ante corte o corrupción.

Criterio de salida:

- Aplicar una escena no bloquea el loop.
- Corte de alimentación simulado durante guardado conserva la generación previa.
- Firmware nuevo puede leer `SceneV1`; versiones futuras tienen ruta explícita de migración.
- El modo por defecto continúa siendo seguro y no usa efectos de alto parpadeo.

### Fase 5 — Portal generado desde fuentes web

**Esfuerzo:** 4–7 días. **Prioridad:** media.

Trabajo:

- Extraer progresivamente HTML, CSS y JS a `webui/src`.
- Crear build determinista minify + gzip + header generado.
- Servir assets con longitud conocida y `Content-Encoding: gzip`, evitando construir páginas grandes como `String` cuando sea posible.
- Generar controles desde capabilities.
- Agregar selector visual de escenas, swatches de paleta, transición y un preview simple del collar.
- Mostrar presupuesto/corriente estimada solo en modo avanzado.
- Conservar funcionamiento offline, responsive, teclado, contraste y pruebas visuales.

Criterio de salida:

- Un checkout limpio reproduce byte por byte los assets generados.
- El portal no necesita CDN ni conexión externa.
- Smoke, Playwright, visuales y auditoría básica de accesibilidad pasan.
- Se mide flash y heap antes/después; cualquier regresión queda justificada.

### Fase 6 — Extensibilidad DIY opcional

**Esfuerzo:** 2–4 días para la infraestructura; cada módulo se estima aparte. **Prioridad:** opcional.

Trabajo:

- Crear registro estático mínimo de módulos opcionales.
- Mantener IMU, pulsómetro u otros sensores detrás de flags de compilación.
- Compilar en CI al menos producción, BLE y Wokwi para detectar roturas de combinación.
- Permitir que cada módulo agregue telemetría a `/api/dev` sin conocer el servidor HTTP.

Criterio de salida:

- La build base no paga RAM ni flash por módulos deshabilitados.
- No hay carga dinámica, ABI de plugins ni configuración avanzada obligatoria.

### Fase 7 — OTA local con rollback

**Esfuerzo:** 4–8 días más revisión de amenaza. **Prioridad:** opcional y posterior.

La tabla de particiones ya reserva dos aplicaciones. Si se implementa OTA:

- Solo local por defecto.
- Requiere PIN y, preferiblemente, ventana habilitada por botón físico.
- Valida chip, tamaño, versión, hash y firma si se adopta distribución pública.
- Marca la imagen como válida solo después de un arranque saludable.
- Mantiene rollback y feedback visible en el collar.
- Nunca expone un updater abierto en AP por comodidad.

No debe bloquear las mejoras LED ni convertirse en requisito para usar el collar.

## 6. Orden recomendado y dependencias

```text
Fase 0: baseline verde
  ├─ Fase 1: bus + límite de potencia
  └─ Fase 2: estado + registro + política
       └─ Fase 3: layout + paletas + crossfade
            ├─ Fase 4: escenas
            └─ Fase 5: nueva UI
                 └─ Fase 6: módulos opcionales

Fase 7 (OTA) puede hacerse después de Fase 0, pero se recomienda al final.
```

Una entrega útil no necesita esperar todas las fases. Las fases 0–3 ya darían un collar más seguro, modular y visualmente pulido.

## 7. Criterios transversales de aceptación

### Firmware

- Loop no bloqueante; ninguna animación usa delays de render.
- Sin asignaciones dinámicas dentro del tick LED.
- IDs de efectos y escenas persistidos son estables.
- Alertas críticas tienen prioridad y latencia acotada a un frame.
- Configuración inválida se rechaza completa; no deja estado parcial.
- Pérdida de energía durante persistencia recupera la última generación válida.
- Presupuesto de flash/RAM definido desde la línea base y verificado en CI.

### Eléctrico y wearable

- Presupuesto de corriente conservador por defecto.
- Pruebas de blanco máximo, patrón de peor caso y batería baja.
- Temperatura medida en regulador, batería, conector y tiras.
- Efectos de parpadeo agresivo no aparecen en presets default y viven en modo avanzado.
- La UI explica que el límite de software no reemplaza protección física.

### Portal y API

- API versionada para lo nuevo y compatibilidad temporal con lo actual.
- UI generada exclusivamente desde capabilities; no duplica catálogos de efectos.
- Sin secretos en status, capabilities, escenas exportadas o logs.
- Portal completamente offline.
- Flujo principal usable con pantalla pequeña y teclado.

### Pruebas

- Suite host completa como gate de CI.
- Characterization/golden vectors para los 12 efectos.
- Tests del limitador con frames sintéticos.
- Wokwi para cambios de GPS/geocerca/estado.
- Playwright y regresión visual para portal.
- Banco físico para corriente y temperatura: esto no puede sustituirse con emulación.

## 8. Ideas “chéveres” para después del núcleo

Estas ideas explotan la nueva arquitectura sin convertirla en requisito:

- **Vista previa del collar:** dos tiras reflejadas en el portal, animadas localmente a baja fidelidad desde los mismos metadatos.
- **Modo paseo nocturno:** cuerpo rojo tenue y status blanco/ámbar; alerta aumenta contraste sin llevar todo a brillo máximo.
- **Geofence pulse:** overlay que viaja hacia afuera en ambas ramas cuando se acerca al límite y cambia a rojo al cruzarlo.
- **Battery-aware show:** el presupuesto visual baja gradualmente con batería, sin saltos de brillo.
- **Scene chips:** cuatro botones grandes —Seguridad, Calmado, Activo, Fiesta— en la portada.
- **Paleta desde el perro:** dos colores personalizados y una paleta derivada, sin editor profesional de gradientes.
- **Sync de varios collares por ESP-NOW:** laboratorio opcional para una caminata/evento, local y sin nube.
- **Replay de ruta + luz:** correlacionar una sesión GPS con los cambios de escena para depuración o una demo.

Las primeras cinco aprovechan piezas ya propuestas. ESP-NOW y replay son experimentos posteriores, no parte del roadmap base.

## 9. Decisiones que deben quedar explícitas antes de programar

1. Presupuesto eléctrico real del hardware: corriente continua/pico admisible del regulador, batería, pistas, cables y conectores.
2. Orientación física de cada tira en el collar.
3. Política exacta de prioridad: geocerca, sin fix, Wi-Fi, BLE, batería y escena manual.
4. Compatibilidad prometida para `/api/config` y valores numéricos de efectos.
5. Número máximo de escenas de usuario y tamaño NVS asignado.
6. Política de compatibilidad y atribución para material de terceros; RGB Dog ya adoptó MIT mediante ADR-0002.
7. Si OTA es una necesidad real o solo una posibilidad futura.

Ninguna de estas decisiones impide comenzar la Fase 0. Las tres primeras sí deben cerrarse antes de terminar las fases 1–3.

## 10. Conclusión

El mejor resultado no es “RGB Dog con WLED adentro”. Es **RGB Dog con la madurez visual y operativa aprendida de WLED**, conservando su identidad de collar inteligente.

El repositorio ya tiene mejores cimientos de dominio y persistencia que muchos proyectos ESP hobby. La oportunidad es colocar un límite de potencia seguro, formalizar el motor LED, hacer que las capacidades sean descubribles, añadir paletas/transiciones/escenas con moderación y sacar la UI de los strings C++ antes de que siga creciendo.

Si solo se implementan cuatro cosas, deberían ser: baseline verde en CI, `PowerLimiter`, `EffectRegistry + LedState` y crossfade con layout semántico. Ese conjunto entrega la mayor mejora técnica, visual y de seguridad por unidad de complejidad.

## Fuentes primarias consultadas

- [Repositorio oficial de WLED](https://github.com/wled/WLED)
- [Release estable WLED v16.0.1](https://github.com/wled/WLED/releases/tag/v16.0.1)
- [JSON API](https://kno.wled.ge/interfaces/json-api/)
- [Segmentos](https://kno.wled.ge/features/segments/)
- [Efectos](https://kno.wled.ge/features/effects/)
- [Paletas](https://kno.wled.ge/features/palettes/)
- [Presets](https://kno.wled.ge/features/presets/)
- [Ajustes y limitación de corriente](https://kno.wled.ge/features/settings/)
- [Custom features y usermods](https://kno.wled.ge/advanced/custom-features/)
- [WebSocket y live preview](https://kno.wled.ge/interfaces/websocket/)
- [Licencia EUPL-1.2 de WLED](https://github.com/wled/WLED/blob/v16.0.1/LICENSE)
