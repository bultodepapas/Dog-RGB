# Plan — Rediseño estético del portal AP/STA (DOG-RGB)

> **Document status:** Historical visual direction (Spanish). The current portal uses a later retro-console design and tested embedded pages; see [Web portal specification](../web_portal_spec.md).

Fecha: 2026-02-03
Autor: Codex (plan solicitado)

## 1) Contexto y objetivo
El portal web del collar DOG-RGB se sirve desde el firmware (ESP32-S3) y debe verse premium, moderno y claro sin comprometer carga, memoria ni batería. Se usará la dirección visual **Glacier Tech** y tipografía **Space Grotesk**.

## 2) Alcance
- Pagina `/` (dashboard principal)
- Pagina `/wifi` (setup STA)
- Pagina `/config` (runtime)
- No cambiar backend ni endpoints

## 3) Restricciones de hardware y performance
- Sin librerias externas ni requests remotos.
- HTML/CSS/JS embebido y liviano.
- Evitar assets grandes (imagenes). Iconos, si existen, deben ser SVG inline minimo.
- Presupuesto sugerido por pagina: HTML total < 25 KB.
- Usar `prefers-reduced-motion` para animaciones.

## 4) Direccion visual seleccionada
- Paleta Glacier Tech:
  - Fondo base: `#F2F6F8`
  - Superficie: `#FFFFFF`
  - Texto: `#0B1220`
  - Texto secundario: `#5D6B7A`
  - Acento principal: `#00D1C1`
  - Acento alerta: `#FF8A00`
  - Error: `#E84545`
  - Borde suave: `#E6EDF2`
- Tipografia: `Space Grotesk` (opcional WOFF2 local muy pequeno; fallback a system).

## 5) Objetivos UX
- Alta legibilidad en movil (1 columna) y desktop (2 columnas).
- Jerarquia visual clara: estado, metricas, acciones.
- Sensacion de calidad: espaciado consistente, cards limpias, sombras suaves.
- Feedback inmediato en botones.

## 6) Plan de implementacion (alto nivel)
1) Crear un **token system** CSS unico y reutilizable (colors, spacing, radius, typography).
2) Unificar layout base y componentes para `/`, `/wifi`, `/config`.
3) Redisenar dashboard con hero compacto, chips de estado y cards elegantes.
4) Redisenar `/wifi` con formulario alineado al look premium.
5) Redisenar `/config` con secciones colapsables (`<details>`), grid responsivo y barra de acciones.
6) Ajustes de JS solo si es necesario para UI (sin afectar API).

## 7) Pseudocodigo de arquitectura (no final)

### 7.1 Base CSS compartido
```css
:root {
  --bg: #F2F6F8;
  --surface: #FFFFFF;
  --text: #0B1220;
  --muted: #5D6B7A;
  --accent: #00D1C1;
  --accent-2: #FF8A00;
  --danger: #E84545;
  --border: #E6EDF2;
  --shadow: 0 8px 24px rgba(11,18,32,0.08);
  --radius: 14px;
  --space-1: 6px;
  --space-2: 10px;
  --space-3: 14px;
  --space-4: 20px;
}

* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: 'Space Grotesk', system-ui, -apple-system, sans-serif;
  background: var(--bg);
  color: var(--text);
}
.container { max-width: 880px; margin: 0 auto; padding: 20px; }
.card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); box-shadow: var(--shadow); }
.button { background: var(--text); color: #fff; border-radius: 10px; padding: 10px 14px; }
.button.secondary { background: transparent; color: var(--text); border: 1px solid var(--border); }
.badge { border-radius: 999px; padding: 4px 10px; font-size: 12px; }
.badge.ok { background: rgba(0,209,193,0.15); color: #007A70; }
.badge.warn { background: rgba(255,138,0,0.15); color: #A05A00; }
.badge.err { background: rgba(232,69,69,0.15); color: #8A1D1D; }

@media (max-width: 600px) {
  .grid-2 { grid-template-columns: 1fr; }
}

@media (prefers-reduced-motion: reduce) {
  * { animation: none !important; transition: none !important; }
}
```

### 7.2 Estructura de pagina `/` (dashboard)
```html
<header class="hero">
  <div>
    <h1>DOG-RGB</h1>
    <p class="muted">Collar inteligente de seguridad</p>
  </div>
  <div class="status-row">
    <span class="badge" id="pill-gps"></span>
    <span class="badge" id="pill-wifi"></span>
    <span class="badge" id="pill-mode"></span>
    <span class="badge" id="pill-home"></span>
  </div>
</header>

<section class="grid-2 metrics">
  <div class="card metric">
    <div class="label">Distancia</div>
    <div class="value" id="dist">--</div>
    <div class="unit">km</div>
  </div>
  <div class="card metric">
    <div class="label">Velocidad promedio</div>
    <div class="value" id="avg">--</div>
    <div class="unit">km/h</div>
  </div>
  <div class="card metric">
    <div class="label">Velocidad maxima</div>
    <div class="value" id="max">--</div>
    <div class="unit">km/h</div>
  </div>
  <div class="card metric">
    <div class="label">Fecha</div>
    <div class="value" id="date">--</div>
    <div class="muted" id="updated">Ultima lectura: --</div>
  </div>
</section>

<section class="card sessions">
  <h2>Sesiones</h2>
  <div id="session-current"></div>
  <div id="history"></div>
</section>

<section class="actions">
  <button class="button" onclick="refreshAll()">Actualizar</button>
  <button id="home_btn" class="button secondary" onclick="updateHome()">Actualizar Home</button>
  <a class="button secondary" href="/config">Config</a>
  <a class="button secondary" href="/wifi">Wi-Fi</a>
</section>
```

### 7.3 Estructura de pagina `/wifi`
```html
<header class="hero">
  <h1>Configurar Wi-Fi</h1>
  <p class="muted">Conecta DOG-RGB a tu red de casa</p>
</header>

<section class="card form">
  <label>SSID</label>
  <input name="ssid" value="...">

  <label>Password</label>
  <input name="pass" type="password" id="pass">
  <label class="muted"><input type="checkbox" id="show_pass"> Mostrar password</label>

  <button class="button" type="submit">Guardar y conectar</button>
</section>

<a class="button secondary" href="/">Volver</a>

<script>
show_pass.onchange = () => pass.type = show_pass.checked ? 'text' : 'password';
</script>
```

### 7.4 Estructura de pagina `/config`
```html
<header class="hero">
  <h1>Config DOG-RGB</h1>
  <p class="muted">Ajustes de LED, geofence y Wi-Fi</p>
</header>

<section class="card action-bar">
  <button class="button" onclick="saveCfg()">Guardar</button>
  <button class="button secondary" onclick="resetCfg()">Restaurar defaults</button>
  <span id="status" class="muted"></span>
</section>

<details open class="card">
  <summary>Comun</summary>
  <!-- campos brightness + modo -->
</details>

<details class="card">
  <summary>Speed ranges</summary>
  <!-- grid ranges -->
</details>

<details class="card">
  <summary>Geofence</summary>
  <!-- fence_max + home -->
</details>

<details class="card">
  <summary>Simple</summary>
  <!-- theme + rgb -->
</details>

<details class="card">
  <summary>Show</summary>
  <!-- info -->
</details>

<details class="card">
  <summary>Efectos por rango</summary>
  <!-- efectos table -->
</details>

<details class="card">
  <summary>Wi-Fi AP</summary>
  <!-- ap fields -->
</details>
```

### 7.5 Optimizar reutilizacion en C++
```cpp
// pages.cpp
static const char* BASE_CSS = R"CSS( ... )CSS";

String html_page() {
  return String(F("<style>")) + BASE_CSS + F("</style>") + F("...");
}

String html_wifi_page() {
  return String(F("<style>")) + BASE_CSS + F("</style>") + F("...");
}

String html_config_page() {
  return String(F("<style>")) + BASE_CSS + F("</style>") + F("...");
}
```

## 8) Criterios de aceptacion
- La UI se percibe premium y consistente en `/`, `/wifi`, `/config`.
- Contraste accesible para texto principal y etiquetas.
- Carga visual fluida en movil.
- HTML total por pagina dentro de presupuesto.
- Sin cambios funcionales a la API ni flujo de datos.

## 9) Proximos pasos (si apruebas)
- Implementar cambios en `Platformio/Dog-RGB/src/web/pages.cpp`.
- Verificar tamanos finales y layout responsivo.
- Ajustar microdetalles (espaciado, tipografia, sombras) para el look final.
