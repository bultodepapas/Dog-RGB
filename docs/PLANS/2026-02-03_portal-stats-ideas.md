# Plan — Ideas para ampliar estadisticas en el portal AP

Este plan propone ideas nuevas para mostrar estadisticas del collar en la UI del access point. No implementa cambios.

---

## 1) Contexto rapido del repo (vista general)

- Firmware principal en `Platformio/Dog-RGB/src/main.cpp` con GNSS, LED UI, portal Wi-Fi (AP/STA) y BLE summary.
- Especificaciones de portal en `docs/web_portal_spec.md` y `docs/wifi_portal_spec.md`.
- Resumen diario expuesto en `/api/summary` y persistido en NVS (`prefs`).
- Portal actual embebido en `html_page()` con 3 metricas basicas.

## 2) Estadisticas actuales (como se calculan y exponen)

- Distancia diaria `total_distance_m` (Haversine, muestreo cada `GPS_SAMPLE_MS`, filtro de picos > 50 m, solo con fix).
- Tiempo activo `active_time_ms` (incrementa cuando velocidad > `SPEED_ACTIVE_KPH`).
- Velocidad maxima diaria `max_speed_kph` y promedio (distancia/tiempo activo).
- Fecha GPS `current_date_yyyymmdd` y `last_update_min` (minutos desde medianoche GPS).
- Estado GPS `has_gps_fix`.
- Datos GNSS disponibles en RAM pero no expuestos: `gps_sats`, `gps_fix_quality`, `last_speed_kph`.
- Geofence/home disponible via `/api/home` con distancia a casa.

## 3) Ideas nuevas para mostrar en el portal AP

- Mostrar fecha del resumen `date` en formato legible. Valor: evita confusion entre dias. Requiere: UI.
- Mostrar tiempo activo estimado = distancia / velocidad promedio (si avg > 0). Valor: contexto de paseo. Requiere: UI.
- Mostrar ritmo medio (min/km) derivado de la velocidad promedio. Valor: lectura rapida. Requiere: UI.
- Mostrar indice de actividad (p. ej. max/avg o avg vs umbral). Valor: intensidad simple. Requiere: UI.
- Exponer `active_time_ms` y mostrar tiempo activo real. Valor: precision. Requiere: agregar campo en `/api/summary`.
- Exponer `last_speed_kph` y mostrar velocidad actual. Valor: feedback inmediato. Requiere: agregar campo en `/api/summary`.
- Exponer `gps_sats` y `gps_fix_quality`. Valor: confianza de GPS. Requiere: agregar campos en `/api/summary`.
- Mostrar edad del ultimo fix (segundos desde `gps_last_fix_ms`). Valor: diagnostico rapido. Requiere: nuevo campo.
- Integrar `/api/home` para mostrar distancia a casa y estado home. Valor: seguridad/perimetro. Requiere: UI + fetch adicional.
- Agregar tiempo por rangos de velocidad. Valor: perfil de actividad diario. Requiere: nuevos acumuladores.
- Agregar conteo de pausas (transiciones activo/inactivo). Valor: comportamiento. Requiere: nueva logica.
- Guardar top 3 picos de velocidad con hora. Valor: momentos destacados. Requiere: buffer y persistencia.
- IMU para clasificacion (reposo/caminar/correr). Valor: actividad indoor. Requiere: sensor + firmware.
- Sensor HR para FC promedio y max. Valor: salud. Requiere: sensor + calibracion.
- Lectura de bateria/voltaje. Valor: mantenimiento. Requiere: circuito + firmware.

## 4) Priorizacion sugerida

- P1 (rapido): fecha + tiempo activo estimado + ritmo medio en UI.
- P1 (firmware minimo): exponer `active_time_ms`, `last_speed_kph`, `gps_sats`.
- P2: integrar `/api/home` en la UI.
- P3: histograma por rangos y picos.
