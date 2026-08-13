# Auditoría de dependencias y plan de actualización

> **Document status:** Historical dependency audit snapshot (Spanish). Verify current pins in `Platformio/Dog-RGB/platformio.ini`, `package.json`, and CI rather than treating recommendations below as a live upgrade queue.

**Proyecto:** Dog-RGB / collar RGB DIY

**Fecha:** 2026-08-01

**Alcance:** firmware ESP32-S3, framework Arduino, librerías C++, simulación Wokwi, pruebas web y herramientas de desarrollo.

> **Estado:** plan ejecutado el 2026-08-01. El resultado, versiones finales,
> pruebas y límites de validación están registrados en
> [dependency_update_execution_2026-08-01.md](dependency_update_execution_2026-08-01.md).

## 1. Resumen ejecutivo

Al iniciar esta auditoría, el proyecto compilaba sobre una base estable, pero su framework embebido estaba atrasado una generación completa:

- `espressif32@6.7.0` resuelve **Arduino-ESP32 2.0.16**, basado en **ESP-IDF 4.4.x**.
- ESP-IDF 4.4 ya terminó su periodo de soporte. El firmware pierde correcciones acumuladas de Wi-Fi, BLE, UART, RMT y estabilidad del sistema.
- La versión oficial más nueva de la plataforma PlatformIO, `espressif32@7.0.1`, **no resuelve este problema** para proyectos Arduino: solo sube a Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7.
- Las dos librerías externas activas también están atrasadas: ArduinoJson `7.2.1 -> 7.4.3` y Adafruit NeoPixel `1.12.3 -> 1.15.5`.
- PlatformIO Core y Wokwi CLI sí están en sus últimas versiones estables detectadas.
- Playwright está resuelto en `1.59.1`; la versión actual es `1.62.1`.

La recomendación no es actualizar todo en una sola operación. El orden seguro es:

1. Congelar una línea base reproducible.
2. Actualizar ArduinoJson y NeoPixel individualmente sobre el core actual.
3. Actualizar las herramientas web.
4. Crear un entorno candidato separado con Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5.
5. Validar simulación y hardware real antes de convertirlo en el entorno de producción.

## 2. La fuente de verdad revisada

Archivos y resolución real inspeccionados:

- `Platformio/Dog-RGB/platformio.ini`
- `Platformio/Dog-RGB/.pio/libdeps/*` y paquetes instalados por PlatformIO
- `package.json` y `package-lock.json`
- `Platformio/Dog-RGB/docs/package.json` y su lockfile
- includes y llamadas reales bajo `Platformio/Dog-RGB/src` y `Platformio/Dog-RGB/include`
- scripts Python, pruebas host, configuración Wokwi y herramientas instaladas

No se consideró una librería activa solo porque aparezca en documentación o archivos de respaldo. Por ejemplo, FastLED aparece en `src/main.cpp.bak`, pero no participa en la compilación actual.

## 3. Inventario exacto al iniciar la auditoría

| Componente | Declarado | Resuelto/instalado | Último estable comprobado | Estado | Decisión |
|---|---:|---:|---:|---|---|
| PlatformIO Core | herramienta global | 6.1.19 | 6.1.19 | Actual | Mantener y documentar |
| PlatformIO Espressif32 | 6.7.0 | 6.7.0 | 7.0.1 oficial | Atrasado, pero el último oficial sigue en Arduino 2.x | No tratar 7.0.1 como migración final |
| Arduino-ESP32 | indirecto | 2.0.16 | 3.3.11 | Mayor atrasado | Migrar en entorno candidato |
| ESP-IDF bajo Arduino | indirecto | 4.4.x | 5.5.5 en Arduino 3.3.11 | Rama sin soporte | Prioridad alta, con validación amplia |
| Toolchain Xtensa/RISC-V | indirecto | 8.4.0 + 2021r2-patch5 | 14.2.0 en candidato | Mayor atrasado | Cambia junto con el core, no por separado |
| esptool | indirecto | 4.5.1 | 5.3.0 en candidato | Atrasado | Cambia junto con la plataforma |
| ArduinoJson | 7.2.1 | 7.2.1 | 7.4.3 | Atrasado; incluye corrección importante | Actualizar primero |
| Adafruit NeoPixel | 1.12.3 | 1.12.3 | 1.15.5 | Atrasado; cambios ESP32/RMT relevantes | Actualizar segundo |
| `@playwright/test` raíz | `^1.57.0` | 1.59.1 | 1.62.1 | Atrasado | Actualizar junto con lock y browsers |
| `playwright` docs | `^1.59.1` | 1.59.1 | 1.62.1 | Atrasado | Actualizar en sincronía con el raíz |
| Node.js | no fijado | 24.12.0 | 24.18.0 LTS | Misma LTS, patch atrasado | Mantener major 24 y fijar expectativa |
| npm | no fijado | 11.6.2 | dependiente de Node | Adecuado | No es entrada del firmware |
| Python | no fijado | 3.12.10 | 3.14.6 | Soportado, no urgente | Conservar 3.12; probar 3.14 opcionalmente |
| Wokwi CLI | herramienta global | 0.26.1 | 0.26.1 | Actual | Mantener |
| Extensión PlatformIO IDE | estación de trabajo | 3.3.4 | no fijada por repo | Informativo | Recomendar, no mezclar con firmware |
| Extensión Wokwi VS Code | estación de trabajo | 3.6.0 | no fijada por repo | Informativo | Recomendar, no mezclar con firmware |

### Dependencias activas que vienen dentro del core

Estas APIs no tienen una versión independiente en `lib_deps`; se actualizan al cambiar Arduino-ESP32:

- Wi-Fi STA/AP, eventos y escaneo: `WiFi`
- portal cautivo: `WebServer`, `DNSServer`, `ESPmDNS`
- almacenamiento: `Preferences` / NVS
- Bluetooth: `BLEDevice`, servidor, advertising y characteristics
- GPS: `HardwareSerial`
- concurrencia: FreeRTOS y colas
- sistema: `esp_system`

Por eso una migración del core tiene una superficie de riesgo mayor que la lista corta de `lib_deps` sugiere.

## 4. Hallazgos

### DEP-001 — La plataforma oficial más nueva no moderniza el framework Arduino

PlatformIO `espressif32@7.0.1` es la versión oficial más reciente, pero su manifiesto fija Arduino-ESP32 `~3.20017.0`, que significa Arduino **2.0.17**, no Arduino 3.2.17. Sigue basado en ESP-IDF 4.4.7.

Esto crea una trampa de versión: `pio pkg outdated` puede quedar “verde” después de subir a 7.0.1 mientras el framework esencial continúa en una rama antigua y sin soporte.

**Ruta recomendada:** evaluar `pioarduino/platform-espressif32@55.03.311`, que empaqueta Arduino-ESP32 3.3.11 oficial, ESP-IDF 5.5.5 y toolchain 14.2.0. Se debe fijar una versión exacta, nunca un alias flotante.

**Trade-off:** pioarduino es una plataforma comunitaria, aunque empaqueta el core oficial de Espressif. Si el proyecto decide aceptar únicamente la plataforma oficial de PlatformIO, `7.0.1` sirve como puente conservador, pero no puede considerarse la solución final.

### DEP-002 — ArduinoJson 7.2.1 debe subir antes de la migración mayor

ArduinoJson 7.4.3 incluye una corrección para un desbordamiento al convertir strings numéricos con muchos dígitos usando `as<T>()`. El proyecto procesa JSON enviado por el portal y convierte valores numéricos; por tanto, la actualización aporta robustez real ante entradas anómalas o corruptas.

La rama 7.3 también endurece la seguridad de copia de documentos y 7.4 reduce memoria usada por strings pequeños.

La API actual seguirá compilando, aunque `StaticJsonDocument` y `containsKey()` ya producen deprecaciones. Conviene separar dos cambios:

1. subir la dependencia a 7.4.3 y ejecutar pruebas;
2. modernizar después a `JsonDocument` y comprobaciones `.is<T>()`, midiendo RAM y flash.

No mezclar ambos en el mismo commit simplifica la atribución de cualquier regresión.

### DEP-003 — NeoPixel 1.15.5 prepara el camino para ESP-IDF 5

Desde 1.12.3 hubo correcciones específicas para ESP32:

- 1.12.4: asignación de buffer RMT y thread safety;
- 1.12.5: de-inicialización/destructor RMT;
- versiones recientes: ruta explícita para la nueva API RMT de ESP-IDF 5, buffer en heap y exclusión mutua.

El collar usa dos tiras SK6812 RGBW de 24 píxeles. En la antigua ruta IDF5, un `show()` puede reservar temporalmente varios KiB en el stack; la implementación actual reduce ese riesgo y serializa el acceso al periférico.

La actualización debe probarse primero sobre Arduino 2.0.16, incluyendo:

- ambas tiras y ambos pines;
- orden RGBW y brillo;
- alternancia repetida de `show()`;
- heap mínimo y latencia máxima del loop;
- señal real en hardware y VCD en Wokwi.

### DEP-004 — El salto Arduino 2.x a 3.x afecta módulos concretos

El cambio no es solo de compilador. La guía oficial documenta cambios en RMT, BLE, Wi-Fi Client y UART. En este proyecto hay que vigilar:

| Área | Uso del proyecto | Riesgo al migrar | Verificación obligatoria |
|---|---|---|---|
| Wi-Fi STA/AP | eventos, `mode`, `disconnect(true,true)`, `softAP`, `begin`, sleep | orden de eventos, init/deinit y reconexión | escenarios AP/STA, cambio de modo, credenciales inválidas y reconexión |
| Portal | WebServer, DNS cautivo y mDNS | cambios en stack Network/WebServer y clientes lentos | probes HTTP/DNS, páginas completas y cambio de canal |
| BLE | servidor, advertising, characteristics | tipos `std::string`/`String` y UUID cambiaron | build separado con BLE habilitado y prueba de cliente real |
| GPS | `HardwareSerial(1)` con pines explícitos | cambios funcionales UART | replay NMEA, pérdida/retorno de fix y puerto físico |
| LEDs | NeoPixel sobre RMT | driver RMT completamente rediseñado | VCD y tiras físicas bajo animación continua |
| Persistencia | Preferences/NVS y registros binarios | compatibilidad de datos existentes | upgrade sin borrar flash, lectura y escritura posterior |
| Wokwi | flags USB específicos para 2.0.16 | workaround puede quedar obsoleto | probar con y sin flags, conservar solo lo necesario |

Hay correcciones posteriores que son relevantes para el patrón del proyecto: Arduino-ESP32 3.3.x corrigió fallos de inicialización/de-inicialización Wi-Fi y problemas asociados a `WiFi.disconnect(true, true)`. Eso refuerza el valor de migrar, pero también exige probar la máquina de estados real del collar.

### DEP-005 — Playwright está duplicado, pero ambos usos son reales

El paquete raíz usa `@playwright/test`; `Platformio/Dog-RGB/docs` usa el paquete `playwright` directamente desde `screenshot.mjs`. No deben consolidarse a ciegas porque cumplen funciones distintas.

Sí deben actualizarse en la misma fase a 1.62.1 para que API, navegador descargado y resultados visuales no queden desalineados. Cada directorio conserva su lockfile.

### DEP-006 — Node y Python deben gestionarse como herramientas, no firmware

- Node 24 es la línea LTS correcta. Se recomienda subir de 24.12.0 al último patch 24.x y declarar `engines.node` o un archivo `.nvmrc`/`.node-version`.
- No conviene saltar a Node 26 Current para automatización estable.
- Python 3.12.10 ejecuta las pruebas y scripts actuales, que usan la librería estándar. PlatformIO 6.1.19 también soporta Python 3.14, pero el upgrade del host no aporta una mejora directa al binario.
- PlatformIO usa un entorno Python aislado; no se deben instalar sus paquetes manualmente en el Python global.

### DEP-007 — La reproducibilidad todavía depende demasiado de la máquina

Las librerías de firmware están fijadas exactamente, lo cual es correcto. Sin embargo, el core de PlatformIO, Node, Wokwi CLI y extensiones no están descritos como requisitos verificables del repositorio.

El plan debe registrar:

- PlatformIO Core mínimo/exacto validado;
- versión LTS de Node;
- Wokwi CLI validado;
- comandos que imprimen el grafo resuelto;
- lockfile de plataforma si la ruta pioarduino lo soporta;
- un snapshot de tamaños de firmware y resultados de pruebas por release.

No se recomienda congelar permanentemente cada herramienta de estación de trabajo. Sí se recomienda documentar el conjunto conocido como bueno y comprobarlo en CI.

### DEP-008 — Archivos de respaldo pueden falsear futuras auditorías

`src/main.cpp.bak` contiene referencias históricas, incluida FastLED, pero no se compila. Debe quedar explícitamente fuera del inventario. En una limpieza posterior conviene mover respaldos históricos fuera de `src` o confiar en Git, para que búsquedas automáticas no reporten dependencias inexistentes.

## 5. Plan de actualización

### Fase 0 — Congelar y medir la línea base

**Objetivo:** poder comparar y revertir cada cambio.

1. Registrar `pio --version`, `pio pkg list`, Node, npm, Playwright y Wokwi CLI.
2. Guardar el tamaño de los dos builds actuales: físico y Wokwi.
3. Ejecutar y registrar las 108 pruebas host actuales.
4. Ejecutar un smoke de Wokwi antes de gastar minutos en la matriz completa.
5. Guardar en un dispositivo físico datos reales de NVS/configuración/tracks para una prueba de upgrade sin erase.
6. Tener a mano el binario anterior y el procedimiento de rollback por USB.

**Salida:** baseline versionado y reproducible; todavía sin cambios funcionales.

### Fase 1 — Actualizaciones pequeñas, una por una

#### 1A. ArduinoJson 7.4.3 — prioridad P0

Cambiar únicamente:

```ini
bblanchon/ArduinoJson@7.4.3
```

Validar:

- builds físico y Wokwi;
- suite host completa;
- carga/guardado de configuraciones válidas;
- JSON incompleto, tipos incorrectos, números extremos y strings numéricos muy largos;
- RAM, flash y warnings.

Después, en un commit separado, reemplazar APIs deprecadas.

#### 1B. Adafruit NeoPixel 1.15.5 — prioridad P0

Cambiar únicamente:

```ini
adafruit/Adafruit NeoPixel@1.15.5
```

Validar:

- efectos y modos existentes;
- dos tiras RGBW;
- timing VCD en Wokwi;
- prueba física de color, parpadeo y temperatura;
- heap mínimo y watchdog durante animación prolongada.

#### 1C. Playwright 1.62.1 — prioridad P1

Actualizar el root y `Platformio/Dog-RGB/docs` por separado, regenerando sus lockfiles y browsers. Ejecutar extracción, screenshots y comparación visual. Las imágenes cambiadas no deben aceptarse automáticamente: primero se explica cada diferencia.

#### 1D. Node 24 LTS — prioridad P2

Actualizar solo el patch 24.x y declarar la versión esperada. No es necesario bloquear la fase de firmware si las pruebas ya funcionan en 24.12.0.

### Fase 2 — Entorno candidato para el framework moderno

No reemplazar inmediatamente `[env:seeed_xiao_esp32s3]`. Crear temporalmente un entorno candidato, por ejemplo `[env:seeed_xiao_esp32s3_next]`, con una versión exacta de pioarduino 55.03.311.

La matriz temporal debe contener:

- **legacy:** plataforma 6.7.0, Arduino 2.0.16;
- **next:** pioarduino 55.03.311, Arduino 3.3.11 / ESP-IDF 5.5.5;
- **wokwi-next:** hereda `next`, con los mínimos flags de simulación realmente necesarios.

Pasos:

1. Compilar sin modificar lógica y capturar todos los errores/warnings.
2. Adaptar APIs incompatibles por subsistema, no con una gran reescritura.
3. Habilitar un build adicional con BLE activo aunque el producto lo tenga apagado por defecto.
4. Revisar los flags USB de Wokwi: el comentario actual documenta un watchdog específico de 2.0.16.
5. Comparar tamaños, heap, tiempo de loop y arranque.
6. Ejecutar la matriz de aceptación descrita abajo.
7. Hacer soak test físico antes de promover el entorno.

### Fase 3 — Promoción y retirada de legacy

Promover `next` a producción solo si:

- no pierde configuraciones ni tracks existentes;
- AP/STA y portal pasan escenarios normales y de fallo;
- GPS mantiene parsing y recuperación;
- LEDs no presentan glitches ni watchdogs;
- BLE compila y funciona cuando se habilita;
- Wokwi sigue siendo útil y estable;
- consumo, temperatura y comportamiento RF no empeoran en hardware.

Conservar legacy durante al menos una release o periodo de uso real. Después eliminarlo para no mantener dos líneas indefinidamente.

### Fase 4 — Mantenimiento periódico

Mensualmente o antes de cada release:

```text
pio pkg outdated
npm outdated
wokwi-cli --version
```

Trimestralmente:

- revisar releases oficiales de Arduino-ESP32, ArduinoJson y NeoPixel;
- revisar la política de soporte de ESP-IDF;
- probar upgrades en un entorno candidato;
- actualizar el baseline de tamaños y pruebas.

No hacer auto-merge de actualizaciones de framework, toolchain, RMT, Wi-Fi o BLE.

## 6. Matriz mínima de aceptación

| Grupo | Pruebas |
|---|---|
| Build | físico, Wokwi, candidato físico, candidato Wokwi y BLE-enabled |
| Host | 108/108 pruebas actuales y cualquier test nuevo |
| AP | inicio, shutdown, timeout, cambio de modo, clientes lentos, múltiples requests |
| STA | credenciales válidas/incorrectas, pérdida y retorno del router, reset completo de Wi-Fi |
| Portal | DNS cautivo, redirects/probes, GET/POST, JSON inválido/extremo, mDNS |
| GPS | NMEA válido, ruido, fix/no-fix, cambio de velocidad, desconexión/reconexión UART |
| LED | dos tiras, RGBW, brillo, todos los modos, VCD y hardware real |
| Persistencia | actualización sin erase, reinicio, compatibilidad NVS y tracks |
| BLE | advertising, conexión, lectura/escritura y desconexión con feature habilitado |
| Recursos | flash, RAM estática, heap mínimo, stack, tiempo máximo de loop, watchdogs |
| Soak | varias horas con LEDs, GPS, cambios Wi-Fi y reinicios controlados |

Wokwi sirve para regresión lógica, serial, UART, eventos y timing visible. No sustituye las pruebas físicas de RF, antena, consumo, temperatura, alimentación ni señal SK6812 real.

## 7. Criterios de rollback

Revertir la actualización candidata si ocurre cualquiera de estos casos:

- pérdida o corrupción de datos persistentes;
- reset, panic o watchdog no presente en baseline;
- AP/STA no recupera después de un fallo razonable;
- pérdida de tramas GPS o latencia que afecte los modos;
- glitch visible de LEDs o incremento importante de tiempo bloqueado;
- regresión de consumo/temperatura relevante para un collar alimentado por batería;
- Wokwi deja de ejecutar los escenarios necesarios y no existe workaround acotado.

El rollback debe volver a un binario y conjunto de dependencias exactos, no simplemente a “la versión anterior” por nombre.

## 8. Decisión recomendada

**Objetivo final recomendado:** Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5 mediante pioarduino 55.03.311, después de actualizar y validar las librerías externas por separado.

**Alternativa conservadora:** PlatformIO oficial 7.0.1 / Arduino 2.0.17. Reduce un poco el atraso, pero conserva ESP-IDF 4.4.7 fuera de soporte y debe tratarse como puente temporal.

No se recomienda migrar ahora el proyecto completo a ESP-IDF nativo ni cambiar de sistema de build. Para este collar DIY, el coste y riesgo no justifican esa reescritura. La meta es una base Arduino moderna, mantenible y comprobada en el hardware real.

## 9. Investigaciones y fuentes oficiales

1. [PlatformIO Espressif32 7.0.1](https://github.com/platformio/platform-espressif32/releases/tag/v7.0.1) — versión oficial más reciente de la plataforma.
2. [Manifiesto de PlatformIO Espressif32 7.0.1](https://raw.githubusercontent.com/platformio/platform-espressif32/v7.0.1/platform.json) — demuestra que Arduino sigue en `~3.20017.0` y toolchain 8.4.0.
3. [Manifiesto de la plataforma actualmente usada, 6.7.0](https://raw.githubusercontent.com/platformio/platform-espressif32/v6.7.0/platform.json) — resolución de Arduino 2.0.16.
4. [Arduino-ESP32 3.3.11](https://github.com/espressif/arduino-esp32/releases/tag/3.3.11) — core oficial actual y base ESP-IDF 5.5.5.
5. [Guía oficial de migración Arduino-ESP32 2.x a 3.0](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html) — cambios incompatibles de RMT, BLE, Wi-Fi y UART.
6. [pioarduino 55.03.311](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.311) — empaquetado PlatformIO del core Arduino oficial actual.
7. [Manifiesto pioarduino 55.03.311](https://raw.githubusercontent.com/pioarduino/platform-espressif32/55.03.311/platform.json) — versiones exactas de framework, toolchains y herramientas.
8. [Historial ArduinoJson 7](https://arduinojson.org/v7/revisions/) — correcciones y cambios desde 7.2.1.
9. [ArduinoJson 7.4.3](https://github.com/bblanchon/ArduinoJson/releases/tag/v7.4.3) — corrección del buffer overrun y recomendación de actualización.
10. [Releases Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel/releases) — cambios RMT, thread safety y versión 1.15.5.
11. [Implementación ESP32 de NeoPixel 1.15.5](https://raw.githubusercontent.com/adafruit/Adafruit_NeoPixel/1.15.5/esp.c) — ruta IDF5, buffer heap y mutex.
12. [Playwright 1.62.1](https://github.com/microsoft/playwright/releases/tag/v1.62.1) — versión actual de la herramienta de navegador.
13. [Estado de releases Node.js](https://nodejs.org/en/about/previous-releases) — Node 24 como línea LTS y Node 26 como Current.
14. [Historial PlatformIO Core](https://docs.platformio.org/en/stable/core/history.html) — 6.1.19 y soporte de Python moderno.
15. [Releases Wokwi CLI](https://github.com/wokwi/wokwi-cli/releases) — 0.26.1 como CLI actual.
16. [Política de soporte de ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/versions.html) — periodo de soporte de releases.
17. [Roadmap oficial ESP-IDF](https://github.com/espressif/esp-idf/blob/master/ROADMAP.md) — estado de mantenimiento de las ramas 5.x.

## 10. Resultado esperado en la vida real

Si se ejecuta este plan, el collar gana:

- menos riesgo de bloqueos por Wi-Fi al cambiar entre AP y STA;
- mejor recuperación después de desconexiones o reinicialización del radio;
- menor riesgo de corrupción o crash al recibir JSON extraño desde el portal;
- driver LED compatible con ESP-IDF 5, con mejor manejo de memoria y concurrencia;
- toolchain y framework con correcciones vigentes;
- actualizaciones futuras más pequeñas porque existe una matriz y un entorno candidato;
- rollback claro si una versión nueva funciona en simulación pero falla en el collar físico.

La actualización no pretende añadir complejidad de producto ni seguridad bancaria. Busca que un dispositivo DIY siga siendo fácil de usar, pero sea más difícil de colgar, corromper o dejar obsoleto por su propia base técnica.
