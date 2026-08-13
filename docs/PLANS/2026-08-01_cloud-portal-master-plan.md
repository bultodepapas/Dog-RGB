# Dog-RGB Home Terminal: plan maestro del portal cloud

> **Document status:** Optional long-range proposal (Spanish), not implemented. The supported product remains local-first and fully usable without a backend. Review [Roadmap](../roadmap.md) before expanding scope.

> Estado: propuesta detallada, lista para revisión técnica antes de implementar  
> Fecha de investigación y redacción: 2026-08-01  
> Alcance: firmware ESP32-S3, portal AP local, API de ingestión, Supabase, Vercel, mapas, estadísticas, seguridad, pruebas y despliegue  
> Proyecto: DIY/personal; se priorizan sencillez operativa, recuperabilidad y costes bajos  
> Sustituye conceptualmente, sin borrar, el alcance limitado de `2026-02-03_supabase-sync-plan.md`

## 1. Resumen ejecutivo

Dog-RGB ya es un collar inteligente funcional: registra GPS, calcula métricas,
mantiene un portal local AP/STA, persiste configuración y rutas en NVS y puede
exportar datos localmente. Este plan añade una capa cloud para que, cuando el
collar regrese a casa, se conecte al Wi-Fi doméstico y sincronice automáticamente
los recorridos con la cuenta de su propietario.

La arquitectura propuesta es:

```text
Dog-RGB ESP32-S3
  |
  | HTTPS + credencial propia del dispositivo
  v
Vercel / Next.js
  |-- portal web
  |-- POST /api/device/v1/claim
  |-- POST /api/device/v1/batches
  `-- POST /api/device/v1/rotate
          |
          | conexión PostgreSQL pooled y de privilegios mínimos
          v
Supabase
  |-- Auth passwordless
  |-- PostgreSQL
  |-- PostGIS
  |-- Row Level Security
  |-- pgTAP
  `-- pg_cron

Navegador
  `-- Google Maps JavaScript, cargado solo al abrir un recorrido
```

Decisiones principales:

1. Usar Supabase para Auth, PostgreSQL, PostGIS y RLS.
2. No conectar el ESP32 directamente a Supabase/PostgREST.
3. Hospedar portal y API de dispositivo en un solo proyecto Next.js/Vercel.
4. Introducir en el AP un código temporal de vinculación, no el secreto
   permanente.
5. Hacer que el collar genere y persista su secreto permanente antes del claim,
   evitando perder la credencial por un corte de energía.
6. Evolucionar el formato de ruta local de v2 a v3 antes de ofrecer gráficas por
   segundos y fases de velocidad.
7. Mantener los datos GPS crudos inmutables y separar los resúmenes derivados.
8. Presentar el producto como sincronización al llegar a casa, no seguimiento
   en tiempo real.
9. Cargar Google Maps solamente en la pantalla de detalle y no usar Directions,
   Roads, Places ni geocodificación en el MVP.
10. Mantener una UI CRT negra/verde coherente con el portal local existente,
    accesible y sin una cuadrícula genérica de tarjetas.

### Índice del documento

1. [Resumen ejecutivo](#1-resumen-ejecutivo)
2. [Estado actual confirmado](#2-estado-actual-confirmado-del-repositorio)
3. [Objetivo de producto](#3-objetivo-de-producto)
4. [Principios técnicos](#4-principios-de-diseño-técnico)
5. [Decisiones de arquitectura](#5-decisiones-de-arquitectura)
6. [Estructura del repositorio](#6-estructura-objetivo-del-repositorio)
7. [Identidad y claim](#7-identidad-claim-y-credenciales)
8. [Track v3](#8-track-v3)
9. [Motor cloud](#9-motor-cloud-del-firmware)
10. [Presión de almacenamiento](#10-política-de-almacenamiento-bajo-presión)
11. [API del dispositivo](#11-api-de-dispositivo)
12. [Ingestión](#12-ingestión-transaccional)
13. [PostgreSQL/Supabase](#13-modelo-postgresqlsupabase)
14. [Finalización](#14-finalización-de-sesiones)
15. [Estadísticas](#15-estadísticas-y-fases)
16. [Portal web](#16-portal-web)
17. [Copy](#17-copy-de-producto)
18. [Mapas](#18-mapas)
19. [Gráficas](#19-gráficas)
20. [Portal AP](#20-portal-ap-cambios-cloud)
21. [Seguridad](#21-seguridad)
22. [Privacidad](#22-privacidad-y-ciclo-de-vida)
23. [Capacidad y costes](#23-capacidad-y-costes)
24. [Observabilidad](#24-observabilidad)
25. [Problemas](#25-matriz-de-problemas)
26. [Pruebas](#26-estrategia-de-pruebas)
27. [Criterios cuantitativos](#27-criterios-cuantitativos)
28. [Fases](#28-fases-de-implementación)
29. [Orden de commits](#29-orden-de-commits)
30. [Checklist](#30-checklist-producción)
31. [Investigación](#31-investigación-y-fuentes)
32. [Preguntas abiertas](#32-preguntas-por-cerrar-en-fase-0)
33. [Recomendación final](#33-recomendación-final)

## 2. Estado actual confirmado del repositorio

### 2.1 Firmware y hardware

El firmware activo vive en `Platformio/Dog-RGB` y utiliza:

- Seeed Studio XIAO ESP32-S3.
- GNSS EBYTE E108-GN02.
- Arduino sobre ESP-IDF.
- ArduinoJson.
- Dos tiras SK6812 RGBW.
- Wi-Fi AP/STA.
- BLE opcional.
- NVS con registros redundantes y CRC.
- Particiones OTA A/B ya reservadas.
- Partición `tracknvs` de `0x30000`, equivalente a 192 KiB.

La tabla de particiones actual está en
[`partitions_dog_rgb.csv`](../../Platformio/Dog-RGB/partitions_dog_rgb.csv).

### 2.2 Portal local existente

El firmware ya expone:

```text
GET /                         portal local
GET /config                   configuración
GET /dev                      diagnóstico
GET /api/status               estado general
GET /api/config               configuración runtime
GET /api/summary              métricas y sesiones
GET /api/track                ruta JSON
GET /api/track.csv            exportación CSV
GET /api/track.geojson        exportación GeoJSON
```

Los endpoints se registran en
[`portal_http.cpp`](../../Platformio/Dog-RGB/src/web/portal_http.cpp), y la UI
embebida CRT vive en
[`pages.cpp`](../../Platformio/Dog-RGB/src/web/pages.cpp).

### 2.3 Wi-Fi actual

El administrador Wi-Fi ya contiene:

- Arranque AP.
- Modo AP+STA.
- Eventos de conexión/desconexión.
- Backoff de STA.
- DNS captive portal.
- Holds para mantener el AP visible durante setup o actividad HTTP.
- Política de ahorro según movimiento e inactividad.
- Persistencia propia de SSID/password.

El XIAO ESP32-S3 dispone de Wi-Fi 2,4 GHz, no 5 GHz. El onboarding debe decirlo
explícitamente.

### 2.4 Ruta local actual

El formato v2 actual es:

```cpp
struct TrackPoint {
  int32_t lat_e7;
  int32_t lon_e7;
  uint16_t t_min;
} __attribute__((packed));
```

Parámetros actuales:

```text
Versión                 2
Slots                   4: tres cerrados y uno actual
Intervalo nominal       5 segundos
Ventana                 2 horas
Máximo                  1.440 puntos por slot
Chunk                   48 puntos
Flush parcial           15 segundos
Integridad              CRC32 en metadata/chunks
```

### 2.5 Limitaciones relevantes

#### Precisión temporal

El punto conserva minutos, no segundos. Varias muestras tomadas dentro de un
mismo minuto resultan indistinguibles temporalmente en cloud.

#### Sin velocidad por punto

La sesión conoce media y máxima, pero no existe una serie de velocidad asociada
a cada coordenada.

#### Puntos estacionarios descartados

`track_try_add_point()` aplica un umbral dinámico de distancia basado en la
configuración y HDOP. Es correcto para reducir ruido de ruta, pero impide medir
de manera fiable intervalos quietos.

#### Acoplamiento entre grabación y actividad

Actualmente la grabación de ruta ocurre dentro del flujo de `active_sample`.
Para fases por velocidad deben separarse dos preguntas:

1. ¿Este fix es suficientemente confiable para guardarse como muestra temporal?
2. ¿Este segmento debe sumar distancia o tiempo activo?

#### Sesiones

Las sesiones actuales corresponden principalmente al ciclo/slot del dispositivo,
no a una detección semántica de paseo. El MVP debe llamarlas `recorridos` o
`sesiones`; no debe afirmar que detecta automáticamente paseos o comportamiento.

#### Coexistencia radio

Wi-Fi y BLE comparten el subsistema de radio. Un POST HTTPS bloqueante dentro del
`loop()` puede afectar GPS, LEDs, BLE, AP o watchdog.

## 3. Objetivo de producto

### 3.1 Historia principal

> Como propietario de Dog-RGB, quiero configurar el Wi-Fi de casa y vincular mi
> collar con una cuenta desde el AP local, para que al regresar a casa el collar
> sincronice automáticamente sus recorridos y yo pueda consultar mapas,
> distancia, velocidad, calidad GPS y fases estimadas de actividad.

### 3.2 Objetivos del MVP

- Login mediante magic link/OTP por email.
- Perfil básico de usuario.
- Perfil del perro.
- Uno o más collares por cuenta, aunque el piloto use solo uno.
- Generación de código temporal de vinculación.
- Entrada de código, SSID y password en el AP local.
- Vinculación recuperable ante reinicios.
- Sincronización por lotes.
- Reintentos idempotentes.
- Historial de recorridos.
- Mapa de detalle.
- Distancia, duración, tiempo activo, velocidad media y máxima confirmada.
- Serie de velocidad.
- Tiempo por fase estimada.
- Calidad GPS.
- Exportación CSV y GeoJSON; GPX puede añadirse después.
- Revocación y nueva vinculación.
- Borrado/exportación de datos.

### 3.3 No objetivos del MVP

- Seguimiento en vivo.
- Red celular.
- Aplicación iOS/Android nativa.
- Alertas push.
- Compartir rutas públicamente.
- Diagnóstico veterinario.
- Clasificación real de comportamiento.
- IMU/TinyML.
- Google Directions API.
- Google Roads API.
- Places o geocodificación.
- Supabase Realtime.
- OTA remoto desde el portal.
- Microservicios.
- Multiempresa, roles administrativos o planes comerciales.

## 4. Principios de diseño técnico

### 4.1 Offline-first del dispositivo

Cloud es un destino de sincronización, no un requisito para registrar GPS. El
collar debe continuar funcionando aunque:

- Supabase esté pausado.
- Vercel esté temporalmente caído.
- El router no tenga Internet.
- Cambie el password de casa.
- El usuario no abra el portal durante semanas.

### 4.2 Acknowledge-after-commit

Un lote se considera subido únicamente cuando la transacción PostgreSQL se ha
confirmado y el collar ha persistido localmente el ACK.

### 4.3 At-least-once + idempotencia

No se intenta conseguir una entrega de red “exactly once”. El dispositivo puede
enviar un lote más de una vez; la base de datos garantiza que solamente tenga un
efecto.

### 4.4 Datos crudos inmutables

Las coordenadas y muestras originales no se corrigen silenciosamente. Las
estadísticas, fases y geometrías son derivados versionados y recalculables.

### 4.5 Privacidad por defecto

Una ruta es privada. No hay enlaces compartibles, perfiles públicos ni tracking
anónimo en el MVP.

### 4.6 Privilegios mínimos

- El navegador usa una clave publishable y sesión de usuario con RLS.
- El ESP32 usa una credencial Dog-RGB propia.
- Vercel usa un rol PostgreSQL específico para claim/ingestión.
- Ningún cliente recibe `service_role`, `sb_secret` ni `DATABASE_URL`.

### 4.7 Complejidad ganada, no especulativa

No se añade particionamiento, colas, Redis, Storage binario, Realtime u otro
proveedor hasta que una métrica o limitación real lo justifique.

## 5. Decisiones de arquitectura

| Decisión | Selección | Motivo |
|---|---|---|
| Framework web | Next.js 16.2 App Router | SSR/RSC, Route Handlers y despliegue directo en Vercel |
| Runtime | Node.js 24 LTS | El repo ya lo fija; producción debe preferir LTS |
| Lenguaje | TypeScript estricto | Contratos y validación de API |
| Hosting | Vercel | Un despliegue para portal y API |
| Auth | Supabase Auth magic link | Menor fricción para proyecto casero |
| DB | Supabase PostgreSQL | Relacional, extensible y fácil de administrar |
| Geoespacial | PostGIS | Point/LineString, distancia y simplificación |
| Seguridad usuario | RLS | Aislamiento en base de datos |
| Ingestión | Route Handler + conexión pooled | Transacciones atómicas sin exponer PostgREST al collar |
| Mapa | Google Maps JS lazy | Demostración visual con pocas cargas |
| Gráficas | uPlot + SVG/HTML propio | Serie temporal ligera y fases accesibles |
| Estilos | CSS Modules + tokens | Control CRT exacto y pocas dependencias |
| Validación | Zod runtime + JSON Schema 2020-12 contractual | Rechazo estricto y fixtures compartidos |
| Tests DB | pgTAP | Verificación automatizada de RLS/esquema |
| Tests E2E | Playwright existente | Reutilizar tooling del repo |

### 5.1 Por qué no usar PostgREST directamente desde el ESP32

- Acopla el firmware al esquema físico.
- Requiere una clave Supabase en el dispositivo.
- Complica idempotencia y transacciones multi-tabla.
- Dificulta versionar el protocolo.
- Amplía la superficie de entrada.
- Complica revocación y rotación por dispositivo.
- Hace más difícil aplicar límites específicos de IoT.

### 5.2 Por qué no usar Edge Functions en el MVP

Son una alternativa válida, pero crearían una segunda superficie de despliegue,
logs y configuración. El contrato `/api/device/v1/*` permite migrar la
implementación más adelante sin actualizar el dispositivo, siempre que el
dominio público permanezca estable.

### 5.3 Por qué no usar Realtime

El producto no es en vivo. Realtime aumentaría conexiones, estados y consumo sin
mejorar el flujo real de regresar a casa y sincronizar.

## 6. Estructura objetivo del repositorio

```text
Dog-RGB/
|-- apps/
|   `-- portal/
|       |-- app/
|       |   |-- (auth)/
|       |   |   |-- login/page.tsx
|       |   |   `-- auth/callback/route.ts
|       |   |-- (terminal)/
|       |   |   `-- app/
|       |   |       |-- layout.tsx
|       |   |       |-- page.tsx
|       |   |       |-- sessions/page.tsx
|       |   |       |-- sessions/[id]/page.tsx
|       |   |       |-- devices/page.tsx
|       |   |       |-- devices/[id]/page.tsx
|       |   |       `-- settings/page.tsx
|       |   `-- api/
|       |       |-- device/v1/claim/route.ts
|       |       |-- device/v1/batches/route.ts
|       |       |-- device/v1/rotate/route.ts
|       |       `-- app/v1/sessions/[id]/samples/route.ts
|       |-- components/
|       |   |-- terminal/
|       |   |-- onboarding/
|       |   |-- maps/
|       |   `-- charts/
|       |-- lib/
|       |   |-- auth/
|       |   |-- db/
|       |   |-- telemetry/
|       |   |-- validation/
|       |   `-- maps/
|       |-- styles/
|       |-- tests/
|       `-- package.json
|-- contracts/
|   `-- telemetry/
|       |-- claim-v1.schema.json
|       |-- batch-v1.schema.json
|       |-- problem-v1.schema.json
|       `-- examples/
|-- supabase/
|   |-- config.toml
|   |-- migrations/
|   |-- seed.sql
|   `-- tests/database/
|-- Platformio/Dog-RGB/
|   |-- include/cloud/
|   |   |-- cloud_config.h
|   |   |-- cloud_sync.h
|   |   |-- cloud_protocol.h
|   |   `-- cloud_time.h
|   `-- src/cloud/
|       |-- cloud_config.cpp
|       |-- cloud_sync.cpp
|       |-- cloud_protocol.cpp
|       `-- cloud_time.cpp
`-- package.json
```

El `package.json` raíz se puede convertir en un npm workspace pequeño sin
eliminar los scripts Playwright existentes:

```json
{
  "private": true,
  "workspaces": ["apps/*"]
}
```

## 7. Identidad, claim y credenciales

### 7.1 Amenaza resuelta

Un diseño donde el servidor genera y devuelve el secreto permanente tiene una
ventana peligrosa:

```text
servidor consume claim -> responde secreto -> dispositivo pierde energía
```

El servidor cree que el collar quedó vinculado, pero el collar no pudo guardar
la credencial.

### 7.2 Solución seleccionada

El collar genera y persiste `device_id`, `device_secret` y `claim_nonce` antes de
realizar el request.

### 7.3 Código temporal

Formato sugerido:

```text
DGR-7K4M-2PXQ-V9WA-J3FN
```

Propiedades:

- 80 bits aleatorios en Base32.
- Legible y copiable.
- Expira en 30 minutos.
- Máximo 10 intentos.
- Se muestra una vez.
- Se guarda en servidor como `HMAC-SHA-256(CLAIM_PEPPER, normalized_code)`.
- No aparece en URL, analytics o logs.

### 7.4 Secreto permanente

Formato conceptual:

```text
dgr_live_<43 caracteres base64url>
```

Propiedades:

- 256 bits aleatorios.
- Generado con RNG de hardware mientras el subsistema RF está activo.
- Persistido en registro NVS A/B antes del claim.
- Transmitido una sola vez durante el claim, siempre mediante HTTPS verificado.
- Guardado en servidor como `HMAC-SHA-256(DEVICE_PEPPER, token)`.
- Rotable y revocable.
- Nunca devuelto por APIs de lectura.

### 7.5 Secuencia de vinculación

```text
Usuario      Portal/Vercel       Collar/AP          PostgreSQL
   |               |                 |                    |
   |-- login ----->|                 |                    |
   |               |-- crear claim ---------------------->|
   |<- código -----|                 |                    |
   |                                 |                    |
   |-- conecta AP ------------------>|                    |
   |-- SSID/pass/código ------------>|                    |
   |                                 | genera device_id   |
   |                                 | genera secret      |
   |                                 | guarda A/B         |
   |                                 |                    |
   |                                 |-- POST claim ----->|
   |                                 |                    | consume claim
   |                                 |                    | guarda hash
   |                                 |<---- success ------|
   |                                 | marca confirmado   |
   |                                 | borra claim local  |
```

### 7.6 Pseudocódigo del collar

```cpp
ProvisionResult provision_cloud(const String &claim_code) {
  CloudCredentialRecord cred = cloud_config::load();

  if (!cred.valid) {
    cred.version = CLOUD_CREDENTIAL_VERSION;
    fill_random_uuid_v4(cred.device_id);
    esp_fill_random(cred.device_secret, sizeof(cred.device_secret));
    esp_fill_random(cred.claim_nonce, sizeof(cred.claim_nonce));
    cred.state = CloudCredentialState::CLAIM_PENDING;

    if (!cloud_config::save_ab(cred)) {
      return ProvisionResult::PERSIST_FAILED;
    }

    CloudCredentialRecord verify = cloud_config::load();
    if (!constant_time_equal(cred, verify)) {
      return ProvisionResult::VERIFY_FAILED;
    }
  }

  if (!cloud_config::save_temporary_claim(claim_code)) {
    return ProvisionResult::CLAIM_SAVE_FAILED;
  }

  cloud_sync::request_wakeup();
  return ProvisionResult::PENDING;
}
```

### 7.7 Claim idempotente

Si la respuesta se pierde, el collar reenvía exactamente:

- El mismo claim code.
- El mismo `device_id`.
- El mismo `device_secret`.
- El mismo `claim_nonce`.

El servidor permite devolver éxito si el claim ya fue consumido por ese mismo
dispositivo y el hash del secreto coincide. Un dispositivo diferente recibe
`409 claim_already_used`.

### 7.8 Pseudocódigo backend del claim

```ts
async function claimDevice(input: ClaimRequest): Promise<ClaimResponse> {
  ClaimRequestSchema.parse(input);

  const claimHash = hmacSha256(
    env.CLAIM_TOKEN_PEPPER,
    normalizeClaimCode(input.claimCode)
  );

  const secretHash = hmacSha256(
    env.DEVICE_TOKEN_PEPPER,
    input.deviceSecret
  );

  return db.begin(async (tx) => {
    await tx`set local statement_timeout = '5s'`;

    const claim = await tx`
      select *
      from private.device_claim_tokens
      where token_hash = ${claimHash}
      for update
    `;

    if (!claim) throw problem('claim_invalid', 401);
    if (claim.expires_at < new Date()) throw problem('claim_expired', 410);
    if (claim.attempts >= 10) throw problem('claim_locked', 429);

    if (claim.used_at) {
      const existing = await findDeviceByClaim(tx, claim.id);

      if (
        existing.id === input.deviceId &&
        timingSafeEqual(existing.secretHash, secretHash)
      ) {
        return {
          schemaVersion: 1,
          deviceId: existing.id,
          claimed: true,
          duplicate: true
        };
      }

      throw problem('claim_already_used', 409);
    }

    await insertDevice(tx, {
      id: input.deviceId,
      ownerId: claim.owner_id,
      name: claim.pending_name,
      hardwareModel: input.hardwareModel,
      firmwareVersion: input.firmwareVersion
    });

    await insertCredentialHash(tx, input.deviceId, secretHash);
    await markClaimUsed(tx, claim.id, input.deviceId);

    return {
      schemaVersion: 1,
      deviceId: input.deviceId,
      claimed: true,
      duplicate: false
    };
  });
}
```

### 7.9 Rotación y revocación

Revocación:

- El usuario la solicita en el portal.
- `revoked_at` se actualiza inmediatamente.
- El siguiente upload obtiene `401 device_unauthorized`.
- El collar entra a `AUTH_REVOKED` y no reintenta indefinidamente.
- Para recuperarlo se genera un claim nuevo desde el AP físico.

Rotación futura:

1. El collar genera un secreto nuevo y lo persiste como `pending_secret`.
2. Llama `/api/device/v1/rotate` autenticado con el secreto anterior.
3. El servidor activa el nuevo hash y conserva el anterior durante una ventana
   corta de gracia.
4. El collar confirma y elimina el anterior.

## 8. Track v3

### 8.1 Formato

```cpp
struct TrackPointV3 {
  int32_t  lat_e7;       // 4 bytes
  int32_t  lon_e7;       // 4 bytes
  uint32_t utc_s;        // 4 bytes, Unix UTC
  uint16_t speed_cmps;   // 2 bytes
  uint8_t  satellites;  // 1 byte
  uint8_t  flags;        // 1 byte
} __attribute__((packed));

static_assert(sizeof(TrackPointV3) == 16, "Unexpected TrackPointV3 size");
```

### 8.2 Flags

```cpp
enum TrackPointFlags : uint8_t {
  TRACK_FIX_VALID       = 1 << 0,
  TRACK_QUALITY_OK      = 1 << 1,
  TRACK_SPEED_VALID     = 1 << 2,
  TRACK_SEGMENT_MOVING  = 1 << 3,
  TRACK_HDOP_OK         = 1 << 4,
  TRACK_TIME_FROM_GNSS  = 1 << 5,
  TRACK_TIME_FROM_SNTP  = 1 << 6,
  TRACK_LEGACY          = 1 << 7
};
```

HDOP completo no se guarda por punto en v3 para mantener 16 bytes. La sesión
puede conservar HDOP mínimo, medio y máximo, mientras el flag indica si cada
muestra superó la política de calidad.

### 8.3 Presupuesto de almacenamiento

```text
16 bytes/punto * 1.440 puntos * 4 slots = 92.160 bytes crudos
Partición tracknvs                         196.608 bytes
```

El margen restante debe absorber:

- Cabeceras.
- CRC.
- Metadata.
- Nombres/keys NVS.
- Overhead interno.
- Reescritura de chunks parciales.
- ACK y estado cloud.
- Fragmentación.

No se aprueba v3 solamente con el cálculo crudo. Debe existir una prueba que
llene cuatro rutas y mida el espacio real restante.

### 8.4 Metadata de slot

```cpp
struct TrackMetaV3 {
  uint8_t  version;
  uint8_t  flags;
  uint8_t  slot;
  uint8_t  reserved;

  uint8_t  boot_id[16];

  uint16_t point_count;
  uint16_t uploaded_through_seq;
  uint16_t next_batch_seq;
  uint16_t sample_ms;

  uint32_t started_at_utc;
  uint32_t ended_at_utc;
  uint32_t last_flush_ms;

  uint32_t crc32;
};
```

### 8.5 Separar grabación y métricas

```cpp
void on_trusted_fix(const GpsFix &fix) {
  update_current_position(fix);

  // Conserva una muestra temporal si el fix es confiable, incluso quieto.
  track_recorder.try_sample(fix);

  // Decide por separado si suma distancia y actividad.
  motion_metrics.consume(fix);

  geofence.consume(fix);
  led_ui.consume(fix);
}
```

### 8.6 Pseudocódigo de muestreo

```cpp
void TrackRecorder::try_sample(const GpsFix &fix) {
  const uint32_t now_ms = millis();

  if (!fix.position_valid || !fix.time_valid) {
    return;
  }

  if (!elapsed(now_ms, last_sample_ms, TRACK_SAMPLE_MS)) {
    return;
  }

  TrackPointV3 point = {
    .lat_e7 = degrees_to_e7(fix.lat_deg),
    .lon_e7 = degrees_to_e7(fix.lon_deg),
    .utc_s = fix.utc_epoch_s,
    .speed_cmps = kph_to_cmps_clamped(fix.speed_kph),
    .satellites = clamp_u8(fix.satellites),
    .flags = build_track_flags(fix)
  };

  append_to_current_chunk(point);
  update_bbox(point);
  last_sample_ms = now_ms;

  if (chunk_full() || flush_due(now_ms)) {
    persist_chunk_transactionally();
  }
}
```

### 8.7 Hora del dispositivo

Orden de confianza:

1. GNSS UTC válido.
2. SNTP después de conectar STA.
3. Último tiempo de servidor persistido y corregido por uptime.
4. Desconocido.

```cpp
enum class TimeQuality : uint8_t {
  UNKNOWN,
  LAST_SERVER_APPROX,
  SNTP_VALID,
  GNSS_VALID
};
```

Antes de iniciar TLS, el tiempo debe ser plausible respecto a la fecha de build
y expiración de certificados. Si es desconocido, se solicita SNTP de manera no
bloqueante.

### 8.8 Migración v2

#### Estrategia MVP recomendada

Para un prototipo personal sin base instalada:

- Preservar configuración runtime.
- Preservar SSID/password.
- Preservar métricas diarias/sesiones resumidas.
- Invalidar solamente `tracknvs` al detectar versión 2.
- Emitir `TRACK_MIGRATION_V2_RESET`.
- Documentar que se exporten rutas valiosas antes de actualizar.

#### Estrategia robusta posterior

- Mantener lector v2 read-only.
- Subir sesiones antiguas como `legacy`.
- Interpolar segundos únicamente para orden estable.
- Mantener `speed_cmps = null`.
- No generar fases.
- Reutilizar el slot solamente tras ACK.

No se recomienda convertir blobs in-place porque aumenta el riesgo de corrupción
ante cortes de energía.

## 9. Motor cloud del firmware

### 9.1 Estados

```cpp
enum class CloudSyncState : uint8_t {
  DISABLED,
  NEEDS_CLAIM,
  WAITING_FOR_STA,
  WAITING_FOR_TIME,
  CLAIM_PENDING,
  IDLE,
  SELECTING_SESSION,
  BUILDING_BATCH,
  CONNECTING,
  SENDING,
  WAITING_RESPONSE,
  COMMITTING_ACK,
  BACKOFF,
  AUTH_REVOKED,
  DATA_CONFLICT,
  STORAGE_BLOCKED
};
```

### 9.2 Invariantes

- Ningún socket HTTPS se bombea desde una sección que impida atender GPS.
- Solo hay un upload en vuelo por dispositivo.
- Un batch no cambia una vez calculado su hash.
- El ACK local se persiste después del ACK remoto.
- Los chunks activos se copian/snapshot antes de transmitir.
- No se borra un claim hasta confirmar el vínculo.
- No se sobrescribe silenciosamente una sesión pendiente.
- El token no se imprime ni aparece en endpoints locales.

### 9.3 Pseudocódigo de la máquina

```cpp
void CloudSync::tick(uint32_t now_ms) {
  switch (state) {
    case CloudSyncState::DISABLED:
      if (credential_exists()) {
        transition(CloudSyncState::IDLE);
      } else if (claim_code_exists()) {
        transition(CloudSyncState::NEEDS_CLAIM);
      }
      break;

    case CloudSyncState::NEEDS_CLAIM:
      transition(
        wifi_mgr::sta_connected()
          ? CloudSyncState::WAITING_FOR_TIME
          : CloudSyncState::WAITING_FOR_STA
      );
      break;

    case CloudSyncState::WAITING_FOR_STA:
      if (wifi_mgr::sta_connected()) {
        transition(CloudSyncState::WAITING_FOR_TIME);
      }
      break;

    case CloudSyncState::WAITING_FOR_TIME:
      if (time_quality_is_tls_safe()) {
        transition(
          claim_confirmed()
            ? CloudSyncState::IDLE
            : CloudSyncState::CLAIM_PENDING
        );
      } else {
        request_sntp_nonblocking();
      }
      break;

    case CloudSyncState::CLAIM_PENDING:
      if (enqueue_claim_request()) {
        transition(CloudSyncState::CONNECTING);
      }
      break;

    case CloudSyncState::IDLE:
      if (!wifi_mgr::sta_connected()) {
        transition(CloudSyncState::WAITING_FOR_STA);
      } else if (has_pending_session()) {
        transition(CloudSyncState::SELECTING_SESSION);
      }
      break;

    case CloudSyncState::SELECTING_SESSION:
      current_upload = select_oldest_pending_session();
      transition(CloudSyncState::BUILDING_BATCH);
      break;

    case CloudSyncState::BUILDING_BATCH:
      if (build_bounded_batch(current_upload)) {
        transition(CloudSyncState::CONNECTING);
      } else {
        transition(CloudSyncState::DATA_CONFLICT);
      }
      break;

    case CloudSyncState::CONNECTING:
      start_https_request();
      transition(CloudSyncState::SENDING);
      break;

    case CloudSyncState::SENDING:
      pump_request_without_blocking();
      if (request_sent()) {
        transition(CloudSyncState::WAITING_RESPONSE);
      } else if (request_failed()) {
        schedule_backoff();
      }
      break;

    case CloudSyncState::WAITING_RESPONSE:
      handle_http_response();
      break;

    case CloudSyncState::COMMITTING_ACK:
      if (persist_ack_ab()) {
        transition(CloudSyncState::IDLE);
      } else {
        transition(CloudSyncState::STORAGE_BLOCKED);
      }
      break;

    case CloudSyncState::BACKOFF:
      if (deadline_reached(now_ms)) {
        transition(CloudSyncState::IDLE);
      }
      break;

    case CloudSyncState::AUTH_REVOKED:
    case CloudSyncState::DATA_CONFLICT:
    case CloudSyncState::STORAGE_BLOCKED:
      // Requieren reconfiguración o intervención explícita.
      break;
  }
}
```

### 9.4 Tarea y colas

Diseño recomendado:

```text
GPS/main loop
  |-- persiste chunks
  |-- publica evento CLOUD_DATA_READY
  `-- consume evento CLOUD_ACK_COMMITTED

Cloud worker, prioridad baja
  |-- snapshot de chunk persistido
  |-- serializa lote acotado
  |-- HTTPS/TLS
  |-- clasifica respuesta
  `-- solicita persistir ACK
```

La tarea de red nunca mantiene un lock NVS mientras realiza DNS, TCP o TLS.

### 9.5 Prioridad de subida

1. Sesión cerrada más antigua sin ACK.
2. Siguiente sesión cerrada.
3. Chunks persistidos de la sesión actual.
4. Resumen actual.

Nunca se transmite un buffer RAM que pueda estar siendo modificado.

### 9.6 Backoff

```cpp
uint32_t compute_backoff(uint8_t failure_count) {
  const uint8_t shift = min<uint8_t>(failure_count, 8);
  uint32_t delay_ms = CLOUD_BACKOFF_INITIAL_MS << shift;
  delay_ms = min(delay_ms, CLOUD_BACKOFF_MAX_MS);
  delay_ms += random_u32() % max<uint32_t>(1, delay_ms / 4);
  return delay_ms;
}
```

Valores iniciales:

```text
Inicial          5 segundos
Máximo          15 minutos
Reset           después de upload exitoso
429/503         respetar Retry-After si es mayor
```

### 9.7 Interpretación HTTP

| Status | Acción |
|---|---|
| 200/201 | Persistir ACK |
| 400 | Marcar lote inválido; no reintentar infinitamente |
| 401 | Entrar en `AUTH_REVOKED` |
| 409 | Detener por conflicto de integridad |
| 410 | Claim expirado; solicitar nuevo código |
| 413 | Bug de tamaño/contrato; dividir o actualizar firmware |
| 422 | Datos semánticamente inválidos |
| 429 | Respetar `Retry-After` |
| 500/502/503/504 | Backoff y reintento |
| Timeout | Reintentar con exactamente el mismo batch |

### 9.8 TLS

- Usar HTTPS verificado.
- Usar CA bundle/root CA, no `setInsecure()`.
- No pinnear el certificado leaf de Vercel; rota y expira.
- Limitar connect/read timeout.
- Reutilizar conexión solamente durante una ventana de sync.
- Cerrarla al terminar para ahorrar energía.
- Medir heap antes/durante/después del handshake.

### 9.9 BLE y energía

- Reducir o suspender advertising BLE durante upload si las pruebas muestran
  degradación de AP/STA.
- Sincronizar principalmente estando estacionario y conectado a casa.
- Evitar polling cloud periódico cuando no existen datos pendientes.
- No mantener TLS abierto permanentemente.
- Medir consumo real antes de fijar intervalos.

## 10. Política de almacenamiento bajo presión

Si los cuatro slots están pendientes, no existe una solución sin trade-off.

Política:

1. Nunca sobrescribir la sesión activa.
2. Reutilizar primero el slot cerrado y confirmado más antiguo.
3. Si todos están sin confirmar, establecer `storage_pressure` y priorizar sync.
4. Mantener las sesiones cerradas más recientes.
5. Si físicamente debe abrirse otra sesión, reemplazar la no confirmada más
   antigua, incrementar `dropped_unacked_sessions` y conservar un tombstone.
6. Exponer la pérdida en diagnóstico; nunca ocultarla.

Tombstone conceptual:

```cpp
struct DroppedSessionRecord {
  uint8_t boot_id[16];
  uint32_t started_at_utc;
  uint32_t ended_at_utc;
  uint16_t point_count;
  uint8_t reason;
  uint32_t crc32;
};
```

## 11. API de dispositivo

### 11.1 Endpoints

```text
POST /api/device/v1/claim
POST /api/device/v1/batches
GET  /api/device/v1/sync-state
POST /api/device/v1/rotate       futuro
```

### 11.2 Autenticación

```http
Authorization: Bearer dgr_live_...
X-DogRGB-Device-ID: 8451071a-8722-4c1f-9fe1-22c6557162a9
X-DogRGB-Firmware: 0.9.0
Content-Type: application/json
```

La credencial no aparece en query string, body de telemetría o logs.

### 11.3 Lote v1

```json
{
  "schema_version": 1,
  "device_id": "8451071a-8722-4c1f-9fe1-22c6557162a9",
  "boot_id": "cab2a670-12f0-4a18-a079-2334e329abaf",
  "batch_seq": 4,
  "first_point_seq": 144,
  "closed": false,
  "firmware": {
    "version": "0.9.0",
    "track_format": 3
  },
  "device_summary": {
    "distance_m": 738,
    "active_s": 422,
    "max_speed_cmps": 514
  },
  "points": [
    {
      "seq": 144,
      "utc_s": 1785594875,
      "lat_e7": 46355100,
      "lon_e7": -741234000,
      "speed_cmps": 184,
      "satellites": 11,
      "flags": 31
    }
  ]
}
```

### 11.4 Límites

```text
Body máximo              128 KiB
Puntos por lote          1..96
Strings firmware/model   longitud limitada
Latitud e7               -900000000..900000000
Longitud e7              -1800000000..1800000000
Velocidad cm/s           0..2000; firmware filtra sobre su máximo válido
Secuencia                 estrictamente creciente
Batch en vuelo            uno por dispositivo
```

### 11.5 Validación estricta

- `schema_version === 1`.
- JSON válido.
- `additionalProperties: false`.
- `device_id` coincide con header y credencial.
- `points[0].seq === first_point_seq`.
- No hay secuencias duplicadas.
- Timestamps no decrecen.
- Timestamps son plausibles.
- Gaps se marcan o rechazan según contrato.
- Coordenadas finitas y dentro de rango.
- No se aceptan `owner_id`, `status`, `derived_*` u otros campos server-owned.

### 11.6 Idempotencia

Identidad de lote:

```text
(device_id, boot_id, batch_seq)
```

Identidad de punto:

```text
(session_id, seq)
```

También se guarda SHA-256 del payload canónico. Mismo identificador y mismo hash
es un reintento válido. Mismo identificador con hash diferente es corrupción y
responde `409 batch_hash_mismatch`.

No se depende únicamente de `Idempotency-Key`, porque el draft IETF de ese header
no es todavía un RFC estable. Puede enviarse para tracing, pero la garantía vive
en el dominio y las constraints SQL.

### 11.7 Respuesta exitosa

```json
{
  "schema_version": 1,
  "session_id": "588b942b-1002-45a1-bae4-c6f890751665",
  "batch_seq": 4,
  "duplicate": false,
  "accepted_points": 48,
  "accepted_through_seq": 191,
  "server_time": "2026-08-01T22:31:54.912Z",
  "session_status": "open"
}
```

### 11.8 Errores RFC 9457

```http
Content-Type: application/problem+json
```

```json
{
  "type": "https://dogrgb.example/problems/sequence-gap",
  "title": "Sequence gap",
  "status": 409,
  "code": "sequence_gap",
  "detail": "The next expected point is 192.",
  "expected_seq": 192,
  "retryable": false,
  "instance": "01K1A7E2S0..."
}
```

No se devuelven stack traces, SQL, nombres internos o secretos.

## 12. Ingestión transaccional

```ts
async function ingestBatch(
  request: Request,
  input: TelemetryBatch
): Promise<BatchResponse> {
  const token = parseBearer(request);
  const tokenHash = hmacSha256(env.DEVICE_TOKEN_PEPPER, token);

  const credential = await findCredentialByDigest(tokenHash);

  if (!credential || credential.revokedAt) {
    throw problem('device_unauthorized', 401);
  }

  if (credential.deviceId !== input.device_id) {
    throw problem('device_identity_mismatch', 401);
  }

  const payloadHash = sha256(canonicalJson(input));

  return db.begin(async (tx) => {
    await tx`set local statement_timeout = '5s'`;

    const existingBatch = await tx`
      select payload_hash, last_point_seq as accepted_through_seq
      from private.ingest_batches
      where device_id = ${input.device_id}
        and boot_id = ${input.boot_id}
        and batch_seq = ${input.batch_seq}
    `;

    if (existingBatch) {
      if (!timingSafeEqual(existingBatch.payload_hash, payloadHash)) {
        throw problem('batch_hash_mismatch', 409);
      }
      return duplicateSuccess(existingBatch);
    }

    const session = await upsertAndLockSession(tx, input);

    if (input.first_point_seq !== session.expected_next_seq) {
      throw problem('sequence_gap', 409, {
        expected_seq: session.expected_next_seq
      });
    }

    const rows = input.points.map(toPointRow);
    await batchInsertPoints(tx, session.id, rows);

    const acceptedThrough = rows.at(-1)!.seq;

    await tx`
      update public.sessions
      set expected_next_seq = ${acceptedThrough + 1},
          point_count = point_count + ${rows.length},
          device_distance_m = ${input.device_summary.distance_m},
          device_active_s = ${input.device_summary.active_s},
          device_max_speed_cmps = ${input.device_summary.max_speed_cmps},
          ended_at = ${rows.at(-1)!.capturedAt},
          status = ${input.closed ? 'finalizing' : 'open'}
      where id = ${session.id}
    `;

    await insertBatchReceipt(tx, {
      deviceId: input.device_id,
      bootId: input.boot_id,
      batchSeq: input.batch_seq,
      payloadHash,
      firstPointSeq: input.first_point_seq,
      lastPointSeq: acceptedThrough,
      acceptedCount: rows.length
    });

    await tx`
      update public.devices
      set last_seen_at = now(),
          last_sync_at = now(),
          firmware_version = ${input.firmware.version}
      where id = ${input.device_id}
    `;

    return successResponse(session, acceptedThrough);
  });
}
```

La transacción no puede contener:

- HTTP saliente.
- Google APIs.
- Email.
- Geocodificación.
- Procesamiento prolongado de geometrías.
- Sleeps.
- Espera del dispositivo.

## 13. Modelo PostgreSQL/Supabase

### 13.1 Esquemas

```text
public
  devices
  sessions
  track_points
  session_phase_totals
  vistas security_invoker de lectura

private
  device_claim_tokens
  device_credentials
  ingest_batches
  rate_limit_counters, solo si se demuestra necesario
  funciones privilegiadas no expuestas
```

Las tablas en `public` tienen RLS. El esquema `private` no se expone mediante la
Data API y usa grants mínimos como defensa adicional.

### 13.2 Extensiones

```sql
create extension if not exists postgis;
```

No habilitar extensiones adicionales sin necesidad demostrada. `pg_cron` puede
usarse para finalización/retención cuando esté disponible en el proyecto.

### 13.3 Devices

```sql
create table public.devices (
  id uuid primary key,
  owner_id uuid not null
    references auth.users(id) on delete cascade,

  name text not null,
  dog_name text not null,
  timezone_name text not null default 'America/Bogota',

  hardware_model text not null,
  firmware_version text,
  track_format smallint,

  claimed_at timestamptz not null default now(),
  last_seen_at timestamptz,
  last_sync_at timestamptz,
  revoked_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),

  constraint devices_name_length
    check (char_length(name) between 1 and 80),
  constraint devices_dog_name_length
    check (char_length(dog_name) between 1 and 80),
  constraint devices_timezone_length
    check (char_length(timezone_name) between 1 and 64)
);

create index devices_owner_id_idx
  on public.devices(owner_id);
```

La zona horaria se guarda como nombre IANA, no como offset fijo. Esto permite
agrupar por día civil sin perder reglas históricas si el proyecto se usa fuera
de Colombia.

### 13.4 Sessions

```sql
create table public.sessions (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null
    references public.devices(id) on delete cascade,
  boot_id uuid not null,

  status text not null default 'open',
  end_reason text,

  started_at timestamptz not null,
  ended_at timestamptz,
  timezone_name text not null,

  expected_next_seq integer not null default 0,
  point_count integer not null default 0,

  device_distance_m integer,
  device_active_s integer,
  device_max_speed_cmps integer,

  derived_distance_m integer,
  derived_active_s integer,
  derived_avg_speed_cmps integer,
  derived_max_speed_cmps integer,

  gps_quality_score smallint,
  min_satellites smallint,
  avg_satellites numeric(5,2),
  max_satellites smallint,

  phase_rules_version smallint,
  metrics_version smallint,

  route geometry(LineString, 4326),
  route_simplified geometry(LineString, 4326),

  created_at timestamptz not null default now(),
  finalized_at timestamptz,

  unique(device_id, boot_id),

  constraint sessions_status_check check (
    status in ('open', 'finalizing', 'ready', 'incomplete', 'corrupt')
  ),
  constraint sessions_point_count_check check (point_count >= 0),
  constraint sessions_expected_seq_check check (expected_next_seq >= 0)
);

create index sessions_device_started_idx
  on public.sessions(device_id, started_at desc, id desc);

create index sessions_status_updated_idx
  on public.sessions(status, ended_at)
  where status in ('open', 'finalizing');

```

El índice compuesto `(device_id, started_at desc, id desc)` sirve para el patrón
real del historial y su cursor. El índice parcial de estado sirve al job que
finaliza sesiones y evita indexar filas `ready` innecesariamente.

No se crea inicialmente un GiST sobre `sessions.route`: las rutas se consultan
por primary key/device, no por intersección espacial. Se añade cuando exista una
consulta por bbox, proximidad o geofence que lo justifique.

### 13.5 Track points

```sql
create table public.track_points (
  session_id uuid not null
    references public.sessions(id) on delete cascade,
  seq integer not null,
  captured_at timestamptz not null,

  lat_e7 integer not null,
  lon_e7 integer not null,
  location geometry(Point, 4326) not null,

  speed_cmps integer,
  satellites smallint,
  flags smallint not null,

  primary key(session_id, seq),

  constraint track_points_seq_check check (seq >= 0),
  constraint track_points_lat_check
    check (lat_e7 between -900000000 and 900000000),
  constraint track_points_lon_check
    check (lon_e7 between -1800000000 and 1800000000),
  constraint track_points_speed_check
    check (speed_cmps is null or speed_cmps between 0 and 2000),
  constraint track_points_satellites_check
    check (satellites is null or satellites between 0 and 255),
  constraint track_points_flags_check
    check (flags between 0 and 255)
);
```

No se añade inicialmente un GiST por punto. El acceso normal es por
`(session_id, seq)`, ya cubierto por la primary key, y el mapa usa la geometría
agregada de `sessions`. Añadir un índice espacial sobre todos los puntos solo se
justifica si aparece una consulta espacial real.

### 13.6 Fases agregadas

```sql
create table public.session_phase_totals (
  session_id uuid not null
    references public.sessions(id) on delete cascade,
  phase text not null,
  duration_s integer not null,
  distance_m integer not null,
  sample_count integer not null,
  rules_version smallint not null,

  primary key(session_id, phase),

  constraint session_phase_name_check check (
    phase in ('rest', 'gentle', 'trot', 'run', 'unknown')
  ),
  constraint session_phase_duration_check check (duration_s >= 0),
  constraint session_phase_distance_check check (distance_m >= 0),
  constraint session_phase_samples_check check (sample_count >= 0)
);
```

### 13.7 Claims privados

```sql
create schema if not exists private;

create table private.device_claim_tokens (
  id bigint generated always as identity primary key,
  owner_id uuid not null
    references auth.users(id) on delete cascade,
  pending_name text not null,
  pending_dog_name text not null,
  token_hash bytea not null unique,
  expires_at timestamptz not null,
  attempts smallint not null default 0,
  used_at timestamptz,
  used_by_device_id uuid,
  created_at timestamptz not null default now(),

  constraint claim_attempts_check check (attempts between 0 and 10)
);

create index device_claim_tokens_expiry_idx
  on private.device_claim_tokens(expires_at)
  where used_at is null;
```

### 13.8 Credenciales privadas

```sql
create table private.device_credentials (
  id bigint generated always as identity primary key,
  device_id uuid not null
    references public.devices(id) on delete cascade,
  token_hash bytea not null unique,
  token_prefix text not null,
  token_last_four text not null,
  created_at timestamptz not null default now(),
  last_used_at timestamptz,
  revoked_at timestamptz
);

create unique index one_active_device_credential_idx
  on private.device_credentials(device_id)
  where revoked_at is null;

create index device_credentials_device_idx
  on private.device_credentials(device_id);
```

`token_prefix` y `token_last_four` son solo información de soporte, nunca
suficiente para autenticar.

### 13.9 Recibos de lotes

```sql
create table private.ingest_batches (
  id bigint generated always as identity primary key,
  device_id uuid not null
    references public.devices(id) on delete cascade,
  boot_id uuid not null,
  batch_seq integer not null,
  payload_hash bytea not null,
  first_point_seq integer not null,
  last_point_seq integer not null,
  accepted_count integer not null,
  received_at timestamptz not null default now(),

  unique(device_id, boot_id, batch_seq),

  constraint ingest_batch_seq_check check (batch_seq >= 0),
  constraint ingest_point_range_check check (
    first_point_seq >= 0 and last_point_seq >= first_point_seq
  ),
  constraint ingest_count_check check (accepted_count between 1 and 96)
);

create index ingest_batches_device_received_idx
  on private.ingest_batches(device_id, received_at desc);
```

### 13.10 RLS

```sql
alter table public.devices enable row level security;
alter table public.sessions enable row level security;
alter table public.track_points enable row level security;
alter table public.session_phase_totals enable row level security;
```

```sql
create policy devices_select_own
on public.devices
for select
to authenticated
using (owner_id = (select auth.uid()));
```

```sql
create policy sessions_select_own
on public.sessions
for select
to authenticated
using (
  exists (
    select 1
    from public.devices d
    where d.id = sessions.device_id
      and d.owner_id = (select auth.uid())
  )
);
```

```sql
create policy track_points_select_own
on public.track_points
for select
to authenticated
using (
  exists (
    select 1
    from public.sessions s
    join public.devices d on d.id = s.device_id
    where s.id = track_points.session_id
      and d.owner_id = (select auth.uid())
  )
);
```

```sql
create policy session_phase_totals_select_own
on public.session_phase_totals
for select
to authenticated
using (
  exists (
    select 1
    from public.sessions s
    join public.devices d on d.id = s.device_id
    where s.id = session_phase_totals.session_id
      and d.owner_id = (select auth.uid())
  )
);
```

No se crean políticas de INSERT/UPDATE/DELETE de telemetría para usuarios. Las
operaciones de perfil que sí se permitan necesitan políticas separadas y una
policy SELECT correspondiente, porque PostgreSQL RLS requiere poder seleccionar
una fila antes de actualizarla.

### 13.11 Views y funciones

Toda vista expuesta debe usar:

```sql
create view public.session_list
with (security_invoker = true)
as
select ...;
```

Las funciones `security definer` no se ubican en `public`; viven en `private`,
usan `set search_path = ''` y reciben grants explícitos.

### 13.12 Rol de ingestión

Objetivo conceptual:

```sql
create role dogrgb_ingest nologin;
create role dogrgb_ingest_login login password '<secret-from-secret-manager>';
grant dogrgb_ingest to dogrgb_ingest_login;

grant usage on schema public, private to dogrgb_ingest;

grant select, insert, update
  on public.devices, public.sessions, public.track_points,
     public.session_phase_totals
  to dogrgb_ingest;

grant select, insert, update
  on private.device_claim_tokens, private.device_credentials,
     private.ingest_batches
  to dogrgb_ingest;

grant usage, select
  on sequence private.device_claim_tokens_id_seq,
              private.device_credentials_id_seq,
              private.ingest_batches_id_seq
  to dogrgb_ingest;
```

Los grants finales deben ser todavía más específicos. El rol no necesita borrar
usuarios, cambiar propietarios, modificar esquemas, crear extensiones o ser
superuser.

La viabilidad exacta del login/pooler con un rol custom se valida en un spike de
fase 1. Si el proyecto/plan no lo soporta adecuadamente, el fallback es un
endpoint server-only con clave secreta Supabase y una función transaccional
`security invoker` de permisos limitados; nunca se expone la clave al cliente.

### 13.13 Conexiones serverless

- Usar Supavisor en transaction mode.
- Configurar el driver con `prepare: false`.
- Reutilizar la instancia del cliente en el módulo cuando el runtime lo permita.
- No abrir una conexión física manual por cada punto.
- Insertar los puntos del lote en una operación multi-row.
- Mantener transacciones cortas.
- Aplicar `statement_timeout` local.

### 13.14 Paginación

```sql
select *
from public.sessions
where device_id = $1
  and (started_at, id) < ($2, $3)
order by started_at desc, id desc
limit 25;
```

No usar `OFFSET` en historial profundo.

### 13.15 Particionamiento

No particionar `track_points` inicialmente. Reconsiderar cuando ocurra alguno:

- Más de cinco millones de puntos.
- Vacuum problemático.
- Retención lenta.
- Índices demasiado grandes.
- `EXPLAIN (ANALYZE, BUFFERS)` demuestra degradación.

## 14. Finalización de sesiones

### 14.1 Estados

```text
open         recibe batches
finalizing   recibió closed o venció timeout
ready        geometría y métricas derivadas completas
incomplete   tiene gaps o falta cierre/datos
corrupt      conflicto/hash/datos imposibles
```

### 14.2 Flujo

```ts
async function finalizeSession(sessionId: string) {
  const points = await loadOrderedPoints(sessionId);
  const sequence = inspectSequence(points);

  if (sequence.hasGaps) {
    await markIncomplete(sessionId, sequence);
    return;
  }

  const metrics = deriveMetrics(points);
  const phases = classifyPhases(points, PHASE_RULES_V1);
  const route = makeLineString(points);
  const simplified = simplifyForOverview(route);

  await db.begin(async (tx) => {
    await replacePhaseTotals(tx, sessionId, phases);
    await updateSessionMetrics(tx, sessionId, metrics);
    await updateSessionGeometry(tx, sessionId, route, simplified);
    await markSessionReady(tx, sessionId, {
      metricsVersion: 1,
      phaseRulesVersion: 1
    });
  });
}
```

### 14.3 Geometría

```sql
select ST_MakeLine(location order by seq)
from public.track_points
where session_id = $1;
```

Representación simplificada:

```sql
ST_SimplifyPreserveTopology(route, tolerance)
```

Con 1.440 puntos, detalle puede usar la ruta completa. La simplificada sirve para
miniaturas, vistas globales o retención futura.

### 14.4 Sesiones abandonadas

Un job diario identifica sesiones `open` sin actividad durante 24 horas y las
marca `finalizing`. Si tienen secuencia completa se finalizan; si no,
`incomplete`.

La finalización no ocurre dentro del upload si implica construir geometrías o
recorrer todos los puntos. El ACK no debe depender de trabajo derivado.

## 15. Estadísticas y fases

### 15.1 Datos paralelos

```text
device_*     calculados por firmware, útiles para coherencia/diagnóstico
derived_*    recalculados desde puntos con versión de algoritmo
```

Una diferencia no se corrige silenciosamente. Se registra una bandera si supera
un umbral.

### 15.2 Distancia

```ts
function deriveDistance(points: TrackPoint[]): number {
  let totalM = 0;

  for (const [a, b] of consecutivePairs(points)) {
    const deltaS = b.utcS - a.utcS;
    if (deltaS <= 0 || deltaS > 15) continue;
    if (!qualityAccepted(a, b)) continue;

    const segmentM = haversineMeters(a, b);
    const noiseFloorM = dynamicNoiseThreshold(a, b);

    if (segmentM < noiseFloorM) continue;
    if (segmentM >= 50) continue;
    totalM += segmentM;
  }

  return Math.round(totalM);
}
```

### 15.3 Tiempo activo

```ts
function deriveActiveSeconds(samples: ClassifiedPoint[]): number {
  let activeS = 0;

  for (const [a, b] of consecutivePairs(samples)) {
    const deltaS = b.utcS - a.utcS;
    if (deltaS <= 0 || deltaS > 15) continue;

    if (a.phase !== 'rest' && a.phase !== 'unknown') {
      activeS += deltaS;
    }
  }

  return activeS;
}
```

### 15.4 Fases v1

| Fase | Rango | Texto UI |
|---|---:|---|
| `rest` | `< 0,7 km/h` | Reposo estimado |
| `gentle` | `0,7–4 km/h` | Movimiento suave |
| `trot` | `4–10 km/h` | Trote estimado |
| `run` | `10–40 km/h` | Carrera estimada |
| `unknown` | sin dato confiable | Sin clasificar |

Son rangos GPS, no comportamiento ni diagnóstico. Internamente se pueden
conservar las diez bandas existentes y agregarlas en cuatro grupos.

### 15.5 Histéresis

```ts
function classifyWithHysteresis(points: TrackPoint[]): ClassifiedPoint[] {
  let current: Phase = 'rest';
  let candidate: Phase = current;
  let candidateSamples = 0;
  const output: ClassifiedPoint[] = [];

  for (const point of points) {
    const raw = classifySpeed(cmpsToKph(point.speedCmps));

    if (raw === current) {
      candidate = current;
      candidateSamples = 0;
    } else if (raw === candidate) {
      candidateSamples += 1;
    } else {
      candidate = raw;
      candidateSamples = 1;
    }

    if (candidateSamples >= 2) {
      current = candidate;
      candidateSamples = 0;
    }

    output.push({ ...point, phase: current });
  }

  return output;
}
```

Dos muestras de cinco segundos confirman una transición. Un gap mayor a 15
segundos rompe el segmento y produce `unknown` hasta recuperar datos.

### 15.6 Máxima confirmada

```ts
function robustMaxSpeed(points: TrackPoint[]): number {
  const confirmed = points.filter((point, index, all) => {
    if (!point.qualityOk || point.speedCmps == null) return false;
    const previous = all[index - 1];
    const next = all[index + 1];
    return nearSpeed(previous, point, 0.35) || nearSpeed(next, point, 0.35);
  });

  return Math.max(0, ...confirmed.map((point) => point.speedCmps!));
}
```

### 15.7 Calidad GPS

Score de 0 a 100 basado en porcentaje `QUALITY_OK`, satélites, gaps, saltos
rechazados y tiempo confiable. No mostrar precisión en metros sin respaldo.

### 15.8 Versionado

```text
metrics_version       1
phase_rules_version   1
```

Cambiar umbrales o algoritmos incrementa la versión y permite recalcular sin
tocar puntos crudos.

## 16. Portal web

### 16.1 Tesis visual

> Un terminal de campo nocturno: negro profundo, fósforo verde preciso,
> cartografía oscura y datos técnicos legibles; ámbar para atención y magenta
> únicamente para fallos.

### 16.2 Tesis de contenido

El portal es una herramienta operativa. La primera pantalla responde:

1. ¿Qué collar estoy viendo?
2. ¿Cuándo sincronizó por última vez?
3. ¿Qué hizo hoy?
4. ¿Cuáles son los últimos recorridos?
5. ¿Hay un problema que requiera acción?

No necesita hero de marketing, testimonios, slogans o cuadrícula de cards.

### 16.3 Tesis de interacción

1. Entrada breve como terminal que despierta, una vez por sesión.
2. Cursor temporal enlazado entre gráfica y mapa.
3. Pulso discreto durante sincronización.
4. Drawer/inspector con transición corta.
5. Todo movimiento desaparece con `prefers-reduced-motion`.

### 16.4 Tokens

```css
:root {
  --bg: #030603;
  --surface: #071007;
  --surface-raised: #0b160c;

  --phosphor: #00ff41;
  --phosphor-bright: #8dff9f;
  --phosphor-dim: #4f8f58;

  --amber: #ffd166;
  --magenta: #ff4fd8;
  --danger: #ff625f;

  --text: #d6ffdc;
  --muted: #79a97f;
  --divider: rgb(0 255 65 / 18%);
  --focus: #ffffff;

  --space-1: 0.25rem;
  --space-2: 0.5rem;
  --space-3: 0.75rem;
  --space-4: 1rem;
  --space-6: 1.5rem;
  --space-8: 2rem;
  --space-12: 3rem;

  --radius-control: 3px;
  --nav-width: 13rem;
  --inspector-width: 21rem;
}
```

El verde es el único acento dominante. Ámbar y magenta comunican estados.

### 16.5 Tipografía

- Una familia monoespaciada autoalojada, por ejemplo IBM Plex Mono.
- Fallback `ui-monospace`, `SFMono-Regular`, `Consolas`, monospace.
- Números tabulares.
- Mayúsculas para labels cortos, no párrafos.
- Texto de ayuda en sentence case.
- Sin Google Fonts CDN.

### 16.6 CRT moderado

```css
.terminal-shell::before {
  content: '';
  position: fixed;
  inset: 0;
  z-index: 100;
  pointer-events: none;
  background: repeating-linear-gradient(
    to bottom,
    transparent 0,
    transparent 3px,
    rgb(0 0 0 / 12%) 4px
  );
  opacity: 0.22;
}
```

No usar flicker permanente. Un boot visual puede durar 250–400 ms y no bloquea
interacción.

```css
@media (prefers-reduced-motion: reduce) {
  *,
  *::before,
  *::after {
    scroll-behavior: auto !important;
    animation-duration: 0.01ms !important;
    animation-iteration-count: 1 !important;
    transition-duration: 0.01ms !important;
  }
}
```

### 16.7 Información y rutas

```text
DOG-RGB HOME TERMINAL
|-- Resumen                         /app
|-- Recorridos                      /app/sessions
|   `-- Detalle                     /app/sessions/[id]
|-- Collar                          /app/devices/[id]
|   |-- Estado
|   |-- Vinculación
|   `-- Seguridad
`-- Ajustes                         /app/settings
    |-- Perro
    |-- Zona horaria
    |-- Unidades
    `-- Privacidad
```

Auth/onboarding:

```text
/login
/auth/callback
/onboarding
```

### 16.8 Wireframe desktop: resumen

```text
+----------------------------------------------------------------+
| DOG-RGB HOME TERMINAL          LUNA          SYNC 18:42:03      |
+---------------+------------------------------------------------+
| RESUMEN       | HOY                                            |
| RECORRIDOS    | 3.42 KM  | 47 MIN ACTIVA | 12.8 KM/H           |
| COLLAR        +------------------------------------------------+
| AJUSTES       |                                                |
|               |              ULTIMO RECORRIDO                  |
|               |                  MAPA                          |
|               |                                                |
|               +------------------------------------------------+
|               | RECORRIDOS RECIENTES                           |
|               | 18:02  3.42 km  52 min  LISTO                  |
|               | 07:31  1.18 km  21 min  LISTO                  |
+---------------+------------------------------------------------+
```

Para minimizar facturación, el resumen puede usar un plano simple o no cargar
mapa; Google Maps se reserva para detalle.

### 16.9 Wireframe desktop: detalle

```text
+----------------------------------------------------------------+
| < RECORRIDOS   01 AGO 2026 | 18:02-18:54       [EXPORTAR]      |
+--------------------------------------+-------------------------+
|                                      | DISTANCIA       3.42 KM |
|                                      | TIEMPO ACTIVO    47 MIN |
|                 MAPA                 | MAX. CONFIRMADA 12.8    |
|                                      | CALIDAD GPS       BUENA |
|                                      |                         |
+--------------------------------------+-------------------------+
| VELOCIDAD                                                      |
| 14 |             /--\                                         |
|  7 |     /-------/    \----\                                  |
|  0 |-----/                   \----------------------            |
+----------------------------------------------------------------+
| FASES                                                          |
| REPOSO      ########                    12 min                  |
| SUAVE       ###################         29 min                  |
| TROTE       #####                        9 min                  |
| CARRERA     ##                           2 min                  |
+----------------------------------------------------------------+
```

### 16.10 Móvil

- Header compacto.
- Navegación inferior o drawer.
- Mapa 4:3.
- Métricas en dos columnas sin card por cifra.
- Inspector debajo del mapa.
- Gráfica horizontal.
- Touch targets mínimos de 44 px.
- Ninguna acción depende solo de hover.

### 16.11 Componentes

```text
TerminalShell
  TerminalHeader
  TerminalNav
  DeviceSwitcher
  SyncIndicator
  MainWorkspace
  ContextInspector

SessionDetail
  SessionHeader
  RouteMap
  RouteTimeline
  MetricStrip
  SpeedPlot
  PhaseBars
  GpsQualitySummary
  ExportMenu
```

Un panel solo parece card cuando la card es la interacción. Para datos rutinarios
se usan secciones, divisores y filas.

### 16.12 Server y Client Components

Server Components:

- Layout.
- Resumen.
- Lista de sesiones.
- Estado del dispositivo.
- Settings.
- Autorización inicial.

Client Components:

- Google Map.
- uPlot.
- Cursor mapa/gráfica.
- Copiar claim.
- Countdown.
- Drawer móvil.

No marcar layouts enteros con `'use client'`.

## 17. Copy de producto

### 17.1 Login

```text
DOG-RGB HOME TERMINAL

ENTRAR AL TERMINAL

Escribe tu correo. Te enviaremos un enlace para abrir tu cuenta
sin contraseña.

[ correo@ejemplo.com ]

[ ENVIAR ENLACE ]

El enlace expira y solo puede usarse para iniciar sesión.
```

### 17.2 Onboarding

```text
VINCULAR UN COLLAR

1. Genera un código temporal.
2. Conéctate a la red Wi-Fi DogRGB.
3. Abre dogrgb.local o 192.168.4.1.
4. Introduce el Wi-Fi de casa y el código.
5. El collar aparecerá aquí después de conectarse.

El XIAO ESP32-S3 solo puede conectarse a redes Wi-Fi de 2,4 GHz.
```

### 17.3 Código

```text
CODIGO DE VINCULACION

DGR-7K4M-2PXQ-V9WA-J3FN

Expira en 28:41.

[ COPIAR CODIGO ]

No cierres esta pantalla hasta guardarlo en el collar.
```

### 17.4 Estados

```text
NUNCA SINCRONIZADO
El collar todavía no ha enviado datos.

ESPERANDO AL COLLAR
La vinculación está preparada. Introduce el código en el AP.

SINCRONIZANDO
El collar está enviando 3 lotes pendientes.

ACTUALIZADO
Última sincronización: hoy, 18:42.

SIN CONEXION RECIENTE
No recibimos datos desde hace 3 días. Los recorridos guardados
siguen disponibles.

CREDENCIAL REVOCADA
El collar necesita vincularse nuevamente desde su access point.
```

### 17.5 Copy prohibido

No afirmar:

- `Tu mascota está aquí`.
- `Ubicación actual`.
- `Está descansando`.
- `Está corriendo`.
- `Estado de salud`.
- `Actividad recomendada`.

Usar:

- `Última posición registrada`.
- `Fase estimada por velocidad GPS`.
- `Datos recibidos durante la última sincronización`.
- `No es una medición médica`.

## 18. Mapas

### 18.1 Carga

- Cargar `maps` dinámicamente al montar `RouteMap`.
- No cargar Maps en login, onboarding, resumen o lista.
- Clave separada para preview/producción.
- Restringir por referrer y API.
- Definir cuotas/alertas.
- Mostrar fallback si no existe key.

### 18.2 Render

- Polyline o Data Layer GeoJSON.
- `fitBounds()` con bbox.
- Inicio/final.
- Punto activo enlazado.
- Estilo oscuro con Map ID o JSON compatible.
- Reducir POIs/labels para que domine la ruta.

### 18.3 APIs no usadas

- Directions: el perro no sigue necesariamente calles.
- Roads: altera la ruta al ajustarla a carreteras.
- Places: innecesaria.
- Geocoding: innecesaria para mostrar recorrido.

### 18.4 Adapter

```ts
interface RouteMapAdapter {
  mount(container: HTMLElement, options: MapOptions): Promise<void>;
  setRoute(points: LatLng[]): void;
  fitBounds(bounds: Bounds): void;
  setActivePoint(point: LatLng | null): void;
  destroy(): void;
}
```

Permite sustituir Google Maps por MapLibre sin reescribir la pantalla.

### 18.5 Privacidad

Google recibe coordenadas cuando el navegador renderiza el mapa. La política de
privacidad lo debe declarar. No se envían a Google al consultar listas sin mapa.

## 19. Gráficas

### 19.1 Selección

uPlot para velocidad y distancia acumulada. HTML/SVG semántico para fases.

```ts
type SpeedSeries = [
  timestampsSeconds: number[],
  speedKph: Array<number | null>
];
```

### 19.2 Cursor enlazado

```ts
function onPlotCursor(index: number | null) {
  if (index == null) {
    mapAdapter.setActivePoint(null);
    return;
  }

  mapAdapter.setActivePoint({
    lat: points[index].latE7 / 1e7,
    lng: points[index].lonE7 / 1e7
  });
}
```

### 19.3 Accesibilidad

Cada Canvas incluye título, descripción, resumen textual, valores min/media/max
y tabla/descarga. El color nunca es el único canal.

## 20. Portal AP: cambios cloud

### 20.1 UI

```text
CLOUD SYNC

ESTADO            NO VINCULADO
WI-FI DE CASA     [ MiCasa_2G           ]
CONTRASENA        [ ***********         ]
CODIGO DOG-RGB    [ DGR-____-____-____ ]

[ GUARDAR Y CONECTAR ]

El collar guardará la configuración, se conectará a Internet
y confirmará la vinculación. Esta página puede desconectarse
durante el cambio de canal Wi-Fi.
```

### 20.2 Provision

```http
POST /api/cloud/provision
Content-Type: application/json
```

```json
{
  "claim_code": "DGR-7K4M-2PXQ-V9WA-J3FN"
}
```

SSID/password pueden continuar en el flujo Wi-Fi existente, pero la UI presenta
una operación lógica y el firmware guarda ambos registros de forma recuperable.

### 20.3 Status

```http
GET /api/cloud/status
```

```json
{
  "configured": true,
  "claim_state": "confirmed",
  "sync_state": "backoff",
  "pending_sessions": 2,
  "pending_points": 816,
  "last_success_utc": 1785594875,
  "last_http_status": 503,
  "last_error_code": "server_unavailable",
  "next_retry_s": 43,
  "dropped_unacked_sessions": 0
}
```

Nunca devuelve claim, secret, hash, password Wi-Fi, pepper o URL con credenciales.

### 20.4 Password AP

`Dog12345` es aceptable solo durante desarrollo. Antes de entregar unidades:

- Password aleatorio por dispositivo.
- Guardado NVS.
- Etiqueta física.
- Posibilidad de cambiarlo.
- Operaciones sensibles solo en ventana física.
- Nunca derivarlo del MAC.

## 21. Seguridad

### 21.1 Activos y atacantes

Activos: rutas, SSID/password, cuenta, credencial, métricas. Atacantes: visitante
no autenticado, usuario intentando acceso cruzado, persona cercana al AP, bot,
persona con acceso físico y fugas accidentales.

### 21.2 Controles

- TLS verificado.
- Claim corto y temporal.
- Secreto único.
- HMAC + pepper.
- Comparación constante.
- RLS.
- Private schema.
- Rol mínimo.
- Body/point limits.
- Rate limiting.
- Allowlist de campos.
- Logs redacted.
- Revocación.
- pgTAP/advisors.
- Google key restringida.

### 21.3 Rate limiting MVP

- Claim: intentos persistidos + límite IP/WAF disponible.
- Upload: uno en vuelo por dispositivo y límites de tamaño/frecuencia.
- Login: límites de Supabase Auth.
- No añadir Redis solo para el piloto.

### 21.4 Seguridad física

Sin Flash/NVS Encryption, acceso físico puede permitir extracción. Para unidades
entregables planear NVS encryption, Flash encryption, Secure Boot v2 y flujo de
firma. Activar eFuses cambia el reflasheo y requiere recuperación probada.

### 21.5 Auth

- No usar `user_metadata` para autorización.
- RLS basado en `auth.uid()` y tablas propias.
- Revocar sesiones antes de borrar una cuenta si se requiere inmediatez.
- No confiar en claims JWT obsoletos sin refresh.

## 22. Privacidad y ciclo de vida

### 22.1 Principios

- Rutas privadas.
- Sin sharing público.
- Sin analytics con coordenadas.
- Sin payloads completos en logs.
- Copy claro sobre Google Maps.
- Exportación y borrado.

### 22.2 Borrado

Dispositivo:

1. Revocar credenciales.
2. Cerrar sesiones.
3. Confirmar.
4. Eliminar dispositivo.
5. Cascada a sesiones, puntos, fases y recibos.

Cuenta:

1. Revocar sesiones Auth.
2. Revocar dispositivos.
3. Exportar si se solicitó.
4. Eliminar usuario/cascadas.

### 22.3 Retención

MVP: conservar hasta borrado manual, alertar a 70 % y medir tamaño por sesión.
Si se acerca al límite: puntos 180 días, resúmenes/geometría simplificada por más
tiempo y exportación antes de purga. No activar destrucción automática sin
consentimiento visible.

## 23. Capacidad y costes

### 23.1 Volumen

```text
720 puntos/hora
1.440 puntos/2 horas
262.800 puntos/año a 1 hora/día
525.600 puntos/año a 2 horas/día
```

Datos v3 crudos:

```text
1 hora/día  ~4,2 MB/año
2 horas/día ~8,4 MB/año
```

PostgreSQL ocupará mucho más por headers, geometría e índices. Medir con seed de
un año; esperar decenas/cientos de MB, no solo 8,4 MB.

### 23.2 Supabase

Free sirve para demo pero puede pausarse por inactividad y tiene límites/backups
menores. Offline-first permite reintentar. Para aparato confiable, Pro es mejor.

### 23.3 Google Maps

Las cargas de mapa, no los puntos de Polyline, afectan facturación. Cargar solo
detalle, aplicar cuota, alertas y restricciones. Mantener adapter para MapLibre.

### 23.4 Vercel

128 KiB queda bajo el límite de 4,5 MB. Elegir región de funciones cercana a
Supabase. Preview no debe apuntar accidentalmente a producción.

## 24. Observabilidad

### 24.1 Firmware

Logs permitidos:

```text
[CLOUD] state=WAIT_STA
[CLOUD] claim attempt=1
[CLOUD] claim ok device=8451071a
[CLOUD] batch boot=cab2a670 seq=4 points=48
[CLOUD] ack through=191
[CLOUD] retry status=503 in_s=43
[CLOUD] auth_revoked
```

Logs prohibidos:

```text
[CLOUD] token=dgr_live_...
[CLOUD] wifi_password=...
[CLOUD] full_payload={...coordinates...}
```

### 24.2 Backend

Campos:

```text
request_id
route
device_id_prefix
boot_id
batch_seq
point_count
duplicate
http_status
duration_ms
error_code
```

No guardar coordenadas en logs operativos normales.

### 24.3 Métricas

- Claims exitosos/fallidos.
- Tokens expirados.
- Fallos de auth.
- Batches/duplicados/conflictos.
- Puntos aceptados.
- Latencia p50/p95.
- Sesiones incompletas/corruptas.
- Tiempo captura→sync.
- DB size.
- Google map loads.
- `dropped_unacked_sessions`.

## 25. Matriz de problemas

| Problema | Consecuencia | Prevención/recuperación |
|---|---|---|
| Router solo 5 GHz | Nunca conecta | Copy explícito 2,4 GHz |
| SSID oculto | Setup confuso | Entrada manual y documentación |
| Password incorrecto | Sin sync | AP disponible y razón diagnóstica |
| Cambio de canal AP+STA | Teléfono pierde portal | Aviso previo |
| Teléfono sin Internet en AP | No abre cloud | Generar/copy código antes |
| Corte después del claim | Credencial perdida | Secreto persistido antes |
| Respuesta claim perdida | Claim atascado | Claim idempotente |
| Respuesta batch perdida | Duplicado | Constraint + hash |
| Mismo ID, contenido distinto | Corrupción | 409 y estado terminal |
| Token revocado | Reintentos infinitos | `AUTH_REVOKED` |
| Certificado rota | TLS falla | CA bundle, no leaf pinning |
| Reloj inválido | TLS falla | GNSS→SNTP→último tiempo |
| HTTPS bloquea loop | Pierde fixes | Worker bajo |
| BLE compite | AP/STA inestable | Reducir BLE en sync |
| Cuatro slots pendientes | Sin capacidad | Backpressure/tombstone |
| GPS deriva quieto | Distancia falsa | Guardar muestra y filtrar aparte |
| Pico velocidad | Máxima absurda | Máxima confirmada |
| Minuto repetido | Gráfica inválida | UTC segundos v3 |
| Gap de secuencia | Ruta incompleta | Estado `incomplete` |
| Cambio zona | Día incorrecto | UTC + IANA snapshot |
| Session ID manipulado | Ruta ajena | RLS + BOLA tests |
| Mass assignment | Cambia owner/status | Zod strict/allowlist |
| Request gigante | DoS/coste | 128 KiB/96 puntos |
| Flood claims | Abuso | TTL/intentos/rate limit |
| Supabase pausado | Backlog | Offline-first/backoff |
| Google key filtrada | Coste | Restricciones/cuota |
| Maps falla | Pantalla vacía | Resumen/gráfica/fallback |
| CRT excesivo | Fatiga | Reduced motion/sin flicker |
| Borrado accidental | Pérdida | Confirmación/exportación |
| Cambio algoritmo | Historial inconsistente | Versionado |
| Preview usa prod | Datos contaminados | Entornos separados |
| Rol DB excesivo | Incidente | Grants mínimos/tests |
| Índices excesivos | Upload lento | Indexar patrones reales |

## 26. Estrategia de pruebas

### 26.1 Firmware host

- `sizeof(TrackPointV3) == 16`.
- Serialización/deserialización.
- CRC metadata/chunks.
- Chunk lleno/parcial.
- Escritura A/B interrumpida.
- ACK A/B.
- Selección de slot.
- Backpressure/tombstone.
- Migración/reset v2.
- UUID/secret.
- Claim redacted.
- Backoff/jitter/rollover.
- TimeQuality.
- Clasificación HTTP.
- Secuencia/batch builder.
- Límite 96 puntos/128 KiB.
- Inmutabilidad después de hash.

### 26.2 Wokwi/integración

- STA correcto/incorrecto.
- Router sin Internet.
- DNS lento.
- Claim exitoso/respuesta perdida.
- HTTP 401/409/410/413/422/429/503.
- Reinicio antes/durante/después del POST.
- Ruta 1.440 puntos.
- Cuatro rutas.
- BLE coexistiendo.
- AP activo durante setup.
- Loop latency bajo carga cloud.

### 26.3 pgTAP

```text
Usuario A lee dispositivo A                   PASS
Usuario A no lee dispositivo B                PASS
Usuario A no lee sesión B                     PASS
Usuario A no lee puntos B                     PASS
Usuario A no lee fases B                      PASS
anon no lee rutas                             PASS
authenticated no inserta telemetría           PASS
rol ingest no borra usuarios                  PASS
rol ingest no cambia owner_id                 PASS
FKs tienen índice                             PASS
views usan security_invoker                   PASS
private no está expuesto                      PASS
```

### 26.4 API

- Claim válido/expirado/bloqueado.
- Reintento mismo collar.
- Claim usado por otro.
- Token inválido/revocado.
- Header/device mismatch.
- Campo adicional/server-owned.
- Coordenadas/timestamp inválidos.
- Secuencia duplicada/gap.
- Hash diferente.
- 96/97 puntos.
- 128/129 KiB.
- Retry-After.
- Dos requests concurrentes iguales.
- Rollback no emite ACK.

### 26.5 Portal

- Magic link/callback/link expirado.
- Cuenta vacía.
- Crear perro/claim.
- Countdown/copy.
- Claim confirmado.
- Historial vacío/paginado.
- Estados de sesión.
- Mapa sin key/error.
- uPlot con gaps.
- Cursor enlazado.
- Exportación/revocación/borrado.
- Teclado/foco/reduced motion.
- Mobile 360 px.
- Visual regression CRT.

### 26.6 Prueba física

```text
1. Grabar dos horas.
2. Confirmar 1.440 muestras locales.
3. Mantener router sin Internet.
4. Apagar collar.
5. Restaurar Internet.
6. Encender en casa.
7. Iniciar upload.
8. Cortar energía al 50 %.
9. Reiniciar.
10. Completar upload.
11. Confirmar secuencia 0..1439 sin huecos.
12. Confirmar cero duplicados.
13. Confirmar mapa/gráfica/fases.
14. Revocar token.
15. Confirmar AUTH_REVOKED.
16. Re-vincular físicamente.
```

## 27. Criterios cuantitativos

### Firmware

- Sin red bloqueante prolongada en loop.
- Sin regresión del límite de latencia existente.
- Cero secretos en logs/status.
- Cero duplicados por retry.
- Cero pérdida en ventana ACK remoto/local.
- Upload de 1.440 puntos <2 min en Wi-Fi normal.
- Sin watchdog durante TLS.
- Heap estable en repetición.
- Cuatro rutas v3 con margen documentado.

### Backend

- Claim p95 <1 s excluyendo cold start/red.
- Batch p95 <1 s.
- Transacción normal <500 ms.
- Rechazar >128 KiB y >96 puntos.
- Cero acceso cruzado RLS.
- Advisors sin findings críticos.
- Historial usa índice compuesto.

### Portal

- LCP <=2,5 s.
- INP <=200 ms.
- CLS <=0,1.
- Maps fuera del bundle de lista/resumen.
- Portal usable sin Maps.
- Contraste AA.
- Teclado completo.
- Alternativa textual de Canvas.
- Touch targets 44 px.

## 28. Fases de implementación

### Fase 0: congelar contrato, 2–3 días

ADRs, JSON Schemas, fixtures, errores, máquina de estados, ERD, threat model,
retención y wireframes. Gate: no quedan decisiones implícitas de ACK/retry.

### Fase 1: scaffold y spikes, 2–4 días

Workspace, Next, Node 24, TypeScript, CSS, Supabase local, PostGIS, Vercel
Preview, spike Supavisor/rol y TLS XIAO. Gate: build/tests/spikes medidos.

### Fase 2: Auth, DB y RLS, 3–5 días

Magic link, callback, migraciones, índices, private schema, rol, policies, seed,
pgTAP y advisors. Gate: aislamiento A/B y EXPLAIN correctos.

### Fase 3: onboarding, 2–4 días

Perfil, pending device, claim, TTL, copy 2,4 GHz, endpoint idempotente, límites y
estados. Gate: respuesta perdida no duplica collar.

### Fase 4: Track v3, 5–8 días

Struct, metadata, UTC, velocidad, flags, boot ID, grabación quieta, distancia
separada, CRC, cuatro rutas y v2. Gate: capacidad/integridad/regresiones.

### Fase 5: cloud/AP, 3–5 días

Credencial A/B, RNG, claim, endpoint/status redacted, UI local y TimeQuality.
Gate: cortes en cada paso recuperables.

### Fase 6: sync, 6–10 días

Worker, cola, TLS, lotes, ACK, backoff, HTTP, slots, storage pressure y logs.
Gate: prueba física con corte, cero duplicados y loop estable.

### Fase 7: portal operativo, 4–7 días

Shell, navegación, collar, resumen, historial, cursor pagination, estados y
revocación. Gate: funciona sin Maps.

### Fase 8: métricas/mapa/gráficas, 5–8 días

Finalizer, geometría, algoritmos, adapter, Maps lazy, uPlot, cursor, export y
accesibilidad. Gate: 1.440 puntos fluidos, bundle y alternativas correctos.

### Fase 9: hardening/beta, 4–6 días

Límites, cuotas, regiones, entornos, backups, retención, borrado, advisors, logs,
manual y beta física. Gate: checklist y recuperación documentados.

### Estimación

```text
MVP recortado         15–22 días concentrados
Plan completo         36–57 días concentrados
Tiempo libre          8–12 semanas razonables
```

La incertidumbre mayor está en TLS/coexistencia/energía y NVS v3, no en UI.

## 29. Orden de commits

```text
1. docs: add cloud ADRs and telemetry contracts
2. test: add TrackPoint v3 size/storage fixtures
3. firmware: introduce TrackPoint v3 behind feature flag
4. firmware: separate route sampling from distance metrics
5. chore: scaffold portal workspace
6. db: add Supabase base schema and RLS tests
7. web: add passwordless auth and terminal shell
8. api: add idempotent device claim
9. firmware: add cloud credentials and AP provision endpoint
10. firmware: add cloud sync state machine
11. api: add idempotent batch ingest
12. db: add session finalization and derived metrics
13. web: add sessions list and detail
14. web: add lazy map and speed chart
15. security: harden limits, logs, revocation and deletion
16. docs: add operating, recovery and privacy guides
```

No mezclar Track v3, Auth, SQL y red en un cambio masivo.

## 30. Checklist producción

### Firmware

- [ ] Track v3 medido con cuatro slots.
- [ ] Worker no bloquea loop.
- [ ] TLS verifica CA.
- [ ] No existe `setInsecure()`.
- [ ] Token redacted.
- [ ] ACK transaccional.
- [ ] Backoff con jitter.
- [ ] 401/409 terminales.
- [ ] Copy 2,4 GHz.
- [ ] Corte de energía probado.
- [ ] BLE coexistencia probada.

### Supabase

- [ ] Migraciones reproducibles.
- [ ] RLS en tablas expuestas.
- [ ] Views `security_invoker`.
- [ ] Funciones privilegiadas fuera de `public`.
- [ ] FK indexes.
- [ ] pgTAP.
- [ ] Advisors.
- [ ] Backups/export.
- [ ] Ningún secret en cliente.

### Vercel

- [ ] Región cercana a DB.
- [ ] Preview separado.
- [ ] Vars server-only.
- [ ] Body 128 KiB.
- [ ] Rate limits.
- [ ] Logs redacted.
- [ ] Dominio estable.

### Google

- [ ] Key separada.
- [ ] Referrer restrictions.
- [ ] API restrictions.
- [ ] Budget/quota.
- [ ] Lazy load.
- [ ] Fallback.

### UX/privacidad

- [ ] No tracking live.
- [ ] No diagnóstico.
- [ ] Copy Google/privacy.
- [ ] Export/borrado.
- [ ] Reduced motion.
- [ ] Teclado/contraste.

## 31. Investigación y fuentes

Se priorizaron fuentes primarias. Versiones, precios y límites deben revisarse
justo antes de implementar/desplegar.

### Next.js, React y Node

1. [Next.js App Router](https://nextjs.org/docs/app): arquitectura App Router.
2. [Server and Client Components](https://nextjs.org/docs/app/getting-started/server-and-client-components): RSC por defecto.
3. [Route Handlers](https://nextjs.org/docs/app/getting-started/route-handlers): API HTTP.
4. [Next.js 16.2](https://nextjs.org/blog/next-16-2): versión estable evaluada.
5. [Next.js 16](https://nextjs.org/blog/next-16): runtime/tooling y `proxy.ts`.
6. [React 19.2](https://react.dev/blog/2025/10/01/react-19-2): React actual del stack.
7. [Node releases](https://nodejs.org/en/about/previous-releases): Node 24 LTS.

### Vercel

8. [Next.js on Vercel](https://vercel.com/docs/frameworks/full-stack/nextjs).
9. [Function limitations](https://vercel.com/docs/functions/limitations): body 4,5 MB.
10. [Fluid Compute](https://vercel.com/docs/fluid-compute).
11. [Function regions](https://vercel.com/docs/functions/configuring-functions/region).
12. [Environment variables](https://vercel.com/docs/environment-variables).
13. [Rate limiting guide](https://examples.vercel.com/kb/guide/add-rate-limiting-vercel).

### Supabase Auth y seguridad

14. [Supabase SSR](https://supabase.com/docs/guides/auth/server-side/creating-a-client?queryGroups=framework&framework=nextjs).
15. [Passwordless email](https://supabase.com/docs/guides/auth/auth-email-passwordless).
16. [User sessions](https://supabase.com/docs/guides/auth/sessions).
17. [RLS](https://supabase.com/docs/guides/database/postgres/row-level-security).
18. [API keys](https://supabase.com/docs/guides/getting-started/api-keys).
19. [Securing Data API](https://supabase.com/docs/guides/api/securing-your-api).
20. [Database functions](https://supabase.com/docs/guides/database/functions).
21. [Postgres roles](https://supabase.com/docs/guides/database/postgres/roles).
22. [Product security](https://supabase.com/docs/guides/security/product-security).

### Supabase/PostgreSQL

23. [PostGIS](https://supabase.com/docs/guides/database/extensions/postgis).
24. [Database testing](https://supabase.com/docs/guides/database/testing).
25. [Testing overview](https://supabase.com/docs/guides/local-development/testing/overview).
26. [Connecting to Postgres](https://supabase.com/docs/guides/database/connecting-to-postgres).
27. [Drizzle guide](https://supabase.com/docs/guides/database/drizzle).
28. [Query optimization](https://supabase.com/docs/guides/database/query-optimization).
29. [Managing indexes](https://supabase.com/docs/guides/database/postgres/indexes).
30. [PostgreSQL date/time](https://www.postgresql.org/docs/current/datatype-datetime.html).
31. [PostgreSQL indexes](https://www.postgresql.org/docs/18/indexes.html).
32. [PostgreSQL GiST](https://www.postgresql.org/docs/18/gist.html).
33. [PostgreSQL COPY](https://www.postgresql.org/docs/current/sql-copy.html).
34. [Supabase Cron](https://supabase.com/docs/guides/cron).
35. [Supabase backups](https://supabase.com/docs/guides/platform/backups).
36. [Supabase pricing](https://supabase.com/pricing).

### PostGIS

37. [ST_MakeLine](https://www.postgis.net/docs/manual-2.4/ST_MakeLine.html).
38. [ST_SimplifyPreserveTopology](https://www.postgis.net/docs/ST_SimplifyPreserveTopology.html).

### Google Maps

39. [Dynamic import](https://developers.google.com/maps/documentation/javascript/load-maps-js-api).
40. [Pricing](https://developers.google.com/maps/billing-and-pricing/pricing).
41. [API security](https://developers.google.com/maps/api-security-best-practices).
42. [Data Layer](https://developers.google.com/maps/documentation/javascript/datalayer).
43. [Shapes/Polyline](https://developers.google.com/maps/documentation/javascript/shapes).
44. [Cloud styling](https://developers.google.com/maps/documentation/javascript/cloud-customization).
45. [Style reference](https://developers.google.com/maps/documentation/javascript/style-reference).

### UI, performance y accesibilidad

46. [uPlot](https://github.com/leeoniya/uplot).
47. [Motion LazyMotion](https://motion.dev/docs/react-lazy-motion).
48. [Web Vitals](https://web.dev/articles/vitals).
49. [WCAG contrast](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum).
50. [Pause, Stop, Hide](https://www.w3.org/WAI/WCAG22/Understanding/pause-stop-hide.html).
51. [Accessibility Principles](https://www.w3.org/WAI/fundamentals/accessibility-principles/).
52. [Focus Not Obscured](https://www.w3.org/WAI/WCAG22/Understanding/focus-not-obscured-minimum).

### ESP32

53. [XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
54. [ESP HTTP Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_http_client.html).
55. [System Time/SNTP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/system_time.html).
56. [Hardware RNG](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/random.html).
57. [Security Overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/security.html).
58. [Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/flash-encryption.html).
59. [Security workflows](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/security-features-enablement-workflows.html).
60. [NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32s3/api-reference/storage/nvs_flash.html).
61. [Arduino-ESP32 releases](https://github.com/espressif/arduino-esp32/releases).

### API e IoT

62. [RFC 6750](https://www.rfc-editor.org/info/rfc6750/): Bearer/TLS.
63. [RFC 9457](https://www.ietf.org/rfc/rfc9457.html): Problem Details.
64. [RFC 9562](https://www.rfc-editor.org/info/rfc9562/): UUID.
65. [JSON Schema 2020-12](https://json-schema.org/draft/2020-12).
66. [IETF HTTPAPI](https://datatracker.ietf.org/wg/httpapi/): Idempotency-Key draft.
67. [OWASP API Top 10](https://owasp.org/API-Security/editions/2023/en/0x11-t10/).
68. [OWASP BOLA](https://owasp.org/API-Security/editions/2023/en/0xa1-broken-object-level-authorization/).
69. [OWASP property authorization](https://owasp.org/API-Security/editions/2023/en/0xa3-broken-object-property-level-authorization/).
70. [NIST IoT FAQ](https://www.nist.gov/itl/applied-cybersecurity/nist-cybersecurity-iot-program/faqs).
71. [NIST IoT catalog](https://pages.nist.gov/IoT-Device-Cybersecurity-Requirement-Catalogs/technical/).
72. [PlatformIO multi-project workspace](https://docs.platformio.org/en/latest/integration/ide/codeanywhere.html).

## 32. Preguntas por cerrar en fase 0

1. Dominio definitivo del firmware.
2. Si el primer upgrade puede resetear rutas v2.
3. Duración claim: propuesta 30 minutos.
4. Intentos: propuesta 10.
5. Retención inicial.
6. Password AP único antes del piloto.
7. Zona default: `America/Bogota`.
8. Nombre final; `Dog-RGB Home Terminal` es provisional.
9. Google Maps en primer MVP o plano simple inicial.
10. Presupuesto mensual máximo.

## 33. Recomendación final

```text
Contrato
  -> Track v3
  -> Claim recuperable
  -> Sync idempotente
  -> Auth/RLS
  -> Portal operativo
  -> Finalización/estadísticas
  -> Mapas/gráficas
  -> Hardening/beta
```

Primer entregable:

1. `contracts/telemetry`.
2. ADRs.
3. SQL y tests RLS.
4. TrackPoint v3 detrás de feature flag.
5. Prueba real de cuatro rutas NVS.
6. Máquina de estados simulada sin red.

Después se integra TLS y el portal visual. El riesgo central no es dibujar el
dashboard; es garantizar que una ruta sobreviva almacenamiento, red intermitente,
duplicados y apagados sin degradar el collar existente.
