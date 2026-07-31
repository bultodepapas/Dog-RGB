# Dog-RGB Firmware

Firmware GPS-first para collar Dog-RGB en Seeed Studio XIAO ESP32-S3.

## Objetivo

- Leer GNSS por UART y calcular distancia, tiempo activo, velocidad promedio y velocidad maxima.
- Guardar resumen diario, historial corto de sesiones y puntos de ruta GPS.
- Servir portal local por Wi-Fi para dashboard, configuracion, diagnostico y export de rutas.
- Manejar tiras SK6812 RGBW como UI visual, con LEDs reservados para estado/alertas.

## Hardware

- MCU: Seeed Studio XIAO ESP32-S3.
- GNSS: EBYTE E108-GN02 a 9600 baud.
- LEDs: SK6812 RGBW.
- LED A data: D0 / GPIO1.
- LED B data: D1 / GPIO2.
- Status LED placa: D2 / GPIO3.
- GNSS TX/RX: D6/GPIO43 y D7/GPIO44.

## Modos LED

El modo visual principal se configura desde el portal:

- `speed`: efectos por rango de velocidad GPS.
- `geofence`: efectos por distancia al Home.
- `simple`: un efecto fijo para toda la tira.
- `show`: demo automatica de efectos.

Los primeros `LED_STATUS_COUNT` LEDs de cada tira estan reservados para estado Wi-Fi/GPS/error. Con `LED_STRIP_MODE = 2` y `LED_STATUS_COUNT = 2`, esto equivale a 4 LEDs fisicos de alerta.

## Modo DIA

Modo DIA es una compuerta de ahorro de bateria activable desde `/config`. No reemplaza `speed`, `geofence`, `simple` ni `show`.

Cuando esta activado y hay hora GPS confiable:

- De 06:00 a antes de 16:00 hora local, apaga solo los LEDs de efectos del cuerpo.
- Los 4 LEDs de alerta/estado siguen funcionando.
- El rastreo GPS, metricas, rutas, Wi-Fi y portal siguen funcionando.
- Las luces de bienvenida de arranque siguen funcionando antes de aplicar la compuerta DIA.

Si no hay fecha/hora GPS confiable, Modo DIA queda en `waiting_time` y no apaga efectos para evitar apagados falsos.

La hora local se calcula desde RMC GPS, asumiendo `DAY_MODE_TZ_OFFSET_MIN = -300` para America/Bogota. Los detalles tecnicos estan en [docs/modo-dia.md](docs/modo-dia.md).

## Portal HTTP

Endpoints principales:

- `GET /`: dashboard.
- `GET /config`: configuracion de modo, brillo, Modo DIA, GPS, geocerca y efectos.
- `GET /dev`: diagnostico tecnico.
- `GET /api/status`: estado resumido para dashboard.
- `GET /api/config`: configuracion persistente actual.
- `POST /api/config`: guarda configuracion.
- `POST /api/mode`: cambia solo el modo visual.
- `GET /api/summary`: resumen diario/sesiones.
- `GET /api/track.json`, `/api/track.csv`, `/api/track.geojson`: export de ruta.

## Build Y Verificacion

```powershell
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -e seeed_xiao_esp32s3
python -m unittest test.test_day_mode_static -v
```

`platformio.ini` define el entorno `seeed_xiao_esp32s3` con Arduino framework, ArduinoJson y Adafruit NeoPixel.

## Notas De Persistencia

La configuracion runtime se guarda en NVS mediante `Preferences`. La version actual de configuracion es `5`; la migracion desde versiones anteriores conserva modos, rangos, efectos, Wi-Fi y GPS, y deja Modo DIA desactivado por defecto.

El historial de ruta usa la particion NVS dedicada `tracknvs` (192 KiB). Conserva las ultimas 2 horas (1.440 puntos a intervalos de 5 s) y reescribe el chunk parcial cada 15 s para limitar la perdida ante un reinicio. El formato v2 protege metadata y chunks con CRC-32/IEEE; al exportar se rechazan chunks truncados, alterados o con coordenadas/horas invalidas sin perder los otros chunks validos. La primera carga del esquema/formato dedicado inicia un historial de ruta nuevo; la configuracion y las credenciales permanecen en la particion NVS principal.

Para instalar esta version usa el upload completo de PlatformIO (`pio run -e seeed_xiao_esp32s3 -t upload`) para grabar tambien `partitions_dog_rgb.csv`; copiar solamente `firmware.bin` no instala la nueva particion de historial.
