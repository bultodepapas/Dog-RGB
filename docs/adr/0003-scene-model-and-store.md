# ADR-0003: Modelo, reproducción y persistencia de escenas LED

**Estado:** Aceptado

**Fecha:** 2026-08-13

**Alcance:** Escenas LED de Fase 4, su contrato público, reproducción, persistencia y relación con `LedPolicyEngine`.

## Contexto

Dog-RGB ya tenía doce efectos, ocho paletas RGBW, layout semántico, crossfade y un modo Show. Sin embargo, Show fabricaba combinaciones efímeras: no existía una receta visual nombrada que pudiera repetirse, respaldarse o probarse como unidad.

WLED demuestra el valor de separar presets nombrados, reproducción y estado instantáneo. Dog-RGB no necesita su escala, filesystem, playlists anidadas ni comandos arbitrarios. Es un collar con 48 píxeles, restricciones de memoria y reglas de producto —status, alertas, Day Mode y límite eléctrico— que una receta estética nunca debe poder reemplazar.

La política de [ADR-0001](0001-wled-clean-room-y-licencia-del-proyecto.md) aplica íntegramente: la implementación adopta ideas públicas, pero el modelo, wire format, almacenamiento, API y pruebas son propios.

## Decisión

### Una escena es una receta de cuerpo

`SceneV1` contiene únicamente effect/paleta A/B, colores base/acento, velocidad, intensidad, escala relativa del cuerpo, transición, mirror, elegibilidad Show y nombre. No serializa `LedState`, modo, status, alerta, GPS, Home, Wi-Fi, brillo global, límite de corriente ni secretos.

`LedPolicyEngine` conserva la autoridad. Welcome y Day Mode pueden ocultar el cuerpo; System y Geofence siguen controlando la región de alerta; brillo global y `PowerLimiter` siguen siendo límites superiores. `body_level` escala solo el target del cuerpo antes del crossfade.

### Catálogo pequeño e IDs estables

- `0`: ninguna escena;
- `1..4`: built-ins `high_visibility`, `calm`, `active` y `party`;
- `128..131`: cuatro slots públicos del usuario;
- los rangos intermedios y `132..254` quedan reservados; `255` es inválido.

Los built-ins son inmutables, viven en flash y los cuatro son elegibles para Show. Los slots del usuario son editables y se incluyen en Show solo si están ocupados, son válidos y tienen `show_eligible=true`.

Se versionan por separado:

- `SCENE_SCHEMA_VERSION = 1`: semántica de `SceneV1` y JSON;
- `SCENE_RECORD_VERSION = 1`: envoltura persistida;
- `SCENE_REGISTRY_VERSION = 1`: built-ins y claves;
- los registries de effects y paletas conservan sus propias versiones.

### Formato binario canónico

Una escena se codifica campo por campo, little-endian, en exactamente 44 bytes. No se vuelca la memoria de un struct C++ y no depende de padding o ABI.

El banco persistido `SCN1` ocupa 196 bytes: header/generación/metadatos, cuatro slots de 44 bytes y CRC-32/IEEE. Los slots desocupados deben estar en cero. Dos bancos consumen como máximo 392 bytes de payload.

La validación es fail-closed y común a catálogo, API y store: IDs, flags, reservados, UTF-8, terminación, rangos, effects, paletas, mirror y safety deben ser coherentes. No hay remapeo silencioso a defaults.

### Store A/B independiente

Las escenas usan el namespace `dogrgb_scn` y las keys `scene_a`/`scene_b` en la partición NVS general. No migran `RuntimeConfig`, no cambian su schema 6 ni usan `tracknvs` o SPIFFS.

`SceneStore` selecciona generaciones de forma wrap-safe, verifica CRC/semántica, alterna bancos y confirma cada escritura por readback. Un no-op no escribe ni incrementa generación. Ante resultado incierto relee ambos bancos y publica solo el estado durable observado.

Un record futuro o sobredimensionado entra en modo read-only y nunca se reescribe automáticamente. Un estado corrupto o ambiguo exige recuperación explícita; esa recuperación escribe el mismo banco generación 1 en A y B para no dejar una única copia supuestamente recuperada.

La lógica se desacopla mediante `SceneRecordBackend`, lo cual permite simular truncamiento, corrupción, errores de lectura/escritura y cortes de energía sin depender de ESP32.

### Reproducción volátil y un solo dueño de Show

`ScenePlayer` mantiene memoria fija: un comando pendiente, un snapshot activo y una bolsa Show de ocho IDs. Apply/cancel no escriben NVS. El último comando pendiente gana y se consume desde el tick LED, no desde el handler HTTP.

Una aplicación manual crea un override volátil y usa `LedIntent::SceneManual`; no crea otro `LedMode`. Cambiar explícitamente el modo o cancelar elimina el override. Editar el slot activo no altera su snapshot; se marca `stale` hasta reaplicarlo.

Show usa Fisher–Yates sobre todas las escenas elegibles, visita cada ID una vez por bolsa y evita repetir el último ID al empezar la siguiente cuando hay alternativas. Welcome y Day Mode pausan su reloj de 30 segundos. El generador Show anterior se elimina para conservar un único dueño de la reproducción.

### API estricta y acotada

Se adoptan siete rutas explícitas bajo `/api/v1/led/scenes`: listado, apply, cancel, save, delete, export e import. Todas las mutaciones requieren `X-Dog-Portal`, el PIN opcional y JSON. Save/delete/import usan `expected_generation` para detectar clientes stale.

El body se limita antes del parse a 4096 bytes, exige `Content-Length` y permite nesting máximo 6. El valor 6 sustituye la estimación inicial de 5 porque la envoltura canónica de import llega legítimamente a `document.scenes[].branch_a.effect.id`; 5 rechazaba el propio export. Form y multipart se rechazan, y el collector raw solo reserva memoria después de validar autorización, longitud y media type.

Import es replace-all, construye un banco scratch, valida todo y solo entonces persiste. `dry_run` ejecuta la misma validación sin mutar. Export usa allowlist y contiene exclusivamente recetas de usuario.

### Presupuesto de recursos

El límite de RAM adicional se mantiene en 1 KiB. El presupuesto de flash se revisa de 20 KiB a 32 KiB: la implementación final medida agrega 29.256 B e incluye codec JSON estricto, siete rutas, diagnósticos y traducción completa de errores. Se conserva el contrato robusto en vez de eliminar validaciones para satisfacer una estimación prematura. El valor medido sigue dentro de la partición de aplicación con amplio margen.

Heap transitorio, latencia NVS y gap LED durante escritura siguen siendo gates HIL; un build o una prueba host no los convierte en mediciones físicas.

## Consecuencias

### Positivas

- Hay una identidad visual repetible y portable sin ampliar el número de efectos.
- Show, aplicación manual y edición comparten un solo catálogo y player.
- Las recetas no pueden suplantar seguridad, telemetría ni política del producto.
- El hot path no depende de NVS, ArduinoJson, `String` ni asignación dinámica.
- Cortes y versiones futuras tienen comportamiento explícito, testeable y diagnosticable.
- La futura UI de Fase 5B puede consumir un contrato real sin inventar otro schema en JavaScript.

### Costos y límites

- Solo existen cuatro slots de usuario y no hay merge, playlists, horarios ni boot preset.
- La API de escenas existe antes que su editor visual; por ahora se usa con clientes HTTP propios.
- Guardar/importar sigue siendo una operación NVS síncrona del plano de control.
- El contrato suma 576 B de RAM estática y 29.256 B de flash frente a Fase 3.
- La aceptación física/HIL de continuidad, heap, latencia, orientación y temperatura permanece pendiente.

## Compatibilidad

Se preservan `RuntimeConfig` schema 6, `ConfigRecord` versión 2, `MODE_SHOW = 2`, los IDs `0..11` de effects, los IDs de paletas y las rutas existentes. Los campos nuevos de state/capabilities/diagnóstico son aditivos.

## Criterio para revisar esta ADR

Revisar antes de cambiar IDs, tamaños wire, cantidad de slots, semántica replace-all, autoridad de la política o backend de persistencia. Una UI nueva no justifica por sí sola romper el schema: debe consumir estas reglas o proponer una ADR de migración con compatibilidad y pruebas.
