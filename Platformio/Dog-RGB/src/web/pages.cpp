#include "web/pages.h"

#include <pgmspace.h>

#include "wifi/wifi_mgr.h"

namespace {
const char BASE_CSS[] PROGMEM = R"CSS(
:root{--bg:#F2F6F8;--surface:#FFFFFF;--text:#0B1220;--muted:#5D6B7A;--accent:#00D1C1;--accent-2:#FF8A00;--danger:#E84545;--border:#E6EDF2;--shadow:0 8px 24px rgba(11,18,32,0.08);--radius:14px;--space-1:6px;--space-2:10px;--space-3:14px;--space-4:20px;--space-5:28px;}
*{box-sizing:border-box;}
body{margin:0;font-family:'Space Grotesk','Avenir Next','Segoe UI',system-ui,sans-serif;background:linear-gradient(180deg,#F2F6F8 0%,#EDF3F6 100%);color:var(--text);}
a{color:var(--text);text-decoration:none;}
h1,h2{margin:0 0 8px 0;}
h1{font-size:24px;letter-spacing:0.4px;}
h2{font-size:18px;}
.container{max-width:900px;margin:0 auto;padding:20px;}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);padding:var(--space-4);}
.hero{display:flex;flex-direction:column;gap:var(--space-3);}
.hero-top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:var(--space-3);}
.brand{font-size:26px;font-weight:700;letter-spacing:0.6px;}
.tagline{font-size:13px;color:var(--muted);}
.chips{display:flex;flex-wrap:wrap;gap:8px;}
.pill{font-size:12px;border-radius:999px;padding:6px 10px;background:#F1F4F7;border:1px solid var(--border);color:var(--text);}
.pill.ok{background:rgba(0,209,193,0.15);border-color:rgba(0,209,193,0.35);color:#007A70;}
.pill.warn{background:rgba(255,138,0,0.15);border-color:rgba(255,138,0,0.35);color:#A05A00;}
.pill.bad{background:rgba(232,69,69,0.15);border-color:rgba(232,69,69,0.35);color:#8A1D1D;}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.grid{display:grid;gap:12px;}
.grid-2{grid-template-columns:repeat(2,minmax(0,1fr));}
.grid-3{grid-template-columns:repeat(3,minmax(0,1fr));}
.label{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:0.06em;}
.value{font-size:24px;font-weight:700;}
.data{font-size:14px;font-weight:600;color:var(--text);}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,"Liberation Mono","Courier New",monospace;}
.code{background:#0B1220;color:#EAF2F6;padding:12px;border-radius:10px;overflow:auto;font-size:12px;}
.metric .label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:0.08em;}
.metric .value{font-size:28px;font-weight:700;line-height:1.1;}
.metric .unit{font-size:12px;color:var(--muted);}
.muted{color:var(--muted);font-size:12px;}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:10px 14px;border-radius:10px;border:1px solid transparent;background:var(--text);color:#fff;font-weight:600;font-size:14px;cursor:pointer;}
.btn.ghost{background:transparent;color:var(--text);border-color:var(--border);}
.btn:disabled{opacity:0.6;cursor:not-allowed;}
.actions{display:flex;flex-wrap:wrap;gap:10px;margin:14px 0;}
.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:6px;}
input,select{width:100%;padding:10px 12px;border:1px solid var(--border);border-radius:10px;background:#fff;font-family:inherit;font-size:14px;color:var(--text);}
input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,209,193,0.15);}
input[type="checkbox"]{width:auto;margin-right:6px;}
.section{margin-top:14px;}
.section.invalid{border-color:var(--danger);background:#FFF5F5;}
input.invalid,select.invalid{border-color:var(--danger);background:#FFF5F5;}
.error{color:var(--danger);font-size:12px;}
.error-box{padding:12px;}
.error-box:empty{display:none;}
.warn{color:#A05A00;font-size:12px;}
.help{color:var(--muted);font-size:12px;margin-top:6px;}
.notice{color:var(--muted);font-size:12px;margin-top:6px;}
.effects-row{display:grid;grid-template-columns:64px 1fr 1fr 1fr 1fr;grid-template-areas:"range a b speed intensity";gap:10px;align-items:end;margin:10px 0;padding:12px;background:#F8FAFB;border:1px solid var(--border);border-radius:12px;}
.effects-row .range-label{grid-area:range;font-weight:700;color:var(--muted);align-self:center;}
.effects-row .field-a{grid-area:a;}
.effects-row .field-b{grid-area:b;}
.effects-row .field-speed{grid-area:speed;}
.effects-row .field-intensity{grid-area:intensity;}
details.section > summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between;font-weight:600;}
details.section > summary::-webkit-details-marker{display:none;}
details.section > summary::after{content:'+';font-weight:700;color:var(--muted);}
details.section[open] > summary::after{content:'-';}
.section-body{margin-top:10px;}
.action-bar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}
.mode-row{display:flex;flex-wrap:wrap;gap:10px;align-items:end;}
.field-inline{min-width:180px;}
.session-card{margin:8px 0;}
.track-controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-bottom:10px;}
.track-canvas{width:100%;height:220px;border:1px solid var(--border);border-radius:12px;background:#F8FAFB;}
@media (max-width:760px){.grid-2,.grid-3{grid-template-columns:1fr;}.effects-row{grid-template-columns:52px 1fr;grid-template-areas:"range a" "range b" "range speed" "range intensity";}.effects-row .range-label{align-self:start;padding-top:4px;}.hero-top{flex-direction:column;align-items:flex-start;}}
@media (prefers-reduced-motion:reduce){*{animation:none !important;transition:none !important;}}
)CSS";
} // namespace

String web_pages::html_page() {
  String page;
  page.reserve(12000);
  page += F(R"HTML(
<!doctype html>
<html>
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
  <div class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <div class="brand">DOG-RGB</div>
          <div class="tagline">Collar inteligente de seguridad</div>
        </div>
        <div class="chips">
          <span class="pill" id="pill-gps">GPS: --</span>
          <span class="pill" id="pill-wifi">Wi-Fi: --</span>
          <span class="pill" id="pill-mode">Modo: --</span>
          <span class="pill" id="pill-home">Home: --</span>
        </div>
      </div>
      <div class="mode-row">
        <div class="field-inline">
          <label class="muted">Modo</label>
          <select id="mode_select">
            <option value="speed">Velocidad</option>
            <option value="geofence">Geocerca</option>
            <option value="simple">Simple</option>
            <option value="show">Show</option>
          </select>
        </div>
        <button class="btn" onclick="saveMode()">Aplicar</button>
        <span class="muted" id="mode_status"></span>
      </div>
    </div>

    <div class="grid grid-2 section">
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
    </div>

    <div class="card section">
      <h2>Sesiones</h2>
      <div id="session-current"></div>
      <div id="history"></div>
    </div>

    <div class="card section">
      <div class="track-controls">
        <h2>Ruta</h2>
        <select id="track_session">
          <option value="current">Sesion actual</option>
          <option value="0">Sesion 1 (ultima)</option>
          <option value="1">Sesion 2</option>
          <option value="2">Sesion 3</option>
        </select>
        <a class="btn ghost" id="track_csv" href="/api/track.csv">Export CSV</a>
        <a class="btn ghost" id="track_geo" href="/api/track.geojson">Export GeoJSON</a>
      </div>
      <canvas id="track_map" class="track-canvas"></canvas>
      <div class="muted" id="track_status">Ruta: --</div>
    </div>

    <div class="actions">
      <button class="btn" onclick="refreshAll()">Actualizar</button>
      <button class="btn ghost" id="home_btn" onclick="updateHome()" style="display:none">Actualizar Home</button>
      <a class="btn ghost" href="/config">Config</a>
      <a class="btn ghost" href="/wifi">Wi-Fi</a>
      <a class="btn ghost" href="/dev">Dev</a>
    </div>
    <div class="muted" id="status">Estado: --</div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);
    const modeSelect = $('mode_select');
    const homeBtn = $('home_btn');
    const modeStatus = $('mode_status');
    const trackCanvas = $('track_map');
    const trackSession = $('track_session');
    const trackStatus = $('track_status');
    const trackCsv = $('track_csv');
    const trackGeo = $('track_geo');
    function minToTime(m){var h=Math.floor(m/60);var mm=m%60;return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0');}
    function cmpsToKph(v){return (v*0.036).toFixed(1);}
    function fmtDate(d){if(!d){return '--';}var s=String(d);if(s.length!==8){return s;}return s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8);}
    function yyyymmddToDate(d){if(!d){return '--/--';}var y=Math.floor(d/10000);var m=Math.floor((d%10000)/100);var day=d%100;return String(day).padStart(2,'0')+'/'+String(m).padStart(2,'0');}
    function formatDuration(s){var h=Math.floor(s/3600);var m=Math.floor((s%3600)/60);return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0');}
    function hasFlag(flags,bit){return (flags&(1<<bit))!==0;}
    function setPill(id,text,tone){var el=$(id);el.textContent=text;el.className='pill'+(tone?(' '+tone):'');}

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

    function clearTrack(msg){
      if(trackCanvas){
        const ctx=trackCanvas.getContext('2d');
        const dpr=window.devicePixelRatio||1;
        const rect=trackCanvas.getBoundingClientRect();
        ctx.setTransform(dpr,0,0,dpr,0,0);
        ctx.clearRect(0,0,rect.width,rect.height);
      }
      if(trackStatus) trackStatus.textContent=msg||'Ruta: --';
    }

    function drawTrack(points,bbox){
      if(!trackCanvas){return;}
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
      ctx.strokeStyle='#0B1220';
      ctx.lineWidth=2;
      ctx.stroke();

      if(points.length>0){
        const start=points[0];
        const end=points[points.length-1];
        const sx=pad+(start[1]-minLon)*scale;
        const sy=rect.height-(pad+(start[0]-minLat)*scale);
        const ex=pad+(end[1]-minLon)*scale;
        const ey=rect.height-(pad+(end[0]-minLat)*scale);
        ctx.fillStyle='#00D1C1';
        ctx.beginPath();ctx.arc(sx,sy,4,0,Math.PI*2);ctx.fill();
        ctx.fillStyle='#E84545';
        ctx.beginPath();ctx.arc(ex,ey,4,0,Math.PI*2);ctx.fill();
      }
    }

    async function loadTrack(){
      if(!trackSession){return;}
      const session=trackSession.value||'current';
      setTrackLinks(session);
      if(trackStatus) trackStatus.textContent='Ruta: cargando...';
      try{
        const r=await fetch('/api/track?session='+session+'&max_points=400');
        const d=await r.json();
        if(!d||!d.count||!d.points||d.points.length<2){
          clearTrack('Ruta: sin datos');
          return;
        }
        if(!d.bbox){
          clearTrack('Ruta: sin bbox');
          return;
        }
        drawTrack(d.points,d.bbox);
        var sDate=fmtDate(d.start_date);
        var eDate=fmtDate(d.end_date||d.start_date);
        var sTime=minToTime(d.start_min||0);
        var eTime=minToTime(d.end_min||0);
        if(trackStatus) trackStatus.textContent='Ruta: '+d.count+' pts | '+sDate+' '+sTime+' a '+eDate+' '+eTime;
      }catch(e){
        clearTrack('Ruta: error');
      }
    }

    function renderSummary(d){
      if(!d||!d.has_data){
        $('status').textContent='Estado: Sin datos';
        $('dist').textContent='--';
        $('avg').textContent='--';
        $('max').textContent='--';
        $('date').textContent='--';
        $('updated').textContent='Ultima lectura: --';
        return;
      }
      $('dist').textContent=(d.distance_m/1000).toFixed(2);
      $('avg').textContent=cmpsToKph(d.avg_speed_cmps);
      $('max').textContent=cmpsToKph(d.max_speed_cmps);
      $('date').textContent=fmtDate(d.date);
      $('updated').textContent='Ultima lectura: '+minToTime(d.last_update_min);
      if (d.gps_fix) {
        $('status').textContent='Estado: GPS OK';
      } else if (d.gps_raw_fix) {
        $('status').textContent='Estado: GPS no confiable';
      } else {
        $('status').textContent='Estado: Sin GPS';
      }
    }

    function renderSessionCard(label,s){
      if(!s){return '';}
      var flags=s.flags||0;
      var noFix=hasFlag(flags,3)||!hasFlag(flags,0);
      if(noFix){
        return "<div class='card session-card'><div class='label'>"+label+"</div><div>Sin GPS</div></div>";
      }
      var startDate=yyyymmddToDate(s.start_date);
      var startTime=minToTime(s.start_min||0);
      var endDate=yyyymmddToDate(s.end_date||s.start_date);
      var endTime=minToTime(s.end_min||s.start_min||0);
      var distKm=(s.distance_m/1000).toFixed(2);
      var avg=cmpsToKph(s.avg_speed_cmps||0);
      var max=cmpsToKph(s.max_speed_cmps||0);
      var active=formatDuration(s.active_s||0);
      return "<div class='card session-card'><div class='label'>"+label+"</div>"+
             "<div class='muted'>"+startDate+" "+startTime+" a "+endDate+" "+endTime+"</div>"+
             "<div>Distancia: <strong>"+distKm+"</strong> km</div>"+
             "<div class='muted'>Tiempo activo: "+active+"</div>"+
             "<div class='muted'>Vel. prom: "+avg+" km/h | Vel. max: "+max+" km/h</div></div>";
    }

    function renderSessionCurrent(s){
      var el=$('session-current');
      if(!el){return;}
      if(!s){
        el.innerHTML="<div class='card session-card'>Sesion actual: --</div>";
        return;
      }
      el.innerHTML=renderSessionCard('Sesion actual',s);
    }

    function renderHistory(list){
      var el=$('history');
      if(!el){return;}
      if(!list||list.length===0){
        el.innerHTML="<div class='card session-card'>Sin historial</div>";
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

      var modeText='Modo: '+(s.mode||'--');
      setPill('pill-mode',modeText,'');
      if (s.mode && document.activeElement !== modeSelect){
        modeSelect.value = s.mode;
      }
      if (homeBtn){
        const showHome = (s.mode === 'geofence');
        homeBtn.style.display = showHome ? 'inline-flex' : 'none';
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

    async function saveMode(){
      modeStatus.textContent='Guardando...';
      try{
        const payload={mode:modeSelect.value};
        const r=await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)}).then(r=>r.json());
        if (r.status === 'ok'){
          modeStatus.textContent='OK';
          loadStatus();
        } else {
          modeStatus.textContent='Error';
        }
      }catch(e){
        modeStatus.textContent='Error';
      }
    }

    function refreshAll(){loadSummary();loadStatus();loadTrack();}
    async function updateHome(){
      if (homeBtn) homeBtn.disabled = true;
      try{
        const r = await fetch('/api/home/set',{method:'POST'}).then(r=>r.json());
        if (r.status === 'ok'){
          loadStatus();
        }
      }catch(e){}
      if (homeBtn) homeBtn.disabled = false;
    }
    if (trackSession){
      trackSession.addEventListener('change',loadTrack);
    }
    window.addEventListener('resize',resizeTrackCanvas);
    resizeTrackCanvas();
    refreshAll();
    setInterval(loadStatus,5000);
    setInterval(loadSummary,10000);
    setInterval(loadTrack,15000);
  </script>
</body>
</html>
)HTML");
  return page;
}
String web_pages::html_wifi_page() {
  String page;
  page.reserve(9000);
  page += F(R"HTML(
<!doctype html>
<html>
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
  <div class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <div class="brand">DOG-RGB</div>
          <div class="tagline">Configurar Wi-Fi</div>
        </div>
      </div>
      <div class="muted">Conecta el collar a tu red de casa.</div>
    </div>

    <form class="card section" id="sta_form" method="post" action="/api/wifi">
      <div class="field">
        <label>SSID</label>
        <input name="ssid" value=")HTML");
  page += wifi_mgr::ssid();
  page += F(R"HTML(">
      </div>
      <div class="field">
        <label>Password</label>
        <input name="pass" id="pass" type="password" placeholder="Password">
      </div>
      <label class="muted"><input type="checkbox" id="show_pass"> Mostrar password</label>
      <div class="actions">
        <button class="btn" type="submit">Guardar y conectar</button>
      </div>
      <div id="sta_status" class="notice"></div>
    </form>

    <div class="card section" id="ap_block">
      <h2>Wi-Fi AP</h2>
      <div class="muted">Configura el hotspot del collar.</div>
      <div class="grid grid-2 section-body">
        <div class="field"><label>SSID</label><input id="ap_ssid" type="text"></div>
        <div class="field"><label>mDNS</label><input id="mdns" type="text"></div>
      </div>
      <div class="grid grid-2">
        <div class="field"><label>Password</label><input id="ap_pass" type="password" placeholder="(sin cambio)"></div>
        <div class="field">
          <label>AP abierto</label>
          <label class="muted"><input id="ap_open" type="checkbox"> Sin password</label>
        </div>
      </div>
      <div id="ap_hint" class="muted"></div>
      <div id="ap_warn" class="warn"></div>
      <div class="actions">
        <button class="btn" type="button" onclick="saveAp()">Guardar AP</button>
      </div>
      <div id="ap_status" class="muted"></div>
    </div>

    <div class="actions">
      <a class="btn ghost" href="/">Volver</a>
    </div>
  </div>

  <script>
    const pass = document.getElementById('pass');
    const show = document.getElementById('show_pass');
    show.onchange = () => { pass.type = show.checked ? 'text' : 'password'; };
    const staForm = document.getElementById('sta_form');
    const staStatus = document.getElementById('sta_status');
    const apSsid = document.getElementById('ap_ssid');
    const mdns = document.getElementById('mdns');
    const apPass = document.getElementById('ap_pass');
    const apOpen = document.getElementById('ap_open');
    const apHint = document.getElementById('ap_hint');
    const apWarn = document.getElementById('ap_warn');
    const apStatus = document.getElementById('ap_status');
    let apHasPass = false;
    let initialAp = null;
    let baseCfg = null;

    function setApStatus(msg, tone){
      apStatus.textContent = msg || '';
      if (tone === 'error') apStatus.className = 'error';
      else if (tone === 'warn') apStatus.className = 'warn';
      else apStatus.className = 'muted';
    }

    function clearApInvalid(){
      [apSsid, apPass, mdns].forEach(el => el && el.classList.remove('invalid'));
    }

    function apChanged(){
      if (!initialAp) return false;
      return apSsid.value.trim() !== initialAp.ap_ssid ||
             mdns.value.trim() !== initialAp.mdns ||
             apOpen.checked !== initialAp.ap_open ||
             apPass.value !== '';
    }

    function updateApState(){
      if (apOpen.checked){
        apPass.value = '';
        apPass.disabled = true;
        apHint.innerText = 'AP abierto';
      } else {
        apPass.disabled = false;
        apHint.innerText = apHasPass ? 'Password configurada' : 'Sin password';
      }
      apWarn.innerText = apChanged() ? 'Nota: cambiar AP puede desconectar la sesion.' : '';
    }

    async function loadConfig(){
      try{
        baseCfg = await fetch('/api/config').then(r=>r.json());
        apSsid.value = baseCfg.wifi.ap_ssid || '';
        mdns.value = baseCfg.wifi.mdns || '';
        apHasPass = !!baseCfg.wifi.has_ap_pass;
        apOpen.checked = !apHasPass;
        initialAp = { ap_ssid: apSsid.value.trim(), mdns: mdns.value.trim(), ap_open: apOpen.checked };
        updateApState();
      }catch(e){
        setApStatus('Error cargando config.', 'error');
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
        setApStatus('SSID 1..32.', 'error');
        return;
      }
      if (!apOpen.checked && passVal.length > 0 && passVal.length < 8){
        apPass.classList.add('invalid');
        setApStatus('Password >= 8.', 'error');
        return;
      }
      if (!validMdns(mdnsVal)){
        mdns.classList.add('invalid');
        setApStatus('mDNS invalido (1..32 a-z0-9-).', 'error');
        return;
      }
      if (apChanged()){
        if (!confirm('Guardar cambios? El AP puede reiniciarse.')) return;
      }
      setApStatus('Guardando...', 'muted');
      baseCfg.wifi = baseCfg.wifi || {};
      baseCfg.wifi.ap_ssid = ssid;
      baseCfg.wifi.ap_open = apOpen.checked;
      baseCfg.wifi.ap_pass = passVal;
      baseCfg.wifi.mdns = mdnsVal;
      try{
        const r = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(baseCfg)}).then(r=>r.json());
        if (r.status !== 'ok'){
          setApStatus('Error', 'error');
          return;
        }
        setApStatus(r.status + (r.wifi_restart ? ' (reiniciando AP)' : ''), 'muted');
        initialAp = { ap_ssid: ssid, mdns: mdnsVal, ap_open: apOpen.checked };
        if (apOpen.checked) apHasPass = false;
        else if (passVal.length >= 8) apHasPass = true;
        apPass.value = '';
        updateApState();
      }catch(e){
        setApStatus('Error', 'error');
      }
    }

    staForm.onsubmit = async (e) => {
      e.preventDefault();
      staStatus.textContent = 'Guardando...';
      try{
        const fd = new FormData(staForm);
        const r = await fetch('/api/wifi',{method:'POST',body:fd});
        const text = await r.text();
        staStatus.textContent = r.ok ? 'Guardado, conectando...' : ('Error: ' + text);
      }catch(e){
        staStatus.textContent = 'Error';
      }
    };

    apOpen.onchange = updateApState;
    apPass.oninput = updateApState;
    apSsid.oninput = updateApState;
    mdns.oninput = updateApState;
    loadConfig();
  </script>
</body>
</html>
)HTML");
  return page;
}
String web_pages::html_config_page() {
  String page;
  page.reserve(16000);
  page += F(R"CFG(
<!doctype html>
<html>
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
  <div class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <div class="brand">DOG-RGB</div>
          <div class="tagline">Configuracion avanzada</div>
        </div>
      </div>
      <div class="muted">Ajustes de LED y geofence</div>
    </div>

    <div id="errors" class="card error-box error section"></div>

    <div class="card action-bar section">
      <button class="btn" type="button" onclick="saveCfg()">Guardar</button>
      <button class="btn ghost" type="button" onclick="resetCfg()">Restaurar defaults</button>
      <span id="status" class="muted"></span>
    </div>

    <details class="card section" id="common_block" open>
      <summary>Comun</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label>Brightness (1..255)</label>
            <input id="brightness" type="number" min="1" max="255">
          </div>
          <div class="field">
            <label>Modo</label>
            <select id="mode">
              <option value="speed">Velocidad</option>
              <option value="geofence">Geocerca</option>
              <option value="simple">Simple</option>
              <option value="show">Show</option>
            </select>
          </div>
        </div>
        <div id="mode_help" class="help"></div>
      </div>
    </details>

    <details class="card section" id="speed_block" open>
      <summary>Speed ranges (kph)</summary>
      <div class="section-body">
        <div class="grid grid-3">
          <div class="field"><label>R1</label><input id="r1" type="number" step="0.1"></div>
          <div class="field"><label>R2</label><input id="r2" type="number" step="0.1"></div>
          <div class="field"><label>R3</label><input id="r3" type="number" step="0.1"></div>
          <div class="field"><label>R4</label><input id="r4" type="number" step="0.1"></div>
          <div class="field"><label>R5</label><input id="r5" type="number" step="0.1"></div>
          <div class="field"><label>R6</label><input id="r6" type="number" step="0.1"></div>
          <div class="field"><label>R7</label><input id="r7" type="number" step="0.1"></div>
          <div class="field"><label>R8</label><input id="r8" type="number" step="0.1"></div>
          <div class="field"><label>R9</label><input id="r9" type="number" step="0.1"></div>
        </div>
        <div class="help">R10 es mayor que R9.</div>
      </div>
    </details>

    <details class="card section" id="geofence_block" open>
      <summary>Geofence</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label>Distancia maxima (m)</label>
            <input id="fence_max" type="number" min="50" max="5000">
          </div>
          <div class="field">
            <label>Rangos</label>
            <div id="fence_ranges" class="muted"></div>
          </div>
        </div>
        <div class="row" style="margin-top:8px">
          <button class="btn" type="button" onclick="setHome()">Nuevo Home (GPS actual)</button>
          <button class="btn ghost" type="button" onclick="clearHome()">Clear Home</button>
        </div>
        <div id="home_status" class="muted"></div>
      </div>
    </details>

    <details class="card section" id="gps_block" open>
      <summary>GPS calidad</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label>Fix quality min (0..8)</label>
            <input id="gps_min_fix" type="number" min="0" max="8">
          </div>
          <div class="field">
            <label>Satellites min (3..12)</label>
            <input id="gps_min_sats" type="number" min="3" max="12">
          </div>
        </div>
        <div class="grid grid-2">
          <div class="field">
            <label>HDOP max (0.5..20)</label>
            <input id="gps_max_hdop" type="number" step="0.1" min="0.5" max="20">
          </div>
          <div class="field">
            <label>Max age GGA (ms)</label>
            <input id="gps_max_gga_age" type="number" step="100" min="500" max="10000">
          </div>
        </div>
        <div class="grid grid-3">
          <div class="field">
            <label>Min segment (m)</label>
            <input id="gps_min_segment" type="number" step="0.1" min="0.5" max="20">
          </div>
          <div class="field">
            <label>HDOP factor</label>
            <input id="gps_hdop_factor" type="number" step="0.1" min="0" max="5">
          </div>
          <div class="field">
            <label>Max min segment (m)</label>
            <input id="gps_max_min_segment" type="number" step="0.1" min="1" max="50">
          </div>
        </div>
        <div class="help">Solo se aceptan puntos con calidad suficiente para sumar distancia/tiempo.</div>
      </div>
    </details>

    <details class="card section" id="simple_block" open>
      <summary>Simple</summary>
      <div class="section-body">
        <div class="grid grid-2">
          <div class="field">
            <label>Tema</label>
            <select id="simple_theme">
              <option value="manual">Manual</option>
              <option value="calm">Calm</option>
              <option value="active">Active</option>
              <option value="sport">Sport</option>
              <option value="aurora">Aurora</option>
            </select>
          </div>
          <div class="field">
            <label>Efecto</label>
            <select id="simple_effect"></select>
          </div>
        </div>
        <div class="grid grid-2">
          <div class="field"><label>Speed (0..255)</label><input id="simple_speed" type="number" min="0" max="255"></div>
          <div class="field"><label>Intensity (0..255)</label><input id="simple_intensity" type="number" min="0" max="255"></div>
        </div>
        <div class="grid grid-3">
          <div class="field"><label>R</label><input id="simple_r" type="number" min="0" max="255"></div>
          <div class="field"><label>G</label><input id="simple_g" type="number" min="0" max="255"></div>
          <div class="field"><label>B</label><input id="simple_b" type="number" min="0" max="255"></div>
        </div>
        <div class="help">RAINBOW, GRADIENT_WAVE y FIRE ignoran el color base.</div>
      </div>
    </details>

    <details class="card section" id="show_block" open>
      <summary>Show</summary>
      <div class="section-body">
        <div class="help">Modo demo: rota efectos automaticamente. No hay parametros.</div>
      </div>
    </details>

    <details class="card section" id="effects_block" open>
      <summary>Efectos por rango (1-10)</summary>
      <div class="section-body">
        <div id="effects"></div>
      </div>
    </details>

    <div class="section">
      <a class="btn ghost" href="/">Volver</a>
    </div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);
    const effectsDiv = $('effects');
    const modeEl = $('mode');
    const speedBlock = $('speed_block');
    const geofenceBlock = $('geofence_block');
    const simpleBlock = $('simple_block');
    const showBlock = $('show_block');
    const effectsBlock = $('effects_block');
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
    const statusEl = $('status');
    const errorsEl = $('errors');

    const rangeInputs = [ $('r1'),$('r2'),$('r3'),$('r4'),$('r5'),$('r6'),$('r7'),$('r8'),$('r9') ];

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

    const MODE_HELP = {
      speed:'Usa rangos de velocidad para elegir efectos.',
      geofence:'Usa distancia al Home. Requiere GPS y home definido.',
      simple:'Un efecto unico para toda la tira.',
      show:'Demo automatico de efectos.'
    };

    const ERROR_MAP = {
      brightness:{field:'brightness',msg:'Brillo fuera de rango (1..255).'},
      mode:{field:'mode',msg:'Modo invalido.'},
      fence_max:{field:'fence_max',msg:'Distancia geofence 50..5000.'},
      ranges:{field:'speed_block',msg:'Rangos requeridos.'},
      'ranges value':{field:'speed_block',msg:'Rangos deben ser > 0.'},
      'ranges order':{field:'speed_block',msg:'Rangos deben ser ascendentes.'},
      effects:{field:'effects_block',msg:'Efectos incompletos.'},
      'effect values':{field:'effects_block',msg:'Valores de efecto invalidos.'},
      'effect id':{field:'effects_block',msg:'ID de efecto invalido.'},
      single:{field:'simple_block',msg:'Bloque simple invalido.'},
      'single values':{field:'simple_block',msg:'Valores simple invalidos.'},
      gps:{field:'gps_block',msg:'Parametros GPS invalidos.'}
    };

    function fillEffectSelect(sel){
      sel.innerHTML = EFFECTS.map(e => `<option value="${e.id}">${e.name}</option>`).join('');
    }

    function buildEffectsTable(){
      let html = '';
      for (let i=1;i<=10;i++){
        html += `<div class="effects-row">
          <div class="range-label">R${i}</div>
          <div class="field field-a"><label>A</label><select id="e${i}a"></select></div>
          <div class="field field-b"><label>B</label><select id="e${i}b"></select></div>
          <div class="field field-speed"><label>Speed</label><input id="e${i}s" type="number" min="0" max="255"></div>
          <div class="field field-intensity"><label>Intensity</label><input id="e${i}i" type="number" min="0" max="255"></div>
        </div>`;
      }
      effectsDiv.innerHTML = html;
      for (let i=1;i<=10;i++){
        fillEffectSelect($('e'+i+'a'));
        fillEffectSelect($('e'+i+'b'));
      }
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
      speedBlock.style.display = (mode === 'speed') ? 'block' : 'none';
      geofenceBlock.style.display = (mode === 'geofence') ? 'block' : 'none';
      simpleBlock.style.display = (mode === 'simple') ? 'block' : 'none';
      showBlock.style.display = (mode === 'show') ? 'block' : 'none';
      effectsBlock.style.display = (mode === 'speed' || mode === 'geofence') ? 'block' : 'none';
      $('mode_help').innerText = MODE_HELP[mode] || '';
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

    function isStrictAscending(arr){
      for (let i=1;i<arr.length;i++){
        if (!(arr[i] > arr[i-1])) return false;
      }
      return true;
    }

    function clearErrors(){
      errorsEl.innerHTML = '';
      document.querySelectorAll('.invalid').forEach(el=>el.classList.remove('invalid'));
    }

    function showErrors(list){
      if (!list.length) return;
      errorsEl.innerHTML = list.map(msg=>`<div>${msg}</div>`).join('');
    }

    function validateConfig(cfg){
      const errs = [];
      function addError(field,msg){
        errs.push(msg);
        const el = $(field);
        if (el) el.classList.add('invalid');
      }

      if (cfg.led.brightness < 1 || cfg.led.brightness > 255) addError('brightness','Brillo fuera de rango (1..255).');

      const ranges = cfg.speed_ranges_kph;
      if (ranges.length !== 9) addError('speed_block','Rangos requeridos.');
      for (let i=0;i<ranges.length;i++){
        if (!(ranges[i] > 0)){ addError('speed_block','Rangos deben ser > 0.'); break; }
      }
      if (!isStrictAscending(ranges)) addError('speed_block','Rangos deben ser ascendentes.');

      if (cfg.fence_max_m < 50 || cfg.fence_max_m > 5000) addError('fence_max','Distancia geofence 50..5000.');

      const g = cfg.gps || {};
      if (g.min_fix_quality < 0 || g.min_fix_quality > 8) addError('gps_block','Fix quality min invalido.');
      if (g.min_sats < 3 || g.min_sats > 12) addError('gps_block','Satellites min invalido.');
      if (!(g.max_hdop >= 0.5 && g.max_hdop <= 20)) addError('gps_block','HDOP max invalido.');
      if (g.max_gga_age_ms < 500 || g.max_gga_age_ms > 10000) addError('gps_block','Max age GGA invalido.');
      if (!(g.min_segment_m >= 0.5 && g.min_segment_m <= 20)) addError('gps_block','Min segment invalido.');
      if (!(g.hdop_factor >= 0 && g.hdop_factor <= 5)) addError('gps_block','HDOP factor invalido.');
      if (!(g.max_min_segment_m >= 1 && g.max_min_segment_m <= 50)) addError('gps_block','Max min segment invalido.');
      if (g.min_segment_m > g.max_min_segment_m) addError('gps_block','Min segment > max.');

      for (let i=1;i<=10;i++){
        const e = cfg.effects['range'+i];
        if (!e){ addError('effects_block','Efectos incompletos.'); break; }
        if (e.a < 0 || e.a > 11 || e.b < 0 || e.b > 11) addError('effects_block','ID de efecto invalido.');
        if (e.speed < 0 || e.speed > 255 || e.intensity < 0 || e.intensity > 255) addError('effects_block','Valores de efecto invalidos.');
      }

      const s = cfg.single;
      if (s.effect < 0 || s.effect > 11) addError('simple_block','Efecto simple invalido.');
      if (s.speed < 0 || s.speed > 255) addError('simple_block','Speed simple invalido.');
      if (s.intensity < 0 || s.intensity > 255) addError('simple_block','Intensity simple invalido.');
      if (s.rgb.r < 0 || s.rgb.r > 255 || s.rgb.g < 0 || s.rgb.g > 255 || s.rgb.b < 0 || s.rgb.b > 255) {
        addError('simple_block','RGB simple invalido.');
      }

      if (errs.length) showErrors(errs);
      return errs.length === 0;
    }

    function handleBackendError(reason){
      const e = ERROR_MAP[reason];
      if (!e){
        showErrors(['Error guardando.']);
        return;
      }
      const el = $(e.field);
      if (el) el.classList.add('invalid');
      showErrors([e.msg]);
    }

    function buildPayload(){
      const cfg = {
        version:4,
        mode: modeEl.value,
        fence_max_m: intVal(fenceMax,300),
        led:{brightness: intVal(brightness,1)},
        gps:{
          min_fix_quality: intVal(gpsMinFix,1),
          min_sats: intVal(gpsMinSats,6),
          max_hdop: floatVal(gpsMaxHdop,2.5),
          max_gga_age_ms: intVal(gpsMaxGgaAge,2000),
          min_segment_m: floatVal(gpsMinSegment,3.0),
          hdop_factor: floatVal(gpsHdopFactor,2.0),
          max_min_segment_m: floatVal(gpsMaxMinSegment,10.0)
        },
        speed_ranges_kph: rangeInputs.map(el=>floatVal(el,0)),
        effects:{},
        single:{
          effect: intVal(simpleEffect,0),
          speed: intVal(simpleSpeed,0),
          intensity: intVal(simpleIntensity,0),
          rgb:{r:intVal(simpleR,0), g:intVal(simpleG,0), b:intVal(simpleB,0)}
        }
      };
      for (let i=1;i<=10;i++){
        cfg.effects['range'+i] = {
          a: intVal($('e'+i+'a'),0),
          b: intVal($('e'+i+'b'),0),
          speed: intVal($('e'+i+'s'),0),
          intensity: intVal($('e'+i+'i'),0)
        };
      }
      return cfg;
    }

    async function saveCfg(){
      clearErrors();
      const cfg = buildPayload();
      if (!validateConfig(cfg)) return;
      statusEl.innerText = 'Guardando...';
      try{
        const r = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)}).then(r=>r.json());
        if (r.status !== 'ok'){
          handleBackendError(r.reason);
          statusEl.innerText = 'Error';
          return;
        }
        statusEl.innerText = r.status + (r.wifi_restart ? ' (reiniciando AP)' : '');
      }catch(e){
        statusEl.innerText = 'Error';
      }
    }

    function setHome(){
      fetch('/api/home/set',{method:'POST'}).then(r=>r.json()).then(r=>{
        homeStatus.innerText = r.status==='ok' ? 'Home actualizado' : 'Home error';
        loadHome();
      }).catch(()=>{homeStatus.innerText='Home error';});
    }

    function clearHome(){
      fetch('/api/home/clear',{method:'POST'}).then(r=>r.json()).then(r=>{
        homeStatus.innerText = r.status==='ok' ? 'Home borrado' : '';
        loadHome();
      }).catch(()=>{homeStatus.innerText='Home error';});
    }

    function resetCfg(){
      if (!confirm('Restaurar defaults y reiniciar AP si aplica?')) return;
      fetch('/api/config/reset',{method:'POST'}).then(r=>r.json()).then(r=>{
        statusEl.innerText = r.status;
      }).catch(()=>{statusEl.innerText='error';});
    }

    modeEl.onchange = updateModeVisibility;
    simpleTheme.onchange = () => { if (simpleTheme.value !== 'manual') applyTheme(simpleTheme.value); updateThemeSelection(); };
    [simpleEffect,simpleSpeed,simpleIntensity,simpleR,simpleG,simpleB].forEach(el=>el.oninput=updateThemeSelection);
    fenceMax.oninput = updateFenceRanges;

    buildEffectsTable();
    fillEffectSelect(simpleEffect);

    fetch('/api/config').then(r=>r.json()).then(c=>{
      brightness.value = c.led.brightness;
      modeEl.value = c.mode || 'speed';
      fenceMax.value = c.fence_max_m || 300;
      const g = c.gps || {};
      gpsMinFix.value = (g.min_fix_quality !== undefined ? g.min_fix_quality : 1);
      gpsMinSats.value = (g.min_sats !== undefined ? g.min_sats : 6);
      gpsMaxHdop.value = (g.max_hdop !== undefined ? g.max_hdop : 2.5);
      gpsMaxGgaAge.value = (g.max_gga_age_ms !== undefined ? g.max_gga_age_ms : 2000);
      gpsMinSegment.value = (g.min_segment_m !== undefined ? g.min_segment_m : 3.0);
      gpsHdopFactor.value = (g.hdop_factor !== undefined ? g.hdop_factor : 2.0);
      gpsMaxMinSegment.value = (g.max_min_segment_m !== undefined ? g.max_min_segment_m : 10.0);
      for (let i=0;i<9;i++){ rangeInputs[i].value = c.speed_ranges_kph[i]; }
      for (let i=1;i<=10;i++){
        const e = c.effects['range'+i];
        $('e'+i+'a').value = e.a;
        $('e'+i+'b').value = e.b;
        $('e'+i+'s').value = e.speed;
        $('e'+i+'i').value = e.intensity;
      }
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
      loadHome();
    });
  </script>
</body>
</html>
)CFG");
  return page;
}

String web_pages::html_dev_page() {
  String page;
  page.reserve(14000);
  page += F(R"DEV(
<!doctype html>
<html>
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
  <div class="container">
    <div class="hero card">
      <div class="hero-top">
        <div>
          <div class="brand">DOG-RGB</div>
          <div class="tagline">Developer Console</div>
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
    </div>

    <div class="card section">
      <h2>System</h2>
      <div class="grid grid-2">
        <div class="field"><label>Uptime</label><div class="data mono" id="dev-uptime">--</div></div>
        <div class="field"><label>Build</label><div class="data mono" id="dev-build">--</div></div>
        <div class="field"><label>Heap libre</label><div class="data mono" id="dev-heap">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Wi-Fi</h2>
      <div class="grid grid-2">
        <div class="field"><label>Modo</label><div class="data mono" id="wifi-mode">--</div></div>
        <div class="field"><label>STA</label><div class="data mono" id="wifi-sta">--</div></div>
        <div class="field"><label>AP</label><div class="data mono" id="wifi-ap">--</div></div>
        <div class="field"><label>Stations</label><div class="data mono" id="wifi-stations">--</div></div>
        <div class="field"><label>Wi-Fi Off</label><div class="data mono" id="wifi-off">--</div></div>
        <div class="field"><label>SSID AP</label><div class="data mono" id="wifi-ssid">--</div></div>
        <div class="field"><label>mDNS</label><div class="data mono" id="wifi-mdns">--</div></div>
        <div class="field"><label>STA IP</label><div class="data mono" id="wifi-sta-ip">--</div></div>
        <div class="field"><label>AP IP</label><div class="data mono" id="wifi-ap-ip">--</div></div>
        <div class="field"><label>RSSI</label><div class="data mono" id="wifi-rssi">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>GPS</h2>
      <div class="grid grid-2">
        <div class="field"><label>Fix</label><div class="data mono" id="gps-fix">--</div></div>
        <div class="field"><label>Fix actual</label><div class="data mono" id="gps-current-fix">--</div></div>
        <div class="field"><label>Raw fix</label><div class="data mono" id="gps-raw-fix">--</div></div>
        <div class="field"><label>Trusted fix</label><div class="data mono" id="gps-trusted-fix">--</div></div>
        <div class="field"><label>Sats</label><div class="data mono" id="gps-sats">--</div></div>
        <div class="field"><label>Fix quality</label><div class="data mono" id="gps-fix-quality">--</div></div>
        <div class="field"><label>HDOP</label><div class="data mono" id="gps-hdop">--</div></div>
        <div class="field"><label>Quality OK</label><div class="data mono" id="gps-quality-ok">--</div></div>
        <div class="field"><label>Speed (kph)</label><div class="data mono" id="gps-speed">--</div></div>
        <div class="field"><label>Lat</label><div class="data mono" id="gps-lat">--</div></div>
        <div class="field"><label>Lon</label><div class="data mono" id="gps-lon">--</div></div>
        <div class="field"><label>Fecha</label><div class="data mono" id="gps-date">--</div></div>
        <div class="field"><label>Ult update</label><div class="data mono" id="gps-update">--</div></div>
        <div class="field"><label>Age last byte</label><div class="data mono" id="gps-age-byte">--</div></div>
        <div class="field"><label>Age last fix</label><div class="data mono" id="gps-age-fix">--</div></div>
        <div class="field"><label>Bytes RX</label><div class="data mono" id="gps-bytes">--</div></div>
        <div class="field"><label>Sentences RX</label><div class="data mono" id="gps-sentences">--</div></div>
        <div class="field"><label>RMC seen</label><div class="data mono" id="gps-rmc-seen">--</div></div>
        <div class="field"><label>RMC valid</label><div class="data mono" id="gps-rmc-valid">--</div></div>
        <div class="field"><label>GGA seen</label><div class="data mono" id="gps-gga-seen">--</div></div>
        <div class="field"><label>Overflow</label><div class="data mono" id="gps-overflow">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>LED</h2>
      <div class="grid grid-2">
        <div class="field"><label>Modo</label><div class="data mono" id="led-mode">--</div></div>
        <div class="field"><label>Brightness</label><div class="data mono" id="led-brightness">--</div></div>
        <div class="field"><label>Rango actual</label><div class="data mono" id="led-range">--</div></div>
        <div class="field"><label>Base RGB</label><div class="data mono" id="led-base">--</div></div>
        <div class="field"><label>Effect A</label><div class="data mono" id="led-effect-a">--</div></div>
        <div class="field"><label>Effect B</label><div class="data mono" id="led-effect-b">--</div></div>
        <div class="field"><label>Range Speed</label><div class="data mono" id="led-range-speed">--</div></div>
        <div class="field"><label>Range Intensity</label><div class="data mono" id="led-range-intensity">--</div></div>
        <div class="field"><label>Simple effect</label><div class="data mono" id="led-simple-effect">--</div></div>
        <div class="field"><label>Simple speed</label><div class="data mono" id="led-simple-speed">--</div></div>
        <div class="field"><label>Simple intensity</label><div class="data mono" id="led-simple-intensity">--</div></div>
        <div class="field"><label>Simple RGB</label><div class="data mono" id="led-simple-rgb">--</div></div>
        <div class="field"><label>Show effect</label><div class="data mono" id="led-show-effect">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Geofence</h2>
      <div class="grid grid-2">
        <div class="field"><label>Home set</label><div class="data mono" id="geo-set">--</div></div>
        <div class="field"><label>Source</label><div class="data mono" id="geo-source">--</div></div>
        <div class="field"><label>Home lat</label><div class="data mono" id="geo-lat">--</div></div>
        <div class="field"><label>Home lon</label><div class="data mono" id="geo-lon">--</div></div>
        <div class="field"><label>Distance (m)</label><div class="data mono" id="geo-dist">--</div></div>
        <div class="field"><label>Range</label><div class="data mono" id="geo-range">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Raw JSON</h2>
      <pre id="dev-json" class="code mono"></pre>
    </div>

    <div class="actions">
      <a class="btn ghost" href="/">Volver</a>
    </div>
  </div>

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
        setText('dev-heap', d.system.free_heap);

        const wifi = d.wifi || {};
        const staState = wifi.sta_connected ? 'connected' : (wifi.sta_connecting ? 'connecting' : 'disconnected');
        setText('wifi-mode', wifi.mode);
        setText('wifi-sta', staState);
        setText('wifi-ap', wifi.ap_enabled ? 'on' : 'off');
        setText('wifi-stations', wifi.ap_stations);
        setText('wifi-off', wifi.wifi_off ? 'yes' : 'no');
        setText('wifi-ssid', wifi.ap_ssid);
        setText('wifi-mdns', wifi.mdns);
        setText('wifi-sta-ip', wifi.sta_ip);
        setText('wifi-ap-ip', wifi.ap_ip);
        setText('wifi-rssi', wifi.rssi);

        const gps = d.gps || {};
        setText('gps-fix', gps.fix ? 'yes' : 'no');
        setText('gps-current-fix', gps.current_fix ? 'yes' : 'no');
        setText('gps-raw-fix', gps.raw_fix ? 'yes' : 'no');
        setText('gps-trusted-fix', gps.trusted_fix ? 'yes' : 'no');
        setText('gps-sats', gps.sats);
        setText('gps-fix-quality', gps.fix_quality);
        setText('gps-hdop', (gps.hdop !== undefined) ? gps.hdop.toFixed(2) : '--');
        setText('gps-quality-ok', gps.quality_ok ? 'yes' : 'no');
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
        setText('gps-overflow', gps.overflow);

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

        const geo = d.geofence || {};
        setText('geo-set', geo.set ? 'yes' : 'no');
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
