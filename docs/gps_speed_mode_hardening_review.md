# Informe: GPS, Modo Velocidad y Plan de Hardening

Fecha: 2026-05-06  
Proyecto: Dog-RGB / ESP32-S3 / XIAO ESP32-S3  
Firmware revisado: `Platformio/Dog-RGB`

## Resumen Ejecutivo

El firmware actual tiene una base correcta para lectura GPS por UART, parsing de RMC/GGA, filtros por calidad y uso del modo velocidad para seleccionar rangos LED. La arquitectura general es coherente: `gps::tick()` consume NMEA, `gps.cpp` calcula estado/metricas, y `led_ui.cpp` usa `gps::has_fix()` + `gps::last_speed_kph()` para pintar el modo velocidad.

Los riesgos principales estan en robustez, no en concepto:

- El fix GPS puede quedar pegado si se pierde UART o dejan de llegar RMC validos.
- No se valida checksum NMEA.
- El parser acepta campos parciales sin validar rangos.
- La velocidad cruda puede afectar el modo velocidad aunque las metricas la descarten como pico.
- La distancia puede crecer con deriva GPS aunque el tiempo activo no avance.
- El pin TX del ESP32 hacia RX del GPS puede necesitar proteccion de nivel si el GNSS espera 2.8 V.

La recomendacion es hacer hardening incremental, sin agregar funciones nuevas visibles al usuario. El objetivo es que el sistema falle hacia estados conservadores: sin fix, sin velocidad usable, sin distancia falsa.

## Archivos Revisados

- `Platformio/Dog-RGB/src/gps/gps.cpp`
- `Platformio/Dog-RGB/include/gps/gps.h`
- `Platformio/Dog-RGB/include/config.h`
- `Platformio/Dog-RGB/src/led/led_ui.cpp`
- `Platformio/Dog-RGB/src/config/runtime_config.cpp`
- `Platformio/Dog-RGB/src/main.cpp`
- `Platformio/Dog-RGB/include/pins.h`
- `Platformio/Dog-RGB/platformio.ini`
- `xiao_s3_pin.md`

## Funcionamiento Actual

### Entrada GPS

El modulo `gps.cpp` abre `HardwareSerial GPS(1)` con:

- Baud: `GPS_BAUD = 9600`
- RX ESP32: `PIN_GPS_RX = 44`
- TX ESP32: `PIN_GPS_TX = 43`
- Formato: `SERIAL_8N1`

El loop principal llama:

```cpp
gps::tick();
geofence::tick(now_ms);
gps::save_if_due(now_ms);
gps::track_tick(now_ms);
```

`gps::tick()` lee bytes disponibles, arma lineas NMEA hasta `\n`, y llama `handle_nmea_line()`.

### Parsing NMEA

Actualmente se procesan:

- RMC: `$GPRMC` y `$GNRMC`
- GGA: `$GPGGA` y `$GNGGA`

RMC se usa para:

- Fix bruto `A/V`
- Latitud
- Longitud
- Velocidad en nudos convertida a km/h
- Fecha UTC
- Minuto UTC

GGA se usa para:

- Fix quality
- Satelites
- HDOP

### Fix Confiable

El firmware considera GPS confiable cuando:

- RMC reporta fix valido.
- GGA esta fresco.
- `fix_quality >= gps_min_fix_quality`.
- `sats >= gps_min_sats`.
- `hdop <= gps_max_hdop`.

Valores default:

- `GPS_MIN_FIX_QUALITY_DEFAULT = 1`
- `GPS_MIN_SATS_DEFAULT = 6`
- `GPS_MAX_HDOP_DEFAULT = 2.5`
- `GPS_MAX_GGA_AGE_MS_DEFAULT = 2000`

### Modo Velocidad

En `led_ui.cpp`, si `config::get().mode != MODE_GEOFENCE`, `MODE_SHOW` o `MODE_SIMPLE`, se usa modo velocidad.

La decision actual es:

- Si `!gps::has_fix()`: cuerpo en animacion idle/rainbow.
- Si hay fix: `range = speed_range(gps::last_speed_kph())`.

Los rangos default son:

| Rango | Condicion |
| --- | --- |
| 1 | `<= 2 km/h` |
| 2 | `<= 4 km/h` |
| 3 | `<= 6 km/h` |
| 4 | `<= 8 km/h` |
| 5 | `<= 10 km/h` |
| 6 | `<= 12 km/h` |
| 7 | `<= 14 km/h` |
| 8 | `<= 16 km/h` |
| 9 | `<= 18 km/h` |
| 10 | `> 18 km/h` |

## Hallazgos Tecnicos

### 1. Fix GPS pegado si se pierde UART o RMC

Severidad: alta

`has_gps_fix`, `gps_trusted_fix` y `has_current_fix_val` se actualizan cuando llega una sentencia RMC. Si el GPS deja de transmitir, se desconecta el cable, hay ruido serial o se bloquea el flujo NMEA, el ultimo estado puede mantenerse como valido indefinidamente.

Impacto:

- Modo velocidad podria seguir mostrando rango viejo.
- Estado GPS LED podria seguir indicando OK.
- Politica Wi-Fi/GPS puede tomar decisiones sobre un fix viejo.
- Geofence podria trabajar sobre coordenadas viejas.

Evidencia local:

- `gps::tick()` solo lee UART.
- `has_gps_fix` solo cambia en `handle_nmea_line()`.
- No hay timeout global que invalide fix por edad.

Fix recomendado:

- Agregar una funcion interna `expire_gps_if_stale(now_ms)`.
- Llamarla desde `gps::tick()` despues de `read_gps()`.
- Si `now_ms - gps_last_rmc_ms > GPS_RMC_STALE_MS`, limpiar fix confiable.
- Si `now_ms - gps_last_byte_ms > GPS_UART_STALE_MS`, limpiar fix, current fix y velocidad usable.

### 2. No hay validacion de checksum NMEA

Severidad: alta

El lector acepta cualquier linea con longitud mayor a 6. Las sentencias NMEA traen checksum despues de `*HH`; sin validarlo, un byte corrupto puede alterar velocidad, fecha, latitud o longitud.

Impacto:

- Saltos falsos de posicion.
- Velocidades falsas.
- Reset diario incorrecto si fecha corrupta cambia.
- Historial y track contaminados.

Fix recomendado:

- Rechazar lineas que no empiecen por `$`.
- Rechazar lineas sin `*`.
- Validar que los dos caracteres posteriores a `*` sean hex.
- Calcular XOR entre `$` y `*`.
- Parsear solo si checksum coincide.

### 3. Parser demasiado permisivo

Severidad: media-alta

`strtof()` y `atoi()` devuelven valores aunque el campo este vacio o parcialmente corrupto. Tambien faltan validaciones de rango:

- Latitud: `-90..90`
- Longitud: `-180..180`
- Hora: `00:00..23:59`
- Fecha: dia/mes plausibles
- Velocidad: finita y no negativa
- HDOP: finito y mayor a cero

Impacto:

- Posiciones invalidas pueden entrar en metricas.
- Fechas invalidas pueden resetear dia.
- Un campo corrupto puede pasar como cero y parecer real.

Fix recomendado:

- Crear helpers internos:
  - `is_numeric_field()`
  - `parse_float_field()`
  - `parse_uint_field()`
  - `valid_lat_lon()`
  - `valid_date_yyyymmdd()`
  - `valid_time_min()`
- Hacer que `parse_rmc()` devuelva `false` si falta un campo obligatorio cuando `status == 'A'`.

### 4. Velocidad cruda usada por LEDs aunque sea pico invalido

Severidad: media

`last_speed_kph_val` se actualiza antes del filtro `SPEED_MAX_VALID_KPH`. Luego las metricas solo corren si `speed_kph <= SPEED_MAX_VALID_KPH`, pero el modo velocidad usa `gps::last_speed_kph()` directamente.

Impacto:

- Un pico NMEA de 80 km/h no suma distancia, pero puede poner LED en rango 10.
- La UI visual puede contradecir los filtros internos.

Fix recomendado:

- Mantener dos valores:
  - velocidad cruda para diagnostico si se desea internamente.
  - velocidad usable para UI/metricas.
- Si `speed_kph > SPEED_MAX_VALID_KPH`, no actualizar velocidad usable o ponerla en 0.
- Hacer que `last_speed_kph()` devuelva velocidad usable, no cruda.

### 5. Distancia puede crecer sin tiempo activo

Severidad: media

La distancia se suma si el segmento supera `min_segment_m`, pero `active_time_ms` solo se incrementa si `speed_kph > SPEED_ACTIVE_KPH`. Esto permite distancia sin tiempo activo cuando hay deriva GPS lenta o saltos espaciados.

Impacto:

- Distancia fantasma con el collar quieto al aire libre.
- Promedio de velocidad puede salir inflado porque distancia sube y tiempo activo no.

Fix recomendado:

- Sumar distancia solo cuando la muestra tambien es movimiento activo.
- Alternativa conservadora: sumar distancia si:
  - `speed_kph > SPEED_ACTIVE_KPH`, y
  - segmento dentro de limites, y
  - fix trusted.

### 6. Baseline de distancia no se resetea tras huecos largos

Severidad: media

Si el GPS pierde fix durante un periodo largo y luego vuelve con otra posicion, el siguiente segmento puede conectarse contra el ultimo punto antiguo. El filtro `< 50 m` ayuda, pero no cubre todos los casos.

Impacto:

- Puede sumar un salto falso menor a 50 m despues de reacquisicion.
- Puede contaminar track/historial.

Fix recomendado:

- Si el fix expira, limpiar `has_last_point_val` y `session_has_last_point`.
- Al recuperar fix, la primera muestra debe establecer baseline, no sumar distancia.

### 7. BLE/JSON pueden truncar velocidades sin clamp uniforme

Severidad: baja-media

En snapshots de sesion hay clamp a `65535`, pero en `build_summary_payload_internal()` y JSON diario se hace cast directo a `uint16_t`.

Impacto:

- Si por bug o dato corrupto el promedio sube demasiado, puede truncar.

Fix recomendado:

- Crear helper `kph_to_cmps_u16_clamped()`.
- Usarlo en payload BLE, JSON y sesiones.

### 8. Riesgo electrico en TX ESP32 hacia RX GNSS

Severidad: media en hardware, baja en firmware si no se transmiten comandos

El EBYTE E108-GN02 indica UART con nivel de comunicacion alrededor de 2.8 V. El XIAO ESP32-S3 usa GPIO 3.3 V. GPS TX hacia ESP RX suele ser seguro si el nivel alto 2.8 V cruza VIH del ESP32-S3, pero ESP TX 3.3 V hacia GPS RX puede estar fuera de especificacion si no es tolerante.

Impacto:

- Riesgo de estres electrico en RX del GNSS.
- Mas relevante si en el futuro se envian comandos al GPS.

Fix recomendado:

- Si no se configuran comandos GNSS, dejar GPS RX desconectado.
- Si se necesita TX ESP32 -> RX GPS, usar divisor resistivo o level shifter.
- Documentarlo en `manual_de_construccion.md` y/o `pins.h`.

## Investigacion Externa Consultada

### 1. Espressif ESP32-S3 UART

Fuente: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html

Puntos aplicables:

- ESP32-S3 tiene 3 controladores UART.
- UART se configura con baud, bits, paridad, stop bits y pines.
- El driver tiene buffer RX/TX y eventos de overflow/error disponibles en ESP-IDF.

Aplicacion al proyecto:

- Usar `HardwareSerial GPS(1)` es valido.
- Conviene pensar en overflow y stale timeouts porque UART es stream continuo.

### 2. Arduino ESP32 HardwareSerial

Fuente: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html

Puntos aplicables:

- `HardwareSerial` es la capa Arduino para UART en ESP32.
- Permite asignar pines RX/TX.
- Es adecuada para sensores seriales como GNSS.

Aplicacion al proyecto:

- La configuracion `GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX)` esta alineada con Arduino-ESP32.

### 3. Seeed Studio XIAO ESP32-S3 Pinout

Fuente: https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/

Puntos aplicables:

- D6 corresponde a GPIO43 / UART TX.
- D7 corresponde a GPIO44 / UART RX.
- La placa usa USB nativo; D6/D7 no son el monitor USB.

Aplicacion al proyecto:

- `PIN_GPS_RX = 44` y `PIN_GPS_TX = 43` estan bien para D7/D6.

### 4. EBYTE E108-GN02

Fuente: https://www.ebyte.com/product/1037.html

Puntos aplicables:

- Protocolo NMEA0183.
- UART/GPIO.
- Baud default 9600.
- Soporta BDS/GPS/GLONASS.
- Frecuencia maxima hasta 10 Hz.
- VCC 2.8 V a 4.3 V; nivel UART indicado alrededor de 2.8 V.

Aplicacion al proyecto:

- `GPS_BAUD = 9600` coincide con default.
- El firmware debe soportar talker `$GN...`, no solo `$GP...`; ya soporta `$GNRMC` y `$GNGGA`.
- Revisar nivel logico en TX ESP32 hacia GPS RX.

### 5. Adafruit GPS Parsing Guide

Fuente: https://learn.adafruit.com/adafruit-ultimate-gps/parsed-data-output

Puntos aplicables:

- En proyectos Arduino conviene leer GPS constantemente.
- La velocidad GPS normalmente viene en nudos.
- RMC y GGA suelen ser suficientes para la mayoria de proyectos.

Aplicacion al proyecto:

- El firmware ya lee continuamente en `loop()`.
- La conversion `knots * 1.852` es correcta.
- Usar RMC + GGA es una decision razonable.

### 6. Adafruit GPS API

Fuente: https://adafruit.github.io/Adafruit_GPS/html/class_adafruit___g_p_s.html

Puntos aplicables:

- Librerias GPS robustas exponen fix quality, satelites, HDOP y validacion de parse/checksum.

Aplicacion al proyecto:

- El parser propio deberia incorporar checksum y validacion estricta aunque no se use una libreria externa.

### 7. SparkFun GPS Basics

Fuente: https://learn.sparkfun.com/tutorials/gps-basics

Puntos aplicables:

- GPS necesita buena vista al cielo.
- Obstrucciones y multipath producen errores de metros.
- NMEA es comun sobre serial.
- Muchos receptores actualizan a 1 Hz; otros pueden ir de 5 a 20 Hz.

Aplicacion al proyecto:

- Los filtros por HDOP, satelites y segmento minimo son necesarios.
- La distancia fantasma en reposo es esperable si no se filtra movimiento.

### 8. TinyGPSPlus

Fuente: https://github.com/mikalhart/TinyGPSPlus

Puntos aplicables:

- Parser NMEA compacto para Arduino.
- Extrae posicion, fecha, hora, velocidad y curso.
- Versiones recientes agregan fix quality/fix mode.

Aplicacion al proyecto:

- Confirma que parsear NMEA propio es viable, pero el proyecto debe igualar practicas basicas: checksum, validez y edad de datos.

### 9. NeoGPS

Fuente: https://github.com/SlashDevin/NeoGPS

Puntos aplicables:

- Parser configurable de bajo consumo RAM/CPU.
- Soporta multiples talker IDs: GP, GL, BD/GB, GA, GN.
- Soporta GGA, RMC, VTG, GSA, GSV, etc.

Aplicacion al proyecto:

- El soporte actual de `$GN` es correcto para modulos multiconstelacion.
- Como hardening futuro, conviene aceptar talker IDs validos de forma generica para RMC/GGA, sin abrir mensajes no usados.

### 10. MicroNMEA

Fuente: https://micronmea.readthedocs.io/_/downloads/en/latest/pdf/

Puntos aplicables:

- Parser pequeno para subset NMEA.
- Incluye datos como HDOP, curso y velocidad.

Aplicacion al proyecto:

- Reafirma que un parser pequeno es apropiado para microcontrolador, siempre que sea estricto con checksum y campos.

### 11. u-blox Hardware Integration

Fuente: https://content.u-blox.com/sites/default/files/products/documents/PAM-7Q_HardwareIntegrationManual_%28UBX-13003143%29.pdf

Puntos aplicables:

- GPS/GNSS es sensible a alimentacion, EMI, ESD y layout.
- Backup supply ayuda a reacquisicion.
- UART GNSS usa niveles dependientes de VCC.

Aplicacion al proyecto:

- Separar fisicamente GPS de fuentes de ruido LED/Wi-Fi ayuda al fix.
- Agregar capacitancia y buen GND cerca del GNSS es parte del hardening real.

### 12. Altium GPS Antenna PCB Design

Fuente: https://resources.altium.com/p/gps-antennas-in-your-pcb-design-you-won-t-get-lost-again

Puntos aplicables:

- La antena GPS es susceptible a EMI, crosstalk y ruido de plano de tierra.
- El layout RF afecta sensibilidad y TTFF.

Aplicacion al proyecto:

- En collar con SK6812 y Wi-Fi, el ruido de alimentacion y layout puede degradar GPS.

### 13. DigiKey Coin-Sized GPS Receiver Design

Fuente: https://www.digikey.com/en/articles/designing-a-coin-sized-solar-powered-gps-receiver

Puntos aplicables:

- Conviene aislar el modulo GPS de cristales, fuentes switching y buses rapidos.
- Plano de tierra amplio y continuo mejora retorno y estabilidad.
- Trazas RF cortas y limpias son importantes.

Aplicacion al proyecto:

- El hardening no es solo firmware: alimentacion de LEDs, Wi-Fi y GPS debe estar desacoplada y bien ruteada.

## Plan de Fix y Hardening

### Fase 0: Baseline y Seguridad de Cambios

Objetivo: asegurar que cualquier cambio mejore robustez sin romper comportamiento existente.

Tareas:

- Compilar estado actual.
- Guardar logs seriales de arranque con GPS desconectado, sin fix y con fix.
- Documentar valores actuales de `bytes_rx`, `rmc_seen`, `gga_seen`, `fix_quality`, `sats`, `hdop`, `overflow`.
- Confirmar wiring real:
  - GPS TX -> ESP GPIO44/D7.
  - GPS RX -> ESP GPIO43/D6 solo si se necesita.
  - GND comun.
  - VCC dentro de rango GNSS.

Criterio de salida:

- Build OK.
- Estado actual reproducible antes de modificar.

Estado de verificacion actual:

- Build ejecutado con `/Users/angel/.platformio/penv/bin/pio run -e seeed_xiao_esp32s3`.
- Resultado: SUCCESS.
- RAM: 21.4%.
- Flash: 44.1%.

### Fase 1: Caducidad de Fix y Velocidad Usable

Objetivo: evitar estados GPS viejos.

Cambios:

- Agregar constantes internas:
  - `GPS_RMC_STALE_MS`, recomendado 2500-3000 ms.
  - `GPS_UART_STALE_MS`, recomendado 3000-5000 ms.
- Agregar `expire_gps_if_stale(now_ms)`.
- Limpiar estado confiable si RMC/GGA/UART caducan.
- Resetear baseline de distancia al expirar fix.
- Asegurar que `last_speed_kph()` no devuelva velocidad vieja tras caducidad.

No agrega funciones nuevas al usuario.

Criterios de prueba:

- Desconectar GPS: en menos de 5 s `gps_fix=false`.
- Cortar solo RMC: `NO_RMC` y `gps_fix=false`.
- Volver a conectar: primera muestra no suma salto viejo.

### Fase 2: Checksum NMEA

Objetivo: rechazar datos corruptos antes de parsear.

Cambios:

- Implementar `nmea_checksum_ok(const char *line)`.
- Validar antes de `gps_sentences_rx++` o contar sentencias separando recibidas vs aceptadas.
- Incrementar contador de rechazo opcional interno; si no se quiere API nueva, reutilizar `gps_overflow` no es ideal. Mejor mantenerlo interno o agregar contador solo si se decide exponer diagnostico.

Criterios de prueba:

- Sentencia RMC valida se acepta.
- Misma sentencia con un caracter cambiado se rechaza.
- Linea sin `*HH` se rechaza.
- Linea con checksum no hex se rechaza.

### Fase 3: Parser Estricto RMC/GGA

Objetivo: evitar que campos vacios o parciales entren como cero.

Cambios:

- Validar campos obligatorios de RMC cuando `status == 'A'`.
- Validar lat/lon y hemisferios.
- Validar fecha y hora.
- Validar velocidad finita y no negativa.
- Validar GGA fix/sats/HDOP.

Criterios de prueba:

- RMC `V` no actualiza current fix.
- RMC `A` sin lat/lon no actualiza current fix.
- Fecha invalida no resetea metricas.
- HDOP vacio no pasa `quality_ok`.

### Fase 4: Coherencia de Velocidad, Distancia y Actividad

Objetivo: que UI y metricas usen los mismos filtros conservadores.

Cambios:

- Separar velocidad cruda vs velocidad valida.
- `last_speed_kph()` debe representar velocidad valida para UI.
- Si velocidad supera `SPEED_MAX_VALID_KPH`, no alimentar LEDs ni metricas.
- Sumar distancia solo cuando la muestra cuenta como movimiento activo o cuando una regla coherente de segmento/tiempo confirme movimiento.

Criterios de prueba:

- Inyectar RMC con 80 km/h: no rango 10, no distancia, no max speed.
- Collar quieto con jitter de 1-3 m: distancia no crece significativamente.
- Caminata real: distancia sigue acumulando.

### Fase 5: Persistencia y Resumen

Objetivo: evitar valores truncados o contaminados en BLE/JSON/NVS.

Cambios:

- Helper `kph_to_cmps_u16_clamped()`.
- Usarlo en:
  - `build_summary_payload_internal()`
  - `build_summary_json_internal()`
  - `build_session_snapshot()`
- Evitar guardar metricas si `current_date_yyyymmdd` es invalido.

Criterios de prueba:

- Velocidades altas artificiales no hacen wrap en `uint16_t`.
- JSON y BLE reportan valores consistentes.

### Fase 6: Hardening Electrico y Documentacion

Objetivo: reducir fallas reales de campo.

Cambios/documentacion:

- Documentar GPS TX/RX cruzados.
- Documentar que GPS RX puede quedar desconectado si no se mandan comandos.
- Si se conecta ESP TX a GPS RX, recomendar divisor o level shifter.
- Reforzar desacople:
  - Capacitor cerca del GNSS.
  - GND comun de baja impedancia.
  - Separar alimentacion/retorno LED del GNSS si es posible.
  - Mantener antena GPS lejos de LEDs, DC/DC y Wi-Fi.

Criterios de prueba:

- GPS mantiene bytes/RMC estables con LEDs a brillo operativo.
- No aumenta `overflow` durante efectos LED intensos.
- Fix estable al mover el collar.

## Orden Recomendado de Implementacion

1. Fase 1: timeout de fix y velocidad usable.
2. Fase 2: checksum NMEA.
3. Fase 3: parser estricto.
4. Fase 4: coherencia velocidad/distancia/actividad.
5. Fase 5: clamps de resumen.
6. Fase 6: documentacion/hardware.

Este orden reduce riesgo rapido: primero evita estados pegados, luego rechaza corrupcion, despues ajusta precision.

## Pruebas Manuales Recomendadas

### Prueba A: Sin GPS conectado

Esperado:

- `bytes_rx = 0`.
- `gps_fix = false`.
- LEDs en busqueda/idle.
- No suma distancia.

### Prueba B: GPS conectado sin cielo

Esperado:

- `bytes_rx` sube.
- `rmc_seen` sube.
- `raw_fix = false` o `trusted_fix = false`.
- No suma distancia.

### Prueba C: Fix exterior quieto 10 minutos

Esperado:

- `trusted_fix = true`.
- Distancia acumulada casi cero.
- Max speed razonable.
- Sin saltos de rango LED por jitter.

### Prueba D: Caminata 200-500 m

Esperado:

- Distancia comparable con telefono.
- Velocidad en rangos bajos/medios.
- Sin overflow.

### Prueba E: Trote corto

Esperado:

- Rango LED sube de forma progresiva.
- `max_speed_kph` plausible.
- No hay picos >40 km/h aceptados.

### Prueba F: Perdida y recuperacion de fix

Procedimiento:

- Conseguir fix.
- Tapar antena o desconectar TX GPS.
- Esperar timeout.
- Restaurar GPS.

Esperado:

- Fix cae rapido.
- Al recuperar, no se suma salto desde punto viejo.

## Notas Sobre Lo Que No Se Debe Hacer Ahora

- No agregar Kalman, IMU ni fusion sensorial en esta fase.
- No cambiar la UI ni crear nuevos modos.
- No aumentar frecuencia GNSS a 10 Hz hasta robustecer parser y confirmar carga UART.
- No enviar comandos al GPS si no se resuelve primero el nivel logico de TX hacia RX GNSS.

## Conclusion

El modo velocidad es funcional, pero debe endurecerse para campo real. La prioridad es que el firmware no confie en datos viejos ni corruptos. Con timeouts, checksum, parser estricto y una velocidad usable filtrada, el sistema queda mucho mas predecible sin agregar funciones nuevas.

