# Auditoría de UI/UX del portal web — DOG-RGB

**Fecha:** 2026-08-11
**Alcance:** las 4 pantallas del portal (`/`, `/wifi`, `/config`, `/dev`), sus opciones,
flujos y estados, contrastadas contra el código que las genera
(`Platformio/Dog-RGB/src/web/pages.cpp`).
**Regla dura respetada:** la estética terminal negro/verde se conserva íntegra. Ninguna
propuesta de este informe cambia la paleta, la tipografía monoespaciada, el scanline ni
el glow. Las mediciones de contraste (§3) confirman que la paleta actual ya es sólida;
el problema no es el estilo, es la mecánica.

---

## 1. Resumen ejecutivo

El portal tiene una identidad visual fuerte y coherente, y una arquitectura de
información razonable. **Los problemas serios no son estéticos: son de mecánica de
interacción.** Concretamente, tres defectos hacen que la pantalla de configuración
—la que más trabajo le cuesta al usuario— sea capaz de perder o falsear su trabajo:

1. **Rotar el teléfono borra el trazo GPS** y no se vuelve a dibujar (verificado).
2. **"Restaurar defaults" no recarga el formulario**: la pantalla sigue mostrando los
   valores viejos, y un "Guardar" posterior los reescribe, deshaciendo el reset en
   silencio (verificado).
3. **Guardar desde el botón inferior no produce ningún feedback visible**: el error de
   validación se renderiza 1792 px por encima del viewport, sin scroll ni foco
   (medido). Al usuario le parece que el botón no funciona.

A eso se suman dos ausencias que pesan mucho en el perfil de usuario objetivo (alguien
que arma esto en su garage): **no hay escaneo de redes Wi-Fi** —hay que teclear el SSID
de casa a mano y sin errores desde el móvil— y **no hay vista previa en vivo** del
brillo ni de los efectos, que es justo lo que uno quiere de un collar LED.

**36 hallazgos: 7 altos, 18 medios, 11 bajos.**

Un matiz sobre la estética, porque conviene decirlo: la regla dura no está en tensión
con ninguna de estas correcciones. Todo lo que propongo se resuelve con la paleta que ya
existe (`--accent`, `--muted`, `--danger`, `--accent-2`) y con los componentes que ya
están en `BASE_CSS`.

---

## 2. Método y evidencia

- **Renderizado real.** Las páginas se extraen de `pages.cpp` con
  `tools/ap_portal_preview/extract_pages.py` y se sirven localmente; las 15 capturas se
  regeneraron contra el HEAD actual (15/15 en verde) y están en
  [tests/ap-portal-visual/screenshots/current/](../tests/ap-portal-visual/screenshots/current/).
- **Análisis visual** de cada captura a 428×926 CSS px (iPhone 13 Pro Max, ×3).
- **Sondas instrumentadas.** 16 mediciones automatizadas (P1–P16) sobre el DOM vivo, con
  las APIs mockeadas desde los fixtures del repo. Miden lo que no se puede juzgar a ojo:
  alturas de scroll, conteo de controles, geometría de botones, tamaño de objetivos
  táctiles, contraste calculado, y si un POST sale o no.
- **Lectura del código responsable** de cada hallazgo, con referencia a línea.

Los andamios de las sondas se eliminaron; el árbol quedó limpio. Los números que cito
abajo (`P1`…`P16`) son resultados medidos, no estimaciones.

---

## 3. Lo que está bien — y hay que no romper

Vale la pena fijarlo por escrito antes del listado de problemas, porque parte de lo que
propongo toca estos archivos:

- **La estética funciona y es accesible.** El contraste medido (P15) da entre **6,28:1 y
  6,66:1** en todo el texto secundario, muy por encima del 4,5:1 exigido. El verde
  `--muted:#00A838` sostiene la identidad sin sacrificar legibilidad. Esto no se toca.
- **El `prefers-reduced-motion`** desactiva flicker y blink (`pages.cpp:123`).
- **La jerarquía de las tarjetas de modo** (tarjeta grande + descripción de una línea)
  es una buena decisión: explica antes de pedir.
- **La barra de acciones superior es `sticky`** y funciona (P9: `top=0` al hacer scroll).
- **El copy del bloqueo de portal** (`pages.cpp:1154-1165`) es honesto sobre sus límites
  ("protege de un despiste, no de alguien decidido"). Es el mejor texto del portal.
- **La advertencia de AP abierto** nombra la consecuencia real, no solo la carencia.
- **El `<noscript>`** está en las 4 páginas.

---

## 4. Recorrido pantalla por pantalla

### 4.1 Dashboard `/` — *"¿cómo está mi perro?"*

Captura: `dashboard-active-full.png` · `dashboard-empty-state.png` · `dashboard-route-open.png`

Mide **926 px = exactamente 1 pantalla** (P2), con 10 controles. Es la página más limpia
del portal, y también la que más desaprovecha su espacio.

**D1 · ALTO — Rotar el teléfono borra el trazo GPS.**
Verificado (P1): tras dibujar la ruta, un cambio de viewport deja el canvas **sin un solo
píxel pintado**, y no se vuelve a dibujar nunca.
Causa exacta: `pages.cpp:570` registra `window.addEventListener('resize', resizeTrackCanvas)`,
y `resizeTrackCanvas` (`pages.cpp:286-292`) escribe `trackCanvas.width`/`.height`. Asignar
`width` a un `<canvas>` **borra su bitmap** por especificación. No hay redibujado después.
Esto además explica por qué el canvas sale vacío en `dashboard-route-open.png`: la captura
`fullPage` redimensiona la página y dispara el mismo camino. *La captura del repo es la
evidencia del bug.*

**D2 · MEDIO — La única acción primaria es la redundante.**
"Refresh" es el único botón no-`ghost` de la pantalla (P6), o sea el de mayor peso visual.
Pero la página ya se auto-refresca: `setInterval` de 5 s para status y 10 s para summary
(`pages.cpp:575-576`), más `visibilitychange` (`:571`). Medido: 3 llamadas a la API tras
6 s de inactividad sin tocar nada. El botón más prominente del dashboard no hace nada que
la página no esté haciendo sola, y le roba la jerarquía a "Config LEDs", que es a donde
la gente realmente quiere ir.

**D3 · MEDIO — Lo más valioso está plegado y el resto es hueco.**
"Historial y ruta" —sesiones, distancia por paseo, mapa del recorrido— llega colapsado.
La pantalla termina a los 926 px y debajo queda negro. El contenido con más carga
emocional del producto ("¿por dónde anduvo?") está escondido tras un `[+]`.

**D4 · MEDIO — "Set Home" es invisible salvo en geocerca.**
Medido (P16): en modo velocidad el botón está en `display:none`, sin ninguna pista de que
exista ni de qué lo habilitaría (`pages.cpp:520-522`).

**D5 · BAJO — Redundancia de estado.** La pastilla dice `GPS OK (11)` y tres líneas más
abajo `Estado: GPS OK`.

**D6 · BAJO — Metadatos que se leen como una frase.** `Fecha: 2026-05-06 Ultima lectura: 10:43`
son dos `<span>` con `gap:10px` que el ojo une en una sola oración.

**D7 · BAJO — El estado vacío se contradice.** Muestra `0.00` gigante en Distancia pero
`--` en promedio y máxima, y el mensaje que de verdad explica la situación
("Sin actividad registrada hoy") va en el texto chico y apagado de abajo.

---

### 4.2 `/wifi` — *el punto de entrada de todo usuario nuevo*

Captura: `wifi-connected.png` · `wifi-ap-only.png` · `wifi-open-ap-warning.png`
1567 px (1,7 pantallas), 8 inputs.

**W1 · ALTO — No hay escaneo de redes.**
Medido (P5): no existe botón de búsqueda, el SSID es un `<input type="text">` libre, y en
el servidor **no hay ninguna ruta de escaneo** (`portal_http.cpp:1171-1196`). El usuario
tiene que escribir el nombre exacto de su red de casa a mano, en un teclado de móvil,
sin autocompletado, y si se equivoca en un carácter el único síntoma es el timeout de
30 s de `pollStaStatus` (`pages.cpp:815-818`). Para el perfil "lo armé en el garage",
este es el mayor punto de fricción del portal entero. El ESP32 ya expone
`WiFi.scanNetworks()`; la capacidad está, falta la superficie.

**W2 · MEDIO — Tres avisos apilados sin respirar.**
Medido (P14): al marcar "AP abierto" con cambios pendientes se muestran a la vez
`ap_hint` (10 car., verde apagado), `ap_warn` (45 car., amarillo) y `ap_open_warn`
(149 car., **el mismo amarillo**), los tres con `margin-top: 0px`. Se leen como un solo
bloque de texto amarillo. Una advertencia de seguridad real y una nota operativa menor
compiten con idéntico peso.

**W3 · MEDIO — No se puede olvidar la red de casa.** No hay forma en la UI de borrar la
STA guardada ni de volver a solo-AP a propósito (P5).

**W4 · MEDIO — No se sabe si ya hay password guardada.** El SSID viene precargado desde
NVS pero el campo de password llega vacío con el placeholder "Password". El usuario no
puede distinguir "no hay password" de "hay una y no te la muestro". El bloque del hotspot
sí resuelve esto bien (`apHint` dice "Password configurada"); el de Home Wi-Fi, no.

**W5 · BAJO — Doble etiqueta.** `<span>AP abierto</span>` encima de un checkbox que dice
"Sin password" (`pages.cpp:661-662`): dos nombres para un control.

**W6 · BAJO — "mDNS" sin traducir al usuario.** Un campo llamado "Portal mDNS" con valor
`dog-rgb` y ninguna explicación de que eso convierte la dirección en `dog-rgb.local`.

---

### 4.3 `/config` — *la pantalla que más puede frustrar*

Captura: `config-speed-default.png` · `config-simple-presets.png` · `config-show-mode.png` · `config-validation-errors.png`

La página más pesada con diferencia: **2895 px en modo velocidad y 3356 px en geocerca**,
con **111 controles y 70 inputs** (P2, P3). La visibilidad por modo está bien resuelta
(en `simple` baja a 1634 px y en `show` a 1241 px), pero dentro de cada modo la densidad
es brutal.

**F1 · ALTO — "Borrar Home" no pide confirmación.**
Medido (P12): un solo toque dispara `POST /api/home/clear`, sin diálogo. Es un botón rojo
(`.btn.danger`) pegado a "Nuevo Home (GPS actual)" (`pages.cpp:1047-1048`), y la función
`clearHome()` (`:1592-1597`) no llama a `confirm()` —el portal usa `confirm()` en otros
tres sitios (`:891`, `:1600`, `:1641`), así que la omisión es una inconsistencia, no una
postura de diseño. Borrar el Home rompe el modo geocerca y recuperarlo exige volver al
sitio con fix GPS.

**F2 · ALTO — Los errores no señalan el campo culpable.**
En `config-validation-errors.png` el valor malo es el umbral de **Z2**, pero lo que se
marca en rojo es **la tarjeta entera de "Zonas de velocidad"**, con 9 campos numéricos
dentro. El código lo hace explícito: `addError('speed_lanes_block', ...)` marca el
contenedor, no el input (`pages.cpp:1478-1482`). El mensaje "Rangos deben ser ascendentes"
obliga a comparar nueve números a mano.

**F3 · MEDIO — Sin vista previa en vivo.**
Medido (P11): mover el brillo, cambiar de modo y elegir un preajuste produce **0 POSTs**.
Nada llega al collar hasta pulsar Guardar. Para un producto cuyo output es luz, ajustar a
ciegas y confirmar después es el flujo equivocado.

**F4 · MEDIO — Brillo duplicado.** Un `range` y un `number` para el mismo valor
(`pages.cpp:1005-1006`), en un `grid-2` que en móvil colapsa a dos filas completas: cuatro
renglones ("Brillo", slider, "Valor brillo", campo) para un número.

**F5 · MEDIO — "Preajuste" y "Tema" son el mismo control, ambos visibles.**
Los botones `[data-theme]` y el `<select id="simple_theme">` escriben el mismo estado
(`pages.cpp:1664-1665`). Se ve claramente en `config-simple-presets.png`: "AURORA"
seleccionado arriba y el desplegable "Aurora" repitiéndolo. Además es incoherente con el
patrón de modo, donde el `<select>` equivalente **sí** está oculto (`:1012`).

**F6 · MEDIO — Acción destructiva pegada a la primaria.**
Medido (P4): "Guardar cambios" y "Restaurar defaults" comparten fila con **10 px** de
separación y el mismo tamaño, duplicados arriba y abajo (2 y 2).

**F7 · MEDIO — Una tarjeta entera para decir que no hay nada.** El bloque "Show"
(`pages.cpp:1144-1149`) existe solo para mostrar "Modo demo: rota efectos automaticamente.
No hay parametros." — y ese texto ya está, casi literal, en la tarjeta de modo y en
`MODE_HELP.show`. Tres copias del mismo mensaje en pantalla (visible en
`config-show-mode.png`).

**F8 · MEDIO — Jerga cruda en inglés, visible en todos los modos.**
"Fix quality min (0..8)", "HDOP max", "Max age GGA (ms)", "HDOP factor", "Min segment (m)"
(`pages.cpp:1054-1091`). Está detrás de un desplegable "(avanzado)", lo cual es correcto,
pero se muestra incluso en modo `show`, donde el GPS no influye en los LEDs (P3).

**F9 · BAJO — Abreviaturas sin clave.** Cada una de las 10 zonas tiene `vel` e `int`
(`.ctl-lbl`, 10 px) sin ninguna leyenda que diga que son velocidad e intensidad del efecto.

**F10 · BAJO — Patrones inconsistentes.** `<select id="mode">` oculto tras tarjetas vs.
`<select id="simple_theme">` visible junto a botones equivalentes.

**F11 · BAJO — Ayuda desconectada de su control.** "RAINBOW, GRADIENT_WAVE y FIRE ignoran
el color base" (`:1140`) aparece al final de la sección, muy por debajo de los swatches de
"Color base" a los que se refiere.

---

### 4.4 `/dev` — *diagnóstico*

Captura: `dev-healthy.png`

**V1 · MEDIO — 4740 px (5,1 pantallas) y sólo 5 controles** (P2): es un volcado plano de
unos 60 pares etiqueta/valor. Todos con el mismo peso visual: `Fallos AP: 0`,
`Inicios AP: 3` y `RSSI: -54` se ven idénticos. Una pantalla cuyo propósito es responder
"¿hay algo mal?" no da ninguna señal de que algo esté mal. Es la página menos crítica del
portal, pero también la que más barato se arregla: bastan las clases `.pill.ok/.warn/.bad`
que ya existen.

**V2 · BAJO — `grid-2` colapsa a una columna** en móvil y duplica el largo de una lista
que son pares cortos de etiqueta y número.

---

## 5. Hallazgos transversales

**CC1 · ALTO — Nada protege los cambios sin guardar.**
Medido (P8): con brillo y un umbral de zona modificados, un toque en "← Inicio" navega y
**se pierde todo, sin aviso**. No hay `beforeunload` ni seguimiento de estado sucio. En
una página de 70 inputs, con dos enlaces de salida ("← Inicio" arriba, "Volver" abajo) y
un botón de guardar que no siempre da feedback (CC3), esto se dispara solo. Nótese que
`/wifi` **sí** tiene seguimiento de cambios (`apChanged()`, `pages.cpp:825-831`): la pieza
existe, no se aplicó donde más falta hace.

**CC2 · ALTO — "Restaurar defaults" deja el formulario mintiendo.**
Medido (P7): con el brillo editado a 222, tras un reset exitoso el campo **sigue diciendo
222** y las zonas siguen con sus valores previos. `resetCfg()` (`pages.cpp:1599-1605`)
escribe el estado y no vuelve a leer `/api/config`. Consecuencia real: el usuario ve la
configuración vieja, pulsa "Guardar cambios" y **reescribe encima del reset** sin enterarse.

**CC3 · ALTO — Guardar desde abajo no da feedback.**
Medido (P9): con la página desplazada al pie y un rango inválido, tras pulsar el botón
inferior el cuadro de errores queda en `top: -1792 px`, `visible: false`, sin scroll
(`scrollY` sin cambios) y con el foco todavía en el botón. Y la barra pegajosa tampoco
ayuda: `saveCfg()` hace `return` en la validación fallida **antes** de tocar `#status`
(`pages.cpp:1565-1567`), así que ese hueco queda vacío. Resultado: pulsar Guardar produce
**cero** cambio perceptible. Es el peor momento de interacción del portal.

**CC4 · MEDIO — Se muestra el protocolo crudo como confirmación.**
Medido (P7, P10): tras guardar bien, el texto que ve el usuario es literalmente **"ok"**.
Viene de `statusEl.innerText = r.status` (`pages.cpp:1577`, `:1603`) y de
`setApStatus(r.status + ...)` (`:903`).

**CC5 · MEDIO — El estado nunca se limpia.** Ese "ok" se queda en pantalla
indefinidamente; en la siguiente edición sigue afirmando que todo está guardado.

**CC6 · MEDIO — La carga de configuración no maneja el fallo.**
`fetch('/api/config').then(...)` en `pages.cpp:1675` **no tiene `.catch()`**. Si el collar
está reiniciando o fuera de alcance, la promesa se rechaza en silencio y el formulario se
queda vacío sin ningún aviso. (La validación de rangos impediría guardar ceros encima, así
que no hay corrupción; pero el usuario se queda mirando una pantalla en blanco sin
explicación.) El mismo `fetch` en `/wifi` sí está envuelto en `try/catch` (`:847-859`).

**CC7 · MEDIO — 32 objetivos táctiles por debajo de 44 px.**
Medido (P10): inputs numéricos 33 px de alto, `<select>` 35 px, swatches de color
**32×32**, botones de preajuste **26 px**, y los `<summary>` de sección **20 px**. La fase
de accesibilidad anterior cubrió `.btn`, `.back-link`, `.sl-adv-btn` y los checkbox, pero
no alcanzó a inputs, selects ni a estos controles propios.

**CC8 · MEDIO — Un input deshabilitado es indistinguible de uno activo.**
Medido (P13): al marcar "AP abierto", `#ap_pass` pasa a `disabled` pero su
`background`, `color`, `border` y `opacity` quedan **byte a byte idénticos**. Sólo `.btn`
tiene regla `:disabled` (`pages.cpp:61`).

**CC9 · BAJO — Terminología inconsistente.** El dashboard muestra `Modo: speed` (el
identificador interno, en inglés y minúscula) mientras `/config` lo llama "Velocidad"
(P16). Igual con "Geocerca" en la tarjeta y "Geofence" en el título de la sección
(`:1000` vs `:1034`), y con "← Inicio" arriba y "Volver" abajo para el mismo destino.

**CC10 · BAJO — Tipografía de 10-11 px** en las zonas de velocidad, que es justo la parte
más densa (`.ctl-lbl` 10 px, `.sl-lbl`/`.sl-name` 11 px). El contraste es correcto; el
tamaño no.

---

## 6. Tabla de hallazgos

| # | Severidad | Pantalla | Hallazgo | Evidencia |
| --- | --- | --- | --- | --- |
| D1 | **Alto** | `/` | Rotar el móvil borra el trazo GPS | P1 · `pages.cpp:570,286` |
| W1 | **Alto** | `/wifi` | Sin escaneo de redes; SSID a mano | P5 · sin ruta en `portal_http.cpp` |
| F1 | **Alto** | `/config` | "Borrar Home" sin confirmación | P12 · `pages.cpp:1592` |
| F2 | **Alto** | `/config` | El error marca el bloque, no el campo | visual · `pages.cpp:1478` |
| CC1 | **Alto** | global | Sin protección de cambios sin guardar | P8 |
| CC2 | **Alto** | `/config` | Reset deja valores viejos; Guardar lo deshace | P7 · `pages.cpp:1599` |
| CC3 | **Alto** | `/config` | Guardar desde abajo: cero feedback | P9 · `pages.cpp:1565` |
| D2 | Medio | `/` | La acción primaria es la redundante | P6 |
| D3 | Medio | `/` | Ruta e historial plegados; pantalla vacía | P2 |
| D4 | Medio | `/` | "Set Home" invisible sin explicación | P16 |
| W2 | Medio | `/wifi` | Tres avisos apilados, dos del mismo color | P14 |
| W3 | Medio | `/wifi` | No se puede olvidar la red de casa | P5 |
| W4 | Medio | `/wifi` | No se sabe si hay password guardada | visual |
| F3 | Medio | `/config` | Sin vista previa en vivo | P11 |
| F4 | Medio | `/config` | Brillo duplicado (slider + número) | `pages.cpp:1005` |
| F5 | Medio | `/config` | "Preajuste" y "Tema" duplicados y visibles | `pages.cpp:1664` |
| F6 | Medio | `/config` | Destructivo a 10 px del primario, ×2 | P4 |
| F7 | Medio | `/config` | Tarjeta "Show" sólo para decir que está vacía | `pages.cpp:1144` |
| F8 | Medio | `/config` | Jerga GPS en inglés, visible en todo modo | P3 · `pages.cpp:1054` |
| V1 | Medio | `/dev` | 5,1 pantallas planas, sin señal de salud | P2 |
| CC4 | Medio | global | Se muestra "ok" crudo del protocolo | P7/P10 |
| CC5 | Medio | global | El mensaje de estado nunca se limpia | `pages.cpp:1577` |
| CC6 | Medio | `/config` | `fetch` de config sin `.catch()` | `pages.cpp:1675` |
| CC7 | Medio | global | 32 objetivos táctiles < 44 px | P10 |
| CC8 | Medio | global | Input deshabilitado indistinguible | P13 |
| D5 | Bajo | `/` | Estado duplicado (pastilla + línea) | visual |
| D6 | Bajo | `/` | Fecha y última lectura se funden | visual |
| D7 | Bajo | `/` | Estado vacío contradictorio | visual |
| W5 | Bajo | `/wifi` | Doble etiqueta "AP abierto"/"Sin password" | `pages.cpp:661` |
| W6 | Bajo | `/wifi` | "mDNS" sin explicar | visual |
| F9 | Bajo | `/config` | "vel"/"int" sin leyenda | visual |
| F10 | Bajo | `/config` | Select oculto vs. visible: incoherente | `pages.cpp:1012` |
| F11 | Bajo | `/config` | Ayuda lejos de su control | `pages.cpp:1140` |
| CC9 | Bajo | global | Terminología inconsistente | P16 |
| CC10 | Bajo | global | Tipografía de 10-11 px en zona densa | P15 |
| V2 | Bajo | `/dev` | `grid-2` colapsa y duplica el largo | P2 |

---

## 7. Plan de reparación y mejora

Ordenado por relación daño/esfuerzo. Cada fase deja el portal en un estado coherente y
desplegable. **Ninguna toca la paleta ni la estética.**

### Fase 1 — Detener la pérdida de trabajo *(los 7 altos)*

Es la única fase que corrige defectos que destruyen o falsean datos del usuario.

1. **D1 · Redibujar el trazo tras redimensionar.** Guardar los últimos `points`/`bbox` en
   una variable de módulo y hacer que el handler de `resize` llame a `drawTrack` con ellos
   (con `debounce` de ~150 ms) en vez de sólo redimensionar. Sin datos guardados, el
   handler no debe tocar el canvas.
2. **CC2 · Que el reset recargue.** Extraer el cuerpo del `fetch('/api/config')` de
   `:1675` a una función `applyConfig(c)` y llamarla desde `resetCfg()` tras el éxito.
   Esto elimina de raíz el "Guardar deshace el reset".
3. **CC3 · Feedback donde ocurre la acción.** Al fallar la validación: `scrollIntoView`
   sobre el primer campo inválido, `focus()` en él, y un resumen en el cuadro de errores
   con la cuenta ("3 problemas"). Al guardar bien o mal desde cualquier botón, escribir
   el estado en la barra pegajosa **siempre** (mover el `statusEl.innerText` antes del
   `return` de validación).
4. **CC1 · Guardia de cambios sin guardar.** Un flag `dirty` que se marque en `input`/
   `change` dentro de `<main>` y se limpie al guardar. Con `dirty`, `beforeunload` avisa
   y los enlaces "Inicio"/"Volver" piden confirmación. Reutilizar el patrón de
   `apChanged()` que ya existe en `/wifi`.
5. **F2 · Marcar el campo, no el bloque.** Cambiar `addError('speed_lanes_block', …)` por
   el id del input concreto: en la comprobación de orden, marcar `ln{i}_thr` donde se
   rompe la secuencia, y decirlo en el mensaje ("Z4 debe ser mayor que Z3").
6. **F1 · Confirmar "Borrar Home".** `confirm()` nombrando la consecuencia:
   "Borrar el Home? El modo geocerca dejara de funcionar hasta que definas uno nuevo."
7. **W1 · Escaneo de redes.** `GET /api/wifi/scan` que devuelva SSID + RSSI + si tiene
   password, y en `/wifi` un botón "Buscar redes" que rellene una lista; el campo de texto
   se conserva para redes ocultas. **Es la mejora individual con mayor impacto para el
   usuario objetivo.** Cuidar: el escaneo bloquea brevemente la radio, así que hay que
   avisar en la UI y no lanzarlo automáticamente al cargar.
8. **CC6 · `.catch()` en la carga de config**, con un mensaje accionable
   ("No se pudo leer la configuracion del collar. Reintentar.") y un botón de reintento.

*Verificación:* cada punto tiene ya su sonda (P1, P7, P9, P8, P12, P5). Convertirlas en
tests permanentes en `tests/audit/` cierra el ciclo y evita la regresión.

### Fase 2 — Que el portal se explique

9. **CC4/CC5 · Mensajes humanos y efímeros.** Un helper `flash(msg, tone)` que sustituya
   los `r.status` crudos: "Guardado", "Guardado — reiniciando el hotspot", "No se pudo
   guardar: …". Que se borre solo a los ~4 s.
10. **CC8 · Estilo `:disabled`** para `input`/`select` (opacidad 0,45 y borde apagado),
    con la paleta actual.
11. **W2 · Jerarquizar los avisos.** Un solo contenedor de mensajes con prioridad: la
    advertencia de seguridad en `--danger`, la nota operativa en `--muted`, separación
    vertical real. Nunca dos amarillos seguidos.
12. **W4 · Estado de la password de Home Wi-Fi**, replicando el patrón que ya funciona en
    el hotspot ("Password configurada" / "Sin password").
13. **F9/W6/F11 · Poner las claves donde se usan.** Leyenda "vel = velocidad del efecto ·
    int = intensidad" al inicio de las zonas; una línea bajo mDNS explicando que la
    dirección será `<nombre>.local`; mover la nota de RAINBOW/FIRE junto a "Color base".
14. **CC9 · Vocabulario único.** Traducir el modo en el dashboard con el mismo mapa que
    usa `/config`; "Geocerca" en todos lados; un solo término de navegación.

### Fase 3 — Reducir el trabajo

15. **F3 · Vista previa en vivo** *(la mejora más vistosa)*. `POST /api/preview` que
    aplique brillo/efecto al vuelo **sin persistir en NVS**, disparado con `debounce` de
    ~200 ms al mover el brillo o elegir un efecto, y revertido al estado guardado si se
    sale sin guardar. Encaja con el CSRF actual (`X-Dog-Portal`) y con el bloqueo por PIN.
16. **F4 · Un solo control de brillo:** slider con el número **dentro** de la etiqueta
    ("Brillo — 96"), en una fila.
17. **F5/F10 · Un solo control de tema:** dejar los botones de preajuste y ocultar el
    `<select>`, igual que ya se hace con el modo.
18. **F7 · Eliminar la tarjeta "Show" vacía**; su texto ya vive en la tarjeta de modo.
19. **F6 · Separar lo destructivo.** "Restaurar defaults" fuera de la barra primaria, al
    pie de la página y sólo una vez, con `margin-left:auto` o en su propia fila.
20. **D2/D3 · Reequilibrar el dashboard.** "Config LEDs" pasa a primaria; "Refresh" se
    degrada a `ghost` (o se sustituye por un indicador de "actualizado hace Ns"); y
    "Historial y ruta" llega **abierto**, que es lo que llena la pantalla vacía.
21. **D4 · Explicar "Set Home"** en vez de ocultarlo: mostrarlo deshabilitado con la razón
    ("Disponible en modo Geocerca").
22. **F8 · Suavizar la sección GPS:** etiquetas en español con la unidad y una línea de qué
    hace cada una; ocultarla en modos donde no influye.

### Fase 4 — Pulido

23. **CC7 · Objetivos táctiles a 44 px:** `min-height:44px` en `input`/`select`, swatches
    a 44×44, `.preset-btn` con `min-height:44px`, `<summary>` con `padding` vertical.
    Esto **alarga** las páginas, así que va después de la fase 3, que las acorta.
24. **CC10 · Subir 10-11 px a 12 px** en las zonas de velocidad.
25. **V1/V2 · `/dev` legible:** pastillas `.pill.ok/.warn/.bad` en los contadores que
    importan (fallos AP, overflow GPS, heap), y `grid-2` mantenido en móvil para los pares
    cortos.
26. **D5/D6/D7 · Limpiar el dashboard:** quitar la línea de estado redundante, separar
    fecha y última lectura, y coherencia en el estado vacío (o todo `0.00`, o todo `--`,
    con el mensaje explicativo promovido).

---

## 7.bis Estado de implementación — Fase 1 (completada)

Los 7 hallazgos altos están cerrados. Lo verificado, con lo que se midió antes y después:

| # | Cambio | Antes (medido) | Ahora |
| --- | --- | --- | --- |
| D1 | `handleTrackResize` guarda el último trazo y repinta con *debounce* de 150 ms | canvas sin un solo píxel tras rotar | repinta en portrait y landscape |
| CC2 | `resetCfg` llama a `loadConfig()` tras el éxito | brillo editado seguía en 222 tras el reset | vuelve al valor del collar |
| CC3 | `setStatus` en todas las salidas + `revealFirstError` | error a `top:-1792px`, foco en el botón | campo culpable centrado y con foco |
| CC1 | Bandera `dirty` + `confirmLeave` + `beforeunload` | salía y perdía todo sin avisar | pregunta; al cancelar conserva los cambios |
| F2 | `addError` recibe el id del input concreto | se marcaba la tarjeta con 9 campos | "Z2 debe ser mayor que Z1 (2 km/h)" |
| F1 | `confirm()` nombrando la consecuencia | un toque disparaba `/api/home/clear` | pide confirmación; al cancelar no envía |
| CC6 | `loadConfig` con `catch` y botón "Reintentar" | promesa rechazada en silencio | mensaje accionable |
| W1 | `GET`/`POST /api/wifi/scan` + lista tocable | no existía la capacidad | lista ordenada por señal |

**Decisiones que conviene registrar:**

- **El escaneo es asíncrono a propósito.** `WiFi.scanNetworks(true)` con sondeo desde el
  cliente: un escaneo bloqueante habría congelado el servidor —que es de un solo hilo—
  durante varios segundos, incluida la página que el usuario está mirando. El firmware
  expone `scan_begin/scan_state/scan_entry/scan_release`, con tiempo límite de 15 s y
  liberación del buffer del driver en cuanto el cliente lee la lista.
- **Escanear exige `WIFI_AP_STA`.** Si el collar sirve el portal sólo en AP, se añade la
  interfaz STA para el escaneo en vez de conmutar de modo: bajar el AP habría desconectado
  al propio teléfono que pidió la búsqueda.
- **Los SSID del aire son texto hostil.** Cualquiera puede llamar a su punto de acceso
  `"><img src=x onerror=...>`. Por eso `/wifi` incorpora ahora su propio `esc()` y hay un
  test que intenta exactamente esa inyección. Es el mismo tipo de fallo que la auditoría
  anterior encontró en el SSID guardado, y ahora entraba por una puerta nueva.
- **Se adelantó parte de CC4/CC5** (mensajes en castellano y con autoborrado) sólo dentro
  de `saveCfg`/`resetCfg`, porque CC3 obliga a escribir en `#status` en todas las rutas y
  dejar `"ok"` en una rama y una frase en la de al lado habría sido incoherente. `/wifi` y
  el bloqueo siguen mostrando el estado crudo: eso queda para la fase 2.
- **Presupuestos recalibrados**, como anticipaba §8: `/wifi` 27 500 → 32 500 y `/config`
  46 500 → 53 500, con los `page.reserve()` correspondientes. Se hizo de una vez, no a
  empujones.

**Verificación ejecutada:** firmware `seeed_xiao_esp32s3` SUCCESS (RAM 17,3 % · 56 644 B,
Flash 34,8 % · 1 164 015 B); `web_pages_smoke` en verde con 7 funciones nuevas fijadas;
suite Playwright **48/48**, de los cuales 13 son los tests de regresión nuevos en
[tests/audit/portal.ux.spec.ts](../tests/audit/portal.ux.spec.ts) — uno por hallazgo, y
todos fallaban antes del cambio.

**Sigue sin probarse en hardware.** Los dos puntos a comprobar al flashear: que el escaneo
devuelve redes sin tirar el AP, y que el trazo aguanta una rotación real en el móvil.

---

## 7.ter Estado de implementación — Fase 2 (completada)

| # | Cambio | Antes | Ahora |
| --- | --- | --- | --- |
| CC4 | Mensajes en castellano en `/config`, `/wifi`, Home y bloqueo | `"ok"`, `"Error"`, `"Error: " + reason` | "Guardado", "Sin respuesta del collar. No se guardo." |
| CC5 | Confirmaciones con autoborrado a 4 s; los errores permanecen | el "ok" se quedaba para siempre | desaparece solo; el error no |
| CC8 | `input:disabled,select:disabled` con opacidad, borde discontinuo y cursor | idéntico a un campo activo | claramente inerte |
| W2 | Contenedor `.advisories` con prioridad: seguridad en `--danger`, nota operativa apagada | tres divs pegados, dos del mismo amarillo | jerarquía y separación reales |
| W4 | `has_sta_pass` en `/api/config` + pista bajo el campo | imposible distinguir "no hay" de "hay y no se muestra" | "Ya hay una password guardada. Dejalo vacio para conservarla." |
| W6 | "Nombre corto del portal" + vista previa `dog-rgb.local` en vivo | campo "Portal mDNS" sin explicar | dice qué escribir en el navegador |
| F9 | Leyenda de `vel`/`int` al principio de las zonas | abreviaturas sin clave | explicadas donde se usan |
| F11 | La nota de RAINBOW/FIRE se movió junto a "Color base" | al final de la sección | pegada a su control |
| CC9 | `MODE_NAMES` en el dashboard, "Geocerca" en todas partes, un solo "← Inicio" | `Modo: speed` vs "Velocidad"; "Volver" vs "Inicio" | vocabulario único |

**Dos cosas que salieron mal por el camino y conviene registrar:**

1. **El primer `48/48` de esta fase no valía.** El servidor de vista previa extrae
   `pages.cpp` **una sola vez al arrancar**, y `reuseExistingServer` reutilizó el proceso
   que llevaba horas en marcha: la suite pasó contra páginas obsoletas. Se detectó porque
   las capturas seguían mostrando la UI vieja. Hay que matar el proceso del puerto 4173
   tras tocar `pages.cpp`, o la verificación es una ilusión.
2. **`.field span` era demasiado amplio.** Al meter la vista previa de mDNS dentro de un
   `.help`, heredó `display:block` y `text-transform:uppercase` de la regla pensada para
   las etiquetas de campo, y el texto salió partido y en mayúsculas. Corregido acotándola
   a hijos directos (`.field > label, .field > span`), que es lo que siempre quiso decir.

**Guarda nueva en el smoke:** `FORBIDDEN_PATTERNS` falla la compilación estática si alguien
vuelve a renderizar `r.status` como texto de usuario. Su primera versión daba falsos
positivos con `r.status === 'ok' ? …`; lleva un *lookahead* negativo para distinguir una
comparación de un valor mostrado.

**Verificación:** firmware SUCCESS (RAM 17,3 % · 56 644 B; Flash 35,0 % · 1 168 247 B),
smoke en verde y **59/59 ejecutados dentro del contenedor de CI con la comparación visual
activada** (`AP_PORTAL_VISUAL=1`), no sólo en local. De ellos, **24 son tests de regresión**
en [tests/audit/portal.ux.spec.ts](../tests/audit/portal.ux.spec.ts), 11 nuevos de esta
fase. Presupuesto de `/wifi` recalibrado 32 500 → 36 000.

---

## 7.quater Estado de implementación — Fase 4 (completada)

Ejecutada **antes que la fase 3**, por decisión explícita. Eso tiene una consecuencia
medible que conviene no disimular: la fase 3 era la que *acortaba* las páginas y CC7 las
*alarga*, así que el coste se paga entero (ver la tabla de alturas).

| # | Cambio | Antes (medido) | Ahora (medido) |
| --- | --- | --- | --- |
| CC7 | `min-height:44px` en inputs, selects, `<summary>` y `.preset-btn`; swatches a 44×44 | **32** objetivos < 44 px | **0** en las 4 pantallas |
| CC10 | Tipos de 10-11 px subidos a 12-13 px en las zonas de velocidad | `.ctl-lbl` a 10 px | nada por debajo de 12 px |
| V1 | `setHealth()` colorea sólo los contadores con veredicto | 60 valores del mismo verde | heap, fallos AP y overflow en rojo; fix y calidad en ámbar |
| V2 | `.grid-kv` con `auto-fit minmax(150px)` en `/dev` | 1 columna en móvil | 2 columnas, degradando a 1 a 320 px |
| D5 | La nota del dashboard calla cuando las pastillas ya lo dicen | "Estado: GPS OK" duplicaba la pastilla | vacía en el caso normal |
| D6 | `.meta-pair` con clave y valor apilados | "Fecha: … Ultima lectura: …" se leía como una frase | dos hechos etiquetados |
| D7 | Estado vacío coherente | `0.00` km junto a `--` de media y máxima | `0.00 / 0.0 / 0.0`, o los tres `--` sin fix |

**Coste en scroll (medido a 428 px):**

| Pantalla | Antes de fase 4 | Después | Δ |
| --- | --- | --- | --- |
| `/` | 926 px | 926 px | sin cambio |
| `/wifi` | 1567 px | 1786 px | +14 % |
| `/config` | 2895 px | **4127 px** | **+43 %** (3,1 → 4,5 pantallas) |
| `/dev` | 4740 px | **2979 px** | **−37 %** (5,1 → 3,2 pantallas) |

`/dev` sale ganando: las dos columnas compensan de sobra las filas más altas. `/config`
es el que paga, y es justo lo que la fase 3 recuperaría — eliminar el control de brillo
duplicado, el selector de tema redundante y la tarjeta "Show" vacía. Con la fase 3 hecha,
`/config` debería volver a rondar las 3,5 pantallas conservando los 44 px.

**Decisiones:**

- **El verde "ok" es deliberadamente invisible.** `.health-ok` coincide con el color de
  texto normal. Pintar de verde los sesenta valores sanos habría sido ruido: lo que da
  señal es la excepción. En un collar sano `/dev` se ve como siempre; cuando algo falla,
  salta a la vista.
- **Sólo se colorea lo que tiene veredicto.** `Inicios AP` o `Sats` son informativos y
  quedan neutros a propósito; si todo lleva color, el color deja de significar nada. Hay
  un test que lo comprueba en ambos sentidos.
- **Se añadió `dev.unhealthy.json`.** Un indicador de salud que nunca se ha visto en
  alarma no está probado. El fixture fuerza heap a 14 320 B, 7 fallos de AP, overflow de
  GPS y pérdida de fix, y hay captura visual (`dev-degraded.png`) además del test.
- **Contraste de los colores nuevos verificado**, no supuesto: `health-ok` 14,50:1,
  `health-warn` 14,12:1, `health-bad` 5,08:1 — el rojo es el más justo y aun así supera
  el 4,5:1 exigido.

**Verificación:** firmware SUCCESS (RAM 17,3 % · 56 644 B; Flash 35,0 % · 1 171 071 B),
smoke en verde, **36 tests de regresión** en `portal.ux.spec.ts` (12 nuevos de esta fase),
suite completa verificada en el contenedor de CI con comparación visual. Presupuestos
recalibrados: `html_page` 30 000 → 33 000 y `html_dev_page` 29 500 → 33 000.

---

## 8. Nota de riesgo

El presupuesto de tamaño de página está vigilado por `tools/web_pages_smoke.py`
(`PAGE_BUDGETS` 29 000 / 27 500 / 46 500 / 29 500) y esas guardas **van a saltar** con
las fases 2 y 3. Es el comportamiento correcto: hay que recalibrar presupuesto y
`page.reserve()` de una vez al cerrar cada fase, no aflojarlos de a poco. La fase 3
libera espacio (elimina controles duplicados y una tarjeta entera), así que conviene
medir después de ella y no antes.

Segundo riesgo, el de siempre en este proyecto: **nada de esto está probado en hardware.**
El escaneo de redes (W1) y la vista previa en vivo (F3) son los dos puntos que tocan el
firmware de verdad —radio y bucle de render— y ambos deben validarse en el collar antes
de darse por buenos.
