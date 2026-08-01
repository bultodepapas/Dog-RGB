# Wokwi: simulacion, pruebas y debug de Dog-RGB

Esta simulacion ejecuta el firmware real del collar para la XIAO ESP32-S3. El
codigo de produccion sigue siendo la verdad: Wokwi aporta entradas GNSS
controlables, visualizacion, automatizacion, formas de onda y GDB, pero no
reemplaza las pruebas electricas del prototipo.

## Estado validado (2026-07-31)

- Compilan tanto `seeed_xiao_esp32s3` como `wokwi` con PlatformIO.
- El chip GNSS personalizado compila a WASM con Wokwi CLI 0.26.1.
- El linter no reporta errores; solo informa que el tipo
  `board-xiao-esp32-s3` es `undocumented`, aunque el backend lo simula.
- Hay 82 pruebas host verdes para contratos de firmware y activos Wokwi.
- El escenario `boot` valida firmware, AP, fix confiable, movimiento y buses
  LED/GNSS.
- El escenario `modes` validó speed, simple, show, geofence, modo dia y
  persistencia tras reinicio.
- La matriz GNSS validó no-fix, HDOP, pocos satelites, ausencia de GGA/RMC,
  checksum incorrecto, sentencias malformadas, pico de velocidad, silencio y
  recuperacion.
- `gps-rate-ranges` está validado a 5 Hz: 239 sentencias vistas por firmware,
  cero overflow, rangos 1/2/7/10 correctos y un salto de 743.9 m rechazado. El
  VCD independiente decodifico 17,595 bytes, 244 sentencias NMEA validas y
  frecuencia maxima de 5 Hz.
- La matriz final de fallos produjo un VCD completo de 43.0 s: 54 sentencias
  validas, 8 invalidas esperadas, tick GNSS estable a 1 Hz, cero overflow y
  recuperacion final confirmada.
- `speed-validity` valida especificamente que un pico de 80 km/h conserve
  `usable=0` despues del filtrado, no sume actividad/distancia, use el rango
  seguro 1 y recupere `usable=1`, 12 km/h y rango 7. Su VCD de 17.0 s contiene
  32 sentencias validas, ninguna invalida y tick estable a 1 Hz.
- `loop-diagnostics` atribuye latencia a GPS, control, geofence, persistencia,
  radio, LED, HTTP, formateo y drenaje del logger. Detecto consultas Wi-Fi
  sincronas redundantes de clientes, canal, IP AP y modo. El conteo paso a
  eventos, canal/IP/modo se cachean en sus transiciones y el logger usa una cola
  fija sin heap. La corrida final midio 50 us de radio, 1,894 us de formateo,
  83 us de drenaje, 76,519 us de trabajo maximo y cero bytes descartados.

## Circuito simulado

- Seeed XIAO ESP32-S3 con 8 MB flash y 8 MB PSRAM octal.
- Dos tiras virtuales WS2812 de 24 pixeles en D0/GPIO1 y D1/GPIO2.
- LED de estado en D2/GPIO3.
- GNSS NMEA personalizado a 9600 baud conectado a D7/GPIO44.
- Consola de simulacion UART0 en D9/GPIO8 y D10/GPIO9. D6/D7 quedan
  disponibles para el receptor, igual que en el diseño.
- Analizador logico de cinco canales: ambos buses LED, UART GNSS, tick del chip
  GNSS y LED de estado.
- Portal HTTP redirigido a `http://localhost:8180` cuando se usa el Private IoT
  Gateway.
- Servidores RFC2217 en puerto 4000 y GDB en puerto 3333.

La compilacion fisica conserva USB CDC y refresco LED de produccion. El target
Wokwi es el unico que usa UART0 cableada y limita el transporte visual WS2812 a
5 FPS; el calculo interno de efectos conserva su periodo real de 50 ms.

## Preparacion

Requisitos:

1. PlatformIO Core.
2. Wokwi CLI oficial (el helper tambien busca
   `%USERPROFILE%\.wokwi\bin\wokwi-cli.exe`).
3. Extension Wokwi para VS Code y licencia activa para uso interactivo.
4. `WOKWI_CLI_TOKEN` para escenarios CLI. Puede existir en el proceso o en un
   `.env` local ignorado por Git. Nunca debe versionarse.

Desde `Platformio/Dog-RGB`:

```powershell
.\tools\wokwi.ps1 -Action prepare
```

Esto recompila el chip a WASM, construye `[env:wokwi]` y ejecuta el linter
oficial. Para comprobar tambien la imagen fisica:

```powershell
pio run -e seeed_xiao_esp32s3
python -m unittest discover -s test -p "test_*.py"
```

## Uso interactivo

1. Ejecuta `prepare`.
2. Abre `diagram.json` en VS Code.
3. Ejecuta `Wokwi: Start Simulator`.
4. Observa las tiras, el LED de estado, el monitor serie y `Chips Console`.
5. Selecciona `gnss` para cambiar perfil, velocidad, tasa, hora o posicion sin
   recompilar.

El chip GNSS expone estos controles:

| Control | Rango | Objetivo |
|---|---:|---|
| `profile` | 0..10 | Elegir condicion normal o fallo |
| `speedKph` | 0..40 | Cruzar umbrales de los diez rangos LED |
| `rateHz` | 1..5 | Estresar UART/parser y comprobar overflow |
| `utcHour` | 0..23 | Validar hora local UTC-5 y modo dia |
| `positionM` | 0..900 | Forzar distancia/geofence o un salto GNSS |

Perfiles:

| ID | Emision | Funcion validada |
|---:|---|---|
| 0 | GGA+RMC validos en movimiento | Flujo normal y recuperacion |
| 1 | Fix valido detenido | Reposo y rango 1 |
| 2 | RMC `V` + GGA sin fix | Perdida de fix |
| 3 | HDOP 9.9 | Rechazo por calidad |
| 4 | Silencio UART | Cable/receptor ausente y datos stale |
| 5 | Solo 2 satelites | Umbral de satelites |
| 6 | Solo RMC | Expiracion de GGA |
| 7 | Solo GGA | Expiracion de RMC |
| 8 | Checksum incorrecto | Rechazo de transporte NMEA |
| 9 | Campos malformados con checksum valido | Rechazo del parser |
| 10 | Velocidad de 80 km/h | Filtro de pico imposible para el collar |

El reloj y la posicion progresan de forma coherente con `rateHz` y
`speedKph`. `DEBUG` cambia de nivel en cada ciclo del scheduler, incluso cuando
el perfil 4 no transmite NMEA. `Chips Console` informa cambios de atributos y
diagnosticos acumulados (`cycles`, `frames`, `bytes`, `busy_drops`).

## Control del firmware por consola

Los escenarios usan la accion oficial `write-serial`. En una sesion
interactiva se pueden enviar las mismas lineas:

```text
sim mode speed
sim mode geofence
sim mode show
sim mode simple
sim day on
sim day off
sim home here
sim home clear
sim leds off
sim leds on
sim status
sim reboot
sim help
```

Los cambios de modo/dia y home pasan por las funciones reales de configuracion
y NVS; no modifican variables paralelas de prueba. `sim reboot` usa
`ESP.restart()` y permite comprobar persistencia. `sim leds off` solo detiene
la transmision fisica virtual: los efectos, rangos y logs siguen calculandose.
Se usa en suites GNSS para acelerar CI; no esta disponible en el firmware
fisico.

## Escenarios automatizados

```powershell
# Smoke test electrico/funcional completo
.\tools\wokwi.ps1 -Action test -Scenario wokwi/boot.test.yaml

# Modos, geofence, dia y persistencia
.\tools\wokwi.ps1 -Action test -Scenario wokwi/modes.test.yaml -TimeoutMs 90000

# Calidad basica y recuperacion
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-profiles.test.yaml -TimeoutMs 60000

# Matriz de transporte/parser/calidad
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-faults.test.yaml -TimeoutMs 90000

# Regresion corta: pico de velocidad rechazado y recuperacion
.\tools\wokwi.ps1 -Action test -Scenario wokwi/speed-validity.test.yaml -TimeoutMs 25000

# Atribucion de latencia por subsistema
.\tools\wokwi.ps1 -Action test -Scenario wokwi/loop-diagnostics.test.yaml -TimeoutMs 20000

# 5 Hz, umbrales de velocidad y salto de posicion
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-rate-ranges.test.yaml -TimeoutMs 60000

# Todas, con una sola preparacion
.\tools\wokwi.ps1 -Action suite -TimeoutMs 90000
```

`-CaptureProfile auto` es el valor por defecto:

- `boot.test` usa `full`: registra LEDs, GNSS y estado con un buffer de un
  millon de cambios.
- Los escenarios largos usan `gnss`: un script genera un diagrama desde el
  canonico, desconecta DIN/los canales LED del analizador y deja 250,000
  muestras para UART/tick.
- Se puede forzar `-CaptureProfile full` o `gnss` al investigar un caso.

Esta separacion es deliberada. Un unico VCD con las dos tiras a 20 FPS lleno el
buffer predeterminado cerca de 20 s y truncó el final de la prueba; ampliarlo a
tres millones hizo que 14 s virtuales no terminaran en cinco minutos. Dos
analizadores tampoco resolvieron el problema: el CLI 0.26.1 exporto solo el
primero. Los perfiles generados conservan un solo `diagram.json` como fuente de
verdad y evitan duplicacion/drift.

Cada escenario produce:

- `artifacts/<escenario>.serial.log`;
- `artifacts/<escenario>.vcd`;
- `artifacts/<escenario>.analysis.json`;
- `artifacts/<escenario>.diagram.json` (vista generada).

`tools/analyze_wokwi.py` falla si encuentra crash, error de control, overflow,
señales necesarias ausentes o UART indecodificable. Resume heap minimo, loop
maximo total, trabajo sin logger, costo del logger, maximos por subsistema,
operaciones Wi-Fi, modos/renders, validez de velocidad, razones de segmento/fix
y anomalias. Del VCD decodifica UART
9600-8N1, verifica checksum NMEA, mide tasa GNSS, cuenta transiciones de estado
y estima rafagas WS2812 en el perfil completo.

Los contadores `small_seg_total` y `large_seg_total` son acumulativos. Esto
evita perder saltos breves entre reportes de 2 s, especialmente a 5 Hz.

Para `loop-diagnostics`, el analizador tambien convierte la investigacion en
regresion: falla si trabajo supera 120 ms, formateo 60 ms, drenaje 5 ms, radio
10 ms, o las consultas de clientes/canal 5 ms. `[SYS]` publica ademas maximos
por ranura, cola pendiente y bytes descartados. Estos limites son de simulacion
y no sustituyen mediciones en el ESP32 fisico.

## Logs para analizar el chip y el firmware

Etiquetas principales:

- `[BOOT]`: causa de reset y funciones activas.
- `[GPS_LINK]`: bytes/sentencias/deltas, checksum, parser, pico, stale,
  overflow y edad de cada stream.
- `[GPS_FIX]`: raw/trusted/current y razon exacta de rechazo.
- `[GPS_ANOMALY]`: anomalias observadas desde el reporte anterior.
- `[MOTION]`: modo, velocidad filtrada, validez separada (`usable`), rango,
  distancia, segmento y contadores de
  rechazo.
- `[LED]`: decision de render, efecto, velocidad, intensidad y modo dia.
- `[SYS]`: uptime, heap/min-heap, latencia total, trabajo sin logger, costo del
  logger y maximos de GPS/control/geofence/storage/radio/LED/HTTP.
- `[WIFI]` / `[WIFI_DIAG]`: estado de radios, clientes y transiciones.
- `[SIM_CTRL]` / `[SIM_STATE]`: comandos y estado inducido por automatizacion.
- `[nmea-gps]` en Chips Console: scheduler y transporte del modelo WASM.

Para buscar regresiones conviene comparar el JSON, no textos visuales. Los
valores clave son `max_uart_overflow=0`, ausencia de `fatal_markers`, heap
minimo estable, loop maximo acotado y conteos NMEA coherentes con `rateHz`.

## GDB en VS Code

`wokwi.toml` abre el servidor en `localhost:3333`. Instala la extension C/C++
de Microsoft y copia/combina
`.vscode/wokwi-gdb.launch.example.json` en `.vscode/launch.json` (este ultimo es
local e ignorado por Git).

1. Ejecuta `prepare` para generar `firmware.elf` con simbolos.
2. Inicia Wokwi y espera a que el firmware este corriendo.
3. Pulsa F5 y selecciona `Wokwi ESP32-S3 GDB`.
4. Coloca breakpoints en `gps.cpp`, `led_ui.cpp`, `wifi_mgr.cpp` o
   `wokwi_control.cpp`; inspecciona variables, backtrace y memoria.

Un breakpoint detiene el MCU simulado. Los timers del chip personalizado y las
conexiones externas pueden seguir teniendo estado, por lo que despues de una
pausa larga conviene reiniciar el escenario antes de interpretar edades/stale.

## Portal, red y captura de trafico

Con `Wokwi: Enable Private Wokwi IoT Gateway`, el forwarding configurado
expone el portal en `http://localhost:8180`. El gateway publico permite salida
del ESP32, pero no conexiones entrantes al portal. Para debug de protocolos el
gateway privado puede generar una captura PCAP desde los comandos de Wokwi;
Wireshark permite verificar DNS/HTTP y tiempos de conexion. No se deben poner
credenciales reales en una simulacion compartida.

RFC2217 en el puerto 4000 permite conectar herramientas compatibles al monitor
serie. El puerto 3333 queda reservado para GDB.

## Analizador logico

El diagrama canonico usa:

- `logic.D0`: datos de tira A;
- `logic.D1`: datos de tira B;
- `logic.D2`: TX del GNSS;
- `logic.D3`: tick del scheduler GNSS;
- `logic.D4`: LED de estado.

El buffer se mide en cambios de nivel, no en tiempo; cada muestra consume 9
bytes según la documentacion oficial. Para inspeccion manual abre el VCD con
PulseView o GTKWave. En PulseView usa UART 9600-8N1 para D2 y un decoder WS2812
para D0/D1. Un downsampling de 50 (20 MHz) suele bastar para UART y WS2812; no
lo uses para calcular checksums con el analizador automatizado, que lee el VCD
original.

## Que Wokwi no demuestra

- La tira virtual es WS2812 RGB de 24 bits; no reproduce el canal blanco ni el
  orden RGBW exacto de SK6812.
- No modela corriente real, boost, bateria, caida de tension, nivel logico,
  calentamiento, ESD, agua, flexion ni conectores.
- No simula propagacion RF GNSS, antena, multipath o interferencia mecanica.
- Wi-Fi virtual sirve para protocolos/estado, no para consumo, alcance o
  coexistencia RF representativa.
- GDB, logs y VCD cambian la velocidad de simulacion; son instrumentos, no una
  medicion de tiempo real del hardware.

Por tanto, Wokwi cubre integracion de firmware, parser, estados, persistencia,
automatizacion y señales digitales. El prototipo sigue siendo obligatorio para
potencia, SK6812 RGBW, RF y robustez mecanica.

## Documentacion oficial consultada

1. [Wokwi CLI Usage](https://docs.wokwi.com/wokwi-ci/cli-usage)
2. [Automation Scenarios](https://docs.wokwi.com/wokwi-ci/automation-scenarios)
3. [Project Configuration (`wokwi.toml`)](https://docs.wokwi.com/vscode/project-config)
4. [VS Code Getting Started](https://docs.wokwi.com/vscode/getting-started)
5. [Debugging with GDB in VS Code](https://docs.wokwi.com/vscode/debugging)
6. [Advanced GDB Debugging](https://docs.wokwi.com/gdb-debugging)
7. [Logic Analyzer Guide](https://docs.wokwi.com/guides/logic-analyzer)
8. [Logic Analyzer Part Reference](https://docs.wokwi.com/parts/wokwi-logic-analyzer)
9. [ESP32 Wi-Fi and Private Gateway](https://docs.wokwi.com/guides/esp32-wifi)
10. [Custom Chips Getting Started](https://docs.wokwi.com/chips-api/getting-started)
11. [Custom Chip Attributes API](https://docs.wokwi.com/chips-api/attributes)
12. [Custom Chip UART API](https://docs.wokwi.com/chips-api/uart)
13. [Custom Chip Time API](https://docs.wokwi.com/chips-api/time)

Las APIs de escenarios y chips aun pueden evolucionar. Si una version nueva
del CLI cambia atributos o exportacion VCD, el linter y los 86 tests de activos
deben detectarlo antes de aceptar la actualizacion. El rollover de `millis()` se
valida deterministicamente con pruebas host y aserciones de compilacion alrededor
de `UINT32_MAX`; no hace falta consumir 49,7 dias de simulacion para probarlo.
