# Plan - Historial de las ultimas 3 sesiones en memoria (XIAO ESP32-S3)

> **Document status:** Historical implementation plan (Spanish). Three completed session summaries are implemented with transactional persistence; see [GNSS and metrics](../gps_analysis.md).

Este plan define como guardar las ultimas 3 sesiones de estadisticas del collar usando la memoria disponible del XIAO ESP32-S3. No implementa cambios.

---

## 1) Hechos de hardware y memoria (resumen investigado)

- El board usado es `seeed_xiao_esp32s3` (PlatformIO). El manifest indica 8 MB de flash y 320 KB de RAM interna.
- El XIAO ESP32S3 tiene 8 MB de PSRAM integrada y 8 MB de flash segun la wiki de Seeed. El modelo XIAO ESP32S3 Plus lista 16 MB de flash.
- La PSRAM es volatil, por lo que no sirve para conservar sesiones tras reinicio o apagado. La persistencia debe ir a flash.

## 2) Opciones de almacenamiento persistente

- **NVS (Preferences)**: ya se usa en el firmware. Es ideal para valores pequenos y blobs chicos. Limite de claves: 15 caracteres ASCII. Limite de strings: 4000 bytes, blobs grandes permitidos pero no necesarios. NVS se recomienda para muchos valores pequenos en lugar de pocos blobs grandes.
- **LittleFS/FAT con wear leveling**: util si en el futuro se guarda mucho historial (no requerido para 3 sesiones).

Conclusion: NVS es suficiente y consistente con el codigo actual.

## 3) Definicion de \"sesion\" (confirmada)

- Sesion = ciclo de encendido a apagado.
- Inicio: al boot del MCU. Para datos GPS, el tiempo real inicia cuando llega el primer fix valido.
- Cierre: no hay evento de apagado. Se cierra de forma diferida en el siguiente boot usando el ultimo snapshot persistido.

## 4) Datos a guardar (solo GPS y tiempo activo)

- `start_date` (uint32, yyyymmdd) y `start_min` (uint16). Se setean en el primer fix valido.
- `end_date` (uint32) y `end_min` (uint16) del ultimo fix valido.
- `distance_m` (uint32). Total acumulado con filtro de picos.
- `active_s` (uint32). Tiempo activo en segundos (derivar de `active_time_ms`).
- `max_speed_cmps` (uint16). Velocidad maxima en cm/s.
- `avg_speed_cmps` (uint16). Se guarda para evitar recomputo y mantener consistencia con el resumen actual.
- `flags` (uint8). bit0=gps_fix_visto, bit1=has_data, bit2=in_progress, bit3=no_fix_session.
- `crc` (uint8). XOR simple o CRC8 para validar.

Estimacion: 24-32 bytes por sesion. Tres sesiones ~96 bytes (+ overhead NVS).

### 4.1) Compatibilidad con reset diario actual

- El firmware actual reinicia metricas cuando cambia `date_yyyymmdd`.
- Para que el historial sea por sesion (encendido a apagado), se requiere un set de acumuladores de sesion separados de los diarios.
- Propuesta: mantener los acumuladores diarios sin cambios y agregar acumuladores de sesion paralelos (distancia, active_ms, max_speed).
- El resumen diario actual sigue intacto, y el historial usa los acumuladores de sesion.

### 4.2) Estructura binaria recomendada (sin floats)

Orden y tamanos fijos, little-endian:

- `ver` (uint8) version del struct.
- `flags` (uint8) bit0=gps_fix_visto, bit1=has_data, bit2=in_progress, bit3=no_fix_session.
- `start_date` (uint32, yyyymmdd).
- `start_min` (uint16, min desde medianoche GPS).
- `end_date` (uint32, yyyymmdd).
- `end_min` (uint16).
- `distance_m` (uint32).
- `active_s` (uint32).
- `avg_speed_cmps` (uint16).
- `max_speed_cmps` (uint16).
- `crc` (uint8) XOR o CRC8 de todos los bytes previos.
- `pad` (uint8) reservado para alineacion/futuro.

Total esperado: 28 bytes.

### 4.3) Reglas de llenado

- `start_*`: se setea con el primer fix valido de la sesion.
- `end_*`: se actualiza con cada fix valido (ultimo fix conocido).
- `distance_m`: usa la misma logica de segmentos validos que el resumen diario.
- `active_s`: `active_time_ms / 1000` usando el mismo umbral de actividad.
- `avg_speed_cmps`: se calcula al cerrar sesion con `distance_m / active_s`.
- `max_speed_cmps`: maximo de la sesion, no del dia.

### 4.4) Sesion sin fix GPS

- Si no hubo fix en toda la sesion: `no_fix_session=1` y `has_data=0`.
- `start_*` y `end_*` quedan en 0, y las metricas en 0.
- La UI mostrara "Sin GPS" sin valores num.

## 5) Layout NVS (ring buffer de 3 + sesion actual)

Sugerencia de claves (<= 15 chars):

- `h0`, `h1`, `h2` = blob con `SessionSummary` (sesiones cerradas).
- `h_idx` = indice de escritura (0..2).
- `h_cnt` = numero de entradas validas (0..3).
- `h_ver` = version del struct.
- `s_cur` = blob con la sesion en progreso (snapshot).
- `s_open` = flag de sesion abierta (0/1).

Namespace recomendado: `dogrgb` (mismo que metrics) o `dogrgb_hist` para aislar.

### 5.1) Versionado y migracion

- `h_ver` define la version global del struct.
- Si `h_ver` es desconocida, limpiar `h0..h2`, `h_cnt` y `h_idx` para evitar lecturas corruptas.
- Si falta `h_ver`, asumir version 1 y migrar al boot.

### 5.2) Integridad de datos

- Usar XOR como checksum (similar al BLE) o CRC8.
- Al leer, validar checksum y descartar blobs invalidos.

### 5.3) Orden de lectura (ring buffer)

- `h_idx` apunta al proximo slot de escritura.
- La sesion mas reciente esta en `h[(h_idx-1+3)%3]` si `h_cnt > 0`.
- Para ordenar por recencia, iterar `h_cnt` elementos hacia atras desde `h_idx-1`.

## 6) Estrategia de escritura (eficiente y segura)

- En runtime, actualizar `s_cur` cada `SAVE_INTERVAL_MS` usando datos ya persistidos (no agregar un timer nuevo).
- `s_open = 1` al boot. Se setea `start_date/start_min` al primer fix valido.
- Si el collar se apaga, la sesion queda abierta en NVS. En el siguiente boot:
  - Leer `s_open` y `s_cur`.
  - Si hay `has_data`, cerrar esa sesion y escribirla en `h{idx}`.
  - Si **no** hubo fix en toda la sesion, marcar `no_fix_session=1` y guardar igualmente (para que la UI lo muestre).
  - Limpiar `s_open` y reiniciar `s_cur`.
- Esto evita necesitar un evento de apagado y limita escrituras a 1/minuto (ya existente).

### 6.1) Reduccion de escrituras

- Solo escribir `s_cur` si al menos un campo cambio desde el ultimo snapshot.
- Reusar el mismo tick de guardado que ya usa `SAVE_INTERVAL_MS`.

### 6.2) Cierre diferido y consistencia

- Si `s_open=1` y `s_cur` es valido, cerrar sesion antes de reiniciar acumuladores.
- Si `s_cur` es invalido (checksum), descartar y arrancar sesion nueva.

## 7) API local (resumen + historial)

- Exponer `history` en `/api/summary` con hasta 3 sesiones cerradas.
- Exponer `session_current` con el snapshot en progreso (opcional).
- Mantener compatibilidad con clientes que solo leen el resumen diario.

JSON propuesto (solo campos nuevos):

```
{
  "history": [
    {
      "start_date": 20260203,
      "start_min": 540,
      "end_date": 20260203,
      "end_min": 615,
      "distance_m": 1240,
      "active_s": 900,
      "avg_speed_cmps": 460,
      "max_speed_cmps": 920,
      "flags": 3
    }
  ],
  "session_current": {
    "start_date": 20260203,
    "start_min": 700,
    "distance_m": 300,
    "active_s": 120,
    "avg_speed_cmps": 450,
    "max_speed_cmps": 780,
    "flags": 7
  }
}
```

## 8) Estado actual del portal AP (revisado)

- La pagina principal esta embebida en `html_page()` en `Platformio/Dog-RGB/src/main.cpp`.
- UI actual: titulo "Dog Collar", estado, boton "Actualizar", 3 cards (distancia, velocidad promedio, velocidad maxima), y "Ultima lectura".
- Conversiones actuales en JS: `distance_m -> km`, `avg_speed_cmps -> km/h`, `max_speed_cmps -> km/h`.
- Estado actual mostrado:
  - `Sin datos` si `has_data` es false.
  - `GPS OK` o `Sin GPS` segun `gps_fix`.

## 9) Impacto en UI del portal (historial de sesiones)

- Nueva seccion \"Historial (3 sesiones)\" con cards pequenas: Fecha inicio, Duracion activa, Distancia, Velocidad max.
- Si `no_fix_session=1`, mostrar \"Sin GPS\" y ocultar metricas de distancia/velocidad.
- Mostrar \"Sin historial\" si `h_cnt` = 0.

Texto UI sugerido:

- Sesion normal: "Sesion 1 - 03/02 09:00 a 10:15".
- Sin GPS: "Sesion 1 - Sin GPS (sin datos de ubicacion)".

### 9.1) Ajustes de UI propuestos (AP)

- Agregar bloque "Sesion actual" (si `session_current` existe).
- Mostrar "Ultimo fix: HH:MM" solo si hay fix valido.
- Mostrar tag de estado para cada card del historial: `OK`, `Sin GPS`.
- Mantener el boton "Actualizar" y recargar tanto resumen como historial.

## 10) Validaciones sugeridas

- Encender, obtener fix, apagar sin cerrar, y confirmar que al siguiente boot la sesion se cierra y queda en `h0..h2`.
- Caso sin GPS: guardar sesion con `no_fix_session=1` y sin metricas.
- Verificar CRC al leer y descartar blobs corruptos.

## 11) Casos borde y decisiones

- Si la sesion cruza medianoche, `start_date` y `end_date` pueden diferir.
- Si el ultimo fix es muy viejo, `end_*` quedara con ese valor; es aceptable.
- Si no hay GPS, no se inventa horario; se muestra "Sin GPS".
- Si `active_s = 0`, `avg_speed_cmps` debe ser 0 para evitar division por cero.

## 12) Riesgos y mitigaciones

- Riesgo: reset diario actual borra metricas si no se usan acumuladores de sesion.
- Mitigacion: agregar acumuladores de sesion separados, no ligados a fecha.

## 13) GPS fecha/hora: esperar fix antes de actualizar (plan detallado)

### 12.1) Estado actual (observado)

- `parse_rmc()` siempre parsea fecha y hora si vienen en la sentencia.
- En `handle_nmea_line()` se hace:
  - `has_gps_fix = valid_fix`.
  - `last_update_min = time_min` sin validar fix.
  - `current_date_yyyymmdd` puede cambiar aunque `valid_fix` sea false.
- Resultado: el reloj y el reset diario pueden actualizarse aun sin fix valido.

### 12.2) Objetivo

- Fecha/hora GPS solo se actualizan cuando hay fix valido (RMC status = A).
- Si no hay fix:
  - No se actualiza `last_update_min`.
  - No se cambia `current_date_yyyymmdd`.
  - No se cierra ni reinicia sesion por fecha.

### 12.3) Cambios logicos propuestos

- Mover la actualizacion de fecha/hora dentro del bloque `if (valid_fix)`.
- Regla precisa:
  - Si `valid_fix` y `date_yyyymmdd != 0`, entonces permitir cambiar `current_date_yyyymmdd`.
  - Si `valid_fix`, actualizar `last_update_min = time_min`.
  - Si `valid_fix` y se detecta cambio de fecha, reset diario como hoy.
- Para sesiones:
  - `start_date/start_min` solo se setean con el primer fix valido.
  - `end_date/end_min` solo se actualizan cuando `valid_fix` es true.
  - Si nunca hubo fix, `no_fix_session=1` al cerrar.

### 12.4) Consideraciones con GGA

- `gps_fix_quality` y `gps_sats` provienen de GGA.
- Se mantiene el criterio principal: **solo** RMC status `A` habilita fecha/hora.
- Opcional futuro: exigir `gps_fix_quality > 0` y `gps_sats >= N` para robustez.

### 12.5) Impacto en UI y UX

- `last_update_min` reflejara el ultimo fix valido.
- En "Sin GPS" se mostrara el estado sin cambiar el reloj.
- El resumen diario no saltara de dia hasta que el GPS tenga fix.

### 12.6) Casos de prueba

- GPS sin fix: fecha/hora no cambian, `last_update_min` se mantiene.
- Fix recuperado: fecha/hora actualizan y el resumen diario puede resetearse.
- Sesion completa sin fix: se guarda con `no_fix_session=1`.

## 14) Fuentes

- https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html
- https://wiki.seeedstudio.com/es/xiao_esp32s3_getting_started/
- https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/storage/nvs_flash.html
