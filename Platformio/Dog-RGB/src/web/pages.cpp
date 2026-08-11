#include "web/pages.h"

#include <pgmspace.h>

#include "wifi/wifi_mgr.h"

namespace {
const char BASE_CSS[] PROGMEM = R"CSS(
:root{--bg:#000;--surface:#0A0A0A;--text:#00FF41;--muted:#00A838;--accent:#00FF41;--accent-2:#FFD700;--danger:#FF0055;--border:#003300;--shadow:0 0 10px rgba(0,255,65,0.12);--glow-sm:0 0 4px #00FF41;--glow-md:0 0 8px #00FF41,0 0 16px rgba(0,255,65,0.4);--radius:3px;--space-1:6px;--space-2:10px;--space-3:14px;--space-4:20px;--space-5:28px;--font-mono:"Courier New","Lucida Console","DejaVu Sans Mono",monospace;}
*{box-sizing:border-box;}
label:has(input[type="checkbox"]){display:inline-flex;align-items:center;gap:8px;min-height:44px;}
@keyframes flicker{0%{opacity:0.96}5%{opacity:0.92}10%{opacity:1}70%{opacity:0.98}100%{opacity:1}}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0}}
body{margin:0;font-family:var(--font-mono);font-size:14px;background:#000;color:var(--text);text-shadow:0 0 3px rgba(0,255,65,0.35);line-height:1.5;}
body::after{content:'';position:fixed;top:0;left:0;right:0;bottom:0;background:repeating-linear-gradient(0deg,rgba(0,0,0,0.07) 0px,rgba(0,0,0,0.07) 1px,transparent 1px,transparent 3px);pointer-events:none;z-index:9999;}
a{color:var(--accent);text-decoration:none;}
a:hover{text-shadow:var(--glow-sm);}
h1,h2{margin:0 0 8px 0;text-shadow:var(--glow-md);}
h1{font-size:22px;letter-spacing:0.06em;}
h2{font-size:16px;letter-spacing:0.04em;}
.container{max-width:900px;margin:0 auto;padding:20px;animation:flicker 0.5s ease-in-out forwards;}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);padding:var(--space-4);}
.hero{display:flex;flex-direction:column;gap:var(--space-3);}
.hero-top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:var(--space-3);}
.brand{margin:0;font-size:22px;font-weight:700;letter-spacing:0.1em;text-shadow:var(--glow-md);text-transform:uppercase;}
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
.code{background:#000;color:var(--text);padding:12px;border-radius:var(--radius);overflow-x:auto;white-space:pre;font-size:12px;border:1px solid var(--border);}
.metric .label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;}
.metric .value{font-size:28px;font-weight:700;line-height:1.1;text-shadow:var(--glow-md);}
.metric .unit{font-size:12px;color:var(--muted);}
.dashboard-summary{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(0,1fr);gap:14px;align-items:stretch;}
.primary-metric{display:flex;flex-direction:column;justify-content:center;min-height:118px;}
.primary-metric .value{font-size:42px;font-weight:700;line-height:1;text-shadow:0 0 12px #00FF41,0 0 24px rgba(0,255,65,0.5);}
.stat-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}
.stat{background:#000;border:1px solid var(--border);border-radius:var(--radius);padding:12px;}
.stat .value{font-size:24px;font-weight:700;line-height:1.1;}
/* Two labelled pairs, not one run-on sentence: with a plain gap the eye read
   "Fecha: 2026-05-06 Ultima lectura: 10:43" as a single phrase. */
.summary-meta{grid-column:1/-1;display:flex;flex-wrap:wrap;gap:10px 24px;color:var(--muted);font-size:12px;}
.meta-pair{display:flex;flex-direction:column;gap:2px;}
.meta-k{font-size:10px;text-transform:uppercase;letter-spacing:0.08em;color:var(--muted);}
.meta-v{font-size:13px;color:var(--text);}
/* Label/value pairs are short; forcing them to one column on a phone doubles
   the page for no gain. auto-fit keeps two columns where they fit and falls
   back to one on very narrow screens. */
.grid-kv{grid-template-columns:repeat(auto-fit,minmax(150px,1fr));}
.health-ok{color:var(--accent);}
.health-warn{color:var(--accent-2);}
.health-bad{color:var(--danger);text-shadow:0 0 6px rgba(255,0,85,0.5);}
.empty-state{padding:12px;background:#000;border:1px solid var(--border);border-radius:var(--radius);color:var(--muted);font-size:12px;}
.muted{color:var(--muted);font-size:12px;}
/* The dashboard note is empty in the normal case; without this the flex gap
   still reserves a band of dead space under the buttons. */
#status:empty{display:none;}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:44px;padding:8px 14px;border-radius:var(--radius);border:1px solid var(--accent);background:transparent;color:var(--accent);font-weight:600;font-size:12px;font-family:var(--font-mono);text-transform:uppercase;letter-spacing:0.06em;cursor:pointer;transition:box-shadow 0.15s,background 0.15s;}
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
.advanced-menu > summary{list-style:none;}
.advanced-menu > summary::-webkit-details-marker{display:none;}
.advanced-menu .section{margin-top:8px;}
.mode-cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;}
.mode-card{width:100%;text-align:left;border:1px solid var(--border);background:transparent;border-radius:var(--radius);padding:10px;cursor:pointer;font-family:var(--font-mono);color:var(--muted);}
.mode-card strong{display:block;font-size:12px;margin-bottom:4px;text-transform:uppercase;letter-spacing:0.05em;}
.mode-card span{display:block;color:var(--muted);font-size:11px;line-height:1.3;}
.mode-card.active{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.mode-card:hover{border-color:var(--accent);color:var(--text);}
.preset-row,.swatch-row{display:flex;flex-wrap:wrap;gap:8px;}
.preset-btn{display:inline-flex;align-items:center;min-height:44px;border:1px solid var(--border);border-radius:var(--radius);background:transparent;color:var(--muted);padding:6px 12px;font-family:var(--font-mono);font-size:12px;text-transform:uppercase;cursor:pointer;}
.preset-btn.active{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.swatch{width:44px;height:44px;border-radius:2px;border:2px solid var(--border);cursor:pointer;}
.swatch.active{border-color:var(--accent);box-shadow:var(--glow-sm);}
/* Direct children only: this styles a field's own label, and must not reach
   spans nested inside help text, where it would force block layout and caps. */
.field > label,.field > span{display:block;font-size:11px;color:var(--muted);margin-bottom:6px;text-transform:uppercase;letter-spacing:0.06em;}
input,select{width:100%;min-height:44px;padding:8px 10px;border:1px solid var(--border);border-radius:var(--radius);background:#000;font-family:var(--font-mono);font-size:13px;color:var(--text);}
input:focus,select:focus{border-color:var(--accent);box-shadow:var(--glow-sm);}
input:disabled,select:disabled{opacity:0.45;color:var(--muted);border-style:dashed;cursor:not-allowed;}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px;}
input[type="checkbox"]{flex:none;appearance:none;min-height:0;width:24px;height:24px;margin-right:6px;border:1px solid var(--accent);border-radius:var(--radius);background:#000;cursor:pointer;}
input[type="checkbox"]:checked{background:var(--accent);box-shadow:var(--glow-sm);}
input[type="range"]{accent-color:var(--accent);}
input::placeholder{color:var(--muted);}
.section{margin-top:14px;}
.section.invalid{border-color:var(--danger);}
input.invalid,select.invalid{border-color:var(--danger);}
.error{color:var(--danger);font-size:12px;}
.error-box{padding:12px;}
.error-box:empty{display:none;}
.warn{color:var(--accent-2);font-size:12px;}
.help{color:var(--muted);font-size:12px;margin-top:6px;}
.field-check{display:flex;align-items:center;}
.advisories{display:flex;flex-direction:column;gap:10px;margin-top:12px;}
.advisories:empty{display:none;}
.advisory{font-size:12px;line-height:1.45;padding:8px 10px;border-left:2px solid var(--border);}
.advisory:empty{display:none;}
.advisory.danger{color:var(--danger);border-left-color:var(--danger);background:rgba(255,0,85,0.06);}
.advisory.note{color:var(--muted);border-left-color:var(--border);}
.notice{color:var(--muted);font-size:12px;margin-top:6px;}
.speed-lane{border:1px solid var(--border);border-radius:var(--radius);margin-bottom:5px;background:#000;overflow:hidden;}
.sl-main{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:8px 12px;}
.sl-meta{display:flex;align-items:center;gap:8px;flex:1;min-width:180px;}.sl-dot{width:9px;height:9px;border-radius:50%;flex-shrink:0;}.sl-name{font-size:12px;font-weight:700;text-transform:uppercase;min-width:20px;}
.sl-lbl{font-size:12px;color:var(--muted);min-width:64px;}.sl-range{display:flex;align-items:center;gap:4px;font-size:12px;color:var(--muted);}
.sl-range input{width:64px;padding:3px 6px;font-size:13px;}.sl-ctrls{display:flex;align-items:center;flex-wrap:wrap;gap:6px;}
.sl-ctrls select{min-width:124px;padding:4px 8px;font-size:13px;width:auto;}
.ctl-grp{display:flex;align-items:center;gap:3px;}.ctl-lbl{font-size:12px;color:var(--muted);text-transform:uppercase;}.sl-ctrls input[type=number]{width:62px;padding:4px 6px;font-size:13px;}
.sl-adv{display:flex;align-items:center;gap:8px;padding:6px 12px 8px 29px;border-top:1px solid var(--border);}
.sl-adv label{font-size:12px;color:var(--muted);}.sl-adv select{min-width:124px;padding:4px 8px;font-size:13px;width:auto;}
.sl-adv-btn{display:block;width:100%;min-height:44px;text-align:left;background:none;border:none;border-top:1px solid var(--border);color:var(--muted);font-size:12px;font-family:var(--font-mono);cursor:pointer;padding:3px 12px;text-transform:uppercase;letter-spacing:0.04em;}.sl-adv-btn:hover{color:var(--accent);}
details.section > summary{list-style:none;cursor:pointer;display:flex;align-items:center;min-height:44px;justify-content:space-between;font-weight:600;text-transform:uppercase;letter-spacing:0.06em;font-size:13px;}
details.section > summary::-webkit-details-marker{display:none;}
details.section > summary::after{content:'[+]';font-weight:700;color:var(--muted);font-size:11px;}
details.section[open] > summary::after{content:'[-]';}
.section-body{margin-top:10px;}
.action-bar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.mode-row{display:flex;flex-wrap:wrap;gap:10px;align-items:end;}
.field-inline{min-width:180px;}
.scan-list{display:flex;flex-direction:column;gap:6px;margin:8px 0 14px;}
.scan-list:empty{display:none;}
.scan-item{display:flex;align-items:center;justify-content:space-between;gap:10px;width:100%;min-height:44px;text-align:left;padding:8px 10px;border:1px solid var(--border);border-radius:var(--radius);background:#000;color:var(--text);font-family:var(--font-mono);font-size:12px;cursor:pointer;}
.scan-item:hover{border-color:var(--accent);box-shadow:var(--glow-sm);}
.scan-item.active{border-color:var(--accent);box-shadow:var(--glow-sm);}
.scan-ssid{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.scan-meta{flex:none;color:var(--muted);font-size:11px;letter-spacing:0.06em;}
.session-card{margin:8px 0;padding:10px;background:#000;border:1px solid var(--border);border-radius:var(--radius);}
.track-controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-bottom:10px;}
.track-canvas{width:100%;height:220px;border:1px solid var(--border);border-radius:var(--radius);background:#000;}
.track-note{color:var(--muted);font-size:12px;margin:8px 0;}
.is-hidden{display:none !important;}
.back-link{display:inline-flex;align-items:center;gap:6px;min-height:44px;font-size:12px;color:var(--muted);margin-bottom:14px;text-transform:uppercase;letter-spacing:0.06em;}
.back-link:hover{color:var(--accent);}
@media(max-width:760px){.grid-2,.grid-3,.dashboard-summary{grid-template-columns:1fr;}.stat-grid{grid-template-columns:repeat(2,minmax(0,1fr));}.mode-cards{grid-template-columns:repeat(2,minmax(0,1fr));}.primary-metric{min-height:auto;width:100%;text-align:center;align-items:center;}.primary-metric .value{font-size:36px;}.dashboard-actions .btn,.dashboard-actions summary.btn{flex:1 1 130px;}.sticky-actions{margin-left:-20px;margin-right:-20px;padding-left:20px;padding-right:20px;border-radius:0;}.hero-top{flex-direction:column;align-items:flex-start;}}
@media(prefers-reduced-motion:reduce){*{animation:none !important;transition:none !important;}}
)CSS";

// Any runtime value interpolated into markup must pass through here. The SSID
// is operator-controlled and reaches the page from NVS, so an unescaped quote
// would break out of the attribute it sits in.
String html_escape_attr(const String &value) {
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '&':
        out += F("&amp;");
        break;
      case '<':
        out += F("&lt;");
        break;
      case '>':
        out += F("&gt;");
        break;
      case '"':
        out += F("&quot;");
        break;
      case '\'':
        out += F("&#39;");
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}
} // namespace

String web_pages::html_page() {
  String page;
  page.reserve(36000);
  page += F(R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DOG-RGB</title>
  <style>
)HTML");
  page += FPSTR(BASE_CSS);
  page += F(R"HTML(
  </style>
</head>
<body>
  <noscript><div class="warn" style="padding:12px">Este portal necesita JavaScript activado para mostrar los datos del collar.</div></noscript>
  <main class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <h1 class="brand">DOG-RGB</h1>
          <div class="tagline">Collar inteligente de seguridad</div>
        </div>
        <div class="chips">
          <span class="pill" id="pill-gps">GPS: --</span>
          <span class="pill" id="pill-wifi">Wi-Fi: --</span>
          <span class="pill" id="pill-mode">Modo: --</span>
          <span class="pill" id="pill-day" style="display:none">DIA: --</span>
          <span class="pill" id="pill-home">Home: --</span>
        </div>
      </div>
      <div class="dashboard-actions">
        <button class="btn" onclick="refreshAll()">Refresh</button>
        <a class="btn ghost" href="/config">Config LEDs</a>
        <a class="btn ghost" href="/wifi">Config Wi-Fi</a>
        <a class="btn ghost" href="/dev">Dev</a>
        <button class="btn ghost" id="home_btn" onclick="updateHome()" style="display:none">Set Home</button>
      </div>
      <div class="muted" id="status">Estado: --</div>
    </div>

    <div class="card section dashboard-summary">
      <div class="primary-metric metric">
        <div class="label">Distancia</div>
        <div class="value" id="dist">--</div>
        <div class="unit">km</div>
      </div>
      <div class="stat-grid">
        <div class="stat metric">
          <div class="label">Vel. promedio</div>
          <div class="value" id="avg">--</div>
          <div class="unit">km/h</div>
        </div>
        <div class="stat metric">
          <div class="label">Vel. maxima</div>
          <div class="value" id="max">--</div>
          <div class="unit">km/h</div>
        </div>
      </div>
      <div class="summary-meta">
        <span class="meta-pair"><span class="meta-k">Fecha</span><span class="meta-v" id="date">--</span></span>
        <span class="meta-pair"><span class="meta-k">Ultima lectura</span><span class="meta-v" id="updated">--</span></span>
      </div>
    </div>

    <details class="card section">
      <summary>Historial y ruta</summary>
      <div class="section-body">
        <h2>Sesiones</h2>
        <div id="session-current"></div>
        <div id="history"></div>
        <div class="track-controls section">
          <h2>Ruta GPS</h2>
          <select id="track_session" aria-label="Sesion a mostrar">
            <option value="current">Sesion actual</option>
            <option value="0">Sesion 1 (ultima)</option>
            <option value="1">Sesion 2</option>
            <option value="2">Sesion 3</option>
          </select>
          <button class="btn ghost" id="track_load" type="button" onclick="loadTrack()">Ver trazo</button>
          <a class="btn ghost" id="track_csv" href="/api/track.csv" style="display:none">CSV completo</a>
          <a class="btn ghost" id="track_geo" href="/api/track.geojson" style="display:none">GeoJSON completo</a>
        </div>
        <div class="track-note">Vista previa del trazo GPS, sin mapa base ni escala. Los exports descargan la sesion completa.</div>
        <canvas id="track_map" class="track-canvas is-hidden"></canvas>
        <div class="empty-state" id="track_status">Ruta: sin cargar</div>
      </div>
    </details>
  </main>

  <script>
    // Every portal write goes through here. X-Dog-Portal is what the server's
    // CSRF guard requires; X-Dog-Pin is empty unless the user turned on the
    // optional lock, in which case a 401 prompts for it once per browser tab.
    function dogPin(){try{return sessionStorage.getItem('dogPin')||'';}catch(e){return '';}}
    async function dogPost(url,opts){
      opts=opts||{};
      const send=()=>fetch(url,Object.assign({},opts,{method:'POST',headers:Object.assign({'X-Dog-Portal':'1','X-Dog-Pin':dogPin()},opts.headers||{})}));
      let r=await send();
      if(r.status===401){
        const pin=prompt('Portal bloqueado. Introduce el PIN:');
        if(pin===null){return r;}
        try{sessionStorage.setItem('dogPin',pin);}catch(e){}
        r=await send();
        if(r.status===401){try{sessionStorage.removeItem('dogPin');}catch(e){}}
      }
      return r;
    }
    const $ = (id) => document.getElementById(id);
    const homeBtn = $('home_btn');
    const trackCanvas = $('track_map');
    const trackSession = $('track_session');
    const trackStatus = $('track_status');
    const trackCsv = $('track_csv');
    const trackGeo = $('track_geo');
    const trackLoad = $('track_load');
    let trackLoaded = false;
    // Assigning canvas.width wipes the bitmap, and resizeTrackCanvas does exactly
    // that. Keep the last drawing so a rotation or an address-bar collapse can
    // repaint instead of leaving the user with an empty box.
    let trackDrawn = null;
    let trackResizeTimer = null;
    function minToTime(m){var h=Math.floor(m/60);var mm=m%60;return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0');}
    function cmpsToKph(v){return (v*0.036).toFixed(1);}
    function fmtDate(d){if(!d){return '--';}var s=String(d);if(s.length!==8){return s;}return s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8);}
    function yyyymmddToDate(d){if(!d){return '--/--';}var y=Math.floor(d/10000);var m=Math.floor((d%10000)/100);var day=d%100;return String(day).padStart(2,'0')+'/'+String(m).padStart(2,'0');}
    function formatDuration(s){var h=Math.floor(s/3600);var m=Math.floor((s%3600)/60);return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0');}
    function hasFlag(flags,bit){return (flags&(1<<bit))!==0;}
    function setPill(id,text,tone){var el=$(id);el.textContent=text;el.className='pill'+(tone?(' '+tone):'');}
    const MODE_NAMES={speed:'Velocidad',geofence:'Geocerca',simple:'Simple',show:'Show'};

    function resizeTrackCanvas(){
      if(!trackCanvas){return;}
      const rect=trackCanvas.getBoundingClientRect();
      const dpr=window.devicePixelRatio||1;
      trackCanvas.width=Math.max(1,Math.floor(rect.width*dpr));
      trackCanvas.height=Math.max(1,Math.floor(rect.height*dpr));
    }

    function setTrackLinks(session){
      if(trackCsv) trackCsv.href='/api/track.csv?session='+session;
      if(trackGeo) trackGeo.href='/api/track.geojson?session='+session;
    }

    function setTrackExportsVisible(visible){
      if(trackCsv) trackCsv.style.display=visible?'inline-flex':'none';
      if(trackGeo) trackGeo.style.display=visible?'inline-flex':'none';
    }

    function setTrackCanvasVisible(visible){
      if(trackCanvas) trackCanvas.classList.toggle('is-hidden',!visible);
    }

    function clearTrack(msg){
      if(trackCanvas){
        const ctx=trackCanvas.getContext('2d');
        const dpr=window.devicePixelRatio||1;
        const rect=trackCanvas.getBoundingClientRect();
        ctx.setTransform(dpr,0,0,dpr,0,0);
        ctx.clearRect(0,0,rect.width,rect.height);
      }
      setTrackCanvasVisible(false);
      setTrackExportsVisible(false);
      trackDrawn=null;
      if(trackStatus){
        trackStatus.textContent=msg||'Ruta: --';
        trackStatus.className='empty-state';
      }
    }

    function drawTrack(points,bbox){
      if(!trackCanvas){return;}
      trackDrawn={points:points,bbox:bbox};
      setTrackCanvasVisible(true);
      resizeTrackCanvas();
      const ctx=trackCanvas.getContext('2d');
      const rect=trackCanvas.getBoundingClientRect();
      const dpr=window.devicePixelRatio||1;
      ctx.setTransform(dpr,0,0,dpr,0,0);
      ctx.clearRect(0,0,rect.width,rect.height);

      const pad=12;
      const minLat=bbox.min_lat, maxLat=bbox.max_lat;
      const minLon=bbox.min_lon, maxLon=bbox.max_lon;
      const latSpan=Math.max(1e-6, maxLat-minLat);
      const lonSpan=Math.max(1e-6, maxLon-minLon);
      const scale=Math.min((rect.width-2*pad)/lonSpan, (rect.height-2*pad)/latSpan);

      ctx.beginPath();
      for(let i=0;i<points.length;i++){
        const lat=points[i][0];
        const lon=points[i][1];
        const x=pad+(lon-minLon)*scale;
        const y=rect.height-(pad+(lat-minLat)*scale);
        if(i===0){ctx.moveTo(x,y);} else {ctx.lineTo(x,y);}
      }
      ctx.strokeStyle='#00FF41';
      ctx.lineWidth=3;
      ctx.lineJoin='round';
      ctx.lineCap='round';
      ctx.stroke();

      if(points.length>0){
        const start=points[0];
        const end=points[points.length-1];
        const sx=pad+(start[1]-minLon)*scale;
        const sy=rect.height-(pad+(start[0]-minLat)*scale);
        const ex=pad+(end[1]-minLon)*scale;
        const ey=rect.height-(pad+(end[0]-minLat)*scale);
        ctx.fillStyle='#00FF41';
        ctx.beginPath();ctx.arc(sx,sy,4,0,Math.PI*2);ctx.fill();
        ctx.fillStyle='#FF0055';
        ctx.beginPath();ctx.arc(ex,ey,4,0,Math.PI*2);ctx.fill();
      }
    }

    async function loadTrack(){
      if(!trackSession){return;}
      const session=trackSession.value||'current';
      trackLoaded=true;
      setTrackLinks(session);
      if(trackStatus) trackStatus.textContent='Ruta: cargando...';
      if(trackLoad) trackLoad.disabled=true;
      try{
        const r=await fetch('/api/track?session='+session+'&max_points=250');
        const d=await r.json();
        if(!d||!d.count||!d.points||d.points.length<2){
          clearTrack('Ruta: no hay puntos suficientes');
          return;
        }
        if(!d.bbox){
          clearTrack('Ruta: sin bbox');
          return;
        }
        if(trackStatus){
          trackStatus.className='muted';
          trackStatus.textContent='Ruta: '+d.points.length+' puntos de vista previa, dibujando...';
        }
        drawTrack(d.points,d.bbox);
        setTrackExportsVisible(true);
        var sDate=fmtDate(d.start_date);
        var eDate=fmtDate(d.end_date||d.start_date);
        var sTime=minToTime(d.start_min||0);
        var eTime=minToTime(d.end_min||0);
        var shown=d.points.length;
        var countText=(d.count&&d.count>shown)?(shown+' de '+d.count+' pts'):(shown+' pts');
        if(trackStatus) trackStatus.textContent='Trazo GPS: '+countText+' | '+sDate+' '+sTime+' a '+eDate+' '+eTime;
      }catch(e){
        clearTrack('Ruta: error');
      }finally{
        if(trackLoad) trackLoad.disabled=false;
      }
    }

    // The GPS state already has its own pill, so repeating it here as
    // "Estado: GPS OK" said nothing twice. This line now only speaks when it
    // has something the pills do not carry.
    function setDashNote(msg,tone){
      const el=$('status');
      el.textContent=msg||'';
      el.className=msg?(tone==='warn'?'warn':'muted'):'muted';
    }

    function renderSummary(d){
      if(!d||!d.has_data){
        const gpsFix=!!(d&&d.gps_fix);
        const gpsRaw=!!(d&&d.gps_raw_fix);
        if(gpsFix){
          // With a trusted fix and no movement, the day really is all zeros.
          // Mixing 0.00 with "--" made the empty state contradict itself.
          setDashNote('Sin actividad registrada hoy');
          $('dist').textContent='0.00';
          $('avg').textContent='0.0';
          $('max').textContent='0.0';
        } else {
          setDashNote(gpsRaw?'GPS no confiable todavia: aun no se registra actividad.':'Esperando senal GPS para empezar a registrar.','warn');
          $('dist').textContent='--';
          $('avg').textContent='--';
          $('max').textContent='--';
        }
        $('date').textContent=(d&&d.date)?fmtDate(d.date):'--';
        $('updated').textContent='--';
        return;
      }
      $('dist').textContent=(d.distance_m/1000).toFixed(2);
      $('avg').textContent=cmpsToKph(d.avg_speed_cmps);
      $('max').textContent=cmpsToKph(d.max_speed_cmps);
      $('date').textContent=fmtDate(d.date);
      $('updated').textContent=minToTime(d.last_update_min);
      if (d.gps_fix) {
        setDashNote('');
      } else if (d.gps_raw_fix) {
        setDashNote('GPS no confiable: los datos de hoy pueden estar incompletos.','warn');
      } else {
        setDashNote('Sin GPS: no se esta registrando actividad ahora mismo.','warn');
      }
    }

    function renderSessionCard(label,s){
      if(!s){return '';}
      var flags=s.flags||0;
      var noFix=hasFlag(flags,3)||!hasFlag(flags,0);
      if(noFix){
        return "<div class='session-card'><div class='label'>"+label+"</div><div class='muted'>GPS no disponible para esta sesion</div></div>";
      }
      var startDate=yyyymmddToDate(s.start_date);
      var startTime=minToTime(s.start_min||0);
      var endDate=yyyymmddToDate(s.end_date||s.start_date);
      var endTime=minToTime(s.end_min||s.start_min||0);
      var distKm=(s.distance_m/1000).toFixed(2);
      var avg=cmpsToKph(s.avg_speed_cmps||0);
      var max=cmpsToKph(s.max_speed_cmps||0);
      var active=formatDuration(s.active_s||0);
      return "<div class='session-card'><div class='label'>"+label+"</div>"+
             "<div class='muted'>"+startDate+" "+startTime+" a "+endDate+" "+endTime+"</div>"+
             "<div>Distancia: <strong>"+distKm+"</strong> km</div>"+
             "<div class='muted'>Tiempo activo: "+active+"</div>"+
             "<div class='muted'>Vel. prom: "+avg+" km/h | Vel. max: "+max+" km/h</div></div>";
    }

    function renderSessionCurrent(s){
      var el=$('session-current');
      if(!el){return;}
      if(!s){
        el.innerHTML="<div class='empty-state'>Sesion actual: sin datos todavia</div>";
        return;
      }
      el.innerHTML=renderSessionCard('Sesion actual',s);
    }

    function renderHistory(list){
      var el=$('history');
      if(!el){return;}
      if(!list||list.length===0){
        el.innerHTML="<div class='empty-state'>Sin sesiones anteriores</div>";
        return;
      }
      var out='';
      for(var i=0;i<list.length;i++){
        out+=renderSessionCard('Sesion '+(i+1),list[i]);
      }
      el.innerHTML=out;
    }

    function renderStatus(s){
      if(!s){return;}
      var gpsTrusted=!!(s.gps&&s.gps.fix);
      var gpsRaw=!!(s.gps&&s.gps.raw_fix);
      var gpsTone=gpsTrusted?'ok':'warn';
      var gpsSats=(s.gps&&s.gps.sats!==undefined)?s.gps.sats:'--';
      var gpsText='GPS sin fix ('+gpsSats+')';
      if (gpsTrusted){
        gpsText='GPS OK ('+gpsSats+')';
      } else if (gpsRaw){
        gpsText='GPS no confiable ('+gpsSats+')';
      }
      setPill('pill-gps',gpsText,gpsTone);

      var wifiText='Wi-Fi off';
      var wifiTone='warn';
      if(s.wifi){
        if(s.wifi.sta_connected){wifiText='STA conectada';wifiTone='ok';}
        else if(s.wifi.ap_enabled){wifiText='AP activo';wifiTone='warn';}
      }
      setPill('pill-wifi',wifiText,wifiTone);

      // Same names the config page uses. Showing the internal identifier here
      // ("speed") while /config calls it "Velocidad" made them look unrelated.
      var modeText='Modo: '+(MODE_NAMES[s.mode]||s.mode||'--');
      setPill('pill-mode',modeText,'');
      const dayPill = $('pill-day');
      if (dayPill && s.day_mode && s.day_mode.enabled){
        const active = !!s.day_mode.active;
        const waiting = s.day_mode.state === 'waiting_time';
        dayPill.style.display = 'inline-flex';
        setPill('pill-day', active ? 'DIA activo' : (waiting ? 'DIA esperando hora' : 'DIA armado'), active ? 'ok' : 'warn');
      } else if (dayPill) {
        dayPill.style.display = 'none';
      }
      if (homeBtn){
        homeBtn.style.display = (s.mode === 'geofence') ? 'inline-flex' : 'none';
      }

      var homeText='Home: --';
      var homeTone='warn';
      if(s.home){
        if(s.home.set){
          homeText='Home OK';
          if(s.home.distance_m>=0){homeText+=' ('+s.home.distance_m.toFixed(1)+' m)';}
          homeTone='ok';
        } else {
          homeText='Home no definido';
        }
      }
      setPill('pill-home',homeText,homeTone);
    }

    async function loadSummary(){
      try{
        const d=await fetch('/api/summary').then(r=>r.json());
        renderSummary(d);
        renderSessionCurrent(d.session_current);
        renderHistory(d.history);
      }catch(e){
        $('status').textContent='Estado: Error';
      }
    }

    async function loadStatus(){
      try{
        const s=await fetch('/api/status').then(r=>r.json());
        renderStatus(s);
      }catch(e){}
    }

    function refreshAll(){loadSummary();loadStatus();}
    async function updateHome(){
      if (homeBtn) homeBtn.disabled = true;
      try{
        const r = await dogPost('/api/home/set').then(r=>r.json());
        if (r.status === 'ok'){
          loadStatus();
        }
      }catch(e){}
      if (homeBtn) homeBtn.disabled = false;
    }
    if (trackSession){
      trackSession.addEventListener('change',()=>{ if(trackLoaded) loadTrack(); else setTrackLinks(trackSession.value||'current'); });
    }
    // Repaint rather than just resize: without this the track vanishes on every
    // rotation. Debounced because a rotation fires a burst of resize events.
    function handleTrackResize(){
      if(trackResizeTimer){clearTimeout(trackResizeTimer);}
      trackResizeTimer=setTimeout(()=>{
        trackResizeTimer=null;
        if(trackDrawn){drawTrack(trackDrawn.points,trackDrawn.bbox);}
        else{resizeTrackCanvas();}
      },150);
    }
    window.addEventListener('resize',handleTrackResize);
    document.addEventListener('visibilitychange',()=>{ if(!document.hidden) refreshAll(); });
    resizeTrackCanvas();
    setTrackLinks(trackSession ? (trackSession.value||'current') : 'current');
    refreshAll();
    setInterval(()=>{ if(!document.hidden) loadStatus(); },5000);
    setInterval(()=>{ if(!document.hidden) loadSummary(); },10000);
  </script>
</body>
</html>
)HTML");
  return page;
}
String web_pages::html_wifi_page() {
  String page;
  page.reserve(39000);
  page += F(R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Wi-Fi</title>
  <style>
)HTML");
  page += FPSTR(BASE_CSS);
  page += F(R"HTML(
  </style>
</head>
<body>
  <noscript><div class="warn" style="padding:12px">Este portal necesita JavaScript activado para mostrar los datos del collar.</div></noscript>
  <main class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <h1 class="brand">DOG-RGB</h1>
          <div class="tagline">Configurar Wi-Fi</div>
        </div>
      </div>
      <div class="muted">Home Wi-Fi y hotspot local del collar.</div>
    </div>

    <a class="back-link" href="/">&#8592; Inicio</a>

    <div class="card section">
      <h2>Estado Wi-Fi</h2>
      <div class="grid grid-2 section-body">
        <div class="field"><span>Home Wi-Fi</span><div class="data" id="wifi_home_state">--</div></div>
        <div class="field"><span>Hotspot collar</span><div class="data" id="wifi_ap_state">--</div></div>
        <div class="field"><span>Portal local</span><div class="data mono" id="wifi_portal">--</div></div>
        <div class="field"><span>mDNS</span><div class="data mono" id="wifi_mdns_state">--</div></div>
      </div>
      <div class="actions">
        <button class="btn ghost" id="wifi_refresh_btn" type="button" onclick="loadWifiStatus()">Actualizar estado</button>
      </div>
      <div id="wifi_status_msg" class="notice"></div>
    </div>

    <!-- No method/action on purpose: a native form post cannot carry the
         X-Dog-Portal header, and that same fallback is what let a hostile page
         submit this form cross-origin. Submission goes through onsubmit. -->
    <form class="card section" id="sta_form">
      <h2>Home Wi-Fi</h2>
      <div class="muted">Conecta DOG-RGB al router de casa. El hotspot local queda disponible durante la conexion.</div>
      <div class="field">
        <label for="ssid">Nombre de red (SSID)</label>
        <input id="ssid" name="ssid" autocomplete="off" spellcheck="false" value=")HTML");
  page += html_escape_attr(wifi_mgr::ssid());
  page += F(R"HTML(">
      </div>
      <div class="actions">
        <button class="btn ghost" id="scan_btn" type="button" onclick="startScan()">Buscar redes</button>
      </div>
      <div id="scan_status" class="muted"></div>
      <div id="scan_results" class="scan-list"></div>
      <div class="field">
        <label for="pass">Password red de casa</label>
        <input name="pass" id="pass" type="password" autocomplete="new-password" placeholder="Password">
        <div id="sta_pass_hint" class="help"></div>
      </div>
      <label class="muted"><input type="checkbox" id="show_pass"> Mostrar password</label>
      <div class="actions">
        <button class="btn" id="sta_submit_btn" type="submit">Guardar y conectar</button>
      </div>
      <div id="sta_status" class="notice"></div>
    </form>

    <div class="card section" id="ap_block">
      <h2>Hotspot del collar</h2>
      <div class="muted">Estos datos son para conectarte directo al collar desde el telefono.</div>
      <div class="grid grid-2 section-body">
        <div class="field"><label for="ap_ssid">Nombre hotspot (SSID)</label><input id="ap_ssid" type="text" autocomplete="off" spellcheck="false"></div>
        <div class="field">
          <label for="mdns">Nombre corto del portal</label>
          <input id="mdns" type="text">
          <div class="help">Podras abrir el portal escribiendo <span id="mdns_preview" class="mono">dog-rgb</span>.local en el navegador.</div>
        </div>
      </div>
      <div class="grid grid-2">
        <div class="field">
          <label for="ap_pass">Password hotspot</label>
          <input id="ap_pass" type="password" autocomplete="new-password" placeholder="(sin cambio)">
          <div id="ap_hint" class="help"></div>
        </div>
        <div class="field-check">
          <label class="muted" for="ap_open"><input id="ap_open" type="checkbox"> Hotspot sin password</label>
        </div>
      </div>
      <label class="muted"><input type="checkbox" id="show_ap_pass"> Mostrar password hotspot</label>
      <!-- One advisory area, ordered by weight: the security consequence first
           and in the danger colour, the operational note after it and muted.
           Three same-coloured divs with no spacing read as one wall of text. -->
      <div class="advisories" id="ap_advisories">
        <div id="ap_open_warn" class="advisory danger"></div>
        <div id="ap_warn" class="advisory note"></div>
        <div id="ap_recovery" class="advisory note"></div>
      </div>
      <div class="actions">
        <button class="btn" id="ap_save_btn" type="button" onclick="saveAp()">Guardar AP</button>
      </div>
      <div id="ap_status" class="muted"></div>
    </div>

    <div class="actions">
      <a class="btn ghost" href="/">&#8592; Inicio</a>
    </div>
  </main>

  <script>
    // Every portal write goes through here. X-Dog-Portal is what the server's
    // CSRF guard requires; X-Dog-Pin is empty unless the user turned on the
    // optional lock, in which case a 401 prompts for it once per browser tab.
    function dogPin(){try{return sessionStorage.getItem('dogPin')||'';}catch(e){return '';}}
    async function dogPost(url,opts){
      opts=opts||{};
      const send=()=>fetch(url,Object.assign({},opts,{method:'POST',headers:Object.assign({'X-Dog-Portal':'1','X-Dog-Pin':dogPin()},opts.headers||{})}));
      let r=await send();
      if(r.status===401){
        const pin=prompt('Portal bloqueado. Introduce el PIN:');
        if(pin===null){return r;}
        try{sessionStorage.setItem('dogPin',pin);}catch(e){}
        r=await send();
        if(r.status===401){try{sessionStorage.removeItem('dogPin');}catch(e){}}
      }
      return r;
    }
    // SSIDs come off the air, so they are attacker-controlled text: a neighbour
    // can name their access point anything at all. Everything scanned goes
    // through here before it touches innerHTML.
    function esc(v){return String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
    const ssidInput = document.getElementById('ssid');
    const pass = document.getElementById('pass');
    const show = document.getElementById('show_pass');
    show.onchange = () => { pass.type = show.checked ? 'text' : 'password'; };
    const showApPass = document.getElementById('show_ap_pass');
    const staForm = document.getElementById('sta_form');
    const staStatus = document.getElementById('sta_status');
    const wifiHomeState = document.getElementById('wifi_home_state');
    const wifiApState = document.getElementById('wifi_ap_state');
    const wifiPortal = document.getElementById('wifi_portal');
    // La IP del AP es configurable: tomarla del estado en vez de fijarla.
    let apPortalUrl = 'http://192.168.4.1/';
    const wifiMdnsState = document.getElementById('wifi_mdns_state');
    const wifiStatusMsg = document.getElementById('wifi_status_msg');
    const wifiRefreshBtn = document.getElementById('wifi_refresh_btn');
    const apSsid = document.getElementById('ap_ssid');
    const mdns = document.getElementById('mdns');
    const apPass = document.getElementById('ap_pass');
    const apOpen = document.getElementById('ap_open');
    const apHint = document.getElementById('ap_hint');
    const apWarn = document.getElementById('ap_warn');
    const apOpenWarn = document.getElementById('ap_open_warn');
    const apRecovery = document.getElementById('ap_recovery');
    const apStatus = document.getElementById('ap_status');
    const staSubmitBtn = document.getElementById('sta_submit_btn');
    const apSaveBtn = document.getElementById('ap_save_btn');
    const mdnsPreview = document.getElementById('mdns_preview');
    const staPassHint = document.getElementById('sta_pass_hint');
    let apHasPass = false;
    let staHasPass = false;
    let initialAp = null;
    let baseCfg = null;
    let staPollTimer = null;
    showApPass.onchange = () => { apPass.type = showApPass.checked ? 'text' : 'password'; };

    const STA_ERROR_MAP = {
      ssid:'El nombre de la red no es valido (1 a 32 caracteres).',
      pass:'La password no es valida (hasta 63 caracteres).',
      locked:'El portal esta bloqueado: hace falta el PIN.',
      storage:'El collar no pudo guardar la red. Reintenta.'
    };

    const AP_ERROR_MAP = {
      ssid:{field:apSsid,msg:'El nombre del hotspot debe tener entre 1 y 32 caracteres, sin espacios al principio ni al final.'},
      pass:{field:apPass,msg:'La password del hotspot debe tener entre 8 y 63 caracteres.'},
      'pass required':{field:apPass,msg:'Escribe una password o marca "Hotspot sin password".'},
      mdns:{field:mdns,msg:'El nombre corto solo admite letras, numeros y guiones (1 a 32).'}
    };

    const scanBtn = document.getElementById('scan_btn');
    const scanStatus = document.getElementById('scan_status');
    const scanResults = document.getElementById('scan_results');
    let scanTimer = null;

    function signalBars(rssi){
      const n = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
      return '[' + '#'.repeat(n) + '.'.repeat(4 - n) + ']';
    }

    function stopScanPoll(){
      if (scanTimer){ clearInterval(scanTimer); scanTimer = null; }
    }

    function renderNetworks(list){
      if (!list || !list.length){
        scanResults.innerHTML = '';
        scanStatus.textContent = 'No se encontro ninguna red. Acerca el collar al router y reintenta.';
        return;
      }
      list.sort((a,b)=>b.rssi-a.rssi);
      scanResults.innerHTML = list.map(n=>
        `<button class="scan-item" type="button" data-ssid="${esc(n.ssid)}"><span class="scan-ssid">${esc(n.ssid)}</span><span class="scan-meta">${signalBars(n.rssi)} ${n.open?'ABIERTA':'CLAVE'}</span></button>`
      ).join('');
      scanResults.querySelectorAll('.scan-item').forEach(b=>{
        b.onclick = () => {
          ssidInput.value = b.dataset.ssid;
          scanResults.querySelectorAll('.scan-item').forEach(o=>o.classList.remove('active'));
          b.classList.add('active');
          scanStatus.textContent = 'Red elegida: ' + b.dataset.ssid + '. Escribe el password.';
          pass.focus();
        };
      });
      scanStatus.textContent = list.length + (list.length === 1 ? ' red encontrada.' : ' redes encontradas.') + ' Toca la tuya.';
    }

    // Scanning hops channels, so the hotspot the user is sitting on goes quiet
    // for a moment. Say so, and only ever scan because a button was pressed.
    async function startScan(){
      scanBtn.disabled = true;
      scanResults.innerHTML = '';
      scanStatus.textContent = 'Buscando redes... el hotspot puede ir lento unos segundos.';
      try{
        const res = await dogPost('/api/wifi/scan');
        if (!res.ok){
          scanStatus.textContent = 'No se pudo iniciar la busqueda.';
          scanBtn.disabled = false;
          return;
        }
        pollScan();
      }catch(e){
        scanStatus.textContent = 'No se pudo iniciar la busqueda.';
        scanBtn.disabled = false;
      }
    }

    function pollScan(){
      stopScanPoll();
      const deadline = Date.now() + 25000;
      scanTimer = setInterval(async () => {
        try{
          const d = await fetch('/api/wifi/scan').then(r=>r.json());
          if (d.state === 'scanning'){
            if (Date.now() >= deadline){
              stopScanPoll();
              scanBtn.disabled = false;
              scanStatus.textContent = 'La busqueda tardo demasiado. Reintenta.';
            }
            return;
          }
          stopScanPoll();
          scanBtn.disabled = false;
          if (d.state === 'ready'){
            renderNetworks(d.networks || []);
          } else {
            scanStatus.textContent = 'No se pudo completar la busqueda. Reintenta.';
          }
        }catch(e){
          stopScanPoll();
          scanBtn.disabled = false;
          scanStatus.textContent = 'Se perdio la conexion durante la busqueda.';
        }
      }, 1200);
    }

    // Confirmations clear themselves so the card stops claiming "Guardado" over
    // edits made a minute later. Errors stay until the user acts on them.
    let apStatusTimer = null;
    function setApStatus(msg, tone){
      apStatus.textContent = msg || '';
      if (tone === 'error') apStatus.className = 'error';
      else if (tone === 'warn') apStatus.className = 'warn';
      else apStatus.className = 'muted';
      if (apStatusTimer){ clearTimeout(apStatusTimer); apStatusTimer = null; }
      if (msg && tone !== 'error'){
        apStatusTimer = setTimeout(()=>{ apStatus.textContent=''; apStatusTimer=null; }, 4000);
      }
    }

    function clearApInvalid(){
      [apSsid, apPass, mdns].forEach(el => el && el.classList.remove('invalid'));
    }

    function setText(el, value){
      if (el) el.textContent = value || '--';
    }

    function validMdns(value){
      if (!value || value.length < 1 || value.length > 32) return false;
      if (value[0] === '-' || value[value.length - 1] === '-') return false;
      return /^[A-Za-z0-9-]+$/.test(value);
    }

    function handleApBackendError(reason){
      const e = AP_ERROR_MAP[reason] || null;
      if (!e){
        setApStatus('Error guardando AP.', 'error');
        return;
      }
      if (e.field) e.field.classList.add('invalid');
      setApStatus(e.msg, 'error');
    }

    function renderWifiStatus(s){
      const w = (s && s.wifi) ? s.wifi : {};
      let home = 'Desconectado';
      if (w.sta_connected){
        home = 'Conectado' + (w.sta_ip && w.sta_ip !== '0.0.0.0' ? (' (' + w.sta_ip + ')') : '');
      } else if (w.sta_connecting){
        home = 'Conectando...';
      } else if (w.wifi_off){
        home = 'Wi-Fi off';
      }
      const ap = w.ap_enabled ? ('Activo: ' + (w.ap_ssid || 'DogRGB') + ' (' + (w.ap_stations || 0) + ' clientes)') : 'Apagado';
      const apIp = w.ap_ip || '192.168.4.1';
      apPortalUrl = 'http://' + apIp + '/';
      const mdnsName = w.mdns || '';
      setText(wifiHomeState, home);
      setText(wifiApState, ap);
      setText(wifiPortal, 'http://' + apIp + '/');
      setText(wifiMdnsState, mdnsName ? ('http://' + mdnsName + '.local/') : '--');
      return w;
    }

    async function loadWifiStatus(){
      if (wifiRefreshBtn) wifiRefreshBtn.disabled = true;
      try{
        const s = await fetch('/api/status').then(r=>r.json());
        const w = renderWifiStatus(s);
        wifiStatusMsg.textContent = w.sta_connected ? 'Home Wi-Fi conectado.' : (w.sta_connecting ? 'Intentando conectar a Home Wi-Fi...' : '');
        return w;
      }catch(e){
        wifiStatusMsg.textContent = 'No se pudo leer el estado Wi-Fi.';
        return null;
      }finally{
        if (wifiRefreshBtn) wifiRefreshBtn.disabled = false;
      }
    }

    function pollStaStatus(){
      if (staPollTimer) clearInterval(staPollTimer);
      const deadline = Date.now() + 30000;
      const tick = async () => {
        const w = await loadWifiStatus();
        if (w && w.sta_connected){
          staStatus.textContent = 'Conectado a Home Wi-Fi. Portal: http://' + (w.mdns || 'dog-collar') + '.local/';
          clearInterval(staPollTimer);
          staPollTimer = null;
          return;
        }
        if (Date.now() >= deadline){
          staStatus.textContent = 'No se confirmo conexion. Revisa SSID/password; el hotspot sigue disponible en ' + apPortalUrl + '.';
          clearInterval(staPollTimer);
          staPollTimer = null;
        }
      };
      staPollTimer = setInterval(tick, 3000);
      tick();
    }

    function apChanged(){
      if (!initialAp) return false;
      return apSsid.value.trim() !== initialAp.ap_ssid ||
             mdns.value.trim() !== initialAp.mdns ||
             apOpen.checked !== initialAp.ap_open ||
             apPass.value !== '';
    }

    function updateMdnsPreview(){
      if (mdnsPreview) mdnsPreview.textContent = mdns.value.trim() || 'dog-rgb';
    }

    function updateApState(){
      if (apOpen.checked){
        apPass.value = '';
        apPass.disabled = true;
        apHint.innerText = 'Sin password mientras la casilla este marcada.';
      } else {
        apPass.disabled = false;
        apHint.innerText = apHasPass
          ? 'Ya hay una password guardada. Dejalo vacio para conservarla.'
          : 'No hay password. Escribe una de 8 caracteres o mas.';
      }
      apWarn.innerText = apChanged() ? 'Al guardar, el hotspot se reinicia y el telefono puede desconectarse un momento.' : '';
      apOpenWarn.innerText = apOpen.checked ? 'Sin password, cualquiera que este cerca podra abrir este portal y cambiar la configuracion del collar, incluido el password del hotspot.' : '';
      if (apChanged()) apRecovery.innerText = '';
      updateMdnsPreview();
    }

    // W4: the SSID comes back prefilled but the password never does, so without
    // this the user cannot tell "no password stored" from "stored, not shown"
    // -- and therefore cannot tell whether saving blank wipes it.
    function updateStaPassHint(){
      if (!staPassHint) return;
      staPassHint.innerText = staHasPass
        ? 'Ya hay una password guardada para esta red. Dejalo vacio para conservarla.'
        : 'No hay ninguna password guardada todavia.';
    }

    async function loadConfig(){
      try{
        baseCfg = await fetch('/api/config').then(r=>r.json());
        apSsid.value = baseCfg.wifi.ap_ssid || '';
        mdns.value = baseCfg.wifi.mdns || '';
        apHasPass = !!baseCfg.wifi.has_ap_pass;
        staHasPass = !!baseCfg.wifi.has_sta_pass;
        apOpen.checked = !apHasPass;
        initialAp = { ap_ssid: apSsid.value.trim(), mdns: mdns.value.trim(), ap_open: apOpen.checked };
        updateApState();
        updateStaPassHint();
      }catch(e){
        setApStatus('No se pudo leer la configuracion del collar.', 'error');
      }
    }

    async function saveAp(){
      clearApInvalid();
      if (!baseCfg){
        setApStatus('Error: config no disponible.', 'error');
        return;
      }
      const ssid = apSsid.value.trim();
      const mdnsVal = mdns.value.trim();
      const passVal = apPass.value;
      if (ssid.length < 1 || ssid.length > 32){
        apSsid.classList.add('invalid');
        setApStatus('El nombre del hotspot debe tener entre 1 y 32 caracteres.', 'error');
        return;
      }
      if (!apOpen.checked && passVal.length > 0 && passVal.length < 8){
        apPass.classList.add('invalid');
        setApStatus('La password del hotspot necesita 8 caracteres como minimo.', 'error');
        return;
      }
      if (!apOpen.checked && passVal.length === 0 && !apHasPass){
        apPass.classList.add('invalid');
        setApStatus('Escribe una password o marca "Hotspot sin password".', 'error');
        return;
      }
      if (!validMdns(mdnsVal)){
        mdns.classList.add('invalid');
        setApStatus('El nombre corto solo admite letras, numeros y guiones (1 a 32).', 'error');
        return;
      }
      if (apChanged()){
        if (!confirm('Guardar los cambios del hotspot? Se reinicia y puede que tengas que reconectar el telefono.')) return;
      }
      setApStatus('Guardando...', 'muted');
      const payload = {ap_ssid:ssid, ap_open:apOpen.checked, ap_pass:passVal, mdns:mdnsVal};
      if (apSaveBtn) apSaveBtn.disabled = true;
      try{
        const res = await dogPost('/api/wifi/ap',{headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
        const r = await res.json();
        if (!res.ok || r.status !== 'ok'){
          handleApBackendError(r.reason);
          return;
        }
        setApStatus(r.wifi_restart ? 'Guardado — reiniciando el hotspot' : 'Guardado', 'muted');
        baseCfg.wifi = baseCfg.wifi || {};
        baseCfg.wifi.ap_ssid = ssid;
        baseCfg.wifi.mdns = mdnsVal;
        initialAp = { ap_ssid: ssid, mdns: mdnsVal, ap_open: apOpen.checked };
        if (apOpen.checked) apHasPass = false;
        else if (passVal.length >= 8) apHasPass = true;
        baseCfg.wifi.has_ap_pass = apHasPass;
        apPass.value = '';
        const securityText = apOpen.checked ? 'sin password' : (passVal.length >= 8 ? 'con el password nuevo' : 'con el password ya configurado');
        apRecovery.innerText = r.wifi_restart ? ('Si el telefono se desconecta, reconectate al hotspot "' + ssid + '" ' + securityText + ' y abre ' + apPortalUrl + '.') : 'Hotspot actualizado.';
        updateApState();
        loadWifiStatus();
      }catch(e){
        setApStatus('Sin respuesta del collar. No se guardo.', 'error');
      }finally{
        if (apSaveBtn) apSaveBtn.disabled = false;
      }
    }

    staForm.onsubmit = async (e) => {
      e.preventDefault();
      staStatus.textContent = 'Guardando...';
      if (staSubmitBtn) staSubmitBtn.disabled = true;
      try{
        const fd = new FormData(staForm);
        const r = await dogPost('/api/wifi',{body:fd});
        const body = await r.json().catch(()=>({}));
        if (r.ok && body.status === 'ok'){
          staStatus.textContent = 'Guardado, conectando... el hotspot sigue disponible en ' + apPortalUrl + '.';
          pollStaStatus();
        } else {
          staStatus.textContent = STA_ERROR_MAP[body.reason] || 'No se pudo guardar la red de casa.';
        }
      }catch(e){
        staStatus.textContent = 'Sin respuesta del collar. No se guardo.';
      }finally{
        if (staSubmitBtn) staSubmitBtn.disabled = false;
      }
    };

    apOpen.onchange = updateApState;
    apPass.oninput = updateApState;
    apSsid.oninput = updateApState;
    mdns.oninput = updateApState;
    loadConfig();
    loadWifiStatus();
  </script>
</body>
</html>
)HTML");
  return page;
}
String web_pages::html_config_page() {
  String page;
  page.reserve(58000);
  page += F(R"CFG(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Config</title>
  <style>
)CFG");
  page += FPSTR(BASE_CSS);
  page += F(R"CFG(
  </style>
</head>
<body>
  <noscript><div class="warn" style="padding:12px">Este portal necesita JavaScript activado para mostrar los datos del collar.</div></noscript>
  <main class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <h1 class="brand">DOG-RGB</h1>
          <div class="tagline">Modos y LEDs</div>
        </div>
      </div>
      <div class="muted">Elige el comportamiento principal primero; la calibracion queda en avanzado.</div>
    </div>

    <a class="back-link" href="/">&#8592; Inicio</a>

    <div id="errors" class="card error-box error section"></div>

    <div class="card action-bar section sticky-actions">
      <button class="btn" id="save_btn" type="button" onclick="saveCfg()">Guardar cambios</button>
      <button class="btn danger" id="reset_btn" type="button" onclick="resetCfg()">Restaurar defaults</button>
      <span id="status" class="muted"></span>
    </div>

    <details class="card section" id="common_block" open>
      <summary>Modo y brillo</summary>
      <div class="section-body">
        <div class="mode-cards" id="mode_cards">
          <button class="mode-card" type="button" data-mode-card="speed"><strong>Velocidad</strong><span>LEDs reaccionan al movimiento. Requiere GPS confiable.</span></button>
          <button class="mode-card" type="button" data-mode-card="geofence"><strong>Geocerca</strong><span>LEDs reaccionan a distancia del Home. Requiere GPS y Home.</span></button>
          <button class="mode-card" type="button" data-mode-card="simple"><strong>Simple</strong><span>Un efecto fijo para toda la tira.</span></button>
          <button class="mode-card" type="button" data-mode-card="show"><strong>Show</strong><span>Demo automatica de efectos.</span></button>
        </div>
        <div class="grid grid-2 section-body">
          <div class="field"><label for="brightness_slider">Brillo</label><input id="brightness_slider" type="range" min="1" max="255"></div>
          <div class="field"><label for="brightness">Valor brillo</label><input id="brightness" type="number" min="1" max="255"></div>
        </div>
        <div class="field">
          <label><input id="day_mode_enabled" type="checkbox"> Modo DIA</label>
          <div class="help">Apaga efectos de 06:00 a 16:00; alertas y rastreo siguen activos.</div>
        </div>
        <div class="field" style="display:none">
          <label for="mode">Modo</label>
          <select id="mode">
            <option value="speed">Velocidad</option>
            <option value="geofence">Geocerca</option>
            <option value="simple">Simple</option>
            <option value="show">Show</option>
          </select>
        </div>
        <div id="mode_help" class="help"></div>
      </div>
    </details>

    <details class="card section" id="speed_lanes_block" open>
      <summary>Zonas de velocidad</summary>
      <div class="section-body">
        <div class="help">Cada zona es un tramo de velocidad con su propio efecto.
        <strong>vel</strong> es la velocidad de la animacion y <strong>int</strong> su
        intensidad, ambas de 0 a 255.</div>
        <div id="lanes_container"></div>
        <div class="help">Zona 10 activa cuando velocidad supera el umbral de Zona 9. Los efectos tambien aplican en modo Geocerca.</div>
      </div>
    </details>

    <details class="card section" id="geofence_block" open>
      <summary>Geocerca</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label for="fence_max">Distancia maxima (m)</label>
            <input id="fence_max" type="number" min="50" max="5000">
          </div>
          <div class="field">
            <span>Rangos</span>
            <div id="fence_ranges" class="muted"></div>
          </div>
        </div>
        <div class="row" style="margin-top:8px">
          <button class="btn" type="button" onclick="setHome()">Nuevo Home (GPS actual)</button>
          <button class="btn danger" type="button" onclick="clearHome()">Borrar Home</button>
        </div>
        <div id="home_status" class="muted"></div>
      </div>
    </details>

    <details class="card section" id="gps_block">
      <summary>GPS calidad (avanzado)</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label for="gps_min_fix">Fix quality min (0..8)</label>
            <input id="gps_min_fix" type="number" min="0" max="8">
          </div>
          <div class="field">
            <label for="gps_min_sats">Satellites min (3..12)</label>
            <input id="gps_min_sats" type="number" min="3" max="12">
          </div>
        </div>
        <div class="grid grid-2">
          <div class="field">
            <label for="gps_max_hdop">HDOP max (0.5..20)</label>
            <input id="gps_max_hdop" type="number" step="0.1" min="0.5" max="20">
          </div>
          <div class="field">
            <label for="gps_max_gga_age">Max age GGA (ms)</label>
            <input id="gps_max_gga_age" type="number" step="100" min="500" max="10000">
          </div>
        </div>
        <div class="grid grid-3">
          <div class="field">
            <label for="gps_min_segment">Min segment (m)</label>
            <input id="gps_min_segment" type="number" step="0.1" min="0.5" max="20">
          </div>
          <div class="field">
            <label for="gps_hdop_factor">HDOP factor</label>
            <input id="gps_hdop_factor" type="number" step="0.1" min="0" max="5">
          </div>
          <div class="field">
            <label for="gps_max_min_segment">Max min segment (m)</label>
            <input id="gps_max_min_segment" type="number" step="0.1" min="1" max="50">
          </div>
        </div>
        <div class="help">Solo se aceptan puntos con calidad suficiente para sumar distancia/tiempo.</div>
      </div>
    </details>

    <details class="card section" id="simple_block" open>
      <summary>Simple</summary>
      <div class="section-body">
        <div class="field">
          <span id="lbl_preset">Preajuste</span>
          <div class="preset-row" id="simple_preset_buttons" role="group" aria-labelledby="lbl_preset">
            <button class="preset-btn" type="button" data-theme="calm">Calm</button>
            <button class="preset-btn" type="button" data-theme="active">Active</button>
            <button class="preset-btn" type="button" data-theme="sport">Sport</button>
            <button class="preset-btn" type="button" data-theme="aurora">Aurora</button>
            <button class="preset-btn" type="button" data-theme="manual">Manual</button>
          </div>
        </div>
        <div class="field">
          <span id="lbl_color">Color base</span>
          <div class="swatch-row" id="color_swatches" role="group" aria-labelledby="lbl_color"></div>
          <div class="help">RAINBOW, GRADIENT_WAVE y FIRE generan sus propios colores e ignoran esta eleccion.</div>
        </div>
        <div class="grid grid-2">
          <div class="field">
            <label for="simple_theme">Tema</label>
            <select id="simple_theme">
              <option value="manual">Manual</option>
              <option value="calm">Calm</option>
              <option value="active">Active</option>
              <option value="sport">Sport</option>
              <option value="aurora">Aurora</option>
            </select>
          </div>
          <div class="field">
            <label for="simple_effect">Efecto</label>
            <select id="simple_effect"></select>
          </div>
        </div>
        <div class="grid grid-2">
          <div class="field"><label for="simple_speed">Velocidad (0..255)</label><input id="simple_speed" type="number" min="0" max="255"></div>
          <div class="field"><label for="simple_intensity">Intensidad (0..255)</label><input id="simple_intensity" type="number" min="0" max="255"></div>
        </div>
        <details class="section">
          <summary>RGB manual</summary>
          <div class="grid grid-3 section-body">
            <div class="field"><label for="simple_r">R</label><input id="simple_r" type="number" min="0" max="255"></div>
            <div class="field"><label for="simple_g">G</label><input id="simple_g" type="number" min="0" max="255"></div>
            <div class="field"><label for="simple_b">B</label><input id="simple_b" type="number" min="0" max="255"></div>
          </div>
        </details>
      </div>
    </details>

    <details class="card section" id="show_block" open>
      <summary>Show</summary>
      <div class="section-body">
        <div class="help">Modo demo: rota efectos automaticamente. No hay parametros.</div>
      </div>
    </details>

    <details class="card section" id="lock_section">
      <summary>Bloqueo del portal (opcional)</summary>
      <div class="section-body">
        <div class="help">Desactivado de fabrica. Sirve si compartes la clave del Wi-Fi
        y no quieres que se pueda cambiar la configuracion del collar. Ver el
        dashboard nunca pide PIN.</div>
        <label class="muted" for="lock_enabled"><input id="lock_enabled" type="checkbox"> Pedir un PIN para guardar cambios</label>
        <div class="field" id="lock_pin_field" style="display:none">
          <label for="lock_pin">PIN (4 a 8 digitos)</label>
          <input id="lock_pin" type="password" inputmode="numeric" autocomplete="new-password" placeholder="(sin cambio)">
        </div>
        <div class="help">Se envia en claro por la red local. Protege de un
        despiste, no de alguien decidido que ya este en tu Wi-Fi.</div>
        <div class="warn">Apuntalo: si olvidas el PIN hay que reinstalar el
        firmware por USB para quitarlo. "Restaurar defaults" no lo borra.</div>
        <div class="actions">
          <button class="btn" type="button" onclick="saveLock()">Guardar bloqueo</button>
        </div>
        <div id="lock_status" class="muted"></div>
      </div>
    </details>

    <div class="section">
      <div class="card action-bar">
        <button class="btn" type="button" onclick="saveCfg()">Guardar cambios</button>
        <button class="btn danger" type="button" onclick="resetCfg()">Restaurar defaults</button>
      </div>
      <a class="btn ghost" href="/">&#8592; Inicio</a>
    </div>
  </main>

  <script>
    // Every portal write goes through here. X-Dog-Portal is what the server's
    // CSRF guard requires; X-Dog-Pin is empty unless the user turned on the
    // optional lock, in which case a 401 prompts for it once per browser tab.
    // Todo valor de cadena que acabe en innerHTML pasa por aqui. Hoy ninguno
    // viene del dispositivo, pero el patron no debe depender de eso.
    function esc(v){return String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
    function dogPin(){try{return sessionStorage.getItem('dogPin')||'';}catch(e){return '';}}
    async function dogPost(url,opts){
      opts=opts||{};
      const send=()=>fetch(url,Object.assign({},opts,{method:'POST',headers:Object.assign({'X-Dog-Portal':'1','X-Dog-Pin':dogPin()},opts.headers||{})}));
      let r=await send();
      if(r.status===401){
        const pin=prompt('Portal bloqueado. Introduce el PIN:');
        if(pin===null){return r;}
        try{sessionStorage.setItem('dogPin',pin);}catch(e){}
        r=await send();
        if(r.status===401){try{sessionStorage.removeItem('dogPin');}catch(e){}}
      }
      return r;
    }
    const $ = (id) => document.getElementById(id);
    const modeEl = $('mode');
    const speedLanesBlock = $('speed_lanes_block');
    const geofenceBlock = $('geofence_block');
    const simpleBlock = $('simple_block');
    const showBlock = $('show_block');
    const fenceMax = $('fence_max');
    const fenceRanges = $('fence_ranges');
    const homeStatus = $('home_status');
    const gpsMinFix = $('gps_min_fix');
    const gpsMinSats = $('gps_min_sats');
    const gpsMaxHdop = $('gps_max_hdop');
    const gpsMaxGgaAge = $('gps_max_gga_age');
    const gpsMinSegment = $('gps_min_segment');
    const gpsHdopFactor = $('gps_hdop_factor');
    const gpsMaxMinSegment = $('gps_max_min_segment');
    const simpleTheme = $('simple_theme');
    const simpleEffect = $('simple_effect');
    const simpleSpeed = $('simple_speed');
    const simpleIntensity = $('simple_intensity');
    const simpleR = $('simple_r');
    const simpleG = $('simple_g');
    const simpleB = $('simple_b');
    const brightness = $('brightness');
    const brightnessSlider = $('brightness_slider');
    const dayModeEnabled = $('day_mode_enabled');
    const statusEl = $('status');
    const errorsEl = $('errors');
    const saveBtn = $('save_btn');
    const resetBtn = $('reset_btn');

    // Seventy inputs, two exit links and no autosave: without this, one stray
    // tap on "Inicio" throws away everything the user just set up. The lock
    // section is excluded because it has its own save button and its own state.
    let dirty = false;
    function markDirty(){ dirty = true; }
    function markClean(){ dirty = false; }
    function confirmLeave(){
      return !dirty || confirm('Tienes cambios sin guardar. Si sales ahora se pierden.');
    }
    document.querySelector('main').addEventListener('input', (e)=>{
      if (!e.target.closest('#lock_section')) markDirty();
    });
    document.querySelector('main').addEventListener('change', (e)=>{
      if (!e.target.closest('#lock_section')) markDirty();
    });
    document.querySelectorAll('a[href="/"]').forEach(a=>{
      a.addEventListener('click',(e)=>{ if(!confirmLeave()) e.preventDefault(); });
    });
    window.addEventListener('beforeunload',(e)=>{
      if (!dirty) return;
      e.preventDefault();
      e.returnValue = '';
    });

    const ZONE_COLORS = ['#00F0F0','#00F08C','#00F000','#64F000','#F0F000','#F0B400','#F07800','#F05000','#F02800','#F00000'];
    const ZONE_LABELS = ['Quieto','Paseo','Caminar','Ritmo','Trote','Carrera','Galope','Sprint','Sprint+','Maximo'];
    const EFFECTS = [
      {id:0,name:'SOLID'},{id:1,name:'PULSE'},{id:2,name:'BREATH'},{id:3,name:'CHASE'},
      {id:4,name:'COMET'},{id:5,name:'SINELON'},{id:6,name:'CONFETTI'},{id:7,name:'JUGGLE'},
      {id:8,name:'BPM'},{id:9,name:'RAINBOW'},{id:10,name:'FIRE'},{id:11,name:'GRADIENT_WAVE'}
    ];

    const SIMPLE_THEMES = {
      manual:null,
      calm:{effect:2,speed:60,intensity:90,r:0,g:60,b:60},
      active:{effect:4,speed:120,intensity:140,r:60,g:45,b:0},
      sport:{effect:7,speed:160,intensity:180,r:60,g:0,b:0},
      aurora:{effect:11,speed:120,intensity:180,r:0,g:180,b:120}
    };

    const COLOR_PRESETS = [
      {name:'Teal',r:0,g:60,b:60},{name:'Green',r:0,g:120,b:40},
      {name:'Amber',r:80,g:48,b:0},{name:'Red',r:90,g:0,b:0},
      {name:'Blue',r:0,g:40,b:120},{name:'White',r:120,g:120,b:120}
    ];

    const MODE_HELP = {
      speed:'Usa rangos de velocidad para elegir efectos.',
      geofence:'Usa distancia al Home. Requiere GPS y home definido.',
      simple:'Un efecto unico para toda la tira.',
      show:'Demo automatico de efectos.'
    };

    const ERROR_MAP = {
      brightness:{field:'brightness',msg:'Brillo fuera de rango (1..255).'},
      mode:{field:'mode',msg:'Modo invalido.'},
      fence_max:{field:'fence_max',msg:'Distancia de geocerca fuera de rango (50..5000 m).'},
      ranges:{field:'speed_lanes_block',msg:'Rangos requeridos.'},
      'ranges value':{field:'speed_lanes_block',msg:'Rangos deben ser > 0.'},
      'ranges order':{field:'speed_lanes_block',msg:'Rangos deben ser ascendentes.'},
      effects:{field:'speed_lanes_block',msg:'Efectos incompletos.'},
      'effect values':{field:'speed_lanes_block',msg:'Valores de efecto invalidos.'},
      'effect id':{field:'speed_lanes_block',msg:'ID de efecto invalido.'},
      single:{field:'simple_block',msg:'Bloque simple invalido.'},
      'single values':{field:'simple_block',msg:'Valores simple invalidos.'},
      gps:{field:'gps_block',msg:'Parametros GPS invalidos.'},
      day_mode:{field:'day_mode_enabled',msg:'Modo DIA invalido.'}
    };

    function fillEffectSelect(sel){
      sel.innerHTML = EFFECTS.map(e => `<option value="${e.id}">${esc(e.name)}</option>`).join('');
    }

    function buildSpeedLanes(){
      let html = '';
      for (let i=1;i<=10;i++){
        const color = ZONE_COLORS[i-1];
        const lbl = ZONE_LABELS[i-1];
        const isLast = (i===10);
        let rangeHtml;
        if (i===1){
          rangeHtml = `0 &ndash; <input id="ln${i}_thr" aria-label="Z${i} umbral km/h" type="number" step="0.1" min="0.1" max="40" placeholder="2.0"> km/h`;
        } else if (!isLast){
          rangeHtml = `<span id="ln${i}_prev" class="sl-prev">?</span> &ndash; <input id="ln${i}_thr" aria-label="Z${i} umbral km/h" type="number" step="0.1" min="0.1" max="40"> km/h`;
        } else {
          rangeHtml = `&gt; <span id="ln${i}_prev" class="sl-prev">?</span> km/h`;
        }
        html += `<div class="speed-lane"><div class="sl-main"><div class="sl-meta"><span class="sl-dot" style="background:${esc(color)};box-shadow:0 0 6px ${esc(color)}88"></span><span class="sl-name" style="color:${esc(color)}">Z${i}</span><span class="sl-lbl">${esc(lbl)}</span><span class="sl-range">${rangeHtml}</span></div><div class="sl-ctrls"><select id="ln${i}_eff" aria-label="Z${i} efecto tira A"></select><span class="ctl-grp"><span class="ctl-lbl">vel</span><input id="ln${i}_spd" aria-label="Z${i} velocidad" type="number" min="0" max="255"></span><span class="ctl-grp"><span class="ctl-lbl">int</span><input id="ln${i}_int" aria-label="Z${i} intensidad" type="number" min="0" max="255"></span></div></div><div class="sl-adv" id="ln${i}_adv" style="display:none"><label for="ln${i}_effb">Tira B:</label><select id="ln${i}_effb"></select></div><button class="sl-adv-btn" type="button" onclick="toggleLaneAdv(${i})" id="ln${i}_advbtn">+ tira B independiente</button></div>`;
      }
      $('lanes_container').innerHTML = html;
      for (let i=1;i<=10;i++){
        fillEffectSelect($('ln'+i+'_eff'));
        fillEffectSelect($('ln'+i+'_effb'));
        if (i<10){ $('ln'+i+'_thr').oninput = updateLanePrevs; }
      }
    }

    function updateLanePrevs(){
      for (let i=2;i<=10;i++){
        const prev = $('ln'+(i-1)+'_thr');
        const disp = $('ln'+i+'_prev');
        if (disp && prev) disp.textContent = prev.value || '?';
      }
    }

    function toggleLaneAdv(i){
      markDirty();
      const adv = $('ln'+i+'_adv');
      const btn = $('ln'+i+'_advbtn');
      const vis = adv.style.display !== 'none';
      adv.style.display = vis ? 'none' : 'flex';
      btn.textContent = vis ? '+ tira B independiente' : '- tira B independiente';
      if (!vis) $('ln'+i+'_effb').value = $('ln'+i+'_eff').value;
    }

    function applyTheme(key){
      const t = SIMPLE_THEMES[key];
      if (!t) return;
      simpleEffect.value = t.effect;
      simpleSpeed.value = t.speed;
      simpleIntensity.value = t.intensity;
      simpleR.value = t.r;
      simpleG.value = t.g;
      simpleB.value = t.b;
      updateSwatchSelection();
    }

    function readSimple(){
      return {
        effect: parseInt(simpleEffect.value || '0',10),
        speed: parseInt(simpleSpeed.value || '0',10),
        intensity: parseInt(simpleIntensity.value || '0',10),
        r: parseInt(simpleR.value || '0',10),
        g: parseInt(simpleG.value || '0',10),
        b: parseInt(simpleB.value || '0',10)
      };
    }

    function themeMatches(a,b){
      return a.effect===b.effect&&a.speed===b.speed&&a.intensity===b.intensity&&
             a.r===b.r&&a.g===b.g&&a.b===b.b;
    }

    function updateThemeSelection(){
      const current = readSimple();
      let match = 'manual';
      for (const key in SIMPLE_THEMES){
        if (key === 'manual') continue;
        if (themeMatches(current,SIMPLE_THEMES[key])){ match = key; break; }
      }
      simpleTheme.value = match;
      document.querySelectorAll('[data-theme]').forEach(btn=>btn.classList.toggle('active',btn.dataset.theme===match));
      updateSwatchSelection();
    }

    function buildColorSwatches(){
      const el=$('color_swatches');
      if(!el) return;
      el.innerHTML = COLOR_PRESETS.map(c=>`<button class="swatch" type="button" title="${esc(c.name)}" data-r="${c.r}" data-g="${c.g}" data-b="${c.b}" style="background:rgb(${c.r},${c.g},${c.b})"></button>`).join('');
      document.querySelectorAll('.swatch').forEach(btn=>{
        btn.onclick=()=>{
          markDirty();
          simpleR.value=btn.dataset.r;
          simpleG.value=btn.dataset.g;
          simpleB.value=btn.dataset.b;
          simpleTheme.value='manual';
          updateThemeSelection();
        };
      });
    }

    function updateSwatchSelection(){
      const cur=readSimple();
      document.querySelectorAll('.swatch').forEach(btn=>{
        const active=cur.r===parseInt(btn.dataset.r,10)&&cur.g===parseInt(btn.dataset.g,10)&&cur.b===parseInt(btn.dataset.b,10);
        btn.classList.toggle('active',active);
      });
    }

    function updateModeCards(){
      document.querySelectorAll('[data-mode-card]').forEach(btn=>btn.classList.toggle('active',btn.dataset.modeCard===modeEl.value));
    }

    function selectMode(mode){
      if (modeEl.value !== mode) markDirty();
      modeEl.value=mode;
      updateModeVisibility();
    }

    function syncBrightness(source){
      let v = parseInt(source.value,10);
      if (isNaN(v)) v = 1;
      v = Math.max(1,Math.min(255,v));
      brightness.value = v;
      brightnessSlider.value = v;
    }

    function updateFenceRanges(){
      const max = parseFloat(fenceMax.value || '0');
      if (!max || max <= 0){ fenceRanges.innerText=''; return; }
      const step = max / 10;
      let html = '';
      for (let i=1;i<=10;i++){
        const a = ((i-1)*step).toFixed(1);
        const b = (i*step).toFixed(1);
        html += `R${i}: ${a} - ${b} m<br>`;
      }
      fenceRanges.innerHTML = html;
    }

    function updateModeVisibility(){
      const mode = modeEl.value;
      speedLanesBlock.style.display = (mode === 'speed' || mode === 'geofence') ? 'block' : 'none';
      geofenceBlock.style.display = (mode === 'geofence') ? 'block' : 'none';
      simpleBlock.style.display = (mode === 'simple') ? 'block' : 'none';
      showBlock.style.display = (mode === 'show') ? 'block' : 'none';
      $('mode_help').innerText = MODE_HELP[mode] || '';
      updateModeCards();
    }

    function loadHome(){
      fetch('/api/home').then(r=>r.json()).then(h=>{
        if(!h.home_set){
          homeStatus.innerText = h.gps_fix ? 'Home: no definido (auto 10s con fix)' : 'Home: no definido (sin GPS)';
          return;
        }
        let src = h.home_source || 'auto';
        let dist = h.distance_m >= 0 ? ` | dist ${h.distance_m.toFixed(1)} m` : '';
        homeStatus.innerText = `Home (${src}): ${h.home_lat.toFixed(6)}, ${h.home_lon.toFixed(6)}${dist}`;
      }).catch(()=>{homeStatus.innerText='Home: error';});
    }

    function intVal(el,fallback){
      const n = parseInt(el.value,10);
      return isNaN(n) ? fallback : n;
    }

    function floatVal(el,fallback){
      const n = parseFloat(el.value);
      return isNaN(n) ? fallback : n;
    }

    function clearErrors(){
      errorsEl.innerHTML = '';
      firstInvalid = null;
      document.querySelectorAll('.invalid').forEach(el=>el.classList.remove('invalid'));
    }

    function showErrors(list){
      if (!list.length) return;
      const head = list.length === 1 ? 'No se pudo guardar:' : ('No se pudo guardar (' + list.length + ' problemas):');
      errorsEl.innerHTML = `<div><strong>${esc(head)}</strong></div>` + list.map(msg=>`<div>${esc(msg)}</div>`).join('');
    }

    // Errors name the field that is wrong, not the card it lives in. With ten
    // zones and seventy inputs, "algo esta mal aqui dentro" is not a usable
    // message -- the user has to compare nine numbers by hand to find it.
    let firstInvalid = null;

    function validateConfig(cfg){
      const errs = [];
      function addError(field,msg){
        errs.push(msg);
        const el = $(field);
        if (el){
          el.classList.add('invalid');
          if (!firstInvalid) firstInvalid = el;
        }
      }

      if (cfg.led.brightness < 1 || cfg.led.brightness > 255) addError('brightness','Brillo fuera de rango (1..255).');
      if (!cfg.day_mode || typeof cfg.day_mode.enabled !== 'boolean') addError('day_mode_enabled','Modo DIA invalido.');

      const ranges = cfg.speed_ranges_kph;
      if (ranges.length !== 9){
        addError('speed_lanes_block','Rangos requeridos.');
      } else {
        for (let i=0;i<ranges.length;i++){
          if (!(ranges[i] > 0)) addError('ln'+(i+1)+'_thr','Z'+(i+1)+': el umbral debe ser mayor que 0.');
        }
        for (let i=1;i<ranges.length;i++){
          if (!(ranges[i] > ranges[i-1])) addError('ln'+(i+1)+'_thr','Z'+(i+1)+' debe ser mayor que Z'+i+' ('+ranges[i-1]+' km/h).');
        }
      }

      if (cfg.fence_max_m < 50 || cfg.fence_max_m > 5000) addError('fence_max','Distancia geocerca fuera de rango (50..5000 m).');

      const g = cfg.gps || {};
      if (g.min_fix_quality < 0 || g.min_fix_quality > 8) addError('gps_min_fix','Calidad de fix minima fuera de rango (0..8).');
      if (g.min_sats < 3 || g.min_sats > 12) addError('gps_min_sats','Satelites minimos fuera de rango (3..12).');
      if (!(g.max_hdop >= 0.5 && g.max_hdop <= 20)) addError('gps_max_hdop','HDOP maximo fuera de rango (0.5..20).');
      if (g.max_gga_age_ms < 500 || g.max_gga_age_ms > 10000) addError('gps_max_gga_age','Edad maxima GGA fuera de rango (500..10000 ms).');
      if (!(g.min_segment_m >= 0.5 && g.min_segment_m <= 20)) addError('gps_min_segment','Segmento minimo fuera de rango (0.5..20 m).');
      if (!(g.hdop_factor >= 0 && g.hdop_factor <= 5)) addError('gps_hdop_factor','Factor HDOP fuera de rango (0..5).');
      if (!(g.max_min_segment_m >= 1 && g.max_min_segment_m <= 50)) addError('gps_max_min_segment','Segmento minimo maximo fuera de rango (1..50 m).');
      if (g.min_segment_m > g.max_min_segment_m) addError('gps_min_segment','El segmento minimo no puede superar su tope.');

      for (let i=1;i<=10;i++){
        const e = cfg.effects['range'+i];
        if (!e){ addError('speed_lanes_block','Efectos incompletos.'); break; }
        if (e.a < 0 || e.a > 11) addError('ln'+i+'_eff','Z'+i+': efecto de tira A invalido.');
        if (e.b < 0 || e.b > 11) addError('ln'+i+'_effb','Z'+i+': efecto de tira B invalido.');
        if (e.speed < 0 || e.speed > 255) addError('ln'+i+'_spd','Z'+i+': velocidad fuera de rango (0..255).');
        if (e.intensity < 0 || e.intensity > 255) addError('ln'+i+'_int','Z'+i+': intensidad fuera de rango (0..255).');
      }

      const s = cfg.single;
      if (s.effect < 0 || s.effect > 11) addError('simple_effect','Efecto simple invalido.');
      if (s.speed < 0 || s.speed > 255) addError('simple_speed','Velocidad simple fuera de rango (0..255).');
      if (s.intensity < 0 || s.intensity > 255) addError('simple_intensity','Intensidad simple fuera de rango (0..255).');
      if (s.rgb.r < 0 || s.rgb.r > 255) addError('simple_r','R fuera de rango (0..255).');
      if (s.rgb.g < 0 || s.rgb.g > 255) addError('simple_g','G fuera de rango (0..255).');
      if (s.rgb.b < 0 || s.rgb.b > 255) addError('simple_b','B fuera de rango (0..255).');

      if (errs.length) showErrors(errs);
      return errs.length === 0;
    }

    // The page is several screens tall and there is a save button at each end.
    // Rendering the error next to the top button and leaving the viewport where
    // it was reads, from the bottom of the page, as "the button does nothing".
    function revealFirstError(){
      if (!firstInvalid) return;
      const host = firstInvalid.closest('details');
      if (host && !host.open) host.open = true;
      try{ firstInvalid.scrollIntoView({block:'center',behavior:'smooth'}); }catch(e){ firstInvalid.scrollIntoView(); }
      try{ firstInvalid.focus({preventScroll:true}); }catch(e){}
    }

    function handleBackendError(reason){
      const e = ERROR_MAP[reason];
      if (!e){
        showErrors(['Error guardando.']);
        return;
      }
      const el = $(e.field);
      if (el){
        el.classList.add('invalid');
        if (!firstInvalid) firstInvalid = el;
      }
      showErrors([e.msg]);
    }

    function buildPayload(){
      const cfg = {
        version:5,
        mode: modeEl.value,
        fence_max_m: intVal(fenceMax,300),
        led:{brightness: intVal(brightness,1)},
        day_mode:{enabled: !!dayModeEnabled.checked},
        gps:{
          min_fix_quality: intVal(gpsMinFix,1),
          min_sats: intVal(gpsMinSats,6),
          max_hdop: floatVal(gpsMaxHdop,2.5),
          max_gga_age_ms: intVal(gpsMaxGgaAge,2000),
          min_segment_m: floatVal(gpsMinSegment,3.0),
          hdop_factor: floatVal(gpsHdopFactor,2.0),
          max_min_segment_m: floatVal(gpsMaxMinSegment,10.0)
        },
        speed_ranges_kph: [1,2,3,4,5,6,7,8,9].map(i=>floatVal($('ln'+i+'_thr'),0)),
        effects:{},
        single:{
          effect: intVal(simpleEffect,0),
          speed: intVal(simpleSpeed,0),
          intensity: intVal(simpleIntensity,0),
          rgb:{r:intVal(simpleR,0), g:intVal(simpleG,0), b:intVal(simpleB,0)}
        }
      };
      for (let i=1;i<=10;i++){
        const advVis = $('ln'+i+'_adv').style.display !== 'none';
        const effA = intVal($('ln'+i+'_eff'),0);
        cfg.effects['range'+i] = {
          a: effA,
          b: advVis ? intVal($('ln'+i+'_effb'),0) : effA,
          speed: intVal($('ln'+i+'_spd'),0),
          intensity: intVal($('ln'+i+'_int'),0)
        };
      }
      return cfg;
    }

    // Save is reachable from a button at each end of a page several screens
    // tall, so every outcome has to leave a mark somewhere the user is looking.
    // The sticky bar is always on screen; the error list is not.
    let statusTimer = null;
    function setStatus(msg, tone){
      statusEl.innerText = msg || '';
      statusEl.className = tone === 'error' ? 'error' : (tone === 'warn' ? 'warn' : 'muted');
      if (statusTimer){ clearTimeout(statusTimer); statusTimer = null; }
      if (msg && tone !== 'error'){
        statusTimer = setTimeout(()=>{ statusEl.innerText=''; statusTimer=null; }, 4000);
      }
    }

    async function saveCfg(){
      clearErrors();
      const cfg = buildPayload();
      if (!validateConfig(cfg)){
        setStatus('Revisa los campos marcados.', 'error');
        revealFirstError();
        return;
      }
      setStatus('Guardando...', 'muted');
      if (saveBtn) saveBtn.disabled = true;
      try{
        const r = await dogPost('/api/config',{headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)}).then(r=>r.json());
        if (r.status !== 'ok'){
          handleBackendError(r.reason);
          setStatus('No se pudo guardar.', 'error');
          revealFirstError();
          return;
        }
        markClean();
        setStatus(r.wifi_restart ? 'Guardado — reiniciando el hotspot' : 'Guardado', 'muted');
      }catch(e){
        setStatus('Sin respuesta del collar. No se guardo.', 'error');
      }finally{
        if (saveBtn) saveBtn.disabled = false;
      }
    }

    function setHome(){
      dogPost('/api/home/set').then(r=>r.json()).then(r=>{
        homeStatus.innerText = r.status==='ok' ? 'Home actualizado con la posicion actual.' : 'No se pudo fijar el Home: hace falta senal GPS.';
        loadHome();
      }).catch(()=>{homeStatus.innerText='Sin respuesta del collar.';});
    }

    function clearHome(){
      // Destructive and not trivially reversible: setting a new Home needs the
      // user to be physically back at the spot with a GPS fix.
      if (!confirm('Borrar el Home? El modo Geocerca dejara de funcionar hasta que definas uno nuevo, y para eso hay que estar en el sitio con senal GPS.')) return;
      dogPost('/api/home/clear').then(r=>r.json()).then(r=>{
        homeStatus.innerText = r.status==='ok' ? 'Home borrado.' : 'No se pudo borrar el Home.';
        loadHome();
      }).catch(()=>{homeStatus.innerText='Sin respuesta del collar.';});
    }

    function resetCfg(){
      if (!confirm('Restaurar los valores de fabrica? Se pierde la configuracion actual de LEDs, zonas y GPS.')) return;
      if (resetBtn) resetBtn.disabled = true;
      setStatus('Restaurando...', 'muted');
      dogPost('/api/config/reset').then(r=>r.json()).then(async r=>{
        if (r.status !== 'ok'){
          setStatus('No se pudo restaurar.', 'error');
          return;
        }
        // Without this the form keeps showing the pre-reset values, and the next
        // "Guardar cambios" writes them straight back, undoing the reset without
        // the user ever seeing it happen.
        await loadConfig();
        markClean();
        setStatus('Valores de fabrica restaurados', 'muted');
      }).catch(()=>{setStatus('Sin respuesta del collar.', 'error');})
        .finally(()=>{if(resetBtn) resetBtn.disabled=false;});
    }

    const lockEnabled = document.getElementById('lock_enabled');
    const lockPin = document.getElementById('lock_pin');
    const lockPinField = document.getElementById('lock_pin_field');
    const lockStatus = document.getElementById('lock_status');

    function updateLockFields(){
      lockPinField.style.display = lockEnabled.checked ? '' : 'none';
    }

    async function loadLock(){
      try{
        const r = await fetch('/api/lock').then(r=>r.json());
        lockEnabled.checked = !!r.enabled;
        lockPin.placeholder = r.enabled ? '(sin cambio)' : '';
        lockStatus.innerText = r.enabled ? 'Bloqueo activo' : 'Bloqueo desactivado';
      }catch(e){
        lockStatus.innerText = '';
      }
      updateLockFields();
    }

    async function saveLock(){
      const enable = lockEnabled.checked;
      const pin = lockPin.value;
      // An empty PIN while already enabled means "keep the current one", so
      // there is nothing to send and nothing to change.
      if (enable && pin.length === 0){
        lockStatus.innerText = 'Sin cambios: escribe un PIN nuevo para cambiarlo.';
        return;
      }
      if (enable && !/^[0-9]{4,8}$/.test(pin)){
        lockStatus.innerText = 'El PIN debe tener entre 4 y 8 digitos.';
        return;
      }
      if (!enable && !confirm('Quitar el bloqueo del portal?')) return;
      lockStatus.innerText = 'Guardando...';
      try{
        const res = await dogPost('/api/lock',{headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enable,pin:pin})});
        const r = await res.json();
        if (!res.ok || r.status !== 'ok'){
          lockStatus.innerText = r.reason === 'pin'
            ? 'El PIN debe tener entre 4 y 8 digitos.'
            : (r.reason === 'locked'
                ? 'Hace falta el PIN actual para cambiar el bloqueo.'
                : 'No se pudo guardar el bloqueo.');
          return;
        }
        // The new PIN becomes this tab's credential so the next save works.
        try{ enable ? sessionStorage.setItem('dogPin',pin) : sessionStorage.removeItem('dogPin'); }catch(e){}
        lockPin.value = '';
        await loadLock();
      }catch(e){
        lockStatus.innerText = 'Sin respuesta del collar. No se guardo.';
      }
    }

    lockEnabled.onchange = updateLockFields;
    loadLock();

    modeEl.onchange = updateModeVisibility;
    document.querySelectorAll('[data-mode-card]').forEach(btn=>btn.onclick=()=>selectMode(btn.dataset.modeCard));
    document.querySelectorAll('[data-theme]').forEach(btn=>btn.onclick=()=>{ markDirty(); simpleTheme.value=btn.dataset.theme; if(btn.dataset.theme !== 'manual') applyTheme(btn.dataset.theme); updateThemeSelection(); });
    simpleTheme.onchange = () => { if (simpleTheme.value !== 'manual') applyTheme(simpleTheme.value); updateThemeSelection(); };
    [simpleEffect,simpleSpeed,simpleIntensity,simpleR,simpleG,simpleB].forEach(el=>el.oninput=updateThemeSelection);
    brightnessSlider.oninput = () => syncBrightness(brightnessSlider);
    brightness.oninput = () => syncBrightness(brightness);
    fenceMax.oninput = updateFenceRanges;

    buildSpeedLanes();
    fillEffectSelect(simpleEffect);
    buildColorSwatches();

    function applyConfig(c){
      brightness.value = c.led.brightness;
      brightnessSlider.value = c.led.brightness;
      modeEl.value = c.mode || 'speed';
      const day = c.day_mode || {};
      dayModeEnabled.checked = !!day.enabled;
      fenceMax.value = c.fence_max_m || 300;
      const g = c.gps || {};
      gpsMinFix.value = (g.min_fix_quality !== undefined ? g.min_fix_quality : 1);
      gpsMinSats.value = (g.min_sats !== undefined ? g.min_sats : 6);
      gpsMaxHdop.value = (g.max_hdop !== undefined ? g.max_hdop : 2.5);
      gpsMaxGgaAge.value = (g.max_gga_age_ms !== undefined ? g.max_gga_age_ms : 2000);
      gpsMinSegment.value = (g.min_segment_m !== undefined ? g.min_segment_m : 3.0);
      gpsHdopFactor.value = (g.hdop_factor !== undefined ? g.hdop_factor : 2.0);
      gpsMaxMinSegment.value = (g.max_min_segment_m !== undefined ? g.max_min_segment_m : 10.0);
      for (let i=1;i<=9;i++){ $('ln'+i+'_thr').value = c.speed_ranges_kph[i-1]; }
      for (let i=1;i<=10;i++){
        const e = c.effects['range'+i];
        $('ln'+i+'_eff').value = e.a;
        $('ln'+i+'_effb').value = e.b;
        $('ln'+i+'_spd').value = e.speed;
        $('ln'+i+'_int').value = e.intensity;
        if (e.a !== e.b){
          $('ln'+i+'_adv').style.display = 'flex';
          $('ln'+i+'_advbtn').textContent = '- tira B independiente';
        }
      }
      updateLanePrevs();
      const s = c.single || {};
      const rgb = s.rgb || {};
      simpleEffect.value = (s.effect !== undefined ? s.effect : 0);
      simpleSpeed.value = (s.speed !== undefined ? s.speed : 80);
      simpleIntensity.value = (s.intensity !== undefined ? s.intensity : 140);
      simpleR.value = (rgb.r !== undefined ? rgb.r : 0);
      simpleG.value = (rgb.g !== undefined ? rgb.g : 60);
      simpleB.value = (rgb.b !== undefined ? rgb.b : 60);
      updateModeVisibility();
      updateFenceRanges();
      updateThemeSelection();
      updateSwatchSelection();
      loadHome();
    }

    // Without a failure path a collar that is rebooting or out of range leaves
    // an empty form on screen with nothing to explain it.
    async function loadConfig(){
      try{
        const r = await fetch('/api/config');
        if (!r.ok) throw new Error('http ' + r.status);
        applyConfig(await r.json());
        clearErrors();
        markClean();
        return true;
      }catch(e){
        errorsEl.innerHTML = '<div><strong>No se pudo leer la configuracion del collar.</strong></div>' +
          '<div>Comprueba que sigues conectado al hotspot y reintenta.</div>' +
          '<div class="actions"><button class="btn" type="button" onclick="loadConfig()">Reintentar</button></div>';
        setStatus('Sin configuracion cargada.', 'error');
        return false;
      }
    }

    loadConfig();
  </script>
</body>
</html>
)CFG");
  return page;
}

String web_pages::html_dev_page() {
  String page;
  page.reserve(36000);
  page += F(R"DEV(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Dev</title>
  <style>
)DEV");
  page += FPSTR(BASE_CSS);
  page += F(R"DEV(
  </style>
</head>
<body>
  <noscript><div class="warn" style="padding:12px">Este portal necesita JavaScript activado para mostrar los datos del collar.</div></noscript>
  <main class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <h1 class="brand">DOG-RGB</h1>
          <div class="tagline">Diagnostico tecnico</div>
        </div>
        <div class="chips">
          <span class="pill" id="dev-pill-gps">GPS: --</span>
          <span class="pill" id="dev-pill-wifi">Wi-Fi: --</span>
        </div>
      </div>
      <div class="row">
        <button class="btn" onclick="refresh()">Actualizar</button>
        <label class="muted"><input id="auto" type="checkbox"> Auto (5s)</label>
        <span class="muted" id="dev-updated">Ultima lectura: --</span>
      </div>
      <div class="muted">Vista tecnica para validar AP, GPS, LED y memoria. No es necesaria para uso normal.</div>
    </div>

    <div class="card section">
      <h2>Sistema</h2>
      <div class="grid grid-kv">
        <div class="field"><span>Tiempo activo</span><div class="data mono" id="dev-uptime">--</div></div>
        <div class="field"><span>Compilacion</span><div class="data mono" id="dev-build">--</div></div>
        <div class="field"><span>Heap libre</span><div class="data mono" id="dev-heap">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Wi-Fi</h2>
      <div class="grid grid-kv">
        <div class="field"><span>Modo</span><div class="data mono" id="wifi-mode">--</div></div>
        <div class="field"><span>STA</span><div class="data mono" id="wifi-sta">--</div></div>
        <div class="field"><span>AP</span><div class="data mono" id="wifi-ap">--</div></div>
        <div class="field"><span>Clientes AP</span><div class="data mono" id="wifi-stations">--</div></div>
        <div class="field"><span>Wi-Fi apagado</span><div class="data mono" id="wifi-off">--</div></div>
        <div class="field"><span>SSID AP</span><div class="data mono" id="wifi-ssid">--</div></div>
        <div class="field"><span>mDNS</span><div class="data mono" id="wifi-mdns">--</div></div>
        <div class="field"><span>STA IP</span><div class="data mono" id="wifi-sta-ip">--</div></div>
        <div class="field"><span>AP IP</span><div class="data mono" id="wifi-ap-ip">--</div></div>
        <div class="field"><span>RSSI</span><div class="data mono" id="wifi-rssi">--</div></div>
      </div>
    </div>

    <details class="card section" open>
      <summary>Diagnostico AP</summary>
      <div class="grid grid-kv section-body">
        <div class="field"><span>Inicios AP</span><div class="data mono" id="diag-ap-start">--</div></div>
        <div class="field"><span>Fallos AP</span><div class="data mono" id="diag-ap-fail">--</div></div>
        <div class="field"><span>Paradas AP</span><div class="data mono" id="diag-ap-stop">--</div></div>
        <div class="field"><span>Reinicios AP</span><div class="data mono" id="diag-ap-restart">--</div></div>
        <div class="field"><span>Clientes conectados</span><div class="data mono" id="diag-ap-sta-connect">--</div></div>
        <div class="field"><span>Clientes desconectados</span><div class="data mono" id="diag-ap-sta-disconnect">--</div></div>
        <div class="field"><span>DNS cautivo</span><div class="data mono" id="diag-dns">--</div></div>
        <div class="field"><span>Canal AP</span><div class="data mono" id="diag-channel">--</div></div>
        <div class="field"><span>Hold AP</span><div class="data mono" id="diag-hold">--</div></div>
        <div class="field"><span>Proximo retry AP</span><div class="data mono" id="diag-next-ap-retry">--</div></div>
        <div class="field"><span>Proximo retry STA</span><div class="data mono" id="diag-next-retry">--</div></div>
        <div class="field"><span>Ultima razon AP</span><div class="data mono" id="diag-ap-reason">--</div></div>
        <div class="field"><span>Etapa fallo AP</span><div class="data mono" id="diag-ap-failure-stage">--</div></div>
        <div class="field"><span>Ultima razon STA</span><div class="data mono" id="diag-sta-reason">--</div></div>
      </div>
    </details>

    <div class="card section">
      <h2>GPS</h2>
      <div class="grid grid-kv">
        <div class="field"><span>Fix</span><div class="data mono" id="gps-fix">--</div></div>
        <div class="field"><span>Fix actual</span><div class="data mono" id="gps-current-fix">--</div></div>
        <div class="field"><span>Fix sin filtrar</span><div class="data mono" id="gps-raw-fix">--</div></div>
        <div class="field"><span>Fix confiable</span><div class="data mono" id="gps-trusted-fix">--</div></div>
        <div class="field"><span>Sats</span><div class="data mono" id="gps-sats">--</div></div>
        <div class="field"><span>Calidad fix</span><div class="data mono" id="gps-fix-quality">--</div></div>
        <div class="field"><span>HDOP</span><div class="data mono" id="gps-hdop">--</div></div>
        <div class="field"><span>Calidad OK</span><div class="data mono" id="gps-quality-ok">--</div></div>
        <div class="field"><span>Velocidad (kph)</span><div class="data mono" id="gps-speed">--</div></div>
        <div class="field"><span>Lat</span><div class="data mono" id="gps-lat">--</div></div>
        <div class="field"><span>Lon</span><div class="data mono" id="gps-lon">--</div></div>
        <div class="field"><span>Fecha</span><div class="data mono" id="gps-date">--</div></div>
        <div class="field"><span>Ultima actualizacion</span><div class="data mono" id="gps-update">--</div></div>
        <div class="field"><span>Edad ultimo byte</span><div class="data mono" id="gps-age-byte">--</div></div>
        <div class="field"><span>Edad ultimo fix</span><div class="data mono" id="gps-age-fix">--</div></div>
        <div class="field"><span>Bytes RX</span><div class="data mono" id="gps-bytes">--</div></div>
        <div class="field"><span>Sentencias RX</span><div class="data mono" id="gps-sentences">--</div></div>
        <div class="field"><span>RMC visto</span><div class="data mono" id="gps-rmc-seen">--</div></div>
        <div class="field"><span>RMC valido</span><div class="data mono" id="gps-rmc-valid">--</div></div>
        <div class="field"><span>GGA visto</span><div class="data mono" id="gps-gga-seen">--</div></div>
        <div class="field"><span>Overflow</span><div class="data mono" id="gps-overflow">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>LED</h2>
      <div class="grid grid-kv">
        <div class="field"><span>Modo</span><div class="data mono" id="led-mode">--</div></div>
        <div class="field"><span>Brillo</span><div class="data mono" id="led-brightness">--</div></div>
        <div class="field"><span>Rango actual</span><div class="data mono" id="led-range">--</div></div>
        <div class="field"><span>Base RGB</span><div class="data mono" id="led-base">--</div></div>
        <div class="field"><span>Efecto A</span><div class="data mono" id="led-effect-a">--</div></div>
        <div class="field"><span>Efecto B</span><div class="data mono" id="led-effect-b">--</div></div>
        <div class="field"><span>Velocidad rango</span><div class="data mono" id="led-range-speed">--</div></div>
        <div class="field"><span>Intensidad rango</span><div class="data mono" id="led-range-intensity">--</div></div>
        <div class="field"><span>Efecto simple</span><div class="data mono" id="led-simple-effect">--</div></div>
        <div class="field"><span>Velocidad simple</span><div class="data mono" id="led-simple-speed">--</div></div>
        <div class="field"><span>Intensidad simple</span><div class="data mono" id="led-simple-intensity">--</div></div>
        <div class="field"><span>Simple RGB</span><div class="data mono" id="led-simple-rgb">--</div></div>
        <div class="field"><span>Efecto show</span><div class="data mono" id="led-show-effect">--</div></div>
        <div class="field"><span>Modo DIA</span><div class="data mono" id="day-state">--</div></div>
        <div class="field"><span>DIA hora local</span><div class="data mono" id="day-local">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Geocerca</h2>
      <div class="grid grid-kv">
        <div class="field"><span>Home definido</span><div class="data mono" id="geo-set">--</div></div>
        <div class="field"><span>Fuente</span><div class="data mono" id="geo-source">--</div></div>
        <div class="field"><span>Home lat</span><div class="data mono" id="geo-lat">--</div></div>
        <div class="field"><span>Home lon</span><div class="data mono" id="geo-lon">--</div></div>
        <div class="field"><span>Distancia (m)</span><div class="data mono" id="geo-dist">--</div></div>
        <div class="field"><span>Rango</span><div class="data mono" id="geo-range">--</div></div>
      </div>
    </div>

    <details class="card section">
      <summary>JSON crudo</summary>
      <pre id="dev-json" class="code mono section-body"></pre>
    </details>

    <div class="actions">
      <a class="btn ghost" href="/">&#8592; Inicio</a>
    </div>
  </main>

  <script>
    const $ = (id) => document.getElementById(id);
    let timer = null;

    function setText(id, value){
      const el = $(id);
      if (!el) return;
      el.textContent = (value === undefined || value === null) ? '--' : value;
    }

    function setPill(id, text, tone){
      const el = $(id);
      if (!el) return;
      el.textContent = text;
      el.className = 'pill' + (tone ? (' ' + tone) : '');
    }

    // A page whose whole job is answering "is something wrong?" cannot render
    // every number in the same colour. Only the counters that mean something
    // get a verdict; the rest stay neutral so the coloured ones stand out.
    function setHealth(id, value, tone){
      const el = $(id);
      if (!el) return;
      el.textContent = (value === undefined || value === null) ? '--' : value;
      el.className = 'data mono' + (tone ? (' health-' + tone) : '');
    }

    // Counters that should be zero on a healthy device.
    function zeroIsGood(v){
      const n = Number(v);
      if (!isFinite(n)) return null;
      return n === 0 ? 'ok' : (n < 5 ? 'warn' : 'bad');
    }

    function fmtUptime(ms){
      if (ms === undefined || ms < 0) return '--';
      let s = Math.floor(ms / 1000);
      const h = Math.floor(s / 3600);
      s = s % 3600;
      const m = Math.floor(s / 60);
      s = s % 60;
      return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
    }

    function fmtMin(min){
      if (min === undefined || min < 0) return '--';
      const h = Math.floor(min / 60);
      const m = min % 60;
      return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0');
    }

    function fmtMs(ms){
      if (ms === undefined || ms < 0) return '--';
      if (ms < 1000) return ms + ' ms';
      return (ms / 1000).toFixed(1) + ' s';
    }

    async function refresh(){
      try{
        const d = await fetch('/api/dev').then(r=>r.json());
        setText('dev-uptime', fmtUptime(d.time.uptime_ms));
        setText('dev-build', d.time.build);
        const heap = d.system.free_heap;
        setHealth('dev-heap', heap, heap === undefined ? null : (heap < 20000 ? 'bad' : (heap < 50000 ? 'warn' : 'ok')));

        const wifi = d.wifi || {};
        const staState = wifi.sta_connected ? 'conectada' : (wifi.sta_connecting ? 'conectando' : 'desconectada');
        setText('wifi-mode', wifi.mode);
        setText('wifi-sta', staState);
        setText('wifi-ap', wifi.ap_enabled ? 'activo' : 'apagado');
        setText('wifi-stations', wifi.ap_stations);
        setText('wifi-off', wifi.wifi_off ? 'si' : 'no');
        setText('wifi-ssid', wifi.ap_ssid);
        setText('wifi-mdns', wifi.mdns);
        setText('wifi-sta-ip', wifi.sta_ip);
        setText('wifi-ap-ip', wifi.ap_ip);
        setText('wifi-rssi', wifi.rssi);
        const diag = wifi.diagnostics || {};
        setText('diag-ap-start', diag.ap_start_count);
        setHealth('diag-ap-fail', diag.ap_start_fail_count, zeroIsGood(diag.ap_start_fail_count));
        setText('diag-ap-stop', diag.ap_stop_count);
        setText('diag-ap-restart', diag.ap_restart_count);
        setText('diag-ap-sta-connect', diag.ap_station_connect_count);
        setText('diag-ap-sta-disconnect', diag.ap_station_disconnect_count);
        setText('diag-dns', diag.dns_running ? 'activo' : 'apagado');
        setText('diag-channel', diag.current_ap_channel);
        setText('diag-hold', diag.ap_hold_scheduled ? fmtMs(diag.ap_hold_remaining_ms || 0) : '--');
        setText('diag-next-ap-retry', diag.ap_retry_scheduled ? fmtMs(diag.ap_retry_remaining_ms || 0) : '--');
        setText('diag-next-retry', diag.sta_retry_scheduled ? fmtMs(diag.sta_retry_remaining_ms || 0) : '--');
        setText('diag-ap-reason', diag.last_ap_reason);
        setText('diag-ap-failure-stage', diag.last_ap_failure_stage || '--');
        setText('diag-sta-reason', diag.last_sta_reason);

        const gps = d.gps || {};
        setHealth('gps-fix', gps.fix ? 'si' : 'no', gps.fix ? 'ok' : 'warn');
        setText('gps-current-fix', gps.current_fix ? 'si' : 'no');
        setText('gps-raw-fix', gps.raw_fix ? 'si' : 'no');
        setText('gps-trusted-fix', gps.trusted_fix ? 'si' : 'no');
        setText('gps-sats', gps.sats);
        setText('gps-fix-quality', gps.fix_quality);
        setText('gps-hdop', (gps.hdop !== undefined) ? gps.hdop.toFixed(2) : '--');
        setHealth('gps-quality-ok', gps.quality_ok ? 'si' : 'no', gps.quality_ok ? 'ok' : 'warn');
        setText('gps-speed', (gps.speed_kph !== undefined) ? gps.speed_kph.toFixed(2) : '--');
        setText('gps-lat', (gps.lat !== undefined) ? gps.lat.toFixed(6) : '--');
        setText('gps-lon', (gps.lon !== undefined) ? gps.lon.toFixed(6) : '--');
        setText('gps-date', gps.date);
        setText('gps-update', fmtMin(gps.last_update_min));
        setText('gps-age-byte', fmtMs(gps.age_last_byte_ms));
        setText('gps-age-fix', fmtMs(gps.age_last_fix_ms));
        setText('gps-bytes', gps.bytes_rx);
        setText('gps-sentences', gps.sentences_rx);
        setText('gps-rmc-seen', gps.rmc_seen);
        setText('gps-rmc-valid', gps.rmc_valid);
        setText('gps-gga-seen', gps.gga_seen);
        setHealth('gps-overflow', gps.overflow, gps.overflow === undefined ? null : (gps.overflow ? 'bad' : 'ok'));

        const led = d.led || {};
        setText('led-mode', led.mode);
        setText('led-brightness', led.brightness);
        setText('led-range', led.range);
        if (led.base_rgb){
          setText('led-base', led.base_rgb.r + ',' + led.base_rgb.g + ',' + led.base_rgb.b);
        } else {
          setText('led-base', '--');
        }
        if (led.effect_a){
          setText('led-effect-a', led.effect_a.name + ' (' + led.effect_a.id + ')');
          setText('led-range-speed', led.effect_a.speed);
          setText('led-range-intensity', led.effect_a.intensity);
        } else {
          setText('led-effect-a', '--');
          setText('led-range-speed', '--');
          setText('led-range-intensity', '--');
        }
        if (led.effect_b){
          setText('led-effect-b', led.effect_b.name + ' (' + led.effect_b.id + ')');
        } else {
          setText('led-effect-b', '--');
        }
        if (led.simple){
          setText('led-simple-effect', led.simple.name + ' (' + led.simple.effect + ')');
          setText('led-simple-speed', led.simple.speed);
          setText('led-simple-intensity', led.simple.intensity);
          if (led.simple.rgb){
            setText('led-simple-rgb', led.simple.rgb.r + ',' + led.simple.rgb.g + ',' + led.simple.rgb.b);
          }
        }
        if (led.show){
          setText('led-show-effect', led.show.name + ' (' + led.show.effect + ')');
        }
        const day = d.day_mode || {};
        setText('day-state', day.enabled ? (day.active ? 'activo' : day.state) : 'desactivado');
        setText('day-local', day.time_available ? fmtMin(day.local_min) : '--');

        const geo = d.geofence || {};
        setText('geo-set', geo.set ? 'si' : 'no');
        setText('geo-source', geo.source);
        setText('geo-lat', (geo.home_lat !== undefined) ? geo.home_lat.toFixed(6) : '--');
        setText('geo-lon', (geo.home_lon !== undefined) ? geo.home_lon.toFixed(6) : '--');
        setText('geo-dist', (geo.distance_m !== undefined) ? geo.distance_m.toFixed(1) : '--');
        setText('geo-range', geo.range);

        setPill('dev-pill-gps', gps.fix ? 'GPS OK' : 'GPS sin fix', gps.fix ? 'ok' : 'warn');
        let wifiTone = 'warn';
        let wifiText = 'Wi-Fi off';
        if (wifi.sta_connected){ wifiTone = 'ok'; wifiText = 'STA conectada'; }
        else if (wifi.ap_enabled){ wifiTone = 'warn'; wifiText = 'AP activo'; }
        setPill('dev-pill-wifi', wifiText, wifiTone);

        $('dev-json').textContent = JSON.stringify(d, null, 2);
        const now = new Date();
        $('dev-updated').textContent = 'Ultima lectura: ' + now.toLocaleTimeString();
      }catch(e){
        $('dev-updated').textContent = 'Ultima lectura: error';
      }
    }

    $('auto').onchange = (e) => {
      if (e.target.checked){
        refresh();
        timer = setInterval(refresh, 5000);
      } else if (timer){
        clearInterval(timer);
        timer = null;
      }
    };

    refresh();
  </script>
</body>
</html>
)DEV");
  return page;
}
