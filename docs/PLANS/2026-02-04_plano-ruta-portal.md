# Plan: Plano de ruta GPS en portal AP/STA

## Objetivo
Mostrar un **plano simple de la ruta** recorrida por el perro dentro del portal Wi‑Fi (AP/STA), sin depender de Internet. El plano debe poder abrirse en `http://192.168.4.1` (AP) y, si hay STA, también via mDNS. El resultado esperado es una línea de recorrido con inicio/fin y escala, integrada en la UI actual.

## Resumen del estado actual (repo)
- **GPS**: `Platformio/Dog-RGB/src/gps/gps.cpp` parsea RMC/GGA, calcula métricas diarias y de sesión, filtra por calidad y por segmentos (`min_segment_m`, `HDOP`, `SPEED_MAX_VALID_KPH`, `GPS_SAMPLE_MS`).
- **Persistencia**: sólo métricas y resumen de sesiones en NVS (`docs/architecture.md`). **No hay histórico de puntos GPS**; sólo `last/current lat/lon` en RAM.
- **Portal AP/STA**:
  - API y rutas HTTP en `Platformio/Dog-RGB/src/web/portal_http.cpp`.
  - UI HTML/CSS/JS embebida en `Platformio/Dog-RGB/src/web/pages.cpp`.
  - Endpoints existentes: `/api/summary`, `/api/status`, `/api/dev`, `/api/config`, `/api/home`.
- **Restricción clave**: en modo AP no hay Internet, por lo que un mapa con tiles externos no es confiable. El “plano” debe ser **offline**.

## Decisiones base
1. **Plano offline (sin tiles)**: Canvas/SVG con fondo simple y grid opcional. Proyección equirectangular de lat/lon a XY local.
2. **Ruta de sesión actual en RAM**: ring buffer de puntos GPS, con tamaño acotado. Persistencia opcional (fase posterior).
3. **Filtro de puntos**: reutilizar la misma lógica de validación de `gps.cpp` (fix confiable + `min_segment_m` + `SPEED_MAX_VALID_KPH`) para no dibujar ruido.

## Plan de implementación (propuesto)

### 1) Captura de puntos de ruta (firmware)
**Archivos:** `Platformio/Dog-RGB/src/gps/gps.cpp`, `Platformio/Dog-RGB/include/gps/gps.h`

- **Agregar un ring buffer** para track:
  - `struct TrackPoint { int32_t lat_e7; int32_t lon_e7; uint16_t t_min; }`.
  - `lat_e7/lon_e7` en micro‑grados (1e‑7) para compactar y evitar floats en el buffer.
  - `t_min` opcional (para mostrar timestamps simples o tooltips).
- **Inserción de puntos**: en el mismo bloque donde se actualiza distancia (cuando pasa el filtro de `gps_trusted_fix` + `min_segment_m`), agregar al buffer.
- **Reset del track**: cuando se reinicia el día (`date_yyyymmdd` cambia) o cuando inicia una nueva sesión.
- **Límites de memoria**: empezar con 600–1200 puntos (≈ 4–9 KB). Con 1 Hz, esto cubre 10–20 minutos. Ajustar con pruebas reales.
- **API interna**: funciones en `gps.h`:
  - `bool track_has_data()`
  - `uint16_t track_count()`
  - `void track_snapshot(TrackPoint *out, uint16_t max, TrackMeta *meta)` o `String build_track_json(uint16_t max_points)`

### 2) Endpoint JSON para ruta
**Archivos:** `Platformio/Dog-RGB/src/web/portal_http.cpp`

- Nuevo endpoint: `GET /api/track`.
- JSON propuesto:
  ```json
  {
    "date": 20260204,
    "start_min": 540,
    "end_min": 615,
    "count": 128,
    "bbox": {"min_lat": -34.60, "max_lat": -34.59, "min_lon": -58.39, "max_lon": -58.38},
    "points": [[-34.6037,-58.3816], ...]
  }
  ```
- **Opcional**: `?max_points=200` para downsample en firmware si el buffer es grande.
- **Formato compacto** (futuro): polyline/delta encoding para reducir JSON (no necesario en fase 1).

### 3) UI del plano en el portal
**Archivos:** `Platformio/Dog-RGB/src/web/pages.cpp`

- Añadir un **card** en la página principal con:
  - `<canvas id="track_map">` y un contenedor con estado ("sin datos", "sin GPS").
  - Botón “Actualizar” reutiliza `refreshAll()` para que también traiga `/api/track`.
- **JS**:
  - `fetch('/api/track')`.
  - Proyección: mapear bbox a canvas, mantener aspect ratio, padding.
  - Dibujar polyline, círculo de inicio (verde), fin (rojo), home si existe (`/api/home`).
  - Mostrar distancia total y escala aproximada (m) en el card.
- **UX**:
  - Si `count < 2`: mostrar “sin ruta disponible”.
  - Si `has_data` false: mostrar “sin datos GPS”.

### 4) Opcionales (fase 2)
- **Persistencia del track** en NVS o en un log circular (cuidado con desgaste). Guardar sólo cada N segundos o con decimación.
- **Exportación**: botón para descargar CSV/GeoJSON desde `/api/track.csv` o `/api/track.geojson`.
- **Simplificación**: Douglas‑Peucker en JS para suavizar y reducir puntos (ref. `docs/gps_analysis.md`).
- **Mapa real (si hay Internet)**: detectar STA + conectividad y usar tiles externos con fallback al plano.

## Riesgos y mitigaciones
- **Memoria**: buffer grande puede afectar heap. Mitigar con tamaño fijo pequeño + `max_points`.
- **Ruido GPS**: ya mitigado con `min_segment_m` y `HDOP`; usar mismo filtro para puntos.
- **JSON grande**: limitar puntos y/o simplificar en cliente.
- **AP sin Internet**: usar plano offline por defecto.

## Checklist de validación
- Ruta aparece en AP y STA.
- Sin fix → muestra “sin GPS” sin romper UI.
- Cambia de día → track se limpia y comienza nuevo.
- Con 200–500 puntos, la UI sigue fluida.

## Archivos clave para implementar
- GPS: `Platformio/Dog-RGB/src/gps/gps.cpp`, `Platformio/Dog-RGB/include/gps/gps.h`
- Portal HTTP: `Platformio/Dog-RGB/src/web/portal_http.cpp`
- UI portal: `Platformio/Dog-RGB/src/web/pages.cpp`
- Referencias: `docs/architecture.md`, `docs/wifi_portal_spec.md`, `docs/web_portal_spec.md`
