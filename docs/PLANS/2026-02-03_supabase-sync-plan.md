# Plan — Sync automatico a Supabase cuando conecta Wi-Fi (STA)

Este plan describe como subir estadisticas a Supabase al conectar a internet de casa y como capturar credenciales desde el portal AP. No implementa cambios.

---

## 1) Contexto actual (repo)

- Firmware en `Platformio/Dog-RGB/src/main.cpp` con portal Wi-Fi AP/STA, resumen diario y NVS.
- Resumen diario expuesto via `GET /api/summary` (ver `docs/wifi_portal_spec.md` y `docs/web_portal_spec.md`).
- Config runtime via `/config` y `GET/POST /api/config` (ver `docs/portal_config.md`).
- No existe backend hoy; futura fase menciona app puente BLE (ver `docs/web_portal_spec.md`).

## 2) Objetivo y alcance

- Objetivo: al conectarse a Wi-Fi STA (casa), subir automaticamente el resumen diario a Supabase para luego procesarlo y mostrarlo en una web.
- Alcance: firmware (sync), portal AP (credenciales), backend Supabase (schema + RLS) y web basica de lectura.
- No alcance: tracking en tiempo real, multi-usuario complejo, ni reemplazo del portal local.

## 3) Arquitectura propuesta (alto nivel)

- Collar (ESP32) -> Wi-Fi STA -> Supabase (REST o Edge Function) -> Web.
- Flujo:
  1) STA conectado y con IP.
  2) Construir payload del resumen diario.
  3) Enviar a Supabase (insert/upsert).
  4) Persistir marcador de upload para evitar duplicados.

## 4) Supabase: modelo de datos y seguridad

- Tabla sugerida: `collar_stats_daily`.
  - `id` uuid (default), `device_id` text/uuid.
  - `date_yyyymmdd` int.
  - `distance_m` int, `avg_speed_cmps` int, `max_speed_cmps` int.
  - `last_update_min` int, `gps_fix` bool, `has_data` bool.
  - `fw_version` text, `created_at` timestamptz default now().
- Unico: `(device_id, date_yyyymmdd)` para permitir upsert sin duplicar.
- Tabla `devices`: `device_id`, `owner_user_id`, `upload_token` (hash o token), `label`.
- RLS:
  - Insercion solo si `device_id` existe y token valida.
  - Lectura solo del usuario owner (via auth) o vista publica si se decide.

## 5) Credenciales en el portal AP (UI + API)

- Nueva seccion "Cloud Sync" en `/config`.
- Campos minimos:
  - `cloud_enabled` (toggle).
  - `supabase_url` (base URL del proyecto).
  - `supabase_anon_key` o `function_url` (segun enfoque).
  - `device_id` (UUID o string estable; prellenar con MAC/UID si no existe).
  - `upload_token` (token por dispositivo, opcional si se usa anon + RLS simple).
- Validaciones basicas en frontend y backend.
- En `GET /api/config`: no devolver secretos completos; devolver banderas tipo `has_supabase_key`.
- Persistencia en NVS:
  - Opcion A: namespace nuevo `dogrgb_cloud` (evita tocar `CONFIG_VERSION`).
  - Opcion B: extender `RuntimeConfig` y subir `CONFIG_VERSION`.
  - Mantener claves <= 15 chars (limite NVS).

## 6) Firmware: logica de sincronizacion

- Condicion de disparo:
  - `wifi_sta_connected == true` y `WiFi.status() == WL_CONNECTED`.
  - Opcional: resolver DNS o ping ligero a `supabase_url` antes de enviar.
- Estrategia de envio:
  - Subir al conectar (1 vez) y luego reintentos con backoff si falla.
  - Upsert por `device_id + date_yyyymmdd` para permitir reenvios sin duplicados.
- Manejo de cambio de dia:
  - Antes de resetear metricas al detectar nuevo `date_yyyymmdd`, guardar snapshot del dia anterior en NVS (pending upload).
  - Si no hay Wi-Fi en el momento, enviar ese snapshot cuando vuelva la conexion.
- TLS/HTTPS:
  - Supabase requiere HTTPS. Usar `WiFiClientSecure` con cert pinning si es posible.
  - Alternativa MVP: `setInsecure()` con riesgo documentado (no ideal).

## 7) API de subida (dos opciones)

- Opcion A (simple): PostgREST directo.
  - POST a `/rest/v1/collar_stats_daily`.
  - Headers: `apikey`, `Authorization: Bearer <anon_key>`.
  - Requiere RLS bien configurado + token por dispositivo si se quiere seguridad extra.
- Opcion B (mas segura): Edge Function.
  - Device llama a `https://<project>.supabase.co/functions/v1/upload`.
  - Payload firmado con `upload_token` o HMAC.
  - Function valida y usa service role key en el server, no en el device.

## 8) Procesamiento y web

- Crear vista SQL `collar_stats_daily_view` con conversiones (km, km/h, tiempo activo si existe).
- Web basica:
  - Login Supabase (owner_user_id).
  - Tabla o cards por fecha.
  - Graficas simples (distancia, max speed, promedio).
- Future: agregar historico multi-collar por usuario.

## 9) Validaciones y pruebas

- Conectar a Wi-Fi STA y verificar envio (200/201) y upsert correcto.
- Cortar Wi-Fi durante envio y confirmar reintento/backoff.
- Cambiar de dia sin Wi-Fi y luego reconectar: snapshot pendiente se sube.
- Verificar que credenciales no se exponen en `GET /api/config`.
- Verificar que RLS bloquea inserts sin token valido.

## 10) Fases sugeridas

1. Supabase setup: tablas, indices, RLS, token por device, (opcional) Edge Function.
2. Firmware: storage de credenciales + logica de upload + snapshot al cambio de dia.
3. Portal AP: UI de Cloud Sync + validaciones + manejo seguro de secretos.
4. Web: dashboard de lectura y procesamiento basico.

## 11) Preguntas abiertas

- Seguridad: Opcion A (PostgREST) o B (Edge Function)?
- Formato de `device_id`: UUID generado, MAC, o definido por usuario?
- Frecuencia de upload: solo al conectar o tambien periodica mientras haya Wi-Fi?
- Datos extra a subir: `active_time_ms`, `gps_sats`, `gps_fix_quality`?
