# Simulacion Wokwi

La simulacion reproduce el flujo funcional del collar sin introducir caminos
especiales en el firmware: se compila el mismo codigo, la misma tabla de
particiones y las mismas librerias que para la XIAO fisica.

## Que simula

- Seeed XIAO ESP32-S3, 8 MB flash y 8 MB PSRAM octal.
- Dos tiras WS2812 de 24 pixeles en D0/GPIO1 y D1/GPIO2.
- LED de estado en D2/GPIO3.
- GNSS NMEA personalizado a 9600 baud en D7/GPIO44.
- Wi-Fi virtual y portal HTTP del firmware.
- Analizador logico: D0=`LED-A`, D1=`LED-B`, D2=`GNSS-TX`.
- Consola serie UART0 en D9/GPIO8 y D10/GPIO9, solo para el target Wokwi.
  D6/D7 quedan dedicados al GNSS.

Wokwi documenta oficialmente la [XIAO ESP32-S3 y sus interfaces serie](https://docs.wokwi.com/guides/esp32),
las [tiras WS2812](https://docs.wokwi.com/parts/wokwi-led-strip), el
[formato de `wokwi.toml`](https://docs.wokwi.com/vscode/project-config), los
[chips personalizados](https://docs.wokwi.com/guides/custom-chips-to-wasm) y el
[analizador logico](https://docs.wokwi.com/guides/logic-analyzer).

## Requisitos

1. PlatformIO Core.
2. Wokwi CLI 0.20 o posterior desde el
   [repositorio oficial](https://github.com/wokwi/wokwi-cli/releases). En
   Windows, el helper tambien busca
   `%USERPROFILE%\.wokwi\bin\wokwi-cli.exe`.
3. Para ejecucion interactiva: extension Wokwi para VS Code y licencia
   activada segun la [guia oficial](https://docs.wokwi.com/vscode/getting-started).
4. Para pruebas CLI: token personal creado en
   <https://wokwi.com/dashboard/ci> y expuesto como `WOKWI_CLI_TOKEN`. Nunca se
   guarda en el repositorio.

```powershell
[Environment]::SetEnvironmentVariable('WOKWI_CLI_TOKEN', '<TOKEN>', 'User')
```

Hay que abrir una terminal nueva despues de definir una variable de usuario.
Como alternativa local, el helper carga `WOKWI_CLI_TOKEN` desde `.env`; este
archivo esta excluido por Git y una variable del proceso siempre tiene
prioridad sobre el archivo.

## Preparar y abrir el simulador

Desde `Platformio/Dog-RGB`:

```powershell
.\tools\wokwi.ps1 -Action prepare
```

El comando realiza tres verificaciones encadenadas:

1. Recompila `chips/nmea-gps.chip.c` a WASM.
2. Compila PlatformIO con el entorno `wokwi` esperado por `wokwi.toml`.
3. Ejecuta el linter oficial sobre `diagram.json`.

Despues, abre `diagram.json` en VS Code y ejecuta `Wokwi: Start Simulator`.
El monitor serie aparece siempre. La bienvenida LED se ejecuta al arrancar y,
tras obtener fix GNSS, las tiras reflejan el rango de velocidad configurado.

## Perfiles GNSS interactivos

Selecciona el chip verde `NMEA GPS simulator` durante la simulacion. Sus dos
controles son `profile` y `speedKph`.

| Perfil | Comportamiento | Uso |
|---:|---|---|
| 0 | Fix valido y movimiento continuo a `speedKph` | Efectos por velocidad, distancia, ruta y tiempo activo |
| 1 | Fix valido, posicion y velocidad en cero | Estado de reposo |
| 2 | RMC `V` y GGA sin fix | Perdida de fix del receptor |
| 3 | RMC valido pero HDOP 9.9 | Rechazo por mala calidad |
| 4 | Silencio UART | Cable suelto, receptor apagado y expiracion por datos stale |

El modelo emite GGA y RMC a 1 Hz, calcula el checksum de cada sentencia, mantiene
hora/fecha monotona y mueve la posicion de forma coherente con la velocidad. La
trayectoria invierte suavemente su direccion para poder ejecutar simulaciones
largas sin saltos artificiales.

## Pruebas automatizadas

La sintaxis de escenarios sigue la documentacion oficial de
[Wokwi Automation Scenarios](https://docs.wokwi.com/wokwi-ci/automation-scenarios).

```powershell
# Arranque, GNSS valido, AP y telemetria de movimiento
.\tools\wokwi.ps1 -Action test -Scenario wokwi/boot.test.yaml

# Fix valido -> sin fix -> HDOP malo -> recuperacion
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-profiles.test.yaml -TimeoutMs 45000
```

Los logs quedan en `artifacts/*.serial.log`. Tambien se puede validar solo el
diagrama, sin token:

```powershell
.\tools\wokwi.ps1 -Action lint
```

## Portal HTTP

`wokwi.toml` reenvia el puerto del portal a <http://localhost:8180>. Para que
sea accesible se necesita el Private IoT Gateway, porque el gateway publico no
acepta conexiones entrantes. Activa `Wokwi: Enable Private Wokwi IoT Gateway`
antes de iniciar la simulacion. Esto sigue la guia oficial de
[Wi-Fi ESP32](https://docs.wokwi.com/guides/esp32-wifi).

## Analizador logico

Al detener la simulacion, Wokwi guarda `artifacts/wokwi.vcd`. Los canales son:

- D0: datos de tira A.
- D1: datos de tira B.
- D2: UART TX del GNSS.

El archivo puede abrirse con PulseView o GTKWave. Para UART usa decodificacion
9600 8N1; para las tiras usa un decoder WS2812 si esta disponible.

## Limites conocidos

- La tira visual de Wokwi es WS2812 RGB de 24 bits. Valida temporizacion,
  actualizaciones y efectos, pero no el canal blanco ni el orden RGBW de la
  SK6812 fisica.
- No modela caida de tension, consumo, bateria, nivel logico, RF GNSS, antena,
  temperatura ni interferencia mecanica del collar.
- El LED discreto se representa funcionalmente; el valor de resistencia y la
  corriente deben comprobarse en hardware.
- El linter de Wokwi CLI 0.26.1 informa `board-xiao-esp32-s3` como parte
  “undocumented”, aunque la placa aparece en la lista oficial de hardware y se
  simula. Es un mensaje informativo, no un error ni una advertencia.

La simulacion es una prueba de integracion del firmware. Las verificaciones de
alimentacion, SK6812 RGBW y sensibilidad GNSS siguen requiriendo el prototipo.

### Nota sobre la consola del target Wokwi

La placa fisica conserva USB Serial/JTAG CDC. Solo `[env:wokwi]` desactiva CDC
al compilar contra Arduino-ESP32 2.0.16: en Wokwi esa version entra en watchdog
dentro de `hw_cdc_isr_handler()` antes de ejecutar `setup()`. El macro
`DOG_RGB_WOKWI_SIM` mueve UART0 a los pines libres D9/D10 y el diagrama conecta
alli `$serialMonitor`; la compilacion fisica no define el macro y mantiene su
consola USB y su pinout original.
