# Plan de Implementación: Retro Console UI — DOG-RGB Portal

> **Objetivo:** Transformar el portal web del collar DOG-RGB de tema moderno claro a una interfaz retro-console (fondo negro, texto verde neón fosforescente, efectos CRT), sin comprometer la estabilidad del ESP32-S3 XIAO ni superar los límites de memoria.

---

## 1. Resumen Ejecutivo

| Item | Estado actual | Objetivo |
|---|---|---|
| Fondo | `#F2F6F8` (gris claro) | `#000000` (negro CRT) |
| Texto | `#0B1220` (casi negro) | `#00FF41` (verde Matrix) |
| Fuente | Space Grotesk / sans-serif | `"Courier New", monospace` |
| Bordes | `#E6EDF2` (gris claro) | `#003300` (verde oscuro) |
| Botones | Relleno sólido oscuro | Borde verde neón, fondo transparente |
| Efectos | `box-shadow` suave | `text-shadow` glow + scanlines CSS |
| Tamaño CSS | ~4.2 KB PROGMEM | ~5.1 KB PROGMEM (estimado) |
| MCU CPU cost de efectos | ~0% | ~0% (todos corren en el browser) |

---

## 2. Fuentes de Investigación (10 fuentes)

| # | Fuente | Técnica clave extraída |
|---|---|---|
| 1 | **CSS-Tricks** — "Old Timey Terminal Styling" | `radial-gradient(rgba(0,150,0,0.75),black 120%)` como fondo; `repeating-linear-gradient` para scanlines vía `body::after`; `text-shadow:0 0 5px #C8C8C8` para glow de fósforo |
| 2 | **Terminal.css** (terminalcss.xyz) | Framework CSS minimalista 3KB gzip para terminales; usa `"Menlo","Monaco","Lucida Console","Courier New",monospace`; CSS variables para temas intercambiables |
| 3 | **Wikipedia — ESP32** | ESP32-S3: dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, sin PSRAM en variante base. Flash SPI: 4/8 MB, sirve código/HTML vía caché SPI |
| 4 | **Espressif — Speed Optimization Guide** | Strings en PROGMEM/RODATA no consumen SRAM. `String.reserve()` asigna DRAM temporal en heap. CSS animations corren en el browser = 0% CPU del MCU |
| 5 | **Arduino ESP8266 PROGMEM Guide** | `const char[] PROGMEM` almacena HTML/CSS en flash. `FPSTR()` para pasar a `String`. La concatenación `page += FPSTR(BASE_CSS)` copia de flash a DRAM solo al servir |
| 6 | **RandomNerdTutorials — ESP32 WebServer** | Confirmación: el MCU solo sirve bytes HTML una vez por request. Toda la animación CSS/JS corre en el cliente. Heap crítico solo durante la construcción de la `String` de respuesta |
| 7 | **MDN Web Docs — CSS text-shadow** | `text-shadow: 0 0 8px #00FF41, 0 0 16px #00FF41` = doble glow neón. Técnica de capas múltiples para simular fosforescencia. Impacto GPU = mínimo (solo compositing) |
| 8 | **MDN Web Docs — CSS animation / @keyframes** | `@keyframes blink { 50% { opacity:0 } }` para cursor parpadeante. `animation: blink 1s step-end infinite` = 0% CPU navegador moderno (GPU-composited) |
| 9 | **A-List Apart** — "CSS Glow Effects" | `filter:drop-shadow()` más costoso que `text-shadow`. Para elementos interactivos usar `box-shadow` con color neón en `:hover/:focus`. Evitar `backdrop-filter` en targets de bajo rendimiento |
| 10 | **Google Fonts — VT323 / Share Tech Mono** | VT323: fuente pixelada retro ideal para headers, ~15KB. Share Tech Mono: monoespaciada moderna-retro, ~8KB. **Riesgo para ESP32:** requerir CDN externo falla en modo AP sin internet. Solución: usar `"Courier New",monospace` como fallback seguro sin CDN |

**Conclusión de investigación:** Todos los efectos visuales retro pueden implementarse con CSS puro en el browser. El MCU solo sirve el HTML/CSS una vez. El costo real en el ESP32-S3 es el tamaño del `String` al construir la respuesta (asignación DRAM temporal).

---

## 3. Análisis del Hardware — ESP32-S3 XIAO

### 3.1 Recursos disponibles

```
CPU:      Dual-core Xtensa LX7 @ 240 MHz
SRAM:     512 KB total → ~280-320 KB libre en runtime
Flash:    PROGMEM / ICACHE_RODATA_ATTR (no ocupa SRAM)
WiFi/BT:  Coexistencia, usa ~90 KB DRAM
```

### 3.2 Presupuesto de memoria para páginas HTML

| Página | `String.reserve()` actual | DRAM temporal |
|---|---|---|
| `/` (dashboard) | 25 000 bytes | ~25 KB |
| `/wifi` | 22 000 bytes | ~22 KB |
| `/config` | 36 000 bytes | ~36 KB |
| `/dev` | ~20 000 bytes | ~20 KB |

> **El nuevo CSS retro agrega ~900 bytes más** al `BASE_CSS` (scanlines, keyframes, glow). Impacto: subir `.reserve()` en `/config` de 36 000 → 37 000. Sin riesgo de OOM con ~300 KB libre.

### 3.3 Efectos CSS seguros vs inseguros en ESP32-S3

| Efecto | Método | Seguridad | Notas |
|---|---|---|---|
| Fondo negro CRT | `radial-gradient` en `body` | ✅ Seguro | ~50 bytes CSS extra |
| Texto verde neón | `color` + `text-shadow` | ✅ Seguro | ~30 bytes extra |
| Scanlines (líneas horizontales) | `body::after` `repeating-linear-gradient` | ✅ Seguro | ~80 bytes CSS |
| Cursor parpadeante | `@keyframes blink` + clase `.cursor` | ✅ Seguro | ~60 bytes CSS |
| Glow en botones hover | `.btn:hover { box-shadow }` | ✅ Seguro | ~30 bytes |
| Transiciones suaves | `transition: all 0.15s` | ✅ Seguro | ~20 bytes |
| Fuente CDN (Google Fonts) | `@import url(...)` | ⚠️ Solo si hay internet | Falla en modo AP offline |
| Fuente base64 embebida | `@font-face { src: url(data:...) }` | ⚠️ +15-40 KB por fuente | Sube `.reserve()` significativamente |
| Canvas CRT shader | JavaScript + WebGL | ❌ Evitar | Demasiado JS para servir |
| Animación JS de "typing" | `setInterval` char por char | ❌ Evitar | JS innecesario, ~500 bytes extra |
| `backdrop-filter: blur()` | CSS | ❌ Evitar | Lento en móviles viejos |
| `filter: drop-shadow()` | CSS en muchos elementos | ❌ Evitar | Costoso para GPU móvil |

**Regla de oro para el ESP32-S3:** Si el efecto corre 100% en CSS (keyframes, gradients, shadows), es GRATIS para el MCU. Solo importa el tamaño del string HTML.

---

## 4. Análisis de la UI Actual — Pantalla por Pantalla

### 4.1 `/` — Dashboard

**Estructura actual:**
```
┌─ Hero Card ───────────────────────────────────────────┐
│ [BRAND: DOG-RGB]   [GPS:--][WiFi:--][Modo:--][Home:--] │
│                                                        │
│ [Modo: select▼]  [Aplicar]                            │ ← CONTROL #1
│                                                        │
│ [Actualizar] [Config Wi-Fi] [Ajustes]                 │
│ [Actualizar Home (oculto)] [Avanzado▼]                │
└────────────────────────────────────────────────────────┘
┌─ Summary Card ─────────────────────────────────────────┐
│ Distancia: --km          Vel.prom: --  Vel.máx: --    │
└────────────────────────────────────────────────────────┘
┌─ Historial y ruta (details) ──────────────────────────┐
│ GPS track canvas                                       │
└────────────────────────────────────────────────────────┘
```

**Bugs / Problemas identificados:**

| # | Problema | Código fuente | Impacto |
|---|---|---|---|
| D1 | **Modo duplicado**: `<select id="mode_select">` en hero duplica el `<select id="mode">` de `/config` | `pages.cpp:141` y `pages.cpp:956` | Confusión UX, el usuario no sabe cuál usar |
| D2 | **Layout shift** de `home_btn`: `style="display:none"` reserva espacio invisible, causa reflow al mostrarse | `pages.cpp:155` | Salto de layout en móvil |
| D3 | **Orden de botones confuso**: "Actualizar" primero, luego "Config Wi-Fi", luego "Ajustes". El flujo debería ser: Ajustes → Wi-Fi → Actualizar | `pages.cpp:153-158` | UX — acción más frecuente no es la más visible |
| D4 | **`<details class="advanced-menu">`** con `<summary class="btn ghost">` mezclado entre botones de acción | `pages.cpp:157-159` | Elemento interactivo no estándar entre botones, falla en algunas versiones de iOS |
| D5 | **`pill-home` siempre en DOM**: visible aunque `geofence` no esté activo | `pages.cpp:136` | Carga visual innecesaria |

---

### 4.2 `/wifi` — Configuración Wi-Fi

**Estructura actual:**
```
┌─ Hero Card ───────────────────────────────────────────┐
│ DOG-RGB — Configurar Wi-Fi                            │
└────────────────────────────────────────────────────────┘
┌─ Estado Wi-Fi ─────────────────────────────────────────┐
│ Home Wi-Fi: --   Hotspot: --   Portal: --   mDNS: --  │
│ [Actualizar estado]                                    │
└────────────────────────────────────────────────────────┘
┌─ Home Wi-Fi (form) ───────────────────────────────────┐
│ SSID: [_____]  Password: [_____]  [Conectar]          │
└────────────────────────────────────────────────────────┘
┌─ Configurar Hotspot ──────────────────────────────────┐
│ SSID AP: [_____]  mDNS: [_____]  AP Abierto: [_]     │
│ [Guardar cambios AP]                                   │
└────────────────────────────────────────────────────────┘
```

**Bugs / Problemas:**

| # | Problema | Impacto |
|---|---|---|
| W1 | **Sin botón "Volver"** — no hay `<a href="/">Inicio</a>` | Usuario debe usar botón atrás del navegador |
| W2 | **Labels y estado en inglés mixto**: "Home Wi-Fi" / "AP" vs textos en español | Inconsistencia |
| W3 | Confirmación AP con `confirm()` nativo del browser — bloquea hilo JS | Menor, pero retro console podría usar su propio dialog |

---

### 4.3 `/config` — Configuración LEDs y Modo

**Estructura actual:**
```
┌─ Hero Card ───────────────────────────────────────────┐
│ DOG-RGB — Modos y LEDs                                │
└────────────────────────────────────────────────────────┘
┌─ Sticky Actions (TOP, z-index:2) ─────────────────────┐  ← SUPERPONE CONTENIDO
│ [Guardar cambios]  [Restaurar defaults]  status: --   │
└────────────────────────────────────────────────────────┘
┌─ Modo y brillo (details, open) ────────────────────────┐
│ [Velocidad] [Geocerca] [Simple] [Show]  ← mode-cards  │ ← CONTROL #2a
│ Brillo: [===slider===]  Valor: [___]                  │
│ ☐ Modo DIA                                            │
│ Modo: [select▼]                                       │ ← CONTROL #2b (duplica mode-cards)
└────────────────────────────────────────────────────────┘
┌─ Umbrales velocidad (details, cerrado) ───────────────┐
│ [effects-row 5 columnas] × N rangos                   │
└────────────────────────────────────────────────────────┘
...más secciones (geocerca, simple, show, avanzado)...
```

**Bugs / Problemas:**

| # | Problema | Código fuente | Impacto |
|---|---|---|---|
| C1 | **`sticky-actions` superpone contenido**: `position:sticky;top:8px;z-index:2` — cuando se hace scroll rápido, cubre los primeros campos del formulario | `pages.cpp:53, 932` | Bug visual crítico en móvil |
| C2 | **Doble control de modo**: `mode-cards` (visual, bonito) Y `<select id="mode">` hacen lo mismo | `pages.cpp:941, 956` | Confusión — ¿cuál manda? JS sincroniza ambos pero es frágil |
| C3 | **`effects-row` 5 columnas**: en móvil colapsa a 4 filas × N rangos = pantalla muy larga | `pages.cpp:82, 101` | Mala UX en móvil |
| C4 | **Sin navegación de vuelta**: no hay `<a href="/">` | UX — igual que Wi-Fi |
| C5 | **Reset sin confirmación clara**: `confirm()` nativo al restaurar defaults podría perderse | Datos del usuario en riesgo |

---

### 4.4 `/dev` — Diagnóstico

**Estructura:**  
JSON dumps crudos del sistema (GPS, heap, WiFi, BLE). Orientado a desarrolladores.

**Problemas:**
| # | Problema | Impacto |
|---|---|---|
| V1 | **Sin estilo de "terminal"**: ya muestra JSON pero con fondo blanco | Retro console es ideal aquí — JSON en verde sobre negro es perfecto |
| V2 | Sin modo de refresco automático configurable | Menor |

---

## 5. Inventario Completo de Bugs y Mejoras (priorizado)

| Pri | ID | Tipo | Descripción |
|---|---|---|---|
| 🔴 Alta | C1 | Bug | `sticky-actions` superpone campos al hacer scroll |
| 🔴 Alta | C2/D1 | Duplicado | Doble selector de modo (dashboard + config, y mode-cards + select en config) |
| 🟡 Media | D3 | UX | Orden de botones en dashboard no sigue flujo natural |
| 🟡 Media | D4 | UX | `<details>` mezclado entre botones de acción |
| 🟡 Media | W1/C4 | UX | Sin botón "Volver al inicio" en páginas secundarias |
| 🟡 Media | D2 | Bug | Layout shift del `home_btn` oculto |
| 🟢 Baja | C3 | UX | `effects-row` muy larga en móvil |
| 🟢 Baja | W2 | Estilo | Textos inglés/español mezclados |
| 🟢 Baja | D5 | UX | `pill-home` siempre en DOM aunque no sea relevante |
| 🟢 Baja | V1 | Mejora | `/dev` ya es "terminal" — con retro CSS quedará perfecto sin cambios |

---

## 6. Sistema de Diseño Retro Console

### 6.1 Paleta de colores

```css
/* NUEVA PALETA RETRO CONSOLE */
--bg:          #000000;   /* fondo negro CRT */
--surface:     #0A0A0A;   /* cards ligeramente más claras */
--text:        #00FF41;   /* verde Matrix (#00FF41 = RGB 0,255,65) */
--text-bright: #CCFFCC;   /* blanco-verde para valores prominentes */
--muted:       #00882A;   /* verde apagado para labels */
--accent:      #00FF41;   /* neón principal */
--accent-2:    #FFD700;   /* ámbar/dorado para advertencias */
--danger:      #FF0055;   /* rojo neón para errores */
--border:      #003300;   /* verde muy oscuro para bordes */
--border-glow: #00FF41;   /* borde activo con glow */
--shadow:      0 0 10px rgba(0,255,65,0.15); /* glow suave */
--glow-sm:     0 0 4px #00FF41;
--glow-md:     0 0 8px #00FF41, 0 0 16px rgba(0,255,65,0.4);
--glow-lg:     0 0 8px #00FF41, 0 0 20px #00FF41, 0 0 40px rgba(0,255,65,0.3);
--radius:      3px;       /* bordes cuadrados — más retro */
```

### 6.2 Tipografía

**Estrategia segura para ESP32 (sin CDN):**
```css
--font-mono: "Courier New", "Lucida Console", "DejaVu Sans Mono", monospace;
```

**Estrategia con CDN (solo si hay internet):**
```html
<!-- Añadir al <head> solo si se detecta conectividad externa -->
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap" rel="stylesheet">
```
> ⚠️ En modo AP (sin router), el CDN no carga. La fuente fallback `Courier New` es aceptable y está disponible en todos los SO.

**RECOMENDACIÓN FINAL:** Usar `"Courier New", monospace` siempre. Es el look retro más auténtico, no depende de CDN, y no añade bytes a la página.

### 6.3 Efectos CSS — Código de referencia

#### Scanlines CRT (pseudo-elemento en body)
```css
body::after {
  content: '';
  position: fixed;
  top: 0; left: 0; right: 0; bottom: 0;
  background: repeating-linear-gradient(
    0deg,
    rgba(0,0,0,0.06) 0px,
    rgba(0,0,0,0.06) 1px,
    transparent 1px,
    transparent 3px
  );
  pointer-events: none;
  z-index: 9999;
}
```

#### Glow en texto
```css
/* Glow suave — para texto general */
body { text-shadow: 0 0 3px rgba(0,255,65,0.4); }

/* Glow fuerte — para valores y titles */
.value, .brand { text-shadow: var(--glow-md); }
```

#### Cursor parpadeante
```css
@keyframes blink {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0; }
}
.cursor::after {
  content: '_';
  animation: blink 1s step-end infinite;
  margin-left: 2px;
}
```

#### Animación de entrada (flicker al cargar)
```css
@keyframes flicker {
  0%   { opacity: 0.96; }
  5%   { opacity: 0.92; }
  10%  { opacity: 1; }
  70%  { opacity: 0.98; }
  100% { opacity: 1; }
}
.container { animation: flicker 0.5s ease-in-out forwards; }
```

#### Botones retro
```css
.btn {
  background: transparent;
  color: var(--accent);
  border: 1px solid var(--accent);
  font-family: var(--font-mono);
  text-transform: uppercase;
  letter-spacing: 0.08em;
  transition: box-shadow 0.15s, background 0.15s;
}
.btn:hover {
  background: rgba(0,255,65,0.08);
  box-shadow: var(--glow-sm);
}
.btn:active {
  background: rgba(0,255,65,0.18);
}
.btn.danger {
  color: var(--danger);
  border-color: var(--danger);
}
.btn.danger:hover {
  box-shadow: 0 0 8px var(--danger);
}
```

#### Inputs retro
```css
input, select {
  background: #000;
  color: var(--text);
  border: 1px solid var(--border);
  font-family: var(--font-mono);
}
input:focus, select:focus {
  border-color: var(--accent);
  box-shadow: var(--glow-sm);
  outline: none;
}
```

#### Cards retro
```css
.card {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  box-shadow: var(--shadow);
}
```

#### Pills de estado
```css
.pill.ok  { border-color:#00FF41; color:#00FF41; background:rgba(0,255,65,0.08); }
.pill.warn{ border-color:#FFD700; color:#FFD700; background:rgba(255,215,0,0.08);}
.pill.bad { border-color:#FF0055; color:#FF0055; background:rgba(255,0,85,0.08); }
```

---

## 7. Nuevo BASE_CSS completo (propuesto)

El nuevo `BASE_CSS` reemplaza completamente el existente en `pages.cpp`. Estimado: **~5.1 KB** vs los actuales ~4.2 KB. Diferencia: +~900 bytes de CSS (scanlines, keyframes, glow, fuente mono).

```css
:root{
  --bg:#000000;--surface:#0A0A0A;--text:#00FF41;--text-bright:#CCFFCC;
  --muted:#00882A;--accent:#00FF41;--accent-2:#FFD700;--danger:#FF0055;
  --border:#003300;--shadow:0 0 10px rgba(0,255,65,0.12);
  --glow-sm:0 0 4px #00FF41;
  --glow-md:0 0 8px #00FF41,0 0 16px rgba(0,255,65,0.4);
  --radius:3px;--space-1:6px;--space-2:10px;--space-3:14px;--space-4:20px;--space-5:28px;
  --font-mono:"Courier New","Lucida Console","DejaVu Sans Mono",monospace;
}
*{box-sizing:border-box;}
body{
  margin:0;
  font-family:var(--font-mono);
  font-size:14px;
  background:#000;
  color:var(--text);
  text-shadow:0 0 3px rgba(0,255,65,0.35);
  line-height:1.5;
}
body::after{
  content:'';position:fixed;top:0;left:0;right:0;bottom:0;
  background:repeating-linear-gradient(0deg,rgba(0,0,0,0.06) 0px,rgba(0,0,0,0.06) 1px,transparent 1px,transparent 3px);
  pointer-events:none;z-index:9999;
}
@keyframes flicker{0%{opacity:0.96}5%{opacity:0.92}10%{opacity:1}70%{opacity:0.98}100%{opacity:1}}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0}}
a{color:var(--accent);text-decoration:none;}
a:hover{text-shadow:var(--glow-sm);}
h1,h2{margin:0 0 8px 0;text-shadow:var(--glow-md);}
h1{font-size:22px;letter-spacing:0.06em;}
h2{font-size:16px;letter-spacing:0.04em;}
.container{max-width:900px;margin:0 auto;padding:20px;animation:flicker 0.5s ease-in-out forwards;}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);padding:var(--space-4);}
.hero{display:flex;flex-direction:column;gap:var(--space-3);}
.hero-top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:var(--space-3);}
.brand{font-size:22px;font-weight:700;letter-spacing:0.1em;text-shadow:var(--glow-md);text-transform:uppercase;}
.brand::after{content:'_';animation:blink 1s step-end infinite;}
.tagline{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;}
.chips{display:flex;flex-wrap:wrap;gap:8px;}
.pill{font-size:11px;border-radius:2px;padding:4px 8px;background:transparent;border:1px solid var(--border);color:var(--muted);font-family:var(--font-mono);text-transform:uppercase;letter-spacing:0.05em;}
.pill.ok{border-color:#00FF41;color:#00FF41;background:rgba(0,255,65,0.06);}
.pill.warn{border-color:#FFD700;color:#FFD700;background:rgba(255,215,0,0.06);}
.pill.bad{border-color:#FF0055;color:#FF0055;background:rgba(255,0,85,0.06);}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.grid{display:grid;gap:12px;}
.grid-2{grid-template-columns:repeat(2,minmax(0,1fr));}
.grid-3{grid-template-columns:repeat(3,minmax(0,1fr));}
.label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;}
.value{font-size:24px;font-weight:700;text-shadow:var(--glow-md);}
.data{font-size:14px;font-weight:600;color:var(--text);}
.mono{font-family:var(--font-mono);}
.code{background:#000;color:var(--text);padding:12px;border-radius:var(--radius);overflow:auto;font-size:12px;border:1px solid var(--border);}
.metric .label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;}
.metric .value{font-size:28px;font-weight:700;line-height:1.1;text-shadow:var(--glow-md);}
.metric .unit{font-size:12px;color:var(--muted);}
.dashboard-summary{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,1fr);gap:14px;align-items:stretch;}
.primary-metric{display:flex;flex-direction:column;justify-content:center;min-height:118px;}
.primary-metric .value{font-size:42px;font-weight:700;line-height:1;text-shadow:var(--glow-lg);}
.stat-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}
.stat{background:#000;border:1px solid var(--border);border-radius:var(--radius);padding:12px;}
.stat .value{font-size:24px;font-weight:700;line-height:1.1;}
.summary-meta{grid-column:1/-1;display:flex;flex-wrap:wrap;gap:10px;color:var(--muted);font-size:12px;}
.empty-state{padding:12px;background:#000;border:1px solid var(--border);border-radius:var(--radius);color:var(--muted);font-size:12px;}
.muted{color:var(--muted);font-size:12px;}
.btn{
  display:inline-flex;align-items:center;justify-content:center;gap:8px;
  padding:8px 14px;border-radius:var(--radius);
  border:1px solid var(--accent);background:transparent;
  color:var(--accent);font-weight:600;font-size:12px;
  font-family:var(--font-mono);text-transform:uppercase;letter-spacing:0.06em;
  cursor:pointer;transition:box-shadow 0.15s,background 0.15s;
}
.btn:hover{background:rgba(0,255,65,0.08);box-shadow:var(--glow-sm);}
.btn:active{background:rgba(0,255,65,0.16);}
.btn.ghost{border-color:var(--border);color:var(--muted);}
.btn.ghost:hover{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.btn.danger{border-color:var(--danger);color:var(--danger);}
.btn.danger:hover{box-shadow:0 0 8px var(--danger);}
.btn:disabled{opacity:0.4;cursor:not-allowed;}
.actions{display:flex;flex-wrap:wrap;gap:10px;margin:14px 0;}
.sticky-actions{position:sticky;top:0;z-index:10;background:#000;border-bottom:1px solid var(--border);padding:10px 0;margin-bottom:8px;}
.dashboard-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.advanced-menu{position:relative;}
.advanced-menu>summary{list-style:none;}
.advanced-menu>summary::-webkit-details-marker{display:none;}
.advanced-menu .section{margin-top:8px;}
.mode-cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;}
.mode-card{width:100%;text-align:left;border:1px solid var(--border);background:transparent;border-radius:var(--radius);padding:10px;cursor:pointer;font-family:var(--font-mono);color:var(--muted);}
.mode-card strong{display:block;font-size:12px;margin-bottom:4px;text-transform:uppercase;letter-spacing:0.05em;}
.mode-card span{display:block;color:var(--muted);font-size:11px;line-height:1.3;}
.mode-card.active{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.mode-card:hover{border-color:var(--accent);color:var(--text);}
.preset-row,.swatch-row{display:flex;flex-wrap:wrap;gap:8px;}
.preset-btn{border:1px solid var(--border);border-radius:var(--radius);background:transparent;color:var(--muted);padding:6px 10px;font-family:var(--font-mono);font-size:11px;text-transform:uppercase;cursor:pointer;}
.preset-btn.active{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.swatch{width:32px;height:32px;border-radius:2px;border:2px solid var(--border);cursor:pointer;}
.swatch.active{border-color:var(--accent);box-shadow:var(--glow-sm);}
.field label{display:block;font-size:11px;color:var(--muted);margin-bottom:6px;text-transform:uppercase;letter-spacing:0.06em;}
input,select{
  width:100%;padding:8px 10px;
  border:1px solid var(--border);border-radius:var(--radius);
  background:#000;font-family:var(--font-mono);font-size:13px;color:var(--text);
}
input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:var(--glow-sm);}
input[type="checkbox"]{width:auto;margin-right:6px;accent-color:var(--accent);}
input[type="range"]{accent-color:var(--accent);}
.section{margin-top:14px;}
.section.invalid{border-color:var(--danger);}
input.invalid,select.invalid{border-color:var(--danger);}
.error{color:var(--danger);font-size:12px;}
.error-box{padding:12px;}
.error-box:empty{display:none;}
.warn{color:var(--accent-2);font-size:12px;}
.help{color:var(--muted);font-size:12px;margin-top:6px;}
.notice{color:var(--muted);font-size:12px;margin-top:6px;}
.effects-row{display:grid;grid-template-columns:64px 1fr 1fr 1fr 1fr;grid-template-areas:"range a b speed intensity";gap:10px;align-items:end;margin:10px 0;padding:10px;background:#000;border:1px solid var(--border);border-radius:var(--radius);}
.effects-row .range-label{grid-area:range;font-weight:700;color:var(--muted);align-self:center;font-size:11px;text-transform:uppercase;}
.effects-row .field-a{grid-area:a;}
.effects-row .field-b{grid-area:b;}
.effects-row .field-speed{grid-area:speed;}
.effects-row .field-intensity{grid-area:intensity;}
details.section>summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between;font-weight:600;text-transform:uppercase;letter-spacing:0.06em;font-size:13px;}
details.section>summary::-webkit-details-marker{display:none;}
details.section>summary::after{content:'[+]';font-weight:700;color:var(--muted);font-size:11px;}
details.section[open]>summary::after{content:'[-]';}
.section-body{margin-top:10px;}
.action-bar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.mode-row{display:flex;flex-wrap:wrap;gap:10px;align-items:end;}
.field-inline{min-width:180px;}
.session-card{margin:8px 0;padding:10px;background:#000;border:1px solid var(--border);border-radius:var(--radius);}
.track-controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-bottom:10px;}
.track-canvas{width:100%;height:220px;border:1px solid var(--border);border-radius:var(--radius);background:#000;}
.track-note{color:var(--muted);font-size:12px;margin:8px 0;}
.is-hidden{display:none !important;}
.back-link{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--muted);margin-bottom:14px;text-transform:uppercase;letter-spacing:0.06em;}
.back-link:hover{color:var(--accent);}
@media(max-width:760px){
  .grid-2,.grid-3,.dashboard-summary,.stat-grid,.mode-cards{grid-template-columns:1fr;}
  .primary-metric{min-height:auto;}
  .primary-metric .value{font-size:36px;}
  .dashboard-actions .btn,.dashboard-actions summary.btn{flex:1 1 130px;}
  .sticky-actions{margin-left:-20px;margin-right:-20px;padding-left:20px;padding-right:20px;border-radius:0;}
  .effects-row{grid-template-columns:52px 1fr;grid-template-areas:"range a" "range b" "range speed" "range intensity";}
  .effects-row .range-label{align-self:start;padding-top:4px;}
  .hero-top{flex-direction:column;align-items:flex-start;}
}
@media(prefers-reduced-motion:reduce){*{animation:none !important;transition:none !important;}}
```

---

## 8. Correcciones UX a Aplicar Simultáneamente

### Fix C1: sticky-actions sin overlap

**Cambio en CSS** (ya incluido arriba):
```css
/* Antes: top:8px;z-index:2 — superpone contenido del formulario */
/* Después: */
.sticky-actions {
  position: sticky;
  top: 0;
  z-index: 10;
  background: #000;           /* fondo sólido para cubrir lo que queda abajo */
  border-bottom: 1px solid var(--border);
  padding: 10px 0;
  margin-bottom: 8px;
}
```

### Fix C2/D1: Eliminar selector de modo duplicado del Dashboard

En `html_page()`, eliminar el bloque `mode-row` completo:
```html
<!-- ELIMINAR ESTO del dashboard hero: -->
<div class="mode-row">
  <div class="field-inline">
    <label class="muted">Modo</label>
    <select id="mode_select">...</select>
  </div>
  <button class="btn" id="mode_btn" onclick="saveMode()">Aplicar</button>
  <span class="muted" id="mode_status"></span>
</div>
```
> El modo se cambia desde `/config` donde están los `mode-cards`. El dashboard solo muestra el pill `Modo: --`.

**Impacto:** Elimina `~250 bytes HTML` + `~400 bytes JS` (`saveMode()`, referencias a `mode_select`, `mode_btn`, `mode_status`). Reduce `String.reserve()` de 25 000 → 24 000.

### Fix C2b: Eliminar `<select id="mode">` duplicado en /config

En `html_config_page()`, el `<select id="mode">` duplica los `mode-cards`. **Mantener solo `mode-cards`** (son visualmente superiores y ya sincronizados con JS). Eliminar:
```html
<!-- ELIMINAR de config: -->
<div class="field" style="max-width:260px">
  <label>Modo</label>
  <select id="mode">...</select>
</div>
```

### Fix D3/D4: Reorganizar dashboard-actions

**Nuevo orden** (primero lo más frecuente):
```html
<div class="dashboard-actions">
  <button class="btn" onclick="refreshAll()">↻ Refresh</button>
  <a class="btn ghost" href="/config">⚙ Config</a>
  <a class="btn ghost" href="/wifi">◈ Wi-Fi</a>
  <a class="btn ghost" href="/dev">≡ Dev</a>  <!-- antes era <details> -->
  <button class="btn ghost" id="home_btn" onclick="updateHome()">⌂ Set Home</button>
</div>
```
> Se reemplaza el `<details class="advanced-menu">` por un link directo `<a href="/dev">`. El `home_btn` pasa a ser visible siempre pero con clase `ghost` desactivada hasta que GPS esté listo.

### Fix W1/C4: Añadir botón "← Inicio" en páginas secundarias

Añadir al hero de `/wifi` y `/config`:
```html
<a class="back-link" href="/">← Inicio</a>
```

### Fix GPS track canvas — colores retro

El canvas de ruta GPS usa `strokeStyle:'#00D1C1'` (teal). Cambiar a verde neón:
```javascript
ctx.strokeStyle = '#00FF41';   // verde Matrix
ctx.lineWidth = 3;
// puntos inicio/fin:
ctx.fillStyle = '#00FF41';     // inicio
ctx.fillStyle = '#FF0055';     // fin (rojo neón)
```

---

## 9. Plan de Implementación — Tareas con Checkboxes

### Fase 1: CSS Base (solo cambio visual, sin bugs)
- [ ] **1.1** Reemplazar `BASE_CSS[]` en `pages.cpp` con la nueva versión retro (sección 7)
- [ ] **1.2** Ajustar `String.reserve()` si alguna página necesita más espacio (ver sección 3.2)
- [ ] **1.3** Actualizar colores del canvas GPS en `html_page()` JS: `strokeStyle`, `fillStyle` inicio/fin
- [ ] **1.4** Verificar compilación con PlatformIO: `pio run -e esp32s3`
- [ ] **1.5** Verificar en browser: `/`, `/wifi`, `/config`, `/dev` — inspección visual

### Fase 2: Correcciones UX críticas
- [ ] **2.1** Fix C1: Actualizar `.sticky-actions` CSS (ya incluido en Fase 1)
- [ ] **2.2** Fix C2/D1: Eliminar `<select id="mode_select">` y bloque `mode-row` del dashboard
- [ ] **2.3** Fix C2b: Eliminar `<select id="mode">` duplicado en `/config`
- [ ] **2.4** Actualizar referencias JS en `html_page()`: eliminar `modeSelect`, `modeBtn`, `modeStatus`, función `saveMode()`
- [ ] **2.5** Actualizar `String.reserve(25000)` → `24000` en `html_page()`

### Fase 3: Mejoras UX adicionales
- [ ] **3.1** Fix D3/D4: Reorganizar `dashboard-actions` (eliminar `<details>`, añadir `<a href="/dev">`)
- [ ] **3.2** Fix W1/C4: Añadir `<a class="back-link" href="/">← Inicio</a>` en `/wifi` y `/config`
- [ ] **3.3** Fix D2: Cambiar `home_btn` de `display:none` inicial a clase `btn ghost` visible siempre, texto cambia según estado GPS via JS
- [ ] **3.4** Añadir prefijo `> ` a la brand para look de prompt: `>_ DOG-RGB`

### Fase 4: Pruebas y validación
- [ ] **4.1** Build completo: `pio run -e esp32s3` → verificar que no hay errores de compilación
- [ ] **4.2** Flash y conectar al collar — abrir portal en browser
- [ ] **4.3** Verificar en móvil (iOS Safari + Android Chrome): responsive layout
- [ ] **4.4** Verificar heap libre con `/dev` antes y después: no debe bajar más de 2 KB respecto al baseline
- [ ] **4.5** Verificar que effectos CSS (scanlines, glow, cursor blink) cargan correctamente
- [ ] **4.6** Correr `test_day_mode_static.py` para verificar que lógica de firmware no se rompió

---

## 10. Resumen del Impacto en Memoria

| Cambio | Efecto en DRAM runtime |
|---|---|
| Nuevo BASE_CSS +900 bytes | +900 bytes en PROGMEM (flash), mismo impacto en DRAM al servir |
| Eliminación `mode_select` bloque | -650 bytes en HTML del dashboard |
| Eliminación `<select id="mode">` en config | -200 bytes en HTML de config |
| Añadir botón back-link | +100 bytes por página |
| Net change en `String.reserve()` dashboard | -550 bytes → ajustar a 24 500 |
| Net change en `String.reserve()` config | +700 bytes → ajustar a 37 000 |

**No hay riesgo de OOM.** El heap libre del ESP32-S3 XIAO en runtime es ~280-320 KB. La página más grande (/config ~37 KB) está muy por debajo del límite.

---

## 11. Riesgos y Mitigaciones

| Riesgo | Probabilidad | Mitigación |
|---|---|---|
| Fuente CDN no carga en modo AP | Alta | Usar `"Courier New",monospace` — sin CDN |
| Scanlines `body::after` hace la UI ilegible | Baja | Reducir opacidad a `rgba(0,0,0,0.04)` si molesta |
| `text-shadow` glow cansa la vista en móvil | Media | Respetar `@media(prefers-reduced-motion)` que ya existe |
| `sticky-actions` nuevo aún con overlap | Baja | Probar con scroll real en iOS Safari |
| Eliminar `mode_select` rompe JS | Media | Eliminar también las referencias en el `<script>` del dashboard |
| Nueva `String.reserve()` demasiado pequeña | Baja | Si hay crash, aumentar en 2000 bytes y recompilar |
| Animación `flicker` al cargar distrae | Baja | Es muy sutil (0.5s) y respeta `prefers-reduced-motion` |

---

## 12. Referencias Bibliográficas

1. CSS-Tricks — "Old Timey Terminal Styling" → técnicas CSS retro CRT
2. terminalcss.xyz — Terminal.css framework, variables CSS de terminal
3. Wikipedia — ESP32-S3 specs → dual LX7 240MHz, 512KB SRAM
4. Espressif ESP-IDF Speed Optimization Guide → PROGMEM, IRAM, cache miss
5. Arduino ESP8266 PROGMEM Guide → `FPSTR()`, `String.reserve()`, DRAM heap
6. RandomNerdTutorials — ESP32 WebServer → comportamiento de String HTML en runtime
7. MDN Web Docs — CSS text-shadow → técnica de glow en capas
8. MDN Web Docs — CSS @keyframes animation → cursor blink, GPU compositing
9. A-List Apart — "CSS Glow Effects" → `box-shadow` vs `filter:drop-shadow` en móvil
10. Google Fonts — VT323 / Share Tech Mono → fuentes retro, costo en bytes, dependencia CDN

---

*Generado el 2026-05-06. Versión 1.0. Estado: PENDIENTE DE IMPLEMENTACIÓN.*
