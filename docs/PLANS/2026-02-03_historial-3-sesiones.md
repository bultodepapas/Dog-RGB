# Plan - Historial de las ultimas 3 sesiones en memoria (XIAO ESP32-S3)

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

## 5) Layout NVS (ring buffer de 3 + sesion actual)

Sugerencia de claves (<= 15 chars):

- `h0`, `h1`, `h2` = blob con `SessionSummary` (sesiones cerradas).
- `h_idx` = indice de escritura (0..2).
- `h_cnt` = numero de entradas validas (0..3).
- `h_ver` = version del struct.
- `s_cur` = blob con la sesion en progreso (snapshot).
- `s_open` = flag de sesion abierta (0/1).

Namespace recomendado: `dogrgb` (mismo que metrics) o `dogrgb_hist` para aislar.

## 6) Estrategia de escritura (eficiente y segura)

- En runtime, actualizar `s_cur` cada `SAVE_INTERVAL_MS` usando datos ya persistidos (no agregar un timer nuevo).
- `s_open = 1` al boot. Se setea `start_date/start_min` al primer fix valido.
- Si el collar se apaga, la sesion queda abierta en NVS. En el siguiente boot:
  - Leer `s_open` y `s_cur`.
  - Si hay `has_data`, cerrar esa sesion y escribirla en `h{idx}`.
  - Si **no** hubo fix en toda la sesion, marcar `no_fix_session=1` y guardar igualmente (para que la UI lo muestre).
  - Limpiar `s_open` y reiniciar `s_cur`.
- Esto evita necesitar un evento de apagado y limita escrituras a 1/minuto (ya existente).

## 7) Lectura y orden en el portal

- Al boot, leer `h_cnt`, `h_idx`, `h0..h2`.
- Ordenar por recencia segun el indice circular.
- Exponer array `history` en `/api/summary` y opcionalmente `session_current`.

## 8) Impacto en UI del portal

- Nueva seccion \"Historial (3 sesiones)\" con cards pequenas: Fecha inicio, Duracion activa, Distancia, Velocidad max.
- Si `no_fix_session=1`, mostrar \"Sin GPS\" y ocultar metricas de distancia/velocidad.
- Mostrar \"Sin historial\" si `h_cnt` = 0.

## 9) Validaciones sugeridas

- Encender, obtener fix, apagar sin cerrar, y confirmar que al siguiente boot la sesion se cierra y queda en `h0..h2`.
- Caso sin GPS: guardar sesion con `no_fix_session=1` y sin metricas.
- Verificar CRC al leer y descartar blobs corruptos.

## 10) Fuentes

- https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html
- https://wiki.seeedstudio.com/es/xiao_esp32s3_getting_started/
- https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/storage/nvs_flash.html
