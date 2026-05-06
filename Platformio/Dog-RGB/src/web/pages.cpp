#include "web/pages.h"

#include <pgmspace.h>

#include "wifi/wifi_mgr.h"

namespace {
const char BASE_CSS[] PROGMEM = R"CSS(
:root{--bg:#000;--surface:#0A0A0A;--text:#00FF41;--muted:#00882A;--accent:#00FF41;--accent-2:#FFD700;--danger:#FF0055;--border:#003300;--shadow:0 0 10px rgba(0,255,65,0.12);--glow-sm:0 0 4px #00FF41;--glow-md:0 0 8px #00FF41,0 0 16px rgba(0,255,65,0.4);--radius:3px;--space-1:6px;--space-2:10px;--space-3:14px;--space-4:20px;--space-5:28px;--font-mono:"Courier New","Lucida Console","DejaVu Sans Mono",monospace;}
*{box-sizing:border-box;}
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
.primary-metric .value{font-size:42px;font-weight:700;line-height:1;text-shadow:0 0 12px #00FF41,0 0 24px rgba(0,255,65,0.5);}
.stat-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}
.stat{background:#000;border:1px solid var(--border);border-radius:var(--radius);padding:12px;}
.stat .value{font-size:24px;font-weight:700;line-height:1.1;}
.summary-meta{grid-column:1/-1;display:flex;flex-wrap:wrap;gap:10px;color:var(--muted);font-size:12px;}
.empty-state{padding:12px;background:#000;border:1px solid var(--border);border-radius:var(--radius);color:var(--muted);font-size:12px;}
.muted{color:var(--muted);font-size:12px;}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:8px 14px;border-radius:var(--radius);border:1px solid var(--accent);background:transparent;color:var(--accent);font-weight:600;font-size:12px;font-family:var(--font-mono);text-transform:uppercase;letter-spacing:0.06em;cursor:pointer;transition:box-shadow 0.15s,background 0.15s;}
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
.preset-btn{border:1px solid var(--border);border-radius:var(--radius);background:transparent;color:var(--muted);padding:6px 10px;font-family:var(--font-mono);font-size:11px;text-transform:uppercase;cursor:pointer;}
.preset-btn.active{border-color:var(--accent);color:var(--accent);box-shadow:var(--glow-sm);}
.swatch{width:32px;height:32px;border-radius:2px;border:2px solid var(--border);cursor:pointer;}
.swatch.active{border-color:var(--accent);box-shadow:var(--glow-sm);}
.field label{display:block;font-size:11px;color:var(--muted);margin-bottom:6px;text-transform:uppercase;letter-spacing:0.06em;}
input,select{width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:var(--radius);background:#000;font-family:var(--font-mono);font-size:13px;color:var(--text);}
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
details.section > summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between;font-weight:600;text-transform:uppercase;letter-spacing:0.06em;font-size:13px;}
details.section > summary::-webkit-details-marker{display:none;}
details.section > summary::after{content:'[+]';font-weight:700;color:var(--muted);font-size:11px;}
details.section[open] > summary::after{content:'[-]';}
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
@media(max-width:760px){.grid-2,.grid-3,.dashboard-summary,.stat-grid,.mode-cards{grid-template-columns:1fr;}.primary-metric{min-height:auto;}.primary-metric .value{font-size:36px;}.dashboard-actions .btn,.dashboard-actions summary.btn{flex:1 1 130px;}.sticky-actions{margin-left:-20px;margin-right:-20px;padding-left:20px;padding-right:20px;border-radius:0;}.effects-row{grid-template-columns:52px 1fr;grid-template-areas:"range a" "range b" "range speed" "range intensity";}.effects-row .range-label{align-self:start;padding-top:4px;}.hero-top{flex-direction:column;align-items:flex-start;}}
@media(prefers-reduced-motion:reduce){*{animation:none !important;transition:none !important;}}
)CSS";
} // namespace

String web_pages::html_page() {
  String page;
  page.reserve(25000);
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
          <span class="pill" id="pill-day" style="display:none">DIA: --</span>
          <span class="pill" id="pill-home">Home: --</span>
        </div>
      </div>
      <div class="dashboard-actions">
        <button class="btn" onclick="refreshAll()">Refresh</button>
        <a class="btn ghost" href="/config">Config LEDs</a>
        <a class="btn ghost" href="/wifi">Config Wi-Fi</a>
        <a class="btn ghost" href="/dev">Dev</a>
        <button class="btn ghost" id="home_btn" onclick="updateHome()">Set Home</button>
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
        <span>Fecha: <span id="date">--</span></span>
        <span id="updated">Ultima lectura: --</span>
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
          <select id="track_session">
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
  </div>

  <script>
    const $ = (id) => document.getElementById(id);
    const homeBtn = $('home_btn');
    const trackCanvas = $('track_map');
    const trackSession = $('track_session');
    const trackStatus = $('track_status');
    const trackCsv = $('track_csv');
    const trackGeo = $('track_geo');
    const trackLoad = $('track_load');
    let trackLoaded = false;
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
      if(trackStatus){
        trackStatus.textContent=msg||'Ruta: --';
        trackStatus.className='empty-state';
      }
    }

    function drawTrack(points,bbox){
      if(!trackCanvas){return;}
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

    function renderSummary(d){
      if(!d||!d.has_data){
        const gpsFix=!!(d&&d.gps_fix);
        const gpsRaw=!!(d&&d.gps_raw_fix);
        if(gpsFix){
          $('status').textContent='Estado: Sin actividad registrada hoy';
          $('dist').textContent='0.00';
        } else if(gpsRaw){
          $('status').textContent='Estado: GPS no confiable';
          $('dist').textContent='--';
        } else {
          $('status').textContent='Estado: Esperando GPS';
          $('dist').textContent='--';
        }
        $('avg').textContent='--';
        $('max').textContent='--';
        $('date').textContent=(d&&d.date)?fmtDate(d.date):'--';
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

      var modeText='Modo: '+(s.mode||'--');
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
        const r = await fetch('/api/home/set',{method:'POST'}).then(r=>r.json());
        if (r.status === 'ok'){
          loadStatus();
        }
      }catch(e){}
      if (homeBtn) homeBtn.disabled = false;
    }
    if (trackSession){
      trackSession.addEventListener('change',()=>{ if(trackLoaded) loadTrack(); else setTrackLinks(trackSession.value||'current'); });
    }
    window.addEventListener('resize',resizeTrackCanvas);
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
  page.reserve(22000);
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
      <div class="muted">Home Wi-Fi y hotspot local del collar.</div>
    </div>

    <a class="back-link" href="/">&#8592; Inicio</a>

    <div class="card section">
      <h2>Estado Wi-Fi</h2>
      <div class="grid grid-2 section-body">
        <div class="field"><label>Home Wi-Fi</label><div class="data" id="wifi_home_state">--</div></div>
        <div class="field"><label>Hotspot collar</label><div class="data" id="wifi_ap_state">--</div></div>
        <div class="field"><label>Portal local</label><div class="data mono" id="wifi_portal">--</div></div>
        <div class="field"><label>mDNS</label><div class="data mono" id="wifi_mdns_state">--</div></div>
      </div>
      <div class="actions">
        <button class="btn ghost" id="wifi_refresh_btn" type="button" onclick="loadWifiStatus()">Actualizar estado</button>
      </div>
      <div id="wifi_status_msg" class="notice"></div>
    </div>

    <form class="card section" id="sta_form" method="post" action="/api/wifi">
      <h2>Home Wi-Fi</h2>
      <div class="muted">Conecta DOG-RGB al router de casa. El hotspot local queda disponible durante la conexion.</div>
      <div class="field">
        <label>Nombre de red (SSID)</label>
        <input name="ssid" value=")HTML");
  page += wifi_mgr::ssid();
  page += F(R"HTML(">
      </div>
      <div class="field">
        <label>Password red de casa</label>
        <input name="pass" id="pass" type="password" placeholder="Password">
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
        <div class="field"><label>Nombre hotspot (SSID)</label><input id="ap_ssid" type="text"></div>
        <div class="field"><label>Portal mDNS</label><input id="mdns" type="text"></div>
      </div>
      <div class="grid grid-2">
        <div class="field"><label>Password hotspot</label><input id="ap_pass" type="password" placeholder="(sin cambio)"></div>
        <div class="field">
          <label>AP abierto</label>
          <label class="muted"><input id="ap_open" type="checkbox"> Sin password</label>
        </div>
      </div>
      <label class="muted"><input type="checkbox" id="show_ap_pass"> Mostrar password hotspot</label>
      <div id="ap_hint" class="muted"></div>
      <div id="ap_warn" class="warn"></div>
      <div id="ap_open_warn" class="warn"></div>
      <div id="ap_recovery" class="notice"></div>
      <div class="actions">
        <button class="btn" id="ap_save_btn" type="button" onclick="saveAp()">Guardar AP</button>
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
    const showApPass = document.getElementById('show_ap_pass');
    const staForm = document.getElementById('sta_form');
    const staStatus = document.getElementById('sta_status');
    const wifiHomeState = document.getElementById('wifi_home_state');
    const wifiApState = document.getElementById('wifi_ap_state');
    const wifiPortal = document.getElementById('wifi_portal');
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
    let apHasPass = false;
    let initialAp = null;
    let baseCfg = null;
    let staPollTimer = null;
    showApPass.onchange = () => { apPass.type = showApPass.checked ? 'text' : 'password'; };

    const AP_ERROR_MAP = {
      ssid:{field:apSsid,msg:'SSID 1..32, sin espacios al inicio o final.'},
      pass:{field:apPass,msg:'Password 8..63.'},
      'pass required':{field:apPass,msg:'Password requerida o marca AP abierto.'},
      mdns:{field:mdns,msg:'mDNS invalido (1..32 letras, numeros o guiones).'}
    };

    function setApStatus(msg, tone){
      apStatus.textContent = msg || '';
      if (tone === 'error') apStatus.className = 'error';
      else if (tone === 'warn') apStatus.className = 'warn';
      else apStatus.className = 'muted';
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
          staStatus.textContent = 'No se confirmo conexion. Revisa SSID/password; el hotspot sigue disponible en http://192.168.4.1/.';
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
      apOpenWarn.innerText = apOpen.checked ? 'Advertencia: el hotspot quedara sin password.' : '';
      if (apChanged()) apRecovery.innerText = '';
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
      if (!apOpen.checked && passVal.length === 0 && !apHasPass){
        apPass.classList.add('invalid');
        setApStatus('Password requerida o marca AP abierto.', 'error');
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
      const payload = {ap_ssid:ssid, ap_open:apOpen.checked, ap_pass:passVal, mdns:mdnsVal};
      if (apSaveBtn) apSaveBtn.disabled = true;
      try{
        const res = await fetch('/api/wifi/ap',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
        const r = await res.json();
        if (!res.ok || r.status !== 'ok'){
          handleApBackendError(r.reason);
          return;
        }
        setApStatus(r.status + (r.wifi_restart ? ' (reiniciando AP)' : ''), 'muted');
        baseCfg.wifi = baseCfg.wifi || {};
        baseCfg.wifi.ap_ssid = ssid;
        baseCfg.wifi.mdns = mdnsVal;
        initialAp = { ap_ssid: ssid, mdns: mdnsVal, ap_open: apOpen.checked };
        if (apOpen.checked) apHasPass = false;
        else if (passVal.length >= 8) apHasPass = true;
        baseCfg.wifi.has_ap_pass = apHasPass;
        apPass.value = '';
        const securityText = apOpen.checked ? 'sin password' : (passVal.length >= 8 ? 'con el password nuevo' : 'con el password ya configurado');
        apRecovery.innerText = r.wifi_restart ? ('Si el telefono se desconecta, reconectate al hotspot "' + ssid + '" ' + securityText + ' y abre http://192.168.4.1/.') : 'Hotspot actualizado.';
        updateApState();
        loadWifiStatus();
      }catch(e){
        setApStatus('Error', 'error');
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
        const r = await fetch('/api/wifi',{method:'POST',body:fd});
        const text = await r.text();
        if (r.ok){
          staStatus.textContent = 'Guardado, conectando... el hotspot sigue disponible en http://192.168.4.1/.';
          pollStaStatus();
        } else {
          staStatus.textContent = 'Error: ' + text;
        }
      }catch(e){
        staStatus.textContent = 'Error';
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
  page.reserve(36000);
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
          <div class="field"><label>Brillo</label><input id="brightness_slider" type="range" min="1" max="255"></div>
          <div class="field"><label>Valor brillo</label><input id="brightness" type="number" min="1" max="255"></div>
        </div>
        <div class="field">
          <label><input id="day_mode_enabled" type="checkbox"> Modo DIA</label>
          <div class="help">Apaga efectos de 06:00 a 16:00; alertas y rastreo siguen activos.</div>
        </div>
        <div class="field" style="display:none">
          <label>Modo</label>
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

    <details class="card section" id="speed_block">
      <summary>Umbrales de velocidad (avanzado)</summary>
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
        <div class="field">
          <label>Preajuste</label>
          <div class="preset-row" id="simple_preset_buttons">
            <button class="preset-btn" type="button" data-theme="calm">Calm</button>
            <button class="preset-btn" type="button" data-theme="active">Active</button>
            <button class="preset-btn" type="button" data-theme="sport">Sport</button>
            <button class="preset-btn" type="button" data-theme="aurora">Aurora</button>
            <button class="preset-btn" type="button" data-theme="manual">Manual</button>
          </div>
        </div>
        <div class="field">
          <label>Color base</label>
          <div class="swatch-row" id="color_swatches"></div>
        </div>
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
          <div class="field"><label>Velocidad (0..255)</label><input id="simple_speed" type="number" min="0" max="255"></div>
          <div class="field"><label>Intensidad (0..255)</label><input id="simple_intensity" type="number" min="0" max="255"></div>
        </div>
        <details class="section">
          <summary>RGB manual</summary>
          <div class="grid grid-3 section-body">
            <div class="field"><label>R</label><input id="simple_r" type="number" min="0" max="255"></div>
            <div class="field"><label>G</label><input id="simple_g" type="number" min="0" max="255"></div>
            <div class="field"><label>B</label><input id="simple_b" type="number" min="0" max="255"></div>
          </div>
        </details>
        <div class="help">RAINBOW, GRADIENT_WAVE y FIRE ignoran el color base.</div>
      </div>
    </details>

    <details class="card section" id="show_block" open>
      <summary>Show</summary>
      <div class="section-body">
        <div class="help">Modo demo: rota efectos automaticamente. No hay parametros.</div>
      </div>
    </details>

    <details class="card section" id="effects_block">
      <summary>Ajuste avanzado por rango (1-10)</summary>
      <div class="section-body">
        <div id="effects"></div>
      </div>
    </details>

    <div class="section">
      <div class="card action-bar">
        <button class="btn" type="button" onclick="saveCfg()">Guardar cambios</button>
        <button class="btn danger" type="button" onclick="resetCfg()">Restaurar defaults</button>
      </div>
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
    const brightnessSlider = $('brightness_slider');
    const dayModeEnabled = $('day_mode_enabled');
    const statusEl = $('status');
    const errorsEl = $('errors');
    const saveBtn = $('save_btn');
    const resetBtn = $('reset_btn');

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
      fence_max:{field:'fence_max',msg:'Distancia geofence 50..5000.'},
      ranges:{field:'speed_block',msg:'Rangos requeridos.'},
      'ranges value':{field:'speed_block',msg:'Rangos deben ser > 0.'},
      'ranges order':{field:'speed_block',msg:'Rangos deben ser ascendentes.'},
      effects:{field:'effects_block',msg:'Efectos incompletos.'},
      'effect values':{field:'effects_block',msg:'Valores de efecto invalidos.'},
      'effect id':{field:'effects_block',msg:'ID de efecto invalido.'},
      single:{field:'simple_block',msg:'Bloque simple invalido.'},
      'single values':{field:'simple_block',msg:'Valores simple invalidos.'},
      gps:{field:'gps_block',msg:'Parametros GPS invalidos.'},
      day_mode:{field:'day_mode_enabled',msg:'Modo DIA invalido.'}
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
          <div class="field field-speed"><label>Velocidad</label><input id="e${i}s" type="number" min="0" max="255"></div>
          <div class="field field-intensity"><label>Intensidad</label><input id="e${i}i" type="number" min="0" max="255"></div>
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
      el.innerHTML = COLOR_PRESETS.map(c=>`<button class="swatch" type="button" title="${c.name}" data-r="${c.r}" data-g="${c.g}" data-b="${c.b}" style="background:rgb(${c.r},${c.g},${c.b})"></button>`).join('');
      document.querySelectorAll('.swatch').forEach(btn=>{
        btn.onclick=()=>{
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
      speedBlock.style.display = (mode === 'speed') ? 'block' : 'none';
      geofenceBlock.style.display = (mode === 'geofence') ? 'block' : 'none';
      simpleBlock.style.display = (mode === 'simple') ? 'block' : 'none';
      showBlock.style.display = (mode === 'show') ? 'block' : 'none';
      effectsBlock.style.display = (mode === 'speed' || mode === 'geofence') ? 'block' : 'none';
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
      if (!cfg.day_mode || typeof cfg.day_mode.enabled !== 'boolean') addError('day_mode_enabled','Modo DIA invalido.');

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
      if (saveBtn) saveBtn.disabled = true;
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
      }finally{
        if (saveBtn) saveBtn.disabled = false;
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
      if (resetBtn) resetBtn.disabled = true;
      fetch('/api/config/reset',{method:'POST'}).then(r=>r.json()).then(r=>{
        statusEl.innerText = r.status;
      }).catch(()=>{statusEl.innerText='error';}).finally(()=>{if(resetBtn) resetBtn.disabled=false;});
    }

    modeEl.onchange = updateModeVisibility;
    document.querySelectorAll('[data-mode-card]').forEach(btn=>btn.onclick=()=>selectMode(btn.dataset.modeCard));
    document.querySelectorAll('[data-theme]').forEach(btn=>btn.onclick=()=>{ simpleTheme.value=btn.dataset.theme; if(btn.dataset.theme !== 'manual') applyTheme(btn.dataset.theme); updateThemeSelection(); });
    simpleTheme.onchange = () => { if (simpleTheme.value !== 'manual') applyTheme(simpleTheme.value); updateThemeSelection(); };
    [simpleEffect,simpleSpeed,simpleIntensity,simpleR,simpleG,simpleB].forEach(el=>el.oninput=updateThemeSelection);
    brightnessSlider.oninput = () => syncBrightness(brightnessSlider);
    brightness.oninput = () => syncBrightness(brightness);
    fenceMax.oninput = updateFenceRanges;

    buildEffectsTable();
    fillEffectSelect(simpleEffect);
    buildColorSwatches();

    fetch('/api/config').then(r=>r.json()).then(c=>{
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
      updateSwatchSelection();
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
  page.reserve(26000);
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
      <div class="grid grid-2">
        <div class="field"><label>Tiempo activo</label><div class="data mono" id="dev-uptime">--</div></div>
        <div class="field"><label>Compilacion</label><div class="data mono" id="dev-build">--</div></div>
        <div class="field"><label>Heap libre</label><div class="data mono" id="dev-heap">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Wi-Fi</h2>
      <div class="grid grid-2">
        <div class="field"><label>Modo</label><div class="data mono" id="wifi-mode">--</div></div>
        <div class="field"><label>STA</label><div class="data mono" id="wifi-sta">--</div></div>
        <div class="field"><label>AP</label><div class="data mono" id="wifi-ap">--</div></div>
        <div class="field"><label>Clientes AP</label><div class="data mono" id="wifi-stations">--</div></div>
        <div class="field"><label>Wi-Fi apagado</label><div class="data mono" id="wifi-off">--</div></div>
        <div class="field"><label>SSID AP</label><div class="data mono" id="wifi-ssid">--</div></div>
        <div class="field"><label>mDNS</label><div class="data mono" id="wifi-mdns">--</div></div>
        <div class="field"><label>STA IP</label><div class="data mono" id="wifi-sta-ip">--</div></div>
        <div class="field"><label>AP IP</label><div class="data mono" id="wifi-ap-ip">--</div></div>
        <div class="field"><label>RSSI</label><div class="data mono" id="wifi-rssi">--</div></div>
      </div>
    </div>

    <details class="card section" open>
      <summary>Diagnostico AP</summary>
      <div class="grid grid-2 section-body">
        <div class="field"><label>Inicios AP</label><div class="data mono" id="diag-ap-start">--</div></div>
        <div class="field"><label>Fallos AP</label><div class="data mono" id="diag-ap-fail">--</div></div>
        <div class="field"><label>Paradas AP</label><div class="data mono" id="diag-ap-stop">--</div></div>
        <div class="field"><label>Reinicios AP</label><div class="data mono" id="diag-ap-restart">--</div></div>
        <div class="field"><label>Clientes conectados</label><div class="data mono" id="diag-ap-sta-connect">--</div></div>
        <div class="field"><label>Clientes desconectados</label><div class="data mono" id="diag-ap-sta-disconnect">--</div></div>
        <div class="field"><label>DNS cautivo</label><div class="data mono" id="diag-dns">--</div></div>
        <div class="field"><label>Canal AP</label><div class="data mono" id="diag-channel">--</div></div>
        <div class="field"><label>Hold AP</label><div class="data mono" id="diag-hold">--</div></div>
        <div class="field"><label>Proximo retry STA</label><div class="data mono" id="diag-next-retry">--</div></div>
        <div class="field"><label>Ultima razon AP</label><div class="data mono" id="diag-ap-reason">--</div></div>
        <div class="field"><label>Ultima razon STA</label><div class="data mono" id="diag-sta-reason">--</div></div>
      </div>
    </details>

    <div class="card section">
      <h2>GPS</h2>
      <div class="grid grid-2">
        <div class="field"><label>Fix</label><div class="data mono" id="gps-fix">--</div></div>
        <div class="field"><label>Fix actual</label><div class="data mono" id="gps-current-fix">--</div></div>
        <div class="field"><label>Fix sin filtrar</label><div class="data mono" id="gps-raw-fix">--</div></div>
        <div class="field"><label>Fix confiable</label><div class="data mono" id="gps-trusted-fix">--</div></div>
        <div class="field"><label>Sats</label><div class="data mono" id="gps-sats">--</div></div>
        <div class="field"><label>Calidad fix</label><div class="data mono" id="gps-fix-quality">--</div></div>
        <div class="field"><label>HDOP</label><div class="data mono" id="gps-hdop">--</div></div>
        <div class="field"><label>Calidad OK</label><div class="data mono" id="gps-quality-ok">--</div></div>
        <div class="field"><label>Velocidad (kph)</label><div class="data mono" id="gps-speed">--</div></div>
        <div class="field"><label>Lat</label><div class="data mono" id="gps-lat">--</div></div>
        <div class="field"><label>Lon</label><div class="data mono" id="gps-lon">--</div></div>
        <div class="field"><label>Fecha</label><div class="data mono" id="gps-date">--</div></div>
        <div class="field"><label>Ultima actualizacion</label><div class="data mono" id="gps-update">--</div></div>
        <div class="field"><label>Edad ultimo byte</label><div class="data mono" id="gps-age-byte">--</div></div>
        <div class="field"><label>Edad ultimo fix</label><div class="data mono" id="gps-age-fix">--</div></div>
        <div class="field"><label>Bytes RX</label><div class="data mono" id="gps-bytes">--</div></div>
        <div class="field"><label>Sentencias RX</label><div class="data mono" id="gps-sentences">--</div></div>
        <div class="field"><label>RMC visto</label><div class="data mono" id="gps-rmc-seen">--</div></div>
        <div class="field"><label>RMC valido</label><div class="data mono" id="gps-rmc-valid">--</div></div>
        <div class="field"><label>GGA visto</label><div class="data mono" id="gps-gga-seen">--</div></div>
        <div class="field"><label>Overflow</label><div class="data mono" id="gps-overflow">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>LED</h2>
      <div class="grid grid-2">
        <div class="field"><label>Modo</label><div class="data mono" id="led-mode">--</div></div>
        <div class="field"><label>Brillo</label><div class="data mono" id="led-brightness">--</div></div>
        <div class="field"><label>Rango actual</label><div class="data mono" id="led-range">--</div></div>
        <div class="field"><label>Base RGB</label><div class="data mono" id="led-base">--</div></div>
        <div class="field"><label>Efecto A</label><div class="data mono" id="led-effect-a">--</div></div>
        <div class="field"><label>Efecto B</label><div class="data mono" id="led-effect-b">--</div></div>
        <div class="field"><label>Velocidad rango</label><div class="data mono" id="led-range-speed">--</div></div>
        <div class="field"><label>Intensidad rango</label><div class="data mono" id="led-range-intensity">--</div></div>
        <div class="field"><label>Efecto simple</label><div class="data mono" id="led-simple-effect">--</div></div>
        <div class="field"><label>Velocidad simple</label><div class="data mono" id="led-simple-speed">--</div></div>
        <div class="field"><label>Intensidad simple</label><div class="data mono" id="led-simple-intensity">--</div></div>
        <div class="field"><label>Simple RGB</label><div class="data mono" id="led-simple-rgb">--</div></div>
        <div class="field"><label>Efecto show</label><div class="data mono" id="led-show-effect">--</div></div>
        <div class="field"><label>Modo DIA</label><div class="data mono" id="day-state">--</div></div>
        <div class="field"><label>DIA hora local</label><div class="data mono" id="day-local">--</div></div>
      </div>
    </div>

    <div class="card section">
      <h2>Geocerca</h2>
      <div class="grid grid-2">
        <div class="field"><label>Home definido</label><div class="data mono" id="geo-set">--</div></div>
        <div class="field"><label>Fuente</label><div class="data mono" id="geo-source">--</div></div>
        <div class="field"><label>Home lat</label><div class="data mono" id="geo-lat">--</div></div>
        <div class="field"><label>Home lon</label><div class="data mono" id="geo-lon">--</div></div>
        <div class="field"><label>Distancia (m)</label><div class="data mono" id="geo-dist">--</div></div>
        <div class="field"><label>Rango</label><div class="data mono" id="geo-range">--</div></div>
      </div>
    </div>

    <details class="card section">
      <summary>JSON crudo</summary>
      <pre id="dev-json" class="code mono section-body"></pre>
    </details>

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
        const nowMs = d.time ? d.time.uptime_ms : 0;
        setText('diag-ap-start', diag.ap_start_count);
        setText('diag-ap-fail', diag.ap_start_fail_count);
        setText('diag-ap-stop', diag.ap_stop_count);
        setText('diag-ap-restart', diag.ap_restart_count);
        setText('diag-ap-sta-connect', diag.ap_station_connect_count);
        setText('diag-ap-sta-disconnect', diag.ap_station_disconnect_count);
        setText('diag-dns', diag.dns_running ? 'activo' : 'apagado');
        setText('diag-channel', diag.current_ap_channel);
        setText('diag-hold', (diag.ap_hold_until_ms && diag.ap_hold_until_ms > nowMs) ? fmtMs(diag.ap_hold_until_ms - nowMs) : '--');
        setText('diag-next-retry', (diag.next_sta_retry_ms && diag.next_sta_retry_ms > nowMs) ? fmtMs(diag.next_sta_retry_ms - nowMs) : '--');
        setText('diag-ap-reason', diag.last_ap_reason);
        setText('diag-sta-reason', diag.last_sta_reason);

        const gps = d.gps || {};
        setText('gps-fix', gps.fix ? 'si' : 'no');
        setText('gps-current-fix', gps.current_fix ? 'si' : 'no');
        setText('gps-raw-fix', gps.raw_fix ? 'si' : 'no');
        setText('gps-trusted-fix', gps.trusted_fix ? 'si' : 'no');
        setText('gps-sats', gps.sats);
        setText('gps-fix-quality', gps.fix_quality);
        setText('gps-hdop', (gps.hdop !== undefined) ? gps.hdop.toFixed(2) : '--');
        setText('gps-quality-ok', gps.quality_ok ? 'si' : 'no');
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
