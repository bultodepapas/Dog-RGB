# Plan — Mejora integral del portal web (AP/STA) con pseudocodigo

> **Document status:** Historical implementation plan (Spanish). It predates the modular page/HTTP source and current Playwright suite. See [Web portal specification](../web_portal_spec.md).

Este plan actualiza la interfaz web embebida del collar para que sea mas clara, util y estable. Esta basado en el firmware actual y deja la implementacion casi directa.

---

**Contexto del firmware (actual)**
- Portal embebido en `Platformio/Dog-RGB/src/main.cpp` con HTML hardcodeado en `html_page()`, `html_wifi_page()` y `html_config_page()`.
- Datos en `/api/summary` y configuracion via `/api/config` y `/api/config/reset`.
- Modos reales: `speed`, `geofence`, `show`, `simple`, definidos en `Platformio/Dog-RGB/include/config.h` y usados en `update_led_ui()`.
- `/api/home` ya existe y retorna home + distancia.

---

**Objetivo del plan**
- Reducir friccion al configurar modos y efectos.
- Hacer visible el estado operativo real del collar (GPS, Wi-Fi, home).
- Mantener el footprint liviano para ESP32 (HTML+JS minimizados, sin frameworks).
- Estandarizar validaciones y errores para evitar “guardar y fallar”.

---

**Problemas actuales observados**
- La pagina principal solo muestra 3 metricas y estado GPS, sin contexto Wi-Fi/AP o modo.
- `/config` usa muchos inputs numericos sin guias, y los errores del backend no se traducen a campos.
- No hay estructura de estado compartida ni refresco consistente.
- El HTML es un `String` gigante con concatenaciones repetidas, poco mantenible.

---

**Principios de diseño**
- Informacion primero, control despues.
- UI explicita sobre modos, con textos cortos de impacto.
- Validacion local espejo del backend.
- Render incremental y refresh liviano.

---

**Alcance**
- Mejora de `/` y `/config`.
- Un endpoint agregado `/api/status` para consolidar estado.
- Refactor minino de HTML embebido para mejor mantenimiento.
- No se cambia la logica de LEDs ni el modelo de config.

---

**Plan por fases**

**Fase 1 — Estado y dashboard**
- Agregar `/api/status` con Wi-Fi, GPS, home y modo.
- En `/` mostrar tarjetas compactas con resumen y estado operativo.
- Auto-refresh cada 5s, sin recargar toda la pagina.

**Fase 2 — Config por modo**
- Separar secciones: Comun, Modo, Rango/Geofence/Simple, Efectos, Wi-Fi.
- Controles con `select` para efectos y `slider` para brillo.
- Mensajes de error por campo.

**Fase 3 — Robustez y mantenimiento**
- HTML en `PROGMEM` y envio streaming.
- Centralizar constantes JS (efectos, temas, limites) en un bloque.
- Minimizar JS y CSS manteniendo claridad.

---

**Especificacion de /api/status**

Respuesta JSON (propuesta):
```
{
  "mode": "speed",
  "wifi": {
    "ap_enabled": true,
    "ap_ssid": "DogRGB",
    "ap_stations": 1,
    "sta_connected": false,
    "mdns": "dog-collar"
  },
  "gps": {
    "fix": true,
    "sats": 8,
    "fix_quality": 1
  },
  "home": {
    "set": true,
    "source": "auto",
    "distance_m": 12.5
  }
}
```

Pseudocodigo (backend):
```cpp
handle_status_get() {
  StaticJsonDocument<768> doc;
  doc["mode"] = mode_name(g_cfg.mode);
  doc["wifi"]["ap_enabled"] = ap_enabled;
  doc["wifi"]["ap_ssid"] = g_cfg.ap_ssid;
  doc["wifi"]["ap_stations"] = ap_station_count;
  doc["wifi"]["sta_connected"] = wifi_sta_connected;
  doc["wifi"]["mdns"] = g_cfg.mdns;
  doc["gps"]["fix"] = has_gps_fix;
  doc["gps"]["sats"] = gps_sats;
  doc["gps"]["fix_quality"] = gps_fix_quality;
  doc["home"]["set"] = home_set;
  doc["home"]["source"] = home_source_name(home_source);
  const float d = distance_to_home_m();
  doc["home"]["distance_m"] = (d >= 0.0f ? d : -1.0f);
  send_json(doc);
}
```

---

**UI Dashboard (/) — estructura**

Contenido:
- Status pill: `GPS`, `Wi-Fi`, `Modo`, `Home`.
- Cards: Distancia diaria, Velocidad promedio, Velocidad maxima.
- Footer: hora de ultima lectura y boton “Actualizar”.

Pseudocodigo (frontend):
```js
state = { summary:null, status:null };

async function boot() {
  state.summary = await get('/api/summary');
  state.status = await get('/api/status');
  renderAll();
  setInterval(refreshStatus, 5000);
}

async function refreshStatus() {
  state.status = await get('/api/status');
  renderStatus(state.status);
}

function renderAll() {
  renderSummary(state.summary);
  renderStatus(state.status);
}
```

---

**UI Config (/config) — estructura**

Secciones:
- Comun: brillo, modo, botones Save/Reset, estado AP.
- Speed: rangos y efectos por rango.
- Geofence: `fence_max_m`, home y rango calculado.
- Simple: tema, efecto, speed, intensidad, color base.
- Show: mensaje informativo (sin parametros).
- Wi-Fi AP: ssid, password, open, mdns.

Pseudocodigo (frontend):
```js
const EFFECTS = [
  {id:0,name:"SOLID"}, {id:1,name:"PULSE"}, {id:2,name:"BREATH"},
  {id:3,name:"CHASE"}, {id:4,name:"COMET"}, {id:5,name:"SINELON"},
  {id:6,name:"CONFETTI"}, {id:7,name:"JUGGLE"}, {id:8,name:"BPM"},
  {id:9,name:"RAINBOW"}, {id:10,name:"FIRE"}, {id:11,name:"GRADIENT_WAVE"}
];

const SIMPLE_THEMES = {
  manual:null,
  calm:{effect:2,speed:60,intensity:90,r:0,g:60,b:60},
  active:{effect:4,speed:120,intensity:140,r:60,g:45,b:0},
  sport:{effect:7,speed:160,intensity:180,r:60,g:0,b:0},
  aurora:{effect:11,speed:120,intensity:180,r:0,g:180,b:120}
};

function updateModeVisibility(mode) {
  show(speedBlock, mode === 'speed');
  show(geofenceBlock, mode === 'geofence');
  show(simpleBlock, mode === 'simple');
  show(showBlock, mode === 'show');
}

function renderEffectsTable(effects) {
  for range in 1..10:
    renderSelect(`e${range}a`, EFFECTS, effects[`range${range}`].a);
    renderSelect(`e${range}b`, EFFECTS, effects[`range${range}`].b);
    renderNumber(`e${range}s`, 0, 255, effects[`range${range}`].speed);
    renderNumber(`e${range}i`, 0, 255, effects[`range${range}`].intensity);
}
```

---

**Validaciones locales (espejo backend)**

Pseudocodigo:
```js
function validateConfig(cfg) {
  errors = {};
  if (cfg.led.brightness < 1 || cfg.led.brightness > 255) errors.brightness = "1..255";
  if (!isStrictAscending(cfg.speed_ranges_kph)) errors.ranges = "Rangos ascendentes";
  if (cfg.mode === 'geofence' && (cfg.fence_max_m < 50 || cfg.fence_max_m > 5000)) errors.fence = "50..5000";
  for each range in effects:
    if (range.a < 0 || range.a > 11) errors[`e${i}a`] = "0..11";
    if (range.b < 0 || range.b > 11) errors[`e${i}b`] = "0..11";
  if (cfg.mode === 'simple'):
    validate single.effect 0..11, speed 0..255, intensity 0..255, rgb 0..255.
  if (cfg.wifi.ap_ssid.length < 1 || cfg.wifi.ap_ssid.length > 32) errors.ap_ssid = "1..32";
  if (!cfg.wifi.ap_open && cfg.wifi.ap_pass.length > 0 && cfg.wifi.ap_pass.length < 8) errors.ap_pass = ">=8";
  if (!validMdns(cfg.wifi.mdns)) errors.mdns = "1..32 a-z0-9-";
  return errors;
}
```

---

**Mapeo de errores del backend a UI**

Pseudocodigo:
```js
const ERROR_MAP = {
  brightness: {field:"brightness", msg:"Brillo fuera de rango"},
  mode: {field:"mode", msg:"Modo invalido"},
  fence_max: {field:"fence_max", msg:"Distancia 50..5000"},
  ranges: {field:"ranges", msg:"Rangos requeridos"},
  "ranges value": {field:"ranges", msg:"Rangos deben ser > 0"},
  "ranges order": {field:"ranges", msg:"Rangos deben ser ascendentes"},
  effects: {field:"effects", msg:"Efectos incompletos"},
  "effect values": {field:"effects", msg:"Valores de efecto invalidos"},
  "effect id": {field:"effects", msg:"ID de efecto invalido"},
  single: {field:"single", msg:"Bloque simple invalido"},
  "single values": {field:"single", msg:"Valores simple invalidos"},
  ssid: {field:"ap_ssid", msg:"SSID 1..32"},
  pass: {field:"ap_pass", msg:"Password >= 8"},
  mdns: {field:"mdns", msg:"mDNS invalido"}
};

function handleBackendError(reason) {
  const e = ERROR_MAP[reason];
  if (!e) { showToast("Error guardando"); return; }
  showFieldError(e.field, e.msg);
}
```

---

**UX de modos (texto corto en UI)**
- Speed: “Usa rangos de velocidad para elegir efectos por rango.”
- Geofence: “Usa distancia al Home; requiere GPS y home definido.”
- Simple: “Un efecto unico para toda la tira.”
- Show: “Demo de efectos; cambia automaticamente.”

---

**Refactor HTML embebido (mantenimiento)**

Objetivo:
- Reducir concatenaciones de `String` y mover HTML/JS a `PROGMEM`.
- Separar plantilla base y contenido `script` para editar rapido.

Pseudocodigo:
```cpp
// En lugar de String gigantes
server.send_P(200, "text/html", kPageDashboard);
server.send_P(200, "text/html", kPageConfig);
```

---

**Checklist de implementacion**
- Agregar handler `/api/status`.
- Actualizar `server.on` con ruta nueva.
- Ajustar `html_page()` y `html_config_page()` con layout nuevo y JS unificado.
- Usar `select` para efectos con nombres.
- Incorporar validacion local y mapeo de errores.
- Agregar visualizacion de home y modo.
- Mantener compatibilidad con `POST /api/config` sin nuevos campos.

---

**Criterios de exito**
- Un usuario sin documento extra puede entender el modo activo y sus efectos.
- Guardar config falla raramente y muestra errores accionables.
- La UI se siente mas clara sin incrementar el footprint de memoria de forma relevante.

---

**Archivos a tocar**
- `Platformio/Dog-RGB/src/main.cpp`
- `docs/portal_config.md` para reflejar `/api/status` y nuevos textos UI (opcional pero recomendado).

