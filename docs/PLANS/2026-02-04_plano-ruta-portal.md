# Plan: Plano de ruta GPS en portal AP/STA (3 sesiones, 2 horas, export)

> **Document status:** Historical implementation plan (Spanish). Route capture, the rolling two-hour window, three completed sessions, and JSON/CSV/GeoJSON exports are now implemented in evolved form; see [HTTP API](../api-reference.md).

## Objetivo
Mostrar un **plano simple de la ruta** recorrida por el perro dentro del portal Wi‑Fi (AP/STA), sin depender de Internet, con estas reglas:
- **Últimas 3 sesiones** (sesión = ciclo de encendido).
- **Ventana de 2 horas por sesión** (si la sesión dura más, se conserva el tramo más reciente de 2h).
- **Exportación** (CSV y GeoJSON) además de visualización en el portal.

## Resumen del estado actual (repo)
- **GPS**: `Platformio/Dog-RGB/src/gps/gps.cpp` parsea RMC/GGA, calcula métricas diarias y de sesión, filtra por calidad y por segmentos (`min_segment_m`, `HDOP`, `SPEED_MAX_VALID_KPH`, `GPS_SAMPLE_MS`).
- **Persistencia**: métricas y resumen de sesiones en NVS (`docs/architecture.md`). **No hay histórico de puntos GPS**; sólo `last/current lat/lon` en RAM.
- **Portal AP/STA**:
  - API y rutas HTTP en `Platformio/Dog-RGB/src/web/portal_http.cpp`.
  - UI HTML/CSS/JS embebida en `Platformio/Dog-RGB/src/web/pages.cpp`.
  - Endpoints existentes: `/api/summary`, `/api/status`, `/api/dev`, `/api/config`, `/api/home`.
- **Restricción clave**: en modo AP no hay Internet, por lo que un mapa con tiles externos no es confiable. El “plano” debe ser **offline**.

## Revisión de hardware (impacto en memoria)
- Board configurado en `Platformio/Dog-RGB/platformio.ini`: `seeed_xiao_esp32s3`.
- En el repo existe un plan previo con notas de memoria para XIAO ESP32‑S3 (`docs/PLANS/2026-02-03_historial-3-sesiones.md`). Para no depender de supuestos, el diseño **usa buffers acotados** y se ajusta con medición real de heap.
- Telemetría disponible: `/api/dev` ya expone `free_heap`, útil para validar el presupuesto de RAM antes y después de habilitar la ruta.

## Decisiones base
1. **Plano offline (sin tiles)**: Canvas/SVG con fondo simple y grid opcional. Proyección equirectangular de lat/lon a XY local.
2. **3 sesiones (cerradas) + actual, ventana 2h**: cada sesión mantiene **el tramo más reciente de 2 horas**.
3. **Filtro de puntos**: reutilizar la lógica actual de `gps.cpp` (fix confiable + `min_segment_m` + `SPEED_MAX_VALID_KPH`) para no dibujar ruido.
4. **Exportación integrada**: CSV y GeoJSON desde el portal.

## Plan de implementación (propuesto)

### 1) Modelo de datos y presupuesto de memoria
**Archivos:** `Platformio/Dog-RGB/src/gps/gps.cpp`, `Platformio/Dog-RGB/include/gps/gps.h`

- **Estructura base** (compacta):
  - `struct TrackPoint { int32_t lat_e7; int32_t lon_e7; uint16_t t_min; };`
  - `lat_e7/lon_e7` en micro‑grados (1e‑7) para compactar y evitar floats en el buffer.
- **Sesión de track**:
  - `TrackSession { TrackPoint *buf; uint16_t head; uint16_t count; uint32_t start_date; uint16_t start_min; uint32_t end_date; uint16_t end_min; bbox }`.
  - **4 slots** en rotación (3 cerradas + actual), alineado con el historial existente.
- **Ventana 2h**:
  - Definir `TRACK_SAMPLE_MS` (ej. 2000 ms) y `TRACK_MAX_POINTS = (2h * 3600 / (TRACK_SAMPLE_MS/1000))`.
  - Si se supera el máximo, el ring reemplaza puntos antiguos (se conserva sólo las últimas 2h).
- **Presupuesto de RAM (estimado)**:
  - 1 punto ≈ 10–12 bytes (con alineación).
  - Ejemplo 2s: 2h → 3600 puntos → ~43 KB por sesión.
  - Ejemplo 5s: 2h → 1440 puntos → ~17 KB por sesión.
  - En la implementación, **solo la sesión actual** vive en RAM; las cerradas se leen desde NVS.
  - Ajustar `TRACK_SAMPLE_MS` según `free_heap` real.

### 2) Captura de puntos (firmware)
**Archivos:** `Platformio/Dog-RGB/src/gps/gps.cpp`

- **Inserción de puntos**: en el bloque donde se actualiza distancia (cuando pasa `gps_trusted_fix` + `min_segment_m` + `SPEED_MAX_VALID_KPH`).
- **Actualización de metadatos**: `start_*` con el primer fix válido; `end_*` con el último fix válido; `bbox` incremental.
- **Regla 2h**: si `count > TRACK_MAX_POINTS`, avanzar `head` y descartar el punto más viejo.

### 3) Persistencia resistente (chunked NVS, eficiente)
**Objetivo:** que las 3 rutas sobrevivan reinicios y cortes, con **desgaste controlado**. Es aceptable perder los últimos segundos.

**Estrategia principal (robusta + eficiente):**
- **Chunked NVS**: escribir bloques pequeños (30–60 puntos) en claves consecutivas por sesión. Evita reescribir blobs grandes y limita desgaste.
- **Buffer de persistencia** en RAM: acumular puntos y persistir cada N segundos o al llenar el buffer.
- **Metadatos mínimos** en una clave separada para reconstrucción rápida (start/end, bbox, chunk_count, sample_ms).

**Formato de claves (<= 15 chars):**
- Namespace sugerido: `dogrgb_trk` (aislar y facilitar limpieza).
- Meta por sesión: `t0m`, `t1m`, `t2m`.
- Chunks por sesión: `t0c00`, `t0c01`, ... (2 dígitos) o hex `t0c0a`.
- Índices y control: `t_idx` (sesión más reciente), `t_ver` (versión), `t_open` (sesión en curso).

**Contenido de un chunk (binario):**
- Header pequeño: `count` (u8/u16), `first_t_min` (u16), `flags` (u8), `crc` (u8 opcional).
- Lista de `TrackPoint` (lat_e7, lon_e7, t_min).

**Política de escritura (minimiza desgaste):**
- Persistir **cada 10–20s** o al llegar a 30–60 puntos (lo que ocurra primero).
- Si el GPS está estable pero el perro no se mueve (segmento < `min_segment_m`), **no persistir** (evita ruido).
- Actualizar `meta` **sólo cuando** se escribe un chunk o se cierra sesión.
- Se acepta perder el **último buffer no persistido** (segundos/minutos).

**Rotación de 3 sesiones (cerradas) + actual:**
- Al cerrar sesión, se congela su track (meta + chunks).
- Al iniciar una nueva sesión, se rota el índice (`t_idx`) y se **limpian las claves** del slot que se va a sobrescribir (`tXc*`, `tXm`).

**Reconstrucción / lectura:**
- Para una sesión cerrada: leer meta + todos los chunks.
- Para sesión actual: leer chunks ya persistidos y **sumar** el buffer RAM (no persistido).

**Ventana 2h y conteo de chunks:**
- `TRACK_SAMPLE_MS` + `TRACK_MAX_POINTS` definen el máximo de puntos.
- Con `TRACK_SAMPLE_MS=5000` y 2h → ~1440 puntos. Con 60 puntos por chunk → ~24 chunks/sesión.


### 4) Endpoints de API (visualización + export)
**Archivos:** `Platformio/Dog-RGB/src/web/portal_http.cpp`

- `GET /api/track?session=current|0|1|2&max_points=N`
  - JSON con `count`, `bbox`, `start/end`, y `points`.
- `GET /api/track.csv?session=...`
  - `text/csv` con header `date,min,lat,lon` (o `t_min` y `date_start` para reconstrucción).
- `GET /api/track.geojson?session=...`
  - `application/geo+json` con `FeatureCollection` y `LineString`.

**Nota de memoria:** para CSV/GeoJSON usar **streaming** (`server.setContentLength(CONTENT_LENGTH_UNKNOWN)` + `sendContent`) para evitar `String` gigantes en heap. La lectura por chunks permite stream sin cargar todo el track en RAM.

### 5) UI del plano en el portal
**Archivos:** `Platformio/Dog-RGB/src/web/pages.cpp`

- Card “Ruta” con `<canvas id="track_map">`.
- **Selector de sesión** (3 últimas + “actual”).
- Botones: `Export CSV` y `Export GeoJSON` (link directo al endpoint).
- Mensajes:
  - `Sin GPS` si la sesión no tuvo fix.
  - `Sin ruta disponible` si `count < 2`.
  - `Ventana 2h` si la sesión excede el límite.

### 6) Validaciones clave
- **Memoria**: medir `free_heap` en `/api/dev` antes y después de activar track.
- **Sesiones**: rotación correcta de 3 sesiones cerradas + actual y alineación con `history_idx`.
- **Export**: CSV/GeoJSON descargables desde AP y STA.
- **Sin GPS**: UI estable y sin errores.

## Pseudocódigo (casi real)
El objetivo de este bloque es dejar claro el flujo exacto y las estructuras para acelerar implementación.

### A) Constantes y estructuras (C++)
```cpp
// gps/track.h (o dentro de gps.cpp con static)
static const uint8_t TRACK_VER = 1;
static const uint32_t TRACK_SAMPLE_MS = 5000; // 5s => 2h ~ 1440 puntos
static const uint16_t TRACK_MAX_POINTS = (2UL * 60UL * 60UL * 1000UL) / TRACK_SAMPLE_MS;
static const uint8_t TRACK_CHUNK_POINTS = 48; // 30–60 recomendado
static const uint32_t TRACK_FLUSH_MS = 15000; // 10–20s recomendado

struct TrackPoint {
  int32_t lat_e7;
  int32_t lon_e7;
  uint16_t t_min; // minutos desde medianoche del fix
} __attribute__((packed));

struct TrackChunkHeader {
  uint8_t count;
  uint16_t first_t_min;
  uint8_t flags;
  uint8_t crc; // opcional
} __attribute__((packed));

struct TrackMeta {
  uint8_t ver;
  uint8_t open;        // 1 si sesión abierta
  uint16_t sample_ms;
  uint16_t max_points;
  uint16_t total_points;
  uint8_t chunk_count;
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  int32_t min_lat_e7, max_lat_e7;
  int32_t min_lon_e7, max_lon_e7;
  uint8_t crc;
} __attribute__((packed));

struct TrackSession {
  TrackPoint ring[TRACK_MAX_POINTS];
  uint16_t head;   // índice del más viejo
  uint16_t count;  // cantidad válida en ring
  uint32_t start_date;
  uint16_t start_min;
  uint32_t end_date;
  uint16_t end_min;
  int32_t min_lat_e7, max_lat_e7;
  int32_t min_lon_e7, max_lon_e7;
  TrackPoint flush_buf[TRACK_CHUNK_POINTS];
  uint8_t flush_count;
  uint32_t last_flush_ms;
  uint8_t chunk_count;
};

static TrackSession g_tracks[3];
static uint8_t g_track_slot = 0; // sesión actual
```

### B) Helpers NVS (claves cortas)
```cpp
// Namespace sugerido: "dogrgb_trk"
static void key_meta(char *out, uint8_t slot) { snprintf(out, 5, "t%um", slot); }
static void key_chunk(char *out, uint8_t slot, uint8_t idx) { snprintf(out, 6, "t%uc%02u", slot, idx); }
// global: "t_ver", "t_idx", "t_open"
```

### C) Boot y rotación de sesión
```cpp
void track_begin() {
  Preferences &prefs = storage::prefs_trk(); // nuevo namespace
  uint8_t ver = prefs.getUChar("t_ver", 0);
  if (ver != TRACK_VER) {
    track_clear_all(prefs); // borra t0m/t1m/t2m + tXc**
    prefs.putUChar("t_ver", TRACK_VER);
  }

  uint8_t was_open = prefs.getUChar("t_open", 0);
  uint8_t prev_slot = prefs.getUChar("t_idx", 0);
  if (was_open) {
    // cerrar sesión anterior (boot => nueva sesión)
    TrackMeta meta = track_load_meta(prefs, prev_slot);
    meta.open = 0;
    track_save_meta(prefs, prev_slot, meta);
    prefs.putUChar("t_open", 0);

    // rotar slot
    g_track_slot = (prev_slot + 1) % 3;
    prefs.putUChar("t_idx", g_track_slot);
    track_clear_slot(prefs, g_track_slot);
  } else {
    g_track_slot = prev_slot;
  }

  track_session_reset_ram(g_track_slot);
  track_open_meta(prefs, g_track_slot); // crea meta inicial open=1
  prefs.putUChar("t_open", 1);
}
```

### D) Captura de puntos en GPS (hook)
```cpp
void track_try_add_point(float lat_deg, float lon_deg, uint16_t t_min, uint32_t date_yyyymmdd) {
  TrackSession &ts = g_tracks[g_track_slot];
  const int32_t lat_e7 = (int32_t)lroundf(lat_deg * 1e7f);
  const int32_t lon_e7 = (int32_t)lroundf(lon_deg * 1e7f);

  if (ts.count == 0) {
    ts.start_date = date_yyyymmdd;
    ts.start_min = t_min;
    ts.min_lat_e7 = ts.max_lat_e7 = lat_e7;
    ts.min_lon_e7 = ts.max_lon_e7 = lon_e7;
  }
  ts.end_date = date_yyyymmdd;
  ts.end_min = t_min;
  ts.min_lat_e7 = min(ts.min_lat_e7, lat_e7);
  ts.max_lat_e7 = max(ts.max_lat_e7, lat_e7);
  ts.min_lon_e7 = min(ts.min_lon_e7, lon_e7);
  ts.max_lon_e7 = max(ts.max_lon_e7, lon_e7);

  // Ring buffer 2h
  if (ts.count < TRACK_MAX_POINTS) {
    uint16_t idx = (ts.head + ts.count) % TRACK_MAX_POINTS;
    ts.ring[idx] = {lat_e7, lon_e7, t_min};
    ts.count++;
  } else {
    // overwrite oldest
    ts.ring[ts.head] = {lat_e7, lon_e7, t_min};
    ts.head = (ts.head + 1) % TRACK_MAX_POINTS;
  }

  // Buffer para persistencia
  if (ts.flush_count < TRACK_CHUNK_POINTS) {
    ts.flush_buf[ts.flush_count++] = {lat_e7, lon_e7, t_min};
  }
}
```

Integración con `gps.cpp` (bloque de distancia):
```cpp
if (gps_trusted_fix && speed_kph <= SPEED_MAX_VALID_KPH) {
  if (now_ms - last_sample_ms >= GPS_SAMPLE_MS) {
    last_sample_ms = now_ms;
    // ... lógica de min_segment_m + distancia ...
    if (segment_m >= min_segment_m && segment_m < 50.0f) {
      // agrega punto sólo si se mueve lo suficiente
      track_try_add_point(lat_deg, lon_deg, time_min, date_yyyymmdd);
    }
  }
}
```

### E) Flush por chunks (persistencia eficiente)
```cpp
void track_flush_if_due(uint32_t now_ms) {
  TrackSession &ts = g_tracks[g_track_slot];
  if (ts.flush_count == 0) return;
  if ((now_ms - ts.last_flush_ms) < TRACK_FLUSH_MS &&
      ts.flush_count < TRACK_CHUNK_POINTS) {
    return;
  }

  Preferences &prefs = storage::prefs_trk();
  uint8_t chunk_idx = ts.chunk_count;
  char key[6];
  key_chunk(key, g_track_slot, chunk_idx);

  TrackChunkHeader hdr = {};
  hdr.count = ts.flush_count;
  hdr.first_t_min = ts.flush_buf[0].t_min;
  hdr.flags = 0;
  // hdr.crc = xor/CRC opcional

  // Serialize header + points into a small buffer
  uint8_t blob[sizeof(TrackChunkHeader) + TRACK_CHUNK_POINTS * sizeof(TrackPoint)];
  memcpy(blob, &hdr, sizeof(hdr));
  memcpy(blob + sizeof(hdr), ts.flush_buf, ts.flush_count * sizeof(TrackPoint));

  prefs.putBytes(key, blob, sizeof(hdr) + ts.flush_count * sizeof(TrackPoint));
  ts.chunk_count++;
  ts.flush_count = 0;
  ts.last_flush_ms = now_ms;

  // Update meta on flush only
  TrackMeta meta = track_load_meta(prefs, g_track_slot);
  meta.open = 1;
  meta.sample_ms = TRACK_SAMPLE_MS;
  meta.max_points = TRACK_MAX_POINTS;
  meta.chunk_count = ts.chunk_count;
  meta.total_points = min<uint16_t>(meta.total_points + hdr.count, TRACK_MAX_POINTS);
  meta.start_date = ts.start_date;
  meta.start_min = ts.start_min;
  meta.end_date = ts.end_date;
  meta.end_min = ts.end_min;
  meta.min_lat_e7 = ts.min_lat_e7;
  meta.max_lat_e7 = ts.max_lat_e7;
  meta.min_lon_e7 = ts.min_lon_e7;
  meta.max_lon_e7 = ts.max_lon_e7;
  track_save_meta(prefs, g_track_slot, meta);
}
```

### F) Iteración de puntos (para JSON/CSV/GeoJSON)
```cpp
// Itera chunks persistidos + buffer RAM actual (en orden cronológico)
template <typename F>
void track_iter_points(uint8_t slot, uint16_t max_points, F cb) {
  Preferences &prefs = storage::prefs_trk();
  TrackMeta meta = track_load_meta(prefs, slot);
  uint16_t total = meta.total_points;
  uint16_t stride = 1;
  if (max_points > 0 && total > max_points) {
    stride = (total + max_points - 1) / max_points;
  }

  uint16_t i = 0;
  for (uint8_t c = 0; c < meta.chunk_count; ++c) {
    char key[6];
    key_chunk(key, slot, c);
    uint8_t buf[512]; // tamaño ajustado al chunk
    size_t len = prefs.getBytes(key, buf, sizeof(buf));
    if (len <= sizeof(TrackChunkHeader)) continue;
    TrackChunkHeader hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    TrackPoint *pts = reinterpret_cast<TrackPoint *>(buf + sizeof(hdr));
    for (uint8_t p = 0; p < hdr.count; ++p) {
      if ((i++ % stride) == 0) cb(pts[p]);
    }
  }

  // Si es sesión actual, sumar RAM no persistida
  if (slot == g_track_slot) {
    TrackSession &ts = g_tracks[g_track_slot];
    for (uint8_t p = 0; p < ts.flush_count; ++p) {
      if ((i++ % stride) == 0) cb(ts.flush_buf[p]);
    }
  }
}
```

### G) HTTP: JSON / CSV / GeoJSON (streaming)
```cpp
void handle_track_json() {
  TrackView v = track_get_view(session_from_query());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"count\":");
  server.sendContent(String(v.count));
  server.sendContent(",\"points\":[");
  bool first = true;
  track_iter_points(v.slot, v.max_points, [&](const TrackPoint &p){
    if (!first) server.sendContent(",");
    first = false;
    char line[64];
    const float lat = p.lat_e7 * 1e-7f;
    const float lon = p.lon_e7 * 1e-7f;
    snprintf(line, sizeof(line), "[%.7f,%.7f]", lat, lon);
    server.sendContent(line);
  });
  server.sendContent("]}");
}

void handle_track_csv() {
  TrackView v = track_get_view(session_from_query());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("date,min,lat,lon\n");
  track_iter_points(v.slot, v.max_points, [&](const TrackPoint &p){
    char line[64];
    const float lat = p.lat_e7 * 1e-7f;
    const float lon = p.lon_e7 * 1e-7f;
    snprintf(line, sizeof(line), "%u,%u,%.7f,%.7f\n", v.date, p.t_min, lat, lon);
    server.sendContent(line);
  });
}

void handle_track_geojson() {
  TrackView v = track_get_view(session_from_query());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/geo+json", "");
  server.sendContent("{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\",\"coordinates\":[");
  bool first = true;
  track_iter_points(v.slot, v.max_points, [&](const TrackPoint &p){
    if (!first) server.sendContent(",");
    first = false;
    char line[64];
    const float lat = p.lat_e7 * 1e-7f;
    const float lon = p.lon_e7 * 1e-7f;
    snprintf(line, sizeof(line), "[%.7f,%.7f]", lon, lat); // GeoJSON: lon,lat
    server.sendContent(line);
  });
  server.sendContent("]}},\"properties\":{}}]}");
}
```

### H) UI (JS) – fetch + render
```js
async function loadTrack(sessionId){
  const res = await fetch(`/api/track?session=${sessionId}&max_points=400`);
  const data = await res.json();
  if (!data || data.count < 2) { showEmpty(); return; }
  drawTrackCanvas(data.points, data.bbox);
}

function drawTrackCanvas(points, bbox){
  const canvas = document.getElementById('track_map');
  const ctx = canvas.getContext('2d');
  const pad = 12;
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0,0,w,h);
  // proyectar bbox -> XY
  const minLat=bbox.min_lat, maxLat=bbox.max_lat;
  const minLon=bbox.min_lon, maxLon=bbox.max_lon;
  const scaleX = (w-2*pad)/(maxLon-minLon || 1);
  const scaleY = (h-2*pad)/(maxLat-minLat || 1);
  const scale = Math.min(scaleX, scaleY);

  ctx.beginPath();
  points.forEach((p, i)=>{
    const lat = p[0], lon = p[1];
    const x = pad + (lon - minLon) * scale;
    const y = h - (pad + (lat - minLat) * scale);
    if (i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
  });
  ctx.strokeStyle = '#0B1220';
  ctx.lineWidth = 2;
  ctx.stroke();
}
```

## Riesgos y mitigaciones
- **RAM**: si el heap es bajo, subir `TRACK_SAMPLE_MS` o reducir `TRACK_MAX_POINTS`.
- **Flash wear**: usar chunks y escritura cada 10–20s; evitar actualizar meta en cada punto.
- **Acumulación de claves**: limpiar `tXc*` al rotar sesión para no dejar basura.
- **JSON grande**: limitar `max_points` o usar decimación en firmware.
- **Sesión > 2h**: dejar claro en UI que es una ventana de 2 horas.

## Archivos clave para implementar
- GPS: `Platformio/Dog-RGB/src/gps/gps.cpp`, `Platformio/Dog-RGB/include/gps/gps.h`
- Portal HTTP: `Platformio/Dog-RGB/src/web/portal_http.cpp`
- UI portal: `Platformio/Dog-RGB/src/web/pages.cpp`
- Referencias: `docs/architecture.md`, `docs/wifi_portal_spec.md`, `docs/web_portal_spec.md`, `docs/PLANS/2026-02-03_historial-3-sesiones.md`
