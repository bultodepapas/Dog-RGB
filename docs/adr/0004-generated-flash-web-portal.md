# ADR-0004: Portal web generado y servido desde flash

**Estado:** Aceptado

**Fecha:** 2026-08-13

**Alcance:** Pipeline web de Fase 5, assets de producción, contrato HTTP, preview local y editor de escenas/paletas.

## Contexto

Las cuatro páginas del portal vivían como builders de `String` en `src/web/pages.cpp`. Ese diseño mezclaba HTML, CSS, JavaScript y C++, construía respuestas del tamaño completo de cada página en heap y obligaba al preview a analizar C++ para reconstruir una aproximación del contenido servido.

Fases 3 y 4 añadieron metadatos descubribles de effects, paletas, layout y escenas. Seguir ampliando el builder habría duplicado catálogos en JavaScript y agravado el costo de edición, prueba y memoria. El dispositivo debe continuar compilando offline con PlatformIO y sin convertir Node/npm en una dependencia del firmware para el usuario DIY.

## Decisión

### Una fuente web editable

`webui/src` es la única fuente editable de las páginas. `webui/build.mjs` ensambla el CSS compartido, minifica conservadoramente, genera gzip canónico y emite:

- `webui/generated/manifest.json` versionado;
- `include/web/generated_assets.h` y `src/web/generated_assets.cpp` versionados;
- `.ap-portal-preview/*.html` descomprimidos y descartables.

`pages.cpp`, `pages.h` y el extractor inverso se eliminan. Preview, smoke, Playwright y firmware consumen bytes derivados del mismo build, no implementaciones paralelas.

### Toolchain determinista, fuera del build normal de firmware

Node queda fijado en `.node-version`; `html-minifier-terser` queda fijado en `package.json` y `package-lock.json`. El generador exige la versión exacta de Node, rechaza BOM/CR sueltos, normaliza CRLF a LF, usa un orden explícito de inputs y páginas y produce gzip nivel 9 con timestamp cero. Como zlib escribe un identificador de sistema operativo distinto en Unix y Windows, el byte OS del header se canoniza a `0xff` (`unknown`), que no cambia la semántica de descompresión y hace el payload byte-idéntico entre plataformas.

El manifest registra hashes y tamaños canónicos de inputs, fingerprint agregado, hashes de outputs, tamaños, rutas, MIME, ETag y presupuestos. `webui:check` regenera en memoria y falla si los artefactos tracked están stale. Un pre-script de PlatformIO escrito solo con la biblioteca estándar de Python verifica esos hashes sin ejecutar npm, instalar paquetes ni usar red.

Por tanto, editar la UI sí requiere la toolchain web; compilar un checkout limpio del firmware no.

### Respuesta directa desde flash

`PortalAssetServer` recibe un descriptor de datos gzip en `PROGMEM`, tamaño comprimido/descomprimido, MIME y ETag. Las cuatro rutas usan `send_P` con longitud binaria conocida y no crean un `String` proporcional al HTML.

El contrato inicial es:

- `Content-Encoding: gzip` y `Vary: Accept-Encoding`;
- `Cache-Control: no-cache`, sin service worker;
- ETag por SHA-256 y respuesta `304` a revalidación compatible;
- ausencia de `Accept-Encoding` aceptada por compatibilidad con vistas cautivas;
- rechazo `406` cuando el cliente declara explícitamente que no acepta gzip.

Las páginas siguen autocontenidas: no hay CDN, fuentes, scripts, imágenes ni hojas de estilo remotas. Separar assets HTTP puede reconsiderarse cuando exista evidencia de que el beneficio supera el riesgo en captive views.

### UI descubierta desde firmware

El frontend no mantiene catálogos propios de effects, paletas o escenas. Consume `/api/v1/led/capabilities` y `/api/v1/led/scenes`, incluidos IDs estables, modos de paleta, defaults, rangos útiles, safety, layout, límites y features. Los IDs sentinel de paleta y el primer ID de slots de usuario también son capabilities explícitas.

Una capability de escenas ausente deshabilita solo ese workspace; la configuración general continúa. Save/delete/import llevan `expected_generation`; un conflicto actualiza el banco sin descartar el borrador. Import ejecuta `dry_run` antes de pedir confirmación para reemplazar los cuatro slots. Built-ins se copian a un slot de usuario, nunca se sobrescriben.

### Preview deliberadamente aproximado

El canvas representa las dos ramas, orientación, píxeles de status, effect, paleta y colores a baja cadencia. Se pausa si la página/details no están visibles y se inmoviliza con `prefers-reduced-motion`.

No replica el renderer C++, PRNG, timing exacto, RGB→RGBW, compositor ni `PowerLimiter`; la interfaz lo dice de forma visible y ofrece una descripción textual. No usa WebSocket ni polling de frames.

### Presupuestos

Los gates gzip iniciales son 12 KiB `/`, 13 KiB `/wifi`, 23 KiB `/config`, 10 KiB `/dev` y 55 KiB en conjunto. `/config` sube de la previsión de 20 a 23 KiB porque la implementación 5B incluye validación completa, referencias ID/key, concurrencia, import/export y preview. La excepción queda aislada y no eleva el gate conjunto.

## Consecuencias

### Positivas

- Responder una página deja de requerir heap proporcional a su HTML raw.
- La UI se edita y prueba como web normal, pero el firmware conserva un build offline.
- Hashes, gzip y arrays son reproducibles y revisables.
- El preview prueba los mismos bundles que se flashean.
- Cambiar un registro del firmware cambia los controles sin editar un catálogo JavaScript.
- Las escenas tienen flujo completo de aplicar, copiar/guardar, borrar, exportar, validar e importar.

### Costos y límites

- Los arrays C++ y el manifest generan diffs mecánicos; la revisión semántica debe centrarse en `webui/src`.
- Cada cambio web exige regenerar y versionar artefactos.
- Gzip obligatorio necesita pruebas en teléfonos/captive views reales.
- El preview solo comunica intención; no sirve como prueba del frame físico, corriente, temperatura o cadencia LED.
- La aceptación AP/STA, heap mínimo y estabilidad prolongada en hardware sigue siendo un gate separado.

## Alternativas rechazadas

- **Conservar builders `String`:** mantiene deuda y presión de heap.
- **Ejecutar npm desde PlatformIO:** rompe el build DIY/offline y mezcla toolchains.
- **Guardar archivos en SPIFFS/LittleFS:** añade filesystem, partición y estados de fallo sin necesidad para cuatro páginas fijas.
- **Assets externos/CDN:** no funcionan de forma confiable en un portal cautivo sin Internet.
- **Duplicar effects/paletas en JavaScript:** deriva respecto al firmware y hace inseguros los IDs persistentes.
- **Live preview por frames:** añade red/CPU y un segundo contrato antes de demostrar un caso de uso.

## Verificación

- unit tests de gzip canónico, arrays binarios y equivalencia manifest/preview;
- unit test explícito de equivalencia CRLF/LF y byte OS canónico para reproducibilidad Windows/Unix;
- smoke de fuentes, hashes, presupuestos, descompresión, C++ y contrato HTTP, independiente de residuos de preview en un checkout limpio;
- Playwright funcional, accesibilidad/auditoría y snapshots Linux fijados;
- pre-script offline y build PlatformIO de producción;
- pruebas físicas de captive portal, memoria, latencia y estabilidad antes de declarar cerrada toda la aceptación de Fase 5.
