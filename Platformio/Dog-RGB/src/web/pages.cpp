#include "web/pages.h"

#include "wifi/wifi_mgr.h"

String web_pages::html_page() {
  return String(R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Dog Collar</title>
  <style>
    :root{--bg:#f6f6f6;--card:#fff;--text:#111;--muted:#666;--ok:#0a6;--warn:#b60;--bad:#b00}
    body{font-family:Arial,sans-serif;margin:16px;background:var(--bg);color:var(--text)}
    .container{max-width:720px;margin:0 auto}
    .row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
    .card{background:var(--card);border:1px solid #e2e2e2;border-radius:10px;padding:12px}
    .label{font-size:12px;color:var(--muted)}
    .value{font-size:24px;font-weight:bold}
    .pill{font-size:12px;border-radius:999px;padding:4px 8px;background:#eee;border:1px solid #ddd}
    .pill.ok{background:#e6f8ee;color:var(--ok);border-color:#bdebd0}
    .pill.warn{background:#fff2e0;color:var(--warn);border-color:#ffd7ad}
    .pill.bad{background:#ffe5e5;color:var(--bad);border-color:#ffbdbd}
    .muted{color:var(--muted);font-size:12px}
    button{padding:10px 14px;border:0;border-radius:6px;background:#111;color:#fff}
    select{padding:8px;border:1px solid #ccc;border-radius:6px;background:#fff}
    a.btn{display:inline-block;padding:10px 14px;border-radius:6px;border:1px solid #ddd;background:#fff;color:#111;text-decoration:none}
    @media (max-width:540px){.grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="container">
    <h1>Dog Collar</h1>
    <div class="row" style="margin-bottom:10px">
      <span class="pill" id="pill-gps">GPS: --</span>
      <span class="pill" id="pill-wifi">Wi-Fi: --</span>
      <span class="pill" id="pill-mode">Modo: --</span>
      <span class="pill" id="pill-home">Home: --</span>
    </div>
    <div class="row" style="margin-bottom:10px">
      <label class="muted">Modo</label>
      <select id="mode_select">
        <option value="speed">Velocidad</option>
        <option value="geofence">Geocerca</option>
        <option value="simple">Simple</option>
        <option value="show">Show</option>
      </select>
      <button onclick="saveMode()">Aplicar</button>
      <span class="muted" id="mode_status"></span>
    </div>

    <div class="grid">
      <div class="card">
        <div class="label">Distancia</div>
        <div class="value" id="dist">--</div>
        <div class="muted">km</div>
      </div>
      <div class="card">
        <div class="label">Velocidad promedio</div>
        <div class="value" id="avg">--</div>
        <div class="muted">km/h</div>
      </div>
      <div class="card">
        <div class="label">Velocidad maxima</div>
        <div class="value" id="max">--</div>
        <div class="muted">km/h</div>
      </div>
      <div class="card">
        <div class="label">Fecha</div>
        <div class="value" id="date">--</div>
        <div class="muted" id="updated">Ultima lectura: --</div>
      </div>
    </div>

    <h2>Sesiones</h2>
    <div id="session-current"></div>
    <div id="history"></div>

    <div class="row" style="margin:12px 0">
      <button onclick="refreshAll()">Actualizar</button>
      <a class="btn" href="/config">Config</a>
      <a class="btn" href="/wifi">Wi-Fi</a>
    </div>
    <div class="muted" id="status">Estado: --</div>
  </div>

  <script>
    const $ = (id) => document.getElementById(id);
    const modeSelect = $('mode_select');
    const modeStatus = $('mode_status');
    function minToTime(m){var h=Math.floor(m/60);var mm=m%60;return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0');}
    function cmpsToKph(v){return (v*0.036).toFixed(1);}
    function fmtDate(d){if(!d){return '--';}var s=String(d);if(s.length!==8){return s;}return s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8);}
    function yyyymmddToDate(d){if(!d){return '--/--';}var y=Math.floor(d/10000);var m=Math.floor((d%10000)/100);var day=d%100;return String(day).padStart(2,'0')+'/'+String(m).padStart(2,'0');}
    function formatDuration(s){var h=Math.floor(s/3600);var m=Math.floor((s%3600)/60);return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0');}
    function hasFlag(flags,bit){return (flags&(1<<bit))!==0;}
    function setPill(id,text,tone){var el=$(id);el.textContent=text;el.className='pill'+(tone?(' '+tone):'');}

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
      $('status').textContent='Estado: '+(d.gps_fix?'GPS OK':'Sin GPS');
    }

    function renderSessionCard(label,s){
      if(!s){return '';}
      var flags=s.flags||0;
      var noFix=hasFlag(flags,3)||!hasFlag(flags,0);
      if(noFix){
        return "<div class='card'><div>"+label+"</div><div>Sin GPS</div></div>";
      }
      var startDate=yyyymmddToDate(s.start_date);
      var startTime=minToTime(s.start_min||0);
      var endDate=yyyymmddToDate(s.end_date||s.start_date);
      var endTime=minToTime(s.end_min||s.start_min||0);
      var distKm=(s.distance_m/1000).toFixed(2);
      var avg=cmpsToKph(s.avg_speed_cmps||0);
      var max=cmpsToKph(s.max_speed_cmps||0);
      var active=formatDuration(s.active_s||0);
      return "<div class='card'><div>"+label+" - "+startDate+" "+startTime+" a "+endDate+" "+endTime+
             "</div><div>Distancia: "+distKm+" km</div><div>Tiempo activo: "+active+
             "</div><div>Vel. prom: "+avg+" km/h | Vel. max: "+max+" km/h</div></div>";
    }

    function renderSessionCurrent(s){
      var el=$('session-current');
      if(!el){return;}
      if(!s){
        el.innerHTML="<div class='card'>Sesion actual: --</div>";
        return;
      }
      el.innerHTML=renderSessionCard('Sesion actual',s);
    }

    function renderHistory(list){
      var el=$('history');
      if(!el){return;}
      if(!list||list.length===0){
        el.innerHTML="<div class='card'>Sin historial</div>";
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
      var gpsTone=s.gps&&s.gps.fix?'ok':'warn';
      var gpsText=s.gps&&s.gps.fix?'GPS OK':'GPS sin fix';
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

    function refreshAll(){loadSummary();loadStatus();}
    refreshAll();
    setInterval(loadStatus,5000);
    setInterval(loadSummary,10000);
  </script>
</body>
</html>
)HTML");
}
String web_pages::html_wifi_page() {
  String page = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Wi-Fi</title></head><body><h1>Configurar Wi-Fi</h1>"
                "<form method='post' action='/api/wifi'>"
                "<label>SSID</label><br><input name='ssid' value='" + wifi_mgr::ssid() + "'><br>"
                "<label>Password</label><br><input name='pass' type='password'><br><br>"
                "<button type='submit'>Guardar y conectar</button>"
                "</form><p><a href='/'>Volver</a></p></body></html>";
  return page;
}
String web_pages::html_config_page() {
  return String(R"CFG(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Config</title>
  <style>
    :root{--bg:#f6f6f6;--card:#fff;--text:#111;--muted:#666;--warn:#b60;--bad:#b00}
    body{font-family:Arial,sans-serif;margin:16px;background:var(--bg);color:var(--text)}
    .container{max-width:900px;margin:0 auto}
    .section{background:var(--card);border:1px solid #e2e2e2;border-radius:10px;padding:12px;margin:12px 0}
    .section.invalid{border-color:#b00;background:#fff6f6}
    .row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
    .grid-3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
    .field label{display:block;font-size:12px;color:var(--muted)}
    input,select{width:100%;padding:8px;margin:4px 0;border:1px solid #ccc;border-radius:6px}
    input.invalid,select.invalid{border-color:#b00;background:#fff3f3}
    button{padding:10px 14px;border:0;border-radius:6px;background:#111;color:#fff}
    button.secondary{background:#444}
    .muted{color:var(--muted);font-size:12px}
    .help{color:var(--muted);font-size:12px;margin-top:6px}
    .error{color:var(--bad);font-size:12px;margin:6px 0}
    .warn{color:var(--warn);font-size:12px;margin:6px 0}
    .effects-row{display:grid;grid-template-columns:60px 1fr 1fr 1fr 1fr;gap:8px;align-items:end;margin:8px 0}
    .effects-row .range-label{font-weight:bold}
    a{color:#111}
    @media (max-width:760px){
      .grid,.grid-3{grid-template-columns:1fr}
      .effects-row{grid-template-columns:1fr 1fr}
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Config</h1>
    <div id="errors" class="error"></div>

    <div class="section">
      <h2>Comun</h2>
      <div class="grid">
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
      <div class="row" style="margin-top:8px">
        <button onclick="saveCfg()">Guardar</button>
        <button class="secondary" onclick="resetCfg()">Restaurar defaults</button>
        <span id="status" class="muted"></span>
      </div>
    </div>

    <div class="section" id="speed_block">
      <h2>Speed ranges (kph)</h2>
      <div class="grid-3">
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

    <div class="section" id="geofence_block">
      <h2>Geofence</h2>
      <div class="grid">
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
        <button type="button" onclick="setHome()">Nuevo Home (GPS actual)</button>
        <button type="button" class="secondary" onclick="clearHome()">Clear Home</button>
      </div>
      <div id="home_status" class="muted"></div>
    </div>

    <div class="section" id="simple_block">
      <h2>Simple</h2>
      <div class="grid">
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
      <div class="grid">
        <div class="field"><label>Speed (0..255)</label><input id="simple_speed" type="number" min="0" max="255"></div>
        <div class="field"><label>Intensity (0..255)</label><input id="simple_intensity" type="number" min="0" max="255"></div>
      </div>
      <div class="grid-3">
        <div class="field"><label>R</label><input id="simple_r" type="number" min="0" max="255"></div>
        <div class="field"><label>G</label><input id="simple_g" type="number" min="0" max="255"></div>
        <div class="field"><label>B</label><input id="simple_b" type="number" min="0" max="255"></div>
      </div>
      <div class="help">RAINBOW, GRADIENT_WAVE y FIRE ignoran el color base.</div>
    </div>

    <div class="section" id="show_block">
      <h2>Show</h2>
      <div class="help">Modo demo: rota efectos automaticamente. No hay parametros.</div>
    </div>

    <div class="section" id="effects_block">
      <h2>Efectos por rango (1-10)</h2>
      <div id="effects"></div>
    </div>

    <div class="section" id="wifi_block">
      <h2>Wi-Fi AP</h2>
      <div class="grid">
        <div class="field"><label>SSID</label><input id="ap_ssid" type="text"></div>
        <div class="field"><label>mDNS</label><input id="mdns" type="text"></div>
      </div>
      <div class="grid">
        <div class="field"><label>Password</label><input id="ap_pass" type="password" placeholder="(sin cambio)"></div>
        <div class="field">
          <label>AP abierto</label>
          <label><input id="ap_open" type="checkbox"> Sin password</label>
        </div>
      </div>
      <div id="ap_hint" class="muted"></div>
      <div id="ap_warn" class="warn"></div>
    </div>

    <p><a href="/">Volver</a></p>
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
    const apSsid = $('ap_ssid');
    const apPass = $('ap_pass');
    const apOpen = $('ap_open');
    const mdns = $('mdns');
    const apHint = $('ap_hint');
    const apWarn = $('ap_warn');

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
      ssid:{field:'ap_ssid',msg:'SSID 1..32.'},
      pass:{field:'ap_pass',msg:'Password >= 8.'},
      mdns:{field:'mdns',msg:'mDNS invalido (1..32 a-z0-9-).'}
    };

    let initialWifi = null;
    let apHasPass = false;

    function fillEffectSelect(sel){
      sel.innerHTML = EFFECTS.map(e => `<option value="${e.id}">${e.name}</option>`).join('');
    }

    function buildEffectsTable(){
      let html = '';
      for (let i=1;i<=10;i++){
        html += `<div class="effects-row">
          <div class="range-label">R${i}</div>
          <div class="field"><label>A</label><select id="e${i}a"></select></div>
          <div class="field"><label>B</label><select id="e${i}b"></select></div>
          <div class="field"><label>Speed</label><input id="e${i}s" type="number" min="0" max="255"></div>
          <div class="field"><label>Intensity</label><input id="e${i}i" type="number" min="0" max="255"></div>
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

    function validMdns(value){
      if (!value || value.length < 1 || value.length > 32) return false;
      for (let i=0;i<value.length;i++){
        const c = value[i];
        const ok = (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==='-';
        if (!ok) return false;
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

      if (cfg.wifi.ap_ssid.length < 1 || cfg.wifi.ap_ssid.length > 32) addError('ap_ssid','SSID 1..32.');
      if (!cfg.wifi.ap_open && cfg.wifi.ap_pass.length > 0 && cfg.wifi.ap_pass.length < 8) addError('ap_pass','Password >= 8.');
      if (!validMdns(cfg.wifi.mdns)) addError('mdns','mDNS invalido (1..32 a-z0-9-).');

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

    function wifiChanged(){
      if (!initialWifi) return false;
      return apSsid.value !== initialWifi.ap_ssid ||
             mdns.value !== initialWifi.mdns ||
             apOpen.checked !== initialWifi.ap_open ||
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
      apWarn.innerText = wifiChanged() ? 'Nota: cambiar AP puede desconectar la sesion.' : '';
    }

    function buildPayload(){
      const cfg = {
        version:4,
        mode: modeEl.value,
        fence_max_m: intVal(fenceMax,300),
        led:{brightness: intVal(brightness,1)},
        speed_ranges_kph: rangeInputs.map(el=>floatVal(el,0)),
        effects:{},
        single:{
          effect: intVal(simpleEffect,0),
          speed: intVal(simpleSpeed,0),
          intensity: intVal(simpleIntensity,0),
          rgb:{r:intVal(simpleR,0), g:intVal(simpleG,0), b:intVal(simpleB,0)}
        },
        wifi:{
          ap_ssid: apSsid.value.trim(),
          ap_pass: apPass.value,
          ap_open: apOpen.checked,
          mdns: mdns.value.trim()
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
      if (wifiChanged()){
        if (!confirm('Guardar cambios? El AP puede reiniciarse.')) return;
      }
      statusEl.innerText = 'Guardando...';
      apWarn.innerText = '';
      try{
        const r = await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)}).then(r=>r.json());
        if (r.status !== 'ok'){
          handleBackendError(r.reason);
          statusEl.innerText = 'Error';
          return;
        }
        statusEl.innerText = r.status + (r.wifi_restart ? ' (reiniciando AP)' : '');
        initialWifi = { ap_ssid: apSsid.value, mdns: mdns.value, ap_open: apOpen.checked };
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
    apOpen.onchange = updateApState;
    apPass.oninput = updateApState;
    apSsid.oninput = updateApState;
    mdns.oninput = updateApState;

    buildEffectsTable();
    fillEffectSelect(simpleEffect);

    fetch('/api/config').then(r=>r.json()).then(c=>{
      brightness.value = c.led.brightness;
      modeEl.value = c.mode || 'speed';
      fenceMax.value = c.fence_max_m || 300;
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
      apSsid.value = c.wifi.ap_ssid;
      mdns.value = c.wifi.mdns;
      apOpen.checked = !c.wifi.has_ap_pass;
      apHasPass = c.wifi.has_ap_pass;
      initialWifi = { ap_ssid: apSsid.value, mdns: mdns.value, ap_open: apOpen.checked };
      updateModeVisibility();
      updateFenceRanges();
      updateThemeSelection();
      updateApState();
      loadHome();
    });
  </script>
</body>
</html>
)CFG");
}
