# Auditoría profunda del portal web — Dog-RGB

**Fecha:** 2026-08-11
**Alcance:** subsistema web completo — `src/web/portal_http.cpp` (1098 L), `src/web/pages.cpp` (1876 L), harness de preview (`tools/ap_portal_preview/`), suite Playwright (`tests/ap-portal-visual/`), smoke estático (`tools/web_pages_smoke.py`).
**Commit auditado:** `56a18bd` (rama `main`, árbol limpio)
**Método:** revisión estática de las dos capas web, verificación de fidelidad del harness, ejecución dinámica con Playwright/Chromium 151 (4 páginas × 4 breakpoints), medición programática de accesibilidad y contraste, prueba de concepto de inyección, y ejecución de los controles automáticos existentes del repo.

---

## 1. Resumen ejecutivo

El portal es un artefacto cuidado: la generación de páginas está bien estructurada, el streaming de tracks está explícitamente endurecido contra bloqueos de socket, la validación de entrada en `/api/config` es exhaustiva y sistemática, y el lenguaje visual (terminal CRT verde) es coherente en las cuatro páginas. El cliente usa `textContent` de forma casi universal, evitando toda una clase de XSS de cliente. No hay errores de consola ni desbordamiento horizontal en ningún breakpoint probado.

Dicho eso, la auditoría encontró **una vulnerabilidad crítica explotable con PoC confirmada**, un modelo de seguridad ausente en la capa HTTP, y —lo más preocupante desde el punto de vista de proceso— **la red de seguridad automática del propio repositorio está rota y no está conectada a nada**.

| Severidad | Nº | Titulares |
|---|---|---|
| **Crítico** | 2 | XSS almacenado vía SSID reflejado; cero autenticación en endpoints mutantes |
| **Alto** | 4 | CSRF total; `reserve()` infradimensionado en las 4 páginas; corrupción de export por reentrancia; sin CI y smoke en rojo |
| **Medio** | 10 | Redirección a `0.0.0.0`; 67 controles sin etiqueta; endpoint muerto; harness ciego al punto de inyección; etc. |
| **Bajo** | 9 | Contraste 4.29:1; foco invisible; sin `<noscript>`; sin `autocomplete`; etc. |

**Hallazgo estructural que unifica el resto:** existen tres capas de verificación (smoke estático, preview extraído, suite visual Playwright). Las tres pasan por alto el punto exacto donde vive el fallo crítico, y ninguna se ejecuta automáticamente. El smoke lleva commits fallando en `main`.

---

## 1.bis Estado de la remediación

**Fase 0 implementada el 2026-08-11.** Verificación: `web_pages_smoke.py` verde (venía con 7 fallos), Playwright 28/28.

| Item | Estado | Nota |
|---|---|---|
| R0.1 escapado del SSID | ✅ Hecho | `html_escape_attr()` + aplicación en el único punto de interpolación |
| R0.1 blacklist de caracteres en `valid_ap_ssid` | ⛔ **No implementado — deliberado** | Ver más abajo |
| R0.2 regresión | ✅ Hecho | Regla estática + 3 tests de extremo a extremo |
| R0.3 `reserve()` | ✅ Hecho | 30 500 / 28 500 / 46 000 / 32 500 |
| R0.4 `POST /api/mode` | ✅ Hecho | Handler, ruta y mock retirados |

**Desviación sobre R0.1 (blacklist).** El plan proponía además rechazar `" < > ' &` en `valid_ap_ssid` como defensa en profundidad. **No se implementó, por decisión razonada:** ese validador gobierna también el SSID de la red *doméstica* en `handle_wifi_save`, y `&` es común en nombres de red reales (todo el parque de routers `AT&T`), igual que los apóstrofos. La lista negra habría cambiado un agujero ya cerrado por una limitación funcional real: imposibilidad de conectar el collar a redes legítimas. Con el escapado correcto, esos caracteres son inocuos.

La defensa en profundidad se implementó en su lugar como **regla estructural en el smoke test**: cualquier `page += <expr>;` que no sea un literal de compilación, `FPSTR(BASE_CSS)` o `html_escape_attr(...)` falla la comprobación. Esto protege todos los puntos de interpolación futuros, no sólo el conocido. Verificado reintroduciendo el defecto original:

```
FAIL: pages.cpp:612: unescaped interpolation into markup: page += wifi_mgr::ssid();
```

**Cierre parcial de M5.** `extract_pages.py` ya no descarta las expresiones en silencio: aborta ante cualquier `page +=` que no reconozca, y modela el escapado del firmware para los valores interpolados, sustituibles vía `AP_PORTAL_SUBST`. Los tests de regresión inyectan SSIDs hostiles por esa vía. La salida por defecto es idéntica byte a byte, por lo que las líneas base visuales no se ven afectadas (15/15 siguen pasando).

*Limitación:* `extract_pages.py` reimplementa `html_escape_attr()` en Python y podría desincronizarse del C++. Lo que impide la deriva es la regla estática del smoke, que garantiza que el lado C++ enruta toda interpolación por el helper.

**No verificado:** PlatformIO no está instalado en la máquina de auditoría, así que **los cambios en C++ no se han compilado**. Requieren un `pio run` antes de flashear. El helper sigue el mismo patrón de iteración sobre `String` ya usado en `runtime_config.cpp:599-628`, y `pages.h` incluye `<Arduino.h>`.

### Fase 1 implementada el 2026-08-11

Reorientada respecto al plan original tras la indicación del propietario: esto es un proyecto DIY, y la protección no puede añadir pasos al usuario que monta el collar en su garaje ni impedir que un familiar vea el dashboard desde su móvil. La Fase 1 se dividió en consecuencia.

**Siempre activo, sin coste de UX** — cierra H1 y M8 para todo el mundo por defecto:

| Item | Implementación |
|---|---|
| R1.2 CSRF | Cabecera `X-Dog-Portal` obligatoria en los 6 handlers de escritura. Una petición cross-origin no puede añadirla sin superar un preflight CORS, que el servidor no responde. El JS propio la envía sin coste al ser mismo origen. |
| R1.2 bis | Retirados `method`/`action` del formulario STA: el envío nativo era el vector CSRF y no puede llevar cabeceras. |
| R1.3 Cabeceras | `X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy` y `Cache-Control: no-store` en **todas** las respuestas, aplicadas desde `note_activity()`, que ya invocaba cada handler. |
| R1.4 Aviso AP abierto | El texto ahora nombra la consecuencia real: cualquiera en rango puede cambiar la configuración, incluido el password del hotspot. |

**Opt-in, apagado de fábrica** — R1.1, `portal_lock`:

- PIN de 4–8 dígitos que sólo se pide al **guardar**; las lecturas nunca se bloquean.
- Sección colapsada en `/config`, marcada como opcional. Un build nuevo no pide nada.
- El cliente lo guarda en `sessionStorage`: se teclea una vez por pestaña. Ante un 401 pregunta y reintenta.
- Cambiar o quitar el PIN es a su vez una escritura protegida: un portal bloqueado no se desbloquea sin el PIN actual.

**Desviación de diseño sobre R1.1.** El plan asumía guardar el secreto en `RuntimeConfig`. **No se hizo:** `ConfigRecord` es un blob A/B con CRC y comprobación de tamaño exacto, y añadirle un campo invalidaría todos los registros ya escritos — un usuario perdería sus 10 zonas de LED afinadas al actualizar el firmware. El bloqueo vive en su propia clave NVS (`portal_lock`), con su propio CRC, sin tocar el registro endurecido.

Compromiso asumido y documentado en la UI: un registro corrupto deja el portal **desbloqueado**, no inaccesible. Bloquear la configuración de un dispositivo casero por un CRC fallido es el peor de los dos fallos.

**Riesgos que el usuario debe conocer** (ambos avisados en la propia interfaz):

- El PIN viaja en claro por HTTP en la red local. Protege de un descuido, no de alguien decidido que ya esté en el Wi-Fi. Cifrarlo exigiría TLS, desproporcionado aquí.
- **Un PIN olvidado sólo se quita reinstalando el firmware por USB.** `Restaurar defaults` no lo borra, y de todos modos es una escritura protegida, así que no serviría de vía de recuperación.

**Cobertura añadida:** regla en el smoke que exige que todo POST pase por el helper `dogPost` con la cabecera; 6 tests Playwright nuevos (cabecera CSRF en `/config` y `/wifi`, bloqueo apagado por defecto, PIN malformado rechazado en cliente, y el ciclo 401 → prompt → reintento con PIN). Suite total: 33/33.

**Sigue sin compilar.** Los nuevos `portal_lock.{h,cpp}` y los cambios en `portal_http.cpp` necesitan `pio run`. Dos puntos que la compilación debe confirmar y que no pude verificar: la firma de `WebServer::collectHeaders(const char**, size_t)` en arduino-esp32 3.3.11, y que `offsetof` sobre `LockRecord` esté disponible con los includes usados.

---

## 2. Hallazgos críticos

### C1 — XSS almacenado vía reflexión del SSID sin escapar

**Ubicación:** [pages.cpp:579-581](../Platformio/Dog-RGB/src/web/pages.cpp#L579-L581)

```cpp
        <input name="ssid" value=")HTML");
  page += wifi_mgr::ssid();          // ← sin escapar, dentro de un atributo entrecomillado
  page += F(R"HTML(">
```

Este es **el único punto de interpolación servidor→HTML de todo el portal** (verificado: `grep "page +=" | grep -v 'F(R"' | grep -v FPSTR` devuelve exactamente esta línea). No pasa por ninguna función de escapado — no existe ninguna en `pages.cpp`.

El validador que gobierna ese valor, [`config::valid_ap_ssid()`](../Platformio/Dog-RGB/src/config/runtime_config.cpp#L599-L613), sólo rechaza cadenas vacías, >32 caracteres, espacios en los extremos y bytes de control. **Acepta `"`, `<`, `>`, `'` y `=`** — todo lo necesario para escapar del atributo.

**Cadena de ataque completa (todos los eslabones verificados):**

1. `POST /api/wifi` con `ssid=" autofocus onfocus="…` — sin autenticación (§C2), aceptado por `valid_ap_ssid`.
2. `wifi_mgr::save_creds()` persiste el SSID en NVS.
3. Cualquier visita posterior a `/wifi` reconstruye la página con la carga útil incrustada.
4. El JS del atacante se ejecuta en el origen del portal y puede invocar `/api/config`, `/api/wifi/ap`, `/api/config/reset` — todos sin autenticación ni token.

**Evidencia — PoC ejecutada:** reproduje la concatenación exacta del firmware con un SSID hostil y la serví a Chromium.

```
tests/audit/portal.audit.spec.ts:204 › SSID reflection XSS  ✓ (955ms)

{
  "page": "xss-poc",
  "hostileSsid": "\" autofocus onfocus=\"window.__xss=1",
  "scriptExecuted": true,       ← JS del atacante ejecutado
  "ssidInputValue": ""
}
```
Artefactos: `tests/audit/evidence/xss-poc.html`, `tests/audit/evidence/xss-poc.png`.

**Nota de impacto no relacionada con seguridad:** el mismo defecto rompe la página con SSIDs legítimos. Una red doméstica llamada `Casa "El Pino"` corrompe el formulario sin que medie ningún atacante.

---

### C2 — Ausencia total de autenticación en la capa HTTP

**Ubicación:** [portal_http.cpp:1064-1091](../Platformio/Dog-RGB/src/web/portal_http.cpp#L1064-L1091)

Ninguno de los 21 endpoints registrados comprueba credenciales. Los que mutan estado persistente:

| Endpoint | Efecto |
|---|---|
| `POST /api/config` | Configuración completa, **incluido SSID y contraseña del AP** |
| `POST /api/config/reset` | Borra toda la configuración a defaults |
| `POST /api/wifi` | Sobrescribe credenciales Wi-Fi de casa |
| `POST /api/wifi/ap` | SSID/contraseña del AP, mDNS |
| `POST /api/home/set`, `/clear` | Geocerca |
| `POST /api/mode` | Modo LED (endpoint muerto, §M4) |

El vector más grave: `POST /api/config` permite **cambiar la contraseña del AP**, lo que expulsa al propietario de su propio collar de forma persistente. La recuperación exige acceso físico y reflasheo.

La exposición depende de `ap_open`. Con AP abierto (opción ofrecida en la UI de `/wifi`), cualquiera en rango de radio tiene control administrativo total. Con WPA2, la superficie se reduce a los clientes asociados — pero sigue sin haber separación entre "puedo unirme a la red" y "puedo reconfigurar el dispositivo".

`/api/dev` además expone sin autenticación las MAC de AP y STA ([portal_http.cpp:298-299](../Platformio/Dog-RGB/src/web/portal_http.cpp#L298-L299)), útiles para seguimiento del dispositivo y de su portador.

---

## 3. Hallazgos altos

### H1 — CSRF en todos los endpoints de escritura

No hay tokens, ni validación de `Origin`/`Referer`, ni cabeceras que fuercen preflight. Los tres patrones son explotables desde una página web arbitraria mientras el teléfono está asociado al AP del collar:

1. **`POST /api/config/reset`** — no requiere cuerpo. `fetch(url,{method:'POST',mode:'no-cors'})` cross-origin lo dispara. Destructivo, sin preflight.
2. **`POST /api/wifi`** — el cliente usa `FormData` ([pages.cpp:851-852](../Platformio/Dog-RGB/src/web/pages.cpp#L851-L852)) y el servidor lee `server.arg("ssid")`. Un `<form method=post>` cross-origin nativo es una *simple request*: sin preflight, se ejecuta siempre. **Este es también el vector de entrega de C1.**
3. **`POST /api/config`** — lee el cuerpo vía `server.arg("plain")`. `Content-Type: text/plain` está en la lista segura de CORS, así que JSON enviado con ese content-type llega sin preflight y se parsea igual. Permite el bloqueo del propietario descrito en C2.

El DNS comodín del portal cautivo (`dns_server.start(DNS_PORT, "*", …)`, [portal_http.cpp:134](../Platformio/Dog-RGB/src/web/portal_http.cpp#L134)) agrava el problema: todo nombre resuelve al dispositivo, de modo que el portal no tiene un origen estable sobre el que razonar.

### H2 — `page.reserve()` infradimensionado en las cuatro páginas

Medido sobre el HTML realmente generado:

| Página | `reserve()` | Tamaño real | Déficit |
|---|---|---|---|
| `html_page` | 25 000 | 26 462 | **+1 462** |
| `html_wifi_page` | 22 000 | 24 616 | **+2 616** |
| `html_config_page` | 36 000 | 39 934 | **+3 934** |
| `html_dev_page` | 26 000 | 28 020 | **+2 020** |

Al agotarse la reserva, `String::concat` reasigna. Durante la reasignación coexisten el búfer viejo y el nuevo: para `/config` eso significa exigir **~80 KB contiguos** al heap, y en la fase final de construcción, que es cuando más fragmentado está. Es la causa raíz más probable de fallos intermitentes de servido de página bajo presión de memoria, y no produce ningún error diagnosticable — `String` falla silenciosamente devolviendo una cadena truncada.

Agravante: no hay chunking para páginas, aunque **sí existe** una implementación de streaming por bloques (`TrackStream`) usada para los exports. La técnica correcta ya está en el archivo, aplicada sólo a la ruta menos crítica.

### H3 — Corrupción del export de tracks por reentrancia

**Ubicación:** [gps.cpp:2084-2095](../Platformio/Dog-RGB/src/gps/gps.cpp#L2084-L2095) y [gps.cpp:2135-2147](../Platformio/Dog-RGB/src/gps/gps.cpp#L2135-L2147)

El comentario declara la intención correcta:

> *"Freeze the RAM tail for this iteration. A callback is allowed to service GNSS input, which may append new points, but an export must remain a finite and internally consistent snapshot."*

La congelación protege los **índices** pero no el **contenido**. `TrackStream::flush()` llama a `gps::tick()` en cada bloque de 768 bytes ([portal_http.cpp:73-76](../Platformio/Dog-RGB/src/web/portal_http.cpp#L73-L76)), lo que puede disparar `track_flush_if_due()`. Cuando el búfer está lleno, ese camino ejecuta ([gps.cpp:1971-1974](../Platformio/Dog-RGB/src/gps/gps.cpp#L1971-L1974)):

```cpp
  if (full) {
    track_current.flush_count = 0;
    track_current.persisted_flush_count = 0;
  }
```

A partir de ahí los puntos nuevos sobrescriben `flush_buf[0..]`, mientras el export sigue leyendo `flush_buf[unpersisted_start..unpersisted_end)` con los límites congelados. **El export emite coordenadas nuevas en posiciones antiguas**: puntos duplicados y fuera de orden en la traza exportada.

El mismo `tick()` puede rotar el anillo NVS (`overwrote_oldest` → `chunk_head` avanza). El segundo bucle de iteración recarga los chunks desde NVS con un `chunk_head` ya obsoleto, mientras `skip_oldest` y `stride` se calcularon en la primera pasada. Resultado: puntos perdidos o repetidos.

No es un fallo de seguridad de memoria — es corrupción silenciosa de datos en la funcionalidad principal del producto (el registro de paseos), y se manifiesta precisamente en el caso de más valor: exportar una sesión larga y activa.

### H4 — Sin CI, y el smoke estático del repo lleva commits en rojo

No existe `.github/workflows/`, ni hooks de git activos. Los tres controles del repo son manuales.

`python3 tools/web_pages_smoke.py` sobre `main` limpio:

```
FAIL: missing required snippet: id="mode_btn"
FAIL: missing function definition: saveMode()
FAIL: html_page reserve 25000 is below estimated size 25951
FAIL: html_wifi_page is 24164 bytes, over budget 24000
FAIL: html_wifi_page reserve 22000 is below estimated size 24164
FAIL: html_config_page reserve 36000 is below estimated size 39160
FAIL: html_dev_page reserve 26000 is below estimated size 27587
EXIT=1
```

**El repositorio ya sabía de H2 y de M4.** El control existe, funciona, detecta los defectos correctos — y nadie lo ejecuta. Esto es más grave que cualquier hallazgo individual: significa que la calidad del portal depende de la disciplina manual, y esa disciplina ya falló.

---

## 4. Hallazgos medios

**M1 — Redirección a `http://0.0.0.0/` en modo STA.** `onNotFound(redirect_to_portal)` construye `Location` con `wifi_mgr::ap_ip()`, que devuelve `IPAddress(0,0,0,0)` cuando el AP está apagado ([wifi_mgr.cpp:999-1001](../Platformio/Dog-RGB/src/wifi/wifi_mgr.cpp#L999-L1001)). Toda URL no reconocida en la interfaz STA manda al usuario a un destino muerto. Debería redirigir a `/` relativo.

**M2 — 67 controles de formulario sin nombre accesible en `/config`.** El patrón `<label>Brillo</label><input id="brightness">` usa `<label>` sin `for=` y sin envolver el control. Un lector de pantalla no anuncia nada. Fallo WCAG 2.2 SC 1.3.1 y 4.1.2 (nivel A). Distribución medida: `/config` 67, `/wifi` 5 (incluidos ambos campos de contraseña), `/` 1, `/dev` 0. Además hay 110 `<label>` huérfanos entre las cuatro páginas (31 en `/config`, 69 en `/dev` usados como etiquetas de datos, donde correspondería `<dl>/<dt>/<dd>`).

**M3 — Estructura semántica del documento ausente.** Las cuatro páginas: sin `lang` en `<html>` (SC 3.1.1, nivel A — el contenido es español y el lector lo pronunciará en el idioma por defecto del sistema), sin `<h1>`, sin `<main>`/`<nav>`/`<header>`. `/config` no tiene **ningún** encabezado. La jerarquía arranca en `<h2>`.

**M4 — `POST /api/mode` es un endpoint muerto.** Ninguna referencia en `pages.cpp` (`grep` → 0 resultados). El cambio de modo se hace vía `/api/config`. Es superficie de escritura sin autenticar y sin consumidor. El smoke lo detecta como `saveMode()` ausente.

**M5 — El harness de preview es ciego al único punto de inyección.** `extract_pages.py` sólo captura literales `page += F(R"…")`; descarta las expresiones intermedias. Resultado verificado en `.ap-portal-preview/wifi.html:158`:

```html
<input name="ssid" value="">     <!-- wifi_mgr::ssid() desaparecido -->
```

Toda la suite Playwright corre contra páginas de las que se ha eliminado exactamente el fragmento vulnerable. Ningún test podía haber encontrado C1 — por construcción.

**M6 — La suite de regresión visual no tiene líneas base.** `toHaveScreenshot` se usa en [ap-portal.visual.spec.ts:119](../tests/ap-portal-visual/ap-portal.visual.spec.ts#L119), pero `git ls-files | grep png` no devuelve nada y `.gitignore` excluye `screenshots/current/` y `screenshots/diff/`. `npm run ap-portal:visual` genera baselines en la primera ejecución y pasa trivialmente. La puerta de regresión visual es decorativa.

**M7 — Objetivos táctiles por debajo del mínimo.** 33 controles miden menos de 44×44 px (SC 2.5.8). Los peores son los checkboxes nativos sin estilar: **13×13 px** (`MODO DIA`, `AP abierto`, `Mostrar password`). Los botones `.btn` miden 32 px de alto; los enlaces `.back-link` 63×18 px.

**M8 — Sin cabeceras de seguridad ni control de caché.** No se emite `X-Content-Type-Options`, `Content-Security-Policy` ni `X-Frame-Options` en ninguna respuesta, y no hay `<meta http-equiv="Content-Security-Policy">`. Las páginas HTML y todas las respuestas `/api/*` salen sin `Cache-Control`, dejando el almacenamiento a la heurística del navegador — inadecuado para datos de telemetría en vivo. Los handlers de sonda cautiva sí ponen `no-store`, lo que evidencia la inconsistencia. Además `server.send(200, "text/html", …)` omite `charset=utf-8`; funciona sólo porque el `<meta charset>` lo rescata.

**M9 — `/api/config` no admite actualizaciones parciales.** Todos los campos son opcionales salvo `speed_ranges_kph` y `effects`, que se leen sin comprobar nulidad ([portal_http.cpp:800-804](../Platformio/Dog-RGB/src/web/portal_http.cpp#L800-L804), [817-823](../Platformio/Dog-RGB/src/web/portal_http.cpp#L817-L823)) y devuelven 400 si faltan. Verifiqué que la UI sí envía el documento completo (9 claves, 10 rangos de efectos), así que no hay fallo visible hoy — pero cualquier cliente de API que intente un PATCH lógico recibe `{"reason":"ranges"}` sin explicación.

**M10 — Credenciales STA validadas con validadores de AP.** `handle_wifi_save` aplica `config::valid_ap_ssid` y `valid_ap_pass` a credenciales de la red *doméstica* ([portal_http.cpp:1047-1054](../Platformio/Dog-RGB/src/web/portal_http.cpp#L1047-L1054)). Las reglas de un AP que uno crea no son las de una red ajena a la que uno se une: se rechaza toda contraseña de 1–7 caracteres, incluidas claves WEP legítimas de 5.

---

## 5. Hallazgos bajos

| ID | Hallazgo | Evidencia |
|---|---|---|
| L1 | Contraste 4.29:1 en `--muted` `#00882A` sobre `#0A0A0A` — bajo el 4.5:1 de SC 1.4.3. Afecta `.tagline`, `.pill` neutro, `.muted`, `.label` (11–12 px) en las 4 páginas | `visual-report.json` |
| L2 | Una sola regla `:focus` en todo el CSS ([pages.cpp:81](../Platformio/Dog-RGB/src/web/pages.cpp#L81)), y sólo para `input`/`select`. Botones y enlaces sin indicador propio | `focusRules: 1` |
| L3 | Sin `<noscript>` en ninguna página. El portal es 100 % dependiente de JS; sin él `/dev` muestra 72 marcadores `--`. Relevante porque los navegadores cautivos (CNA) de iOS/Android son WebViews restringidos | `nojs-*.png` |
| L4 | Cero atributos `autocomplete`. Los gestores de contraseñas guardarán la clave del router doméstico asociada a `192.168.4.1` | `grep -c autocomplete` → 0 |
| L5 | `http://192.168.4.1/` codificado a mano en el mensaje de éxito ([pages.cpp:855](../Platformio/Dog-RGB/src/web/pages.cpp#L855)); se desincroniza si cambia la IP del AP | — |
| L6 | `parse_max_points` usa `String::toInt()` sin control de desbordamiento ([portal_http.cpp:177-189](../Platformio/Dog-RGB/src/web/portal_http.cpp#L177-L189)). Sin impacto de memoria (degrada a 0 = sin límite), pero silencioso | — |
| L7 | Los navegadores de Playwright no estaban instalados y no hay `postinstall`. La suite documentada no arranca en un clon limpio | 6 tests fallando antes de `playwright install` |
| L8 | 10 usos de `innerHTML` con concatenación de cadenas. Hoy seguros (todos los valores derivan de números vía `toFixed`/formateadores), pero es un patrón a un cambio de distancia de ser explotable | `pages.cpp:389-431, 1160, 1247, 1343` |
| L9 | `handle_wifi_save` mezcla `text/plain` en éxito y error de validación con `application/json` en error de almacenamiento ([portal_http.cpp:1039-1061](../Platformio/Dog-RGB/src/web/portal_http.cpp#L1039-L1061)) | — |
| L10 | `PUT`/`DELETE` sobre rutas existentes caen en `onNotFound` → 302, en vez de 405 | — |

---

## 6. Lo que está bien (y conviene no romper)

Un informe que sólo enumera defectos desorienta sobre dónde está el riesgo real. Verificado como sólido:

- **Cero errores de consola y cero desbordamiento horizontal** en las 4 páginas × 4 breakpoints (320/428/768/1280 px). El layout responsive es correcto.
- **`textContent` de forma casi universal** en el cliente — evita XSS de cliente en toda la telemetría.
- **Validación de entrada en `/api/config` ejemplar**: rangos comprobados contra constantes con nombre, relaciones cruzadas (`min_segment_m > max_min_segment_m`), `validate_ranges` con comprobación de monotonía y `isfinite`.
- **Patrón de persistencia transaccional**: `persist_config_or_restore()` restaura el estado previo si falla el guardado. Consistente en los cuatro handlers de escritura.
- **Acciones destructivas confirmadas**: `confirm()` en `resetCfg` y `saveAp` ([pages.cpp:1480](../Platformio/Dog-RGB/src/web/pages.cpp#L1480), [814](../Platformio/Dog-RGB/src/web/pages.cpp#L814)).
- **`prefers-reduced-motion` correctamente soportado** ([pages.cpp:120](../Platformio/Dog-RGB/src/web/pages.cpp#L120)) — desactiva el parpadeo del cursor y el flicker del contenedor.
- **`TrackStream`** resuelve bien el problema real de bloqueo de socket durante exports largos: chunking a 768 B, drenaje de GNSS a ambos lados de cada escritura, comprobación de conexión viva.
- **Feedback de estado en la UI**: botones deshabilitados durante peticiones en vuelo, `try/catch` en todos los `fetch`, mensajes de error visibles.

---

## 7. Plan de remediación

### Fase 0 — Contención inmediata (antes del siguiente flasheo)

**R0.1 · Escapar el SSID reflejado — resuelve C1.**
Añadir a `pages.cpp` y aplicarlo en la línea 580:

```cpp
namespace {
String html_escape_attr(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&':  out += F("&amp;");  break;
      case '<':  out += F("&lt;");   break;
      case '>':  out += F("&gt;");   break;
      case '"':  out += F("&quot;"); break;
      case '\'': out += F("&#39;");  break;
      default:   out += c;           break;
    }
  }
  return out;
}
} // namespace
```

Defensa en profundidad, no alternativa: endurecer `valid_ap_ssid` para rechazar `" < > ' &`. Los dos cambios, no uno.

**R0.2 · Test de regresión que no pueda volverse ciego.** Un test C++ o de host que llame al constructor real de la página con un SSID hostil y afirme que la salida no contiene `value="" `sin escapar. Debe ejercitar la ruta del firmware, no el preview extraído — véase R2.2.

**R0.3 · Corregir los cuatro `reserve()` — resuelve H2.** Subir a tamaño real +15 % de holgura: 30 500 / 28 500 / 46 000 / 32 500. Ejecutar `web_pages_smoke.py` para confirmar verde.

**R0.4 · Eliminar `POST /api/mode`** ([portal_http.cpp:1072](../Platformio/Dog-RGB/src/web/portal_http.cpp#L1072)) y su handler. Reduce superficie sin coste funcional. Actualizar `REQUIRED_SNIPPETS`/`REQUIRED_FUNCTIONS` del smoke para retirar `mode_btn`/`saveMode`.

### Fase 1 — Modelo de seguridad (resuelve C2, H1)

**R1.1 · Autenticación en endpoints mutantes.** Para un dispositivo empotrado sin sesión, la opción con mejor relación coste/beneficio es un **secreto de dispositivo** derivado de la MAC + una sal en NVS, mostrado en `/` y exigido como cabecera `X-Dog-Auth` en todo `POST`. Alternativa más estándar: `WebServer::authenticate()` con Digest y credenciales en NVS. Decisión pendiente del propietario; recomiendo Digest por ser código ya existente en la librería.

**R1.2 · Anti-CSRF.** Con R1.1 vía cabecera personalizada el problema se resuelve solo: una cabecera no segura fuerza preflight, que el dispositivo no responderá. Si se elige Digest, añadir además validación estricta de `Origin` contra la IP del AP y el nombre mDNS, rechazando ausencia de `Origin` en `POST`.

**R1.3 · Cabeceras de seguridad por defecto.** Un helper aplicado en todas las respuestas: `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Cache-Control: no-store` en `/api/*` y en las páginas, y una CSP restrictiva. La CSP exige mover los 22 manejadores inline (`onclick=`) a `addEventListener` — trabajo real, pero es la mitigación estructural de la clase de fallo de C1.

**R1.4 · Alerta explícita en modo AP abierto.** La UI ya muestra `ap_open_warn`; el texto debe decir que el AP abierto concede control administrativo completo a cualquiera en rango, no sólo que "no hay contraseña".

### Fase 2 — Integridad de datos y proceso

**R2.1 · Snapshot real del export — resuelve H3.** Dos opciones:
- *(preferida)* Copiar la cola RAM a un búfer local al inicio de `track_iter_points_internal`, y capturar `chunk_head`/`chunk_count` una sola vez para ambos bucles.
- *(alternativa)* Un flag `track_export_active` que haga a `track_flush_if_due` retornar sin efecto durante el export, aceptando el riesgo de pérdida de puntos si el export es largo.

La primera es correcta; la segunda es más simple. Recomiendo la primera: el búfer de cola es de tamaño acotado (`TRACK_CHUNK_POINTS`).

**R2.2 · Cerrar la brecha de fidelidad del harness — resuelve M5.** `extract_pages.py` debe **fallar ruidosamente** al encontrar un `page +=` que no sea un literal ni `FPSTR(BASE_CSS)`, en lugar de descartarlo en silencio. Mejor aún: sustituir la expresión por un marcador inyectable (`{{SSID}}`) que los tests puedan rellenar con valores hostiles. Sin esto, la suite visual seguirá certificando páginas que no son las que el dispositivo sirve.

**R2.3 · CI que ejecute lo que ya existe — resuelve H4.** Un workflow de GitHub Actions con tres pasos: `web_pages_smoke.py`, `playwright test`, y compilación PlatformIO. Bloquear el merge si falla. El coste es una hora; el retorno es que H2 y M4 se habrían detectado solos.

**R2.4 · Comprometer líneas base visuales — resuelve M6.** Generar baselines, quitar `screenshots/` de `.gitignore` en lo que respecta a las referencias, y añadir `npx playwright install --with-deps chromium` como paso de CI y `postinstall` (resuelve L7).

### Fase 3 — Accesibilidad y calidad de interfaz

**R3.1 · Asociar todas las etiquetas — resuelve M2.** 73 controles afectados. La corrección mecánica: `<label for="x">…</label><input id="x">`. En `/dev`, migrar los 69 pares etiqueta/valor a `<dl>/<dt>/<dd>`. Es el hallazgo de accesibilidad de mayor impacto real.

**R3.2 · Estructura del documento — resuelve M3.** `<html lang="es">`, un `<h1>` por página (el bloque `.brand` es el candidato natural), `<main>` envolviendo `.container`, `<nav>` en `.dashboard-actions`.

**R3.3 · Objetivos táctiles — resuelve M7.** `min-height:44px` en `.btn`, estilar los checkboxes a 24 px con área táctil de 44 px, ampliar `.back-link`.

**R3.4 · Contraste y foco — resuelve L1, L2.** Subir `--muted` de `#00882A` a `#00A838` (6.28:1 medido, mantiene el registro visual verde). Añadir una regla `:focus-visible` global con contorno de 2 px en `--accent`.

**R3.5 · `<noscript>` — resuelve L3.** Un aviso en cada página indicando que el portal requiere JavaScript y ofreciendo la IP del AP como texto plano.

### Fase 4 — Consistencia de API

**R4.1** · Permitir actualizaciones parciales en `/api/config` comprobando nulidad de `speed_ranges_kph` y `effects` como en el resto de campos (M9).
**R4.2** · Validadores STA propios, separados de los de AP (M10).
**R4.3** · Respuestas JSON uniformes en `handle_wifi_save` (L9); `405` para métodos no soportados en rutas existentes (L10).
**R4.4** · `Location: /` relativo en `redirect_to_portal` (M1); derivar la URL del portal del estado real en lugar de la constante `192.168.4.1` (L5).

### Orden de ejecución sugerido

| Prioridad | Elementos | Justificación |
|---|---|---|
| **1** | R0.1–R0.4 | Vulnerabilidad explotable con PoC + riesgo de OOM. Horas, no días. |
| **2** | R2.3, R2.2 | Sin CI, todo lo demás vuelve a degradarse. Habilita el resto. |
| **3** | R1.1–R1.4 | El cambio de mayor calado; requiere decisión de producto sobre el modelo de autenticación. |
| **4** | R2.1 | Corrupción silenciosa de la funcionalidad principal. |
| **5** | R3.1–R3.5 | Volumen alto, riesgo bajo, mecánico. Paralelizable. |
| **6** | R4.1–R4.4 | Higiene de API. |

---

## 8. Artefactos de la auditoría

Añadidos al repositorio:

- `tests/audit/portal.audit.spec.ts` — auditoría estática/a11y de 4 páginas × 4 breakpoints, forma del payload de `/api/config`, y PoC de XSS.
- `tests/audit/portal.visual.spec.ts` — contraste WCAG calculado en página, reglas de foco, comportamiento sin JS.
- `tests/audit/evidence/` — `audit-report.json`, `visual-report.json`, capturas móvil/escritorio de las 4 páginas, `xss-poc.html`, `xss-poc.png`, capturas sin JS.

Reproducir:

```bash
npx playwright install chromium
npx playwright test tests/audit/ --project=iphone-13-pro-max-chromium
python3 tools/web_pages_smoke.py
```

**Limitación declarada:** todo el análisis dinámico corrió contra el harness de preview, no contra hardware. Los hallazgos C2, H1, H3, M1 y M10 se derivan de lectura de código y no se ejecutaron sobre un ESP32-S3 real. C1 se confirmó reproduciendo fielmente la concatenación del firmware en un navegador; la explotación de extremo a extremo sobre el dispositivo no se ha ejecutado.
