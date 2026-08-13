# Ejecución de la actualización de dependencias

> **Document status:** Historical execution record (Spanish). It records evidence from 2026-08-01; use [Testing and simulation](testing.md) and the active manifests for current validation.

**Fecha:** 2026-08-01

**Estado:** actualización de repositorio completada y validada en build, pruebas host, navegador y Wokwi. Queda como validación externa la prueba sobre el collar físico.

## Versiones finales

| Componente | Antes | Después |
|---|---:|---:|
| Plataforma ESP32 | PlatformIO Espressif32 6.7.0 | pioarduino 55.03.311, URL exacta |
| Arduino-ESP32 | 2.0.16 | 3.3.11 |
| ESP-IDF | 4.4.x | 5.5.5 |
| Toolchain Xtensa | 8.4.0 | 14.2.0 + 20260121 |
| esptool | 4.5.1 | 5.3.0 |
| GDB ESP32 | toolchain antiguo | esp-gdb 17.1 |
| ArduinoJson | 7.2.1 | 7.4.3 |
| Adafruit NeoPixel | 1.12.3 | 1.15.5 |
| Playwright | 1.59.1 resuelto | 1.62.1 exacto |
| Node esperado | no declarado | 24.18.0, major 24 LTS |
| PlatformIO Core | 6.1.19 | 6.1.19, actual |
| Wokwi CLI | 0.26.1 | 0.26.1, actual |

`pio pkg outdated` y `npm outdated` terminan sin actualizaciones pendientes.

## Cambios de compatibilidad

- La plataforma queda fijada a la release exacta de pioarduino; no usa el alias móvil `stable`.
- ArduinoJson usa `JsonDocument` y acceso null-aware en lugar de APIs deprecadas.
- La redirección local de logs conserva y restaura el macro `Serial`, compatible con Arduino 2.x y 3.x.
- El target Wokwi mantiene UART0 aislada del USB CDC de producción.
- El helper Wokwi reintenta únicamente cierres transitorios del transporte/API. Las assertions del firmware fallan inmediatamente.
- El servidor de preview usa `fileURLToPath()`, por lo que funciona correctamente en Windows y POSIX.
- Los selectores Playwright se alinearon con el DOM real actual del portal.
- La configuración GDB apunta a `tool-xtensa-esp-elf-gdb` 17.1 y a su ejecutable independiente de la versión Python.

## Resultados verificados

### Firmware

| Target | RAM | Flash | Resultado |
|---|---:|---:|---|
| `seeed_xiao_esp32s3` | 56,556 bytes, 17.3% | 1,138,963 bytes, 34.1% | PASS |
| `wokwi` | 56,476 bytes, 17.2% | 1,138,351 bytes, 34.1% | PASS |

La subida de flash frente al baseline es de aproximadamente 170 KiB y todavía deja alrededor del 66% de la partición de aplicación libre. La RAM estática aumentó unos 2.5 KiB.

### Pruebas automatizadas

- Pruebas host Python: **108/108 PASS**.
- Playwright móvil: **15/15 PASS** con Chromium 151 descargado por Playwright 1.62.1.
- Wokwi lint: sin errores; conserva un único aviso informativo por el tipo de placa XIAO no documentado públicamente.
- GDB 17.1: ejecutable localizado y `--version` correcto.

### Matriz Wokwi

| Escenario | Resultado | Evidencia principal |
|---|---|---|
| boot | PASS | AP, GPS, movimiento, LED, UART y VCD |
| gps-faults | PASS | checksum, parser, calidad, stale y recuperación |
| gps-profiles | PASS | fix válido, no-fix, HDOP y recuperación |
| gps-rate-ranges | PASS | 5 Hz, 220 NMEA válidas, cero overflow y salto rechazado |
| loop-diagnostics | PASS | límites de latencia, heap y radio |
| modes | PASS | speed, simple, show, geofence, día y persistencia |
| session-persistence | PASS | dos reinicios, historial exacto y recuperación transaccional |
| speed-validity | PASS | pico imposible rechazado y recuperación posterior |

Los ocho archivos `*.analysis.json` reportan `pass: true`, cero fatal markers y cero overflow UART. El heap mínimo observado fue 234,488 bytes en el escenario de modos.

## Comparación de recursos

Baseline físico con Arduino 2.0.16:

- RAM: 54,092 bytes, 16.5%.
- Flash: 968,301 bytes, 29.0%.

Resultado con Arduino 3.3.11:

- RAM: 56,556 bytes, 17.3%.
- Flash: 1,138,963 bytes, 34.1%.

La diferencia es aceptable para la partición actual. Debe seguir vigilándose en futuras actualizaciones porque BLE, Network/WebServer y las nuevas librerías IDF incorporan más código que la rama anterior.

## Validación física pendiente

La simulación no puede certificar RF, alimentación ni el dispositivo real. Antes de declarar una release de campo deben comprobarse:

1. carga por USB y monitor CDC en la XIAO ESP32-S3;
2. arranque con NVS existente sin borrar flash;
3. conexión del teléfono al AP y cambio AP/STA con un router real;
4. señal y color RGBW de ambas tiras SK6812;
5. UART del EBYTE E108-GN02 y recuperación de fix;
6. consumo, temperatura, brownout y varias horas de uso;
7. BLE en hardware si se habilita esa feature.

No existe una forma responsable de sustituir estas pruebas físicas con Wokwi. El código, dependencias y simulación sí quedan terminados y reproducibles.
