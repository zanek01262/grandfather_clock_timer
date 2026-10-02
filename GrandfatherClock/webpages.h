/* =====================================================================
   webpages.h  —  PROGMEM HTML for the setup splash and live dashboard
   ---------------------------------------------------------------------
   Aesthetic: phosphor oscilloscope — dark CRT green, scanline texture,
   Share Tech Mono / Orbitron type, soft glow. Carried over as the
   project's established visual identity.

   Two pages:
     SETUP_PAGE  — served in AP mode. Scans networks (/scan), lets the
                   user pick one + enter a password, POSTs to /save.
     DASH_PAGE   — served in STA mode. Polls /api/state, renders a live
                   scope trace + meters, and offers live threshold tuning
                   (POST /api/config) and a CSV log download (/api/log).
   ===================================================================== */
#ifndef WEBPAGES_H
#define WEBPAGES_H

#include <Arduino.h>

// ----------------------------------------------------------------------
//  SETUP / SPLASH PAGE  (AP mode)
// ----------------------------------------------------------------------
static const char SETUP_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Grandfather Clock &mdash; Setup</title>
<style>
  /* Brass & Walnut, self-contained. NO external fonts: AP-mode clients
     have no internet and a render-blocking font fetch hangs the page. */
  :root{
    --bg:#191310; --panel:#231b15; --panel2:#2d231a; --line:#3f3125;
    --ink:#f2e8d9; --dim:#b9a691; --faint:#8b7860;
    --accent:#c9a227; --accent-dim:#7d6418; --accent-soft:rgba(201,162,39,.13);
    --good:#8ba645; --bad:#c1543c;
    --display:Georgia,'Times New Roman',serif;
    --body:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
  }
  *{box-sizing:border-box}
  html,body{margin:0;min-height:100%}
  body{background:var(--bg); color:var(--ink); min-height:100vh;
    font-family:var(--body); display:flex; align-items:center;
    justify-content:center; padding:20px; -webkit-font-smoothing:antialiased}
  .panel{width:100%; max-width:420px; background:var(--panel);
    background-image:linear-gradient(180deg,rgba(255,255,255,.025),transparent 60%);
    border:1px solid var(--line); border-radius:12px; padding:28px 26px 30px;
    box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}
  .brand{display:flex; align-items:center; gap:10px; margin-bottom:4px}
  .brand::before{content:""; width:9px; height:9px; border-radius:50%;
    background:var(--accent); box-shadow:0 0 0 4px var(--accent-soft)}
  h1{font-family:var(--display); font-size:23px; font-weight:700; margin:0;
    letter-spacing:.01em}
  .sub{color:var(--faint); font-size:13px; margin:4px 0 22px}
  label{display:block; font-family:var(--display); font-size:13px; font-weight:700;
    letter-spacing:.09em; text-transform:uppercase; color:var(--accent); margin:18px 0 8px}
  .net{border:1px solid var(--line); border-radius:9px; max-height:210px;
    overflow:auto; background:var(--panel2)}
  .net button{display:flex; width:100%; align-items:center; gap:10px;
    background:none; border:0; border-bottom:1px solid var(--line);
    color:var(--ink); font-family:inherit; font-size:14.5px; padding:13px 14px;
    cursor:pointer; text-align:left; transition:background .12s}
  .net button:last-child{border-bottom:0}
  .net button:hover,.net button.sel{background:var(--accent-soft); color:var(--accent)}
  .net .ss{flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; font-weight:500}
  .net .meta{font-size:12px; color:var(--faint)}
  input[type=password],input[type=text]{width:100%; background:var(--panel2);
    border:1px solid var(--line); border-radius:9px; color:var(--ink);
    font-family:inherit; font-size:15px; padding:13px 14px; outline:none;
    transition:border-color .15s}
  input:focus{border-color:var(--accent)}
  .row{display:flex; gap:10px; align-items:center; margin-top:10px}
  .ghost{color:var(--dim); font-size:13px; background:none; border:0; cursor:pointer;
    font-family:var(--body); font-weight:500; padding:0}
  .ghost:hover{color:var(--accent)}
  label .ghost{display:inline; margin-left:8px; text-transform:none;
    letter-spacing:0; font-size:12px; font-family:var(--body); font-weight:500}
  button.go{margin-top:24px; width:100%; padding:14px; border-radius:9px; border:0;
    background:var(--accent); color:#2a2008; font-family:var(--display);
    font-weight:700; font-size:16px; letter-spacing:.06em; text-transform:uppercase;
    cursor:pointer; transition:background .15s}
  button.go:hover{background:#dcb433}
  button.go:disabled{opacity:.4; cursor:not-allowed}
  .status{margin-top:16px; font-size:13.5px; min-height:18px; line-height:1.5}
  .status.ok{color:var(--good)} .status.err{color:#e08a7a}
</style></head>
<body>
  <div class="panel">
    <div class="brand"><h1>Grandfather Clock</h1></div>
    <div class="sub">Connect your device to Wi-Fi</div>

    <label>Network <button class="ghost" id="rescan" type="button">Rescan</button></label>
    <div class="net" id="nets"><button type="button" disabled>Scanning\u2026</button></div>

    <label>Password</label>
    <input type="password" id="pass" placeholder="Wi-Fi password" autocomplete="off">
    <div class="row"><button class="ghost" id="showpw" type="button">Show password</button></div>

    <button class="go" id="save" type="button" disabled>Connect</button>
    <div class="status" id="st"></div>
  </div>
<script>
  let chosen = null;
  const netsEl = document.getElementById('nets');
  const saveBtn = document.getElementById('save');
  const st = document.getElementById('st');

  function bars(rssi){
    if(rssi>=-55) return '▂▄▆█';
    if(rssi>=-65) return '▂▄▆_';
    if(rssi>=-75) return '▂▄__';
    return '▂___';
  }
  function render(list){
    netsEl.innerHTML='';
    if(!list.length){ netsEl.innerHTML='<button type="button" disabled>no networks found</button>'; return; }
    list.sort((a,b)=>b.rssi-a.rssi);
    list.forEach(n=>{
      const b=document.createElement('button'); b.type='button';
      b.innerHTML='<span class="ss">'+n.ssid+'</span>'+
                  '<span class="meta">'+(n.lock?'🔒 ':'')+bars(n.rssi)+'</span>';
      b.onclick=()=>{ chosen=n.ssid;
        [...netsEl.children].forEach(c=>c.classList.remove('sel'));
        b.classList.add('sel'); saveBtn.disabled=false; };
      netsEl.appendChild(b);
    });
  }
  function scan(){
    netsEl.innerHTML='<button type="button" disabled>scanning…</button>';
    fetch('/scan').then(r=>r.json()).then(render)
      .catch(()=>netsEl.innerHTML='<button type="button" disabled>scan failed</button>');
  }
  document.getElementById('rescan').onclick=function(){
    netsEl.innerHTML='<button type="button" disabled>scanning\u2026</button>';
    fetch('/rescan').catch(()=>{});
    setTimeout(scan,3000);   // async scan finishes well within 3s
  };
  document.getElementById('showpw').onclick=function(){
    const p=document.getElementById('pass');
    const show=p.type==='password'; p.type=show?'text':'password';
    this.textContent=show?'hide password':'show password';
  };
  saveBtn.onclick=function(){
    if(!chosen) return;
    saveBtn.disabled=true; st.className='status'; st.textContent='saving…';
    const body='ssid='+encodeURIComponent(chosen)+
               '&pass='+encodeURIComponent(document.getElementById('pass').value);
    fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
      .then(r=>r.json()).then(j=>{
        if(j.ok){ st.className='status ok';
          st.textContent='Saved. Device is rebooting and joining '+chosen+'. You can close this page.'; }
        else { st.className='status err'; st.textContent='Save failed: '+(j.err||'unknown'); saveBtn.disabled=false; }
      }).catch(()=>{ st.className='status ok';
        st.textContent='Device rebooting… (connection dropped, which is expected)'; });
  };
  scan();
</script>
</body></html>
)HTML";

// ----------------------------------------------------------------------
//  LIVE DASHBOARD  (STA mode)
// ----------------------------------------------------------------------
static const char DASH_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Grandfather Clock</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Cormorant+Garamond:wght@600;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>

  /* ---- Brass & Walnut theme ----
     Palette drawn from a longcase clock's case: walnut body, aged brass
     furniture, warm ivory dial text. Canvas colours are exposed as vars
     (--grid/--trace/--thresh/--axis) so the graphs match the chrome. */
  :root{
    --bg:#191310; --panel:#231b15; --panel2:#2d231a; --line:#3f3125;
    --ink:#f2e8d9; --dim:#b9a691; --faint:#8b7860;
    --accent:#c9a227; --accent-dim:#7d6418; --accent-soft:rgba(201,162,39,.13);
    --good:#8ba645; --warn:#d69128; --bad:#c1543c;
    /* legacy aliases retained: some inline styles still reference these */
    --ph:var(--accent); --ph-dim:var(--dim); --amber:var(--warn);
    --radius:10px;
    /* canvas */
    --grid:rgba(201,162,39,.11); --thresh:rgba(214,145,40,.65);
    --trace:#e0c158; --traceglow:rgba(224,193,88,.55); --axis:rgba(201,162,39,.5);
    /* type */
    --display:'Cormorant Garamond',Georgia,'Times New Roman',serif;
    --body:'Inter',system-ui,-apple-system,sans-serif;
    --mono:'JetBrains Mono',ui-monospace,monospace;
  }
  h1{font-family:var(--display);font-weight:700;font-size:27px;letter-spacing:.01em}
  h1::before{background:var(--accent);box-shadow:0 0 0 4px rgba(201,162,39,.16)}
  .panel{background:var(--panel);
    background-image:linear-gradient(180deg,rgba(255,255,255,.025),transparent 60%);
    box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}
  .panel h2{font-family:var(--display);font-weight:700;font-size:15px;
    letter-spacing:.10em;text-transform:uppercase;color:var(--accent)}
  .stat .v,.hv,.gverdict{font-family:var(--display);font-weight:700}
  .stat .v{font-size:29px}
  .scope{background:#120c08;border-color:var(--line)}
  .topbar{background:linear-gradient(90deg,var(--accent),rgba(201,162,39,.15) 55%,transparent)}

  
  *{box-sizing:border-box}
  html,body{margin:0}
  body{
    background:var(--bg); color:var(--ink);
    font-family:var(--body);
    font-size:15px; line-height:1.5; -webkit-font-smoothing:antialiased;
    padding:0;
  }
  .wrap{max-width:1120px; margin:0 auto; padding:24px 20px 64px}

  /* header */
  header{
    display:flex; align-items:center; justify-content:space-between;
    gap:16px; padding:8px 0 22px; flex-wrap:wrap;
  }
  h1{
    font-family:var(--display); font-weight:600;
    font-size:20px; letter-spacing:-.01em; margin:0; color:var(--ink);
    display:flex; align-items:center; gap:10px;
  }
  h1::before{
    content:""; width:9px; height:9px; border-radius:50%;
    background:var(--good); box-shadow:0 0 0 4px rgba(63,185,80,.15);
  }
  .clock{
    font-family:var(--mono); font-size:15px;
    color:var(--dim); font-variant-numeric:tabular-nums; letter-spacing:.02em;
  }

  /* alert bar */
  .alertbar{
    border-radius:12px; padding:14px 18px; margin:0 0 20px;
    font-weight:600; font-size:14.5px; border:1px solid; letter-spacing:-.005em;
    display:flex; align-items:center; gap:10px;
  }
  .alertbar.stop{color:#ffb4ae; border-color:rgba(248,81,73,.4); background:rgba(248,81,73,.10)}
  .alertbar.wind{color:#f0c674; border-color:rgba(210,153,34,.4); background:rgba(210,153,34,.10)}

  /* layout: two-column on wide screens */
  .cols{display:grid; grid-template-columns:1fr; gap:18px}
  @media(min-width:880px){ .cols{grid-template-columns:1.35fr 1fr} }
  .col{display:flex; flex-direction:column; gap:18px; min-width:0}

  /* panels */
  .panel{
    background:var(--panel); border:1px solid var(--line);
    border-radius:var(--radius); padding:20px;
  }
  .panel h2{
    font-family:var(--display); font-weight:600;
    font-size:12px; letter-spacing:.06em; text-transform:uppercase;
    color:var(--dim); margin:0 0 16px;
  }

  /* scope / graph frames */
  .scope{
    border:1px solid var(--line); border-radius:10px; overflow:hidden;
    background:#0b0f14; position:relative;
  }
  canvas{display:block; width:100%; height:210px}
  .scopectl{display:flex; align-items:center; gap:10px; margin-top:14px; flex-wrap:wrap}
  .scopectl label{font-size:12px; color:var(--faint); font-weight:500}
  .scopectl .win{margin-left:auto; font-size:12px; color:var(--faint);
    font-family:'JetBrains Mono',monospace}

  /* stat tiles */
  .grid{display:grid; grid-template-columns:repeat(auto-fit,minmax(130px,1fr)); gap:12px; margin:0}
  .stat{
    background:var(--panel2); border:1px solid var(--line);
    border-radius:11px; padding:14px 16px;
  }
  .stat .k{font-size:11px; letter-spacing:.05em; text-transform:uppercase;
    color:var(--faint); font-weight:600; margin-bottom:6px}
  .stat .v{font-family:var(--display); font-size:24px; font-weight:600;
    color:var(--ink); font-variant-numeric:tabular-nums; letter-spacing:-.02em}

  /* readout rows (horology) */
  .hrow{display:flex; justify-content:space-between; align-items:baseline;
    padding:11px 0; border-bottom:1px solid var(--line)}
  .hrow:last-of-type{border-bottom:0}
  .hk{font-size:13px; color:var(--dim); font-weight:500}
  .hv{font-family:var(--display); font-size:16px; font-weight:600;
    color:var(--ink); font-variant-numeric:tabular-nums; text-align:right}

  /* controls */
  .ctl{display:flex; align-items:center; gap:14px; margin:12px 0}
  .ctl > label{font-size:13px; color:var(--dim); width:92px; flex:none; font-weight:500}
  input[type=range]{flex:1; accent-color:var(--accent); height:4px}
  .ctl output{font-family:var(--mono); color:var(--accent);
    width:70px; text-align:right; font-size:14px; font-variant-numeric:tabular-nums}

  input[type=number]{
    background:var(--panel2); border:1px solid var(--line); border-radius:9px;
    color:var(--ink); font-family:var(--mono); font-size:14px;
    padding:9px 11px; outline:none; transition:border-color .15s;
  }
  input[type=number]:focus{border-color:var(--accent)}

  .actions{display:flex; gap:10px; flex-wrap:wrap; align-items:center}

  button.btn, a.btn{
    font-family:var(--body); font-weight:600; font-size:13.5px;
    text-decoration:none; color:var(--ink); border:1px solid var(--line);
    background:var(--panel2); padding:9px 15px; border-radius:9px; cursor:pointer;
    transition:all .15s; letter-spacing:-.005em; white-space:nowrap;
  }
  button.btn:hover, a.btn:hover{border-color:var(--accent-dim); background:var(--accent-soft); color:var(--accent)}
  button.btn:active{transform:translateY(1px)}
  button.primary{background:var(--accent); border-color:var(--accent); color:#2a2008; font-weight:600}
  button.primary:hover{background:#dcb433; border-color:#dcb433; color:#2a2008}
  button.danger{color:#f0a8a3; border-color:rgba(248,81,73,.3)}
  button.danger:hover{background:rgba(248,81,73,.12); border-color:var(--bad); color:#ffb4ae}

  select.tsel, select{
    background:var(--panel2); border:1px solid var(--line); border-radius:9px;
    color:var(--ink); font-family:var(--body); font-size:13.5px;
    padding:8px 11px; outline:none; cursor:pointer; transition:border-color .15s;
  }
  select:focus{border-color:var(--accent)}

  .toggle-label{font-size:11px; letter-spacing:.05em; text-transform:uppercase;
    color:var(--faint); font-weight:600; margin-left:auto}

  .saved{font-size:13px; color:var(--good); opacity:0; transition:opacity .3s; font-weight:600}
  .saved.show{opacity:1}

  /* help mode */
  #helpToggle.on{background:var(--accent);border-color:var(--accent);color:#fff}
  body.help [data-help]{cursor:help; position:relative;
    outline:1px dashed var(--accent-dim); outline-offset:3px; border-radius:4px}
  .tip{position:fixed; z-index:50; max-width:280px; background:#0b0f14;
    border:1px solid var(--accent-dim); border-radius:10px; padding:11px 13px;
    font-size:13px; line-height:1.45; color:var(--ink); pointer-events:none;
    box-shadow:0 8px 30px rgba(0,0,0,.5); opacity:0; transition:opacity .12s}
  .tip.show{opacity:1}
  .tip .tt{display:block; font-weight:600; color:var(--accent); margin-bottom:3px;
    font-family:var(--display); font-size:12px}
  /* gain meter */
  .gainwrap{margin-top:4px}
  .gbar{position:relative; height:34px; background:var(--panel2);
    border:1px solid var(--line); border-radius:9px; overflow:hidden}
  .gfill{position:absolute; top:0; bottom:0; background:linear-gradient(90deg,var(--accent-dim),var(--accent));
    transition:left .15s,width .15s; opacity:.85}
  .gticks{position:absolute; inset:0; pointer-events:none}
  .gticks span{position:absolute; top:0; bottom:0; width:1px; background:var(--line)}
  .gverdict{font-family:var(--display); font-weight:600; font-size:18px;
    margin:12px 0 4px; letter-spacing:-.01em}
  .gverdict.good{color:var(--good)} .gverdict.clip,.gverdict.hot{color:var(--warn)}
  .gverdict.low,.gverdict.toolow{color:var(--dim)}
  .gadvice{font-size:13.5px; color:var(--dim); line-height:1.5; min-height:38px}
  .gnums{display:flex; gap:18px; margin-top:10px; font-family:var(--mono);
    font-size:12.5px; color:var(--faint)}
  .gnums b{color:var(--ink); font-weight:500}

  /* log viewer table */
  #logTable th{position:sticky;top:0;background:var(--panel2);color:var(--dim);
    font-family:var(--body);font-weight:600;font-size:11px;letter-spacing:.05em;
    text-transform:uppercase;text-align:left;padding:8px 10px;border-bottom:1px solid var(--line)}
  #logTable td{padding:7px 10px;border-bottom:1px solid rgba(63,49,37,.6);
    font-family:var(--mono);font-size:12px;color:var(--ink);white-space:nowrap}
  #logTable tr:last-child td{border-bottom:0}
  #logTable tr.bad td{color:var(--faint)}
  #logTable .del{background:none;border:1px solid var(--line);color:#e08a7a;
    border-radius:6px;padding:3px 8px;cursor:pointer;font-family:var(--body);font-size:11px}
  #logTable .del:hover{border-color:var(--bad);background:rgba(193,84,60,.12)}

  /* full-bleed subtle top accent */
  .topbar{height:3px; background:linear-gradient(90deg,var(--accent),transparent 60%)}
</style></style></head>
<body>
<div class="topbar"></div>
<div class="wrap">
  <header>
    <h1>Grandfather Clock</h1>
    <div style="display:flex;align-items:center;gap:14px">
      <button class="btn" id="helpToggle" title="Explain controls">? Help</button>
      <div class="clock" id="clock">--:--:--</div>
    </div>
  </header>

  <div id="alertBar" class="alertbar" style="display:none"></div>

  <div class="cols">
    <div class="col">

      <div class="panel">
        <h2 data-help="tt:Live signal|Real-time sound level from the mic. Spikes crossing the amber line count as chime candidates.">Live signal</h2>
        <div class="scope"><canvas id="scope" width="728" height="210"></canvas></div>
        <div class="scopectl">
          <label>Scale</label>
          <select id="ymode">
            <option value="lin">Linear</option>
            <option value="log" selected>Log (60 dB)</option>
          </select>
          <label>Y max</label>
          <select id="yscale">
            <option value="auto" selected>Auto</option>
            <option value="0.05">0.05</option>
            <option value="0.1">0.10</option>
            <option value="0.2">0.20</option>
            <option value="0.6">0.60</option>
          </select>
          <label>Window</label>
          <select id="xwin" data-help="tt:Time window|How much history the plot shows. Shorter windows make individual ticks and chimes easier to see; longer ones show the pattern of a whole strike sequence.">
            <option value="400">10 s</option>
            <option value="1200" selected>30 s</option>
            <option value="2400">1 min</option>
            <option value="4800">2 min</option>
            <option value="12000">5 min</option>
            <option value="-900">15 min</option>
            <option value="-1800">30 min</option>
            <option value="-3600">1 hour</option>
          </select>
          <span class="win" id="winNote">25ms bins</span>
        </div>
        <div class="grid" style="margin-top:16px">
          <div class="stat" data-help="tt:Chimes|Total chimes detected since the device last booted."><div class="k">Chimes</div><div class="v" id="chimes">0</div></div>
          <div class="stat"><div class="k">Last chime</div><div class="v" id="last">&mdash;</div></div>
          <div class="stat" data-help="tt:Level|Current sound level above the ambient floor."><div class="k">Level</div><div class="v" id="lvl">0.00</div></div>
          <div class="stat" id="envTile" style="display:none"><div class="k">Temp</div><div class="v" id="temp">&mdash;</div></div>
        </div>
      </div>

      <div class="panel" id="tickPanel">
        <h2 data-help="tt:Timegrapher|Measures the escapement tick-tock. The dot lines\u2019 slope is rate; the gap between them is beat error.">Timegrapher</h2>
        <div class="scope" style="height:180px"><canvas id="tgraph" width="728" height="180"></canvas></div>
        <div class="grid" style="margin:14px 0">
          <div class="stat"><div class="k">Rate</div><div class="v" id="tRate">&mdash;</div></div>
          <div class="stat"><div class="k">Beat error</div><div class="v" id="tBeat">&mdash;</div></div>
          <div class="stat"><div class="k">Beat period</div><div class="v" id="tPeriod">&mdash;</div></div>
        </div>
        <div class="gainwrap" style="margin:4px 0 14px;padding:14px;border:1px solid var(--line);border-radius:9px;background:var(--panel2)">
          <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap">
            <button class="btn" id="tickListen" data-help="tt:Tick sensitivity|Listens for a few seconds and reports whether the escapement is actually audible. Use it while turning the LM393 pot to dial in tick detection.">Listen for ticks</button>
            <select id="tickWin" class="tsel">
              <option value="4">4 s</option><option value="6" selected>6 s</option>
              <option value="10">10 s</option><option value="15">15 s</option>
            </select>
            <span class="hk" id="tlState"></span>
          </div>
          <div class="gverdict" id="tlVerdict" style="font-size:16px;margin:10px 0 3px">&mdash;</div>
          <div class="gadvice" id="tlAdvice" style="min-height:34px">Press Listen while the clock is ticking and the room is quiet.</div>
          <div class="gnums">
            <span>onsets <b id="tlN">&mdash;</b></span>
            <span>interval <b id="tlI">&mdash;</b></span>
            <span>bph <b id="tlB">&mdash;</b></span>
            <span>regularity <b id="tlR">&mdash;</b></span>
          </div>
        </div>
        <div class="actions">
          <button class="btn primary" id="tickArm" data-help="tt:Measure now|Runs a 12-second tick capture immediately instead of waiting for the next scheduled one.">Measure now</button>
          <button class="btn" id="tickCal" data-help="tt:Set as target|Locks the current beat period as the reference, so rate is measured against your clock\u2019s true beat.">Set as target</button>
          <a class="btn" href="/api/tick/log" download="tick.csv">Tick log</a>
          <span class="hk" id="tickInfo" style="margin-left:2px"></span>
          <span class="toggle-label">Tick analysis</span>
          <select id="tickEn" class="tsel"><option value="0">Off</option><option value="1">On</option></select>
        </div>
      </div>

    </div>
    <div class="col">

      <div class="panel">
        <h2 data-help="tt:Horology|Tracks how far off the top of the hour your clock strikes, turns that into a drift rate, and recommends a pendulum adjustment.">Horology</h2>
        <div class="hrow" data-help="tt:Drift rate|How many seconds per day your clock gains (+) or loses (\u2212), from hourly strike timing."><span class="hk">Drift rate</span><span class="hv" id="hRate">&mdash;</span></div>
        <div class="hrow"><span class="hk">Last hourly strike</span><span class="hv" id="hLast">&mdash;</span></div>
        <div class="hrow" data-help="tt:Screw sensitivity|How much one full turn of the rating nut changes the rate. Learned from your past adjustments."><span class="hk">Screw sensitivity</span><span class="hv" id="hK">&mdash;</span></div>
        <div class="hrow" data-help="tt:Recommended adjustment|Suggested turns of the rating nut to zero out the drift. + raises the bob (speeds up)."><span class="hk">Recommended adjustment</span><span class="hv" id="hPred">&mdash;</span></div>
        <div class="ctl" style="margin-top:16px">
          <label>Turns applied</label>
          <input type="number" id="turns" step="0.25" placeholder="+ speeds up" style="width:120px;flex:none">
          <button class="btn primary" id="logAdj">Log</button>
          <span class="saved" id="adjSaved">logged &#10003;</span>
        </div>
        <div class="actions" style="margin-top:12px">
          <button class="btn" id="learn" data-help="tt:Learn chime|Records the next chime and analyzes its pitch so the device can tell real chimes from other noises.">Learn chime</button>
          <span id="learnSt" style="font-size:13px;color:var(--dim)"></span>
          <span class="toggle-label">Tone filter</span>
          <select id="toneEn" class="tsel"><option value="0">Off</option><option value="1">On</option></select>
        </div>
        <div class="actions" style="margin-top:12px">
          <button class="btn" id="windBtn" data-help="tt:Wind log|Records that you wound the clock now, resetting the wind reminder countdown.">I wound the clock</button>
          <span class="hk" id="windInfo" style="margin-left:2px"></span>
          <span class="toggle-label">Half-hour</span>
          <select id="halfEn" class="tsel"><option value="0">Off</option><option value="1">On</option></select>
        </div>
      </div>

      <div class="panel" id="gainPanel">
        <h2 data-help="tt:Microphone gain|Set the little screwdriver pot on the LM393 mic board. Make a steady sound near the mic (or wait for a chime) and adjust until this reads Good.">Microphone gain</h2>
        <div class="gainwrap">
          <div class="gbar" data-help="tt:Signal swing|Shows the raw microphone signal range. It should fill a healthy middle band \u2014 not flat (too quiet) and not slammed to the edges (clipping).">
            <div class="gticks"><span style="left:2%"></span><span style="left:98%"></span></div>
            <div class="gfill" id="gFill" style="left:50%;width:0%"></div>
          </div>
          <div class="gverdict" id="gVerdict">\u2014</div>
          <div class="gadvice" id="gAdvice">Turn on the mic and make a sound to begin.</div>
          <div class="gnums">
            <span>min <b id="gMin">\u2014</b></span>
            <span>max <b id="gMax">\u2014</b></span>
            <span>swing <b id="gSwing">\u2014</b></span>
            <span>clip <b id="gClip">\u2014</b></span>
          </div>
          <div class="gnums" style="margin-top:8px;padding-top:8px;border-top:1px solid var(--line)">
            <span>last chime peak <b id="gPeak">\u2014</b></span>
            <span>recent <b id="gPeaks">\u2014</b></span>
          </div>
        </div>
      </div>

      <div class="panel">
        <h2>Detection tuning</h2>
        <div class="ctl">
          <label data-help="tt:Threshold|How loud a sound must be (above the floor) to count as a chime. Lower = more sensitive.">Threshold</label>
          <input type="range" id="thr" min="0.01" max="0.6" step="0.005">
          <output id="thrV">0.18</output>
        </div>
        <div class="ctl">
          <label data-help="tt:Refractory|Dead time after a detected chime before another can register. Prevents one strike counting twice.">Refractory</label>
          <input type="range" id="ref" min="200" max="4000" step="100">
          <output id="refV">1200ms</output>
        </div>
        <div class="actions">
          <button class="btn primary" id="apply">Apply &amp; save</button>
          <span class="saved" id="saved">saved &#10003;</span>
        </div>
      </div>

      <div class="panel">
        <h2 data-help="tt:Log viewer|Browse the recorded logs in place. On the drift log you can delete a bad measurement \u2014 one mis-counted strike event can visibly skew the drift regression.">Log viewer</h2>
        <div class="actions" style="margin-bottom:12px">
          <select id="logPick" class="tsel">
            <option value="drift">Drift measurements</option>
            <option value="chimes">Chime strikes</option>
          </select>
          <button class="btn primary" id="logLoad">Load</button>
          <span class="hk" id="logInfo"></span>
        </div>
        <div id="logWrap" style="max-height:330px;overflow:auto;border:1px solid var(--line);border-radius:9px;display:none">
          <table id="logTable" style="width:100%;border-collapse:collapse;font-size:12.5px"></table>
        </div>
      </div>

      <div class="panel">
        <h2>Logs &amp; device</h2>
        <div class="actions">
          <button class="btn primary" onclick="dlXlsx('chimes')">Chime log (Excel)</button>
          <button class="btn primary" onclick="dlXlsx('drift')">Drift log (Excel)</button>
          <a class="btn" href="/api/log" download="chimes.csv">CSV</a>
          <a class="btn" href="/api/drift" download="drift.csv">CSV</a>
          <a class="btn" href="/update" target="_blank" data-help="tt:Firmware update|Upload a compiled .bin straight from the browser. This bypasses the Arduino IDE, espota and mDNS entirely \u2014 use Sketch > Export Compiled Binary, then drop the file here.">Update firmware</a>
          <button class="btn danger" id="reset">Reset WiFi</button>
        </div>
      </div>

    </div>
  </div>
</div>

<script>
// Canvas colours come from the theme's CSS variables so the scope and
// timegrapher always match the palette (no hard-coded colours here).
const CV=(n)=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
// ---- scope trace: circular buffer of recent excess levels ----
const cv=document.getElementById('scope'), cx=cv.getContext('2d');
// Bins never received are NaN and drawn as a blank gap. (A zero-filled ring
// that was only partly overwritten showed whatever sat in a slot 5 minutes
// earlier, so a missed chime looked like silence.)
const N=12000, BIN_MS=25, buf=new Float32Array(N).fill(NaN);  // ring holds up to 5 min
const coarse=new Uint8Array(N);   // 1 = patched from the device's 1-second history
const HIST_BINS=40;               // scope bins per history entry (firmware HIST_BINS)
let winBins=1200;                                   // visible span (user set)
// Windows longer than the 5-minute live ring switch to HISTORY MODE: the
// device keeps one peak per second for an hour, so a long view is populated
// immediately instead of waiting (and it survives a page refresh).
let histMode=0;            // 0 = live 25ms stream, else window length in sec
let histData=null;         // Float32Array of 1-second peaks (oldest first)
function fetchHistory(){
  if(!histMode) return;
  fetch('/api/history').then(r=>r.text()).then(t=>{
    const nl=t.indexOf('\n'); if(nl<0) return;
    const hdr=t.slice(0,nl).split(','), scale=+hdr[0]||10000;
    const body=t.slice(nl+1).trim();
    if(!body){histData=new Float32Array(0);return;}
    const parts=body.split(',');
    const a=new Float32Array(parts.length);
    for(let i=0;i<parts.length;i++)a[i]=(+parts[i])/scale;
    histData=a;
  }).catch(()=>{});
}
setInterval(()=>{if(histMode)fetchHistory();},10000);
let newestSeq=-1, newestT=0;   // absolute index + arrival time of newest bin
let threshold=0.18;            // the detector's live value, from /api/state
let thrPreview=null;           // slider position not yet applied
let yMode='auto';              // 'auto' or fixed top-of-scale
let scaleType='log';           // 'lin' | 'log' — log by default: it
                               // shows quiet ticks and loud chimes at once
let yMax=0.2;
const LOG_DECADES=3;           // log view spans yMax down to yMax/1000

// Render at native device resolution — upscaled canvases look blurry
// and make motion appear juddery.
function fitCanvas(){
  const dpr=window.devicePixelRatio||1;
  const cssW=cv.clientWidth||728, cssH=200;
  cv.width=Math.round(cssW*dpr); cv.height=Math.round(cssH*dpr);
  cx.setTransform(dpr,0,0,dpr,0,0);
}
window.addEventListener('resize',fitCanvas);

function yOf(v,h){
  if(scaleType==='log'){
    const vmin=yMax/Math.pow(10,LOG_DECADES);
    return h-(Math.log10(Math.max(v,vmin)/vmin)/LOG_DECADES)*h;
  }
  return h-Math.min(v/yMax,1)*h;
}

function drawScope(){
  const w=cv.clientWidth||728, h=200;
  // --- scale ---
  let target;
  if(yMode==='auto'){
    let m=0;
    if(histMode&&histData&&histData.length){
      const span=Math.min(histMode,histData.length), st=histData.length-span;
      for(let i=0;i<span;i++) if(histData[st+i]>m)m=histData[st+i];
    }else{
      const lo=Math.max(0,newestSeq-winBins+1);
      for(let b=lo;b<=newestSeq;b++){const v=buf[b%N]; if(v>m)m=v;}
    }
    target=Math.max(0.02,Math.min(0.6,m*1.25));
  }else target=+yMode;
  yMax+=(target-yMax)*0.08;

  cx.clearRect(0,0,w,h);
  // vertical grid
  cx.strokeStyle=CV('--grid');cx.lineWidth=1;
  for(let x=0;x<=w;x+=w/8){cx.beginPath();cx.moveTo(x,0);cx.lineTo(x,h);cx.stroke();}
  // horizontal grid: quarters in linear, decades in log
  if(scaleType==='log'){
    cx.fillStyle=CV('--axis');cx.font='10px monospace';
    for(let d=1;d<LOG_DECADES;d++){
      const gy=h-(d/LOG_DECADES)*h;
      cx.beginPath();cx.moveTo(0,gy);cx.lineTo(w,gy);cx.stroke();
      cx.fillText((yMax/Math.pow(10,LOG_DECADES-d)).toPrecision(2),4,gy-3);
    }
  }else{
    for(let y=0;y<=h;y+=h/4){cx.beginPath();cx.moveTo(0,y);cx.lineTo(w,y);cx.stroke();}
  }
  // threshold line (when on-scale)
  const ty=yOf(threshold,h);
  if(ty>=0&&ty<=h){
    cx.strokeStyle=CV('--thresh');cx.setLineDash([6,5]);
    cx.beginPath();cx.moveTo(0,ty);cx.lineTo(w,ty);cx.stroke();cx.setLineDash([]);
  }
  // unapplied slider position, fainter, so tuning can be previewed
  if(thrPreview!==null&&Math.abs(thrPreview-threshold)>1e-4){
    const py=yOf(thrPreview,h);
    if(py>=0&&py<=h){
      cx.save();cx.globalAlpha=0.45;cx.strokeStyle=CV('--thresh');cx.setLineDash([2,4]);
      cx.beginPath();cx.moveTo(0,py);cx.lineTo(w,py);cx.stroke();cx.restore();
    }
  }
  // --- history mode: draw device-supplied 1-second peaks ---
  if(histMode){
    if(histData && histData.length){
      const span=Math.min(histMode,histData.length);
      const start=histData.length-span;
      cx.strokeStyle=CV('--trace');cx.lineWidth=1.5;
      cx.lineJoin='round';cx.lineCap='round';
      cx.shadowColor=CV('--traceglow');cx.shadowBlur=8;
      cx.beginPath();
      for(let i=0;i<span;i++){
        const x=(i/(span-1||1))*w, y=yOf(histData[start+i],h);
        i?cx.lineTo(x,y):cx.moveTo(x,y);
      }
      cx.stroke();cx.shadowBlur=0;
    }else{
      cx.fillStyle=CV('--axis');cx.font='12px monospace';
      cx.fillText('loading history\u2026',12,h/2);
    }
    cx.fillStyle=CV('--axis');cx.font='12px monospace';
    cx.fillText(yMax.toFixed(3)+(scaleType==='log'?' log':'')+'  \u00B7  1s bins',8,14);
    requestAnimationFrame(drawScope);
    return;
  }

  // --- trace: time-interpolated scroll ---
  // Bins arrive in bursts each poll; sliding by wall-clock time between
  // arrivals turns the burst-jumps into continuous 60fps motion.
  if(newestSeq>=0){
    const pos=newestSeq+Math.min((performance.now()-newestT)/BIN_MS,40);
    const pxPerBin=w/winBins;
    cx.strokeStyle=CV('--trace');cx.lineWidth=1.5;
    cx.lineJoin='round';cx.lineCap='round';
    cx.shadowColor=CV('--traceglow');cx.shadowBlur=8;
    const first=Math.max(0,newestSeq-winBins+1);
    // pass 0: 25ms bins; pass 1: stretches patched from 1-second history, fainter
    for(let pass=0;pass<2;pass++){
      cx.globalAlpha=pass?0.45:1;
      cx.beginPath();let started=false;
      for(let b=first;b<=newestSeq;b++){
        const x=w-(pos-b)*pxPerBin;
        if(x<-2)continue;
        const v=buf[b%N];
        if(v!==v||coarse[b%N]!==pass){started=false;continue;}   // NaN: gap, lift the pen
        const y=yOf(v,h);
        started?cx.lineTo(x,y):(cx.moveTo(x,y),started=true);
      }
      cx.stroke();
    }
    cx.globalAlpha=1;cx.shadowBlur=0;
  }
  // scale readout
  cx.fillStyle=CV('--axis');cx.font='12px monospace';
  cx.fillText(yMax.toFixed(3)+(scaleType==='log'?' log':''),8,14);
  requestAnimationFrame(drawScope);
}
fitCanvas();
document.getElementById('yscale').onchange=function(){yMode=this.value;};
document.getElementById('xwin').onchange=function(){
  const v=+this.value;
  const note=document.getElementById('winNote');
  if(v<0){                       // history mode: value is -seconds
    histMode=-v; histData=null; fetchHistory();
    note.textContent=(-v/60)+' min \u00B7 1s bins \u00B7 from device';
  }else{
    histMode=0; winBins=v;
    const secs=winBins*BIN_MS/1000;
    note.textContent=(secs>=60?(secs/60)+' min':secs+' s')+' \u00B7 25ms bins';
  }
};
document.getElementById('ymode').onchange=function(){scaleType=this.value;};
requestAnimationFrame(drawScope);

// ---- polling, decoupled from the render loop ----
function fmtAge(epoch,nowEpoch,valid){
  if(!epoch) return '—';
  if(!valid) return '?';
  let a=nowEpoch-epoch; if(a<0)a=0;
  if(a<90) return a+'s';
  if(a<5400) return Math.floor(a/60)+'m';
  if(a<129600) return Math.floor(a/3600)+'h';
  return Math.floor(a/86400)+'d';
}
let lastChimeCount=-1, recentPeaks=[];
// Store bins [scopeFrom, scopeFrom+scope.length); returns how many newer bins
// the device still holds for us. Bins that aged out of the device's ring
// before we asked become NaN gaps rather than keeping stale values, and are
// queued for backfill from the device's 1-second history.
let gapLo=-1, gapHi=-1;        // span of bins awaiting backfill
let devSeq=0, bootGen=0;       // device's bin count; bumps when it reboots
function ingestScope(s){
  const arr=s.scope||[], from=s.scopeFrom, seq=s.scopeSeq;
  if(newestSeq>=0&&seq-1<newestSeq){                               // device rebooted
    buf.fill(NaN);coarse.fill(0);newestSeq=-1;gapLo=gapHi=-1;bootGen++;
  }
  devSeq=seq;
  if(newestSeq>=0&&from>newestSeq+1){
    const lo=newestSeq+1;
    for(let b=lo,end=Math.min(from,lo+N);b<end;b++){buf[b%N]=NaN;coarse[b%N]=0;}
    gapLo=gapLo<0?lo:Math.min(gapLo,lo); gapHi=Math.max(gapHi,from);
    backfill();
  }
  for(let i=0;i<arr.length;i++){buf[(from+i)%N]=arr[i];coarse[(from+i)%N]=0;}
  const last=from+arr.length-1;
  if(last>newestSeq){newestSeq=last;newestT=performance.now();}
  return seq-1-newestSeq;
}
// A throttled background tab may poll only once a minute, far longer than
// the device's 6.4s scope ring. Fill those gaps from /api/history, whose
// entries are exactly HIST_BINS scope bins each: every chime still shows at
// its true peak, at 1-second resolution (drawn fainter).
let backfilling=false;
async function backfill(){
  if(backfilling||gapLo<0) return;
  backfilling=true;
  const lo=Math.max(gapLo,newestSeq-N+1), hi=gapHi, gen=bootGen;
  gapLo=gapHi=-1;
  let ok=true;
  if(hi>lo){
    try{
      const want=Math.ceil((devSeq-lo)/HIST_BINS)+5;
      const t=await (await fetch('/api/history?n='+want)).text();
      const nl=t.indexOf('\n'), hd=t.slice(0,nl).split(',');
      const scale=+hd[0]||10000, firstBin=+hd[3], per=+hd[4];
      const body=t.slice(nl+1).trim(), v=body?body.split(','):[];
      if(gen===bootGen&&per>0)
        for(let b=Math.max(lo,newestSeq-N+1);b<hi;b++){
          if(buf[b%N]===buf[b%N]) continue;          // real 25ms data present
          const i=Math.floor((b-firstBin)/per);
          if(i>=0&&i<v.length){buf[b%N]=+v[i]/scale;coarse[b%N]=1;}
        }
    }catch(e){
      ok=false;
      if(gen===bootGen){gapLo=gapLo<0?lo:Math.min(gapLo,lo);gapHi=Math.max(gapHi,hi);}
    }
  }
  backfilling=false;
  if(gapLo>=0) setTimeout(backfill,ok?0:2000);   // gaps noted meanwhile, or retry
}
let pollBusy=false, pollTimer=0;
async function poll(){
  if(pollBusy) return;
  pollBusy=true; clearTimeout(pollTimer);
  let behind=0;
  try{
    const s=await (await fetch('/api/state?since='+(newestSeq+1))).json();
    behind=ingestScope(s);
    threshold=s.threshold;
    document.getElementById('chimes').textContent=s.chimes;
    // Track true chime peaks so gain can be judged on real loudness.
    if(s.chimes!==lastChimeCount){
      lastChimeCount=s.chimes;
      if(s.lastPeak>0){
        recentPeaks.push(s.lastPeak); if(recentPeaks.length>6)recentPeaks.shift();
        const gp=document.getElementById('gPeak'), gl=document.getElementById('gPeaks');
        if(gp) gp.textContent=s.lastPeak.toFixed(4);
        if(gl) gl.textContent=recentPeaks.map(v=>v.toFixed(3)).join('  ');
      }
    }
    document.getElementById('lvl').textContent=(+s.peak).toFixed(3);
    document.getElementById('last').textContent=fmtAge(s.lastChime,s.epoch,!!s.timeValid);
    if(s.timeValid){
      const d=new Date(s.epoch*1000);
      document.getElementById('clock').textContent=d.toLocaleTimeString();
    }else{
      document.getElementById('clock').textContent='NTP syncing…';
    }
  }catch(e){}
  pollBusy=false;
  pollTimer=setTimeout(poll,behind>0?0:200);   // after a stall, catch up immediately
}
// A hidden tab's next poll may be throttled up to a minute away; fetch as
// soon as the page is looked at again.
document.addEventListener('visibilitychange',()=>{if(!document.hidden)poll();});

// ---- load config, wire tuning controls ----
const thr=document.getElementById('thr'),thrV=document.getElementById('thrV');
const ref=document.getElementById('ref'),refV=document.getElementById('refV');
// The solid plot line is the threshold the DEVICE is using (from /api/state);
// dragging the slider only previews until Apply. Previously the line followed
// the slider, so an unapplied value made logged chimes appear below it.
thr.oninput=()=>{thrV.textContent=(+thr.value).toFixed(3);thrPreview=+thr.value;};
ref.oninput=()=>{refV.textContent=ref.value+'ms';};
fetch('/api/config').then(r=>r.json()).then(c=>{
  thr.value=c.threshold;thrV.textContent=(+c.threshold).toFixed(3);threshold=+c.threshold;
  ref.value=c.refractoryMs;refV.textContent=c.refractoryMs+'ms';
});
document.getElementById('apply').onclick=()=>{
  const body='threshold='+thr.value+'&refractoryMs='+ref.value;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(r=>r.json()).then(()=>{
      threshold=+thr.value;thrPreview=null;
      const s=document.getElementById('saved');s.classList.add('show');
      setTimeout(()=>s.classList.remove('show'),1200);
    });
};
// ---- horology panel ----
function fmtOffset(o){return (o>=0?'+':'')+o.toFixed(1)+'s';}
async function pollHoro(){
  try{
    const h=await (await fetch('/api/horology')).json();
    document.getElementById('hRate').textContent =
      h.rateValid? (h.rate>=0?'+':'')+h.rate.toFixed(1)+' s/day ('+h.nMeas+' pts)'
                 : 'collecting\u2026 ('+h.nMeas+' pts)';
    document.getElementById('hLast').textContent =
      h.lastEpoch? h.lastCount+' strikes @ '+
        new Date(h.lastEpoch*1000).toLocaleTimeString([],{hour:'numeric',minute:'2-digit'})+
        ' \u2192 '+fmtOffset(h.lastOffset)+(h.lastValid?'':' (ignored)')
      : 'none yet';
    document.getElementById('hK').textContent =
      h.kValid? h.k.toFixed(1)+' s/day per turn' : 'needs an adjustment cycle';
    document.getElementById('hPred').textContent =
      h.predValid? (h.predTurns>=0?'+':'')+h.predTurns.toFixed(2)+' turns'
                 : '\u2014';
    setAlert(h);
    if(halfEn.value!==String(h.halfHour))halfEn.value=String(h.halfHour);
  }catch(e){}
  setTimeout(pollHoro,5000);
}
pollHoro();

document.getElementById('logAdj').onclick=()=>{
  const t=parseFloat(document.getElementById('turns').value);
  if(!t){alert('Enter a nonzero number of turns');return;}
  fetch('/api/adjust',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'turns='+t}).then(r=>r.json()).then(()=>{
      const s=document.getElementById('adjSaved');
      s.classList.add('show');setTimeout(()=>s.classList.remove('show'),1500);
      document.getElementById('turns').value='';
    });
};

const learnSt=document.getElementById('learnSt');
document.getElementById('learn').onclick=()=>{
  fetch('/api/learn/start',{method:'POST'}).then(()=>{
    learnSt.textContent='armed \u2014 make the clock chime (60s window)';
    const iv=setInterval(async()=>{
      const st=await (await fetch('/api/learn/status')).json();
      if(!st.armed){
        clearInterval(iv);
        learnSt.textContent=st.valid?
          'learned: '+st.f1.toFixed(0)+' Hz'+(st.f2>0?' + '+st.f2.toFixed(0)+' Hz':'')
          : 'no capture \u2014 try again';
      }
    },1000);
  });
};
const toneEn=document.getElementById('toneEn');
fetch('/api/learn/status').then(r=>r.json()).then(st=>{
  toneEn.value=st.enabled?'1':'0';
  if(st.f1>0)learnSt.textContent='learned: '+st.f1.toFixed(0)+' Hz'+(st.f2>0?' + '+st.f2.toFixed(0)+' Hz':'');
});
toneEn.onchange=function(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'toneEnabled='+this.value});
};

// ---- timegrapher (Phase E) ----
const tg=document.getElementById('tgraph'), tgx=tg.getContext('2d');
const TGN=120; const tgBuf=[];   // recent {rate,beatErr} samples
function drawTGraph(){
  const w=tg.clientWidth||728, h=170;
  const dpr=window.devicePixelRatio||1;
  if(tg.width!==Math.round(w*dpr)){tg.width=Math.round(w*dpr);tg.height=Math.round(h*dpr);tgx.setTransform(dpr,0,0,dpr,0,0);}
  tgx.clearRect(0,0,w,h);
  tgx.strokeStyle=CV('--grid');tgx.lineWidth=1;
  for(let x=0;x<=w;x+=w/6){tgx.beginPath();tgx.moveTo(x,0);tgx.lineTo(x,h);tgx.stroke();}
  for(let y=0;y<=h;y+=h/4){tgx.beginPath();tgx.moveTo(0,y);tgx.lineTo(w,y);tgx.stroke();}
  // center line = on-time
  tgx.strokeStyle=CV('--thresh');tgx.beginPath();tgx.moveTo(0,h/2);tgx.lineTo(w,h/2);tgx.stroke();
  // Classic timegrapher read: two dot lines (tick & tock). The lines'
  // common SLOPE encodes rate (accumulated phase drift over successive
  // measurements); the vertical GAP between the two lines is beat error.
  // Phase accumulates: each measurement's rate advances the running drift.
  tgx.fillStyle=CV('--trace');tgx.shadowColor=CV('--traceglow');tgx.shadowBlur=6;
  const beScale=(h/2)/40.0;      // beat-error axis: +/-40 ms full height
  const phScale=(h/2)/30.0;      // phase axis: +/-30 s of drift full height
  let phase=0;                   // seconds of accumulated drift (visual)
  const N=tgBuf.length;
  for(let i=0;i<N;i++){
    const s=tgBuf[i];
    // each window ~= TICK_CAPTURE_INTERVAL; advance drift by rate*interval.
    phase += s.rate*(300/86400);           // 5-min window's worth of drift
    if(phase>30)phase=30; if(phase<-30)phase=-30;
    const x=(i/(TGN-1))*w;
    const yMid=h/2 - phase*phScale;
    const be=Math.max(-40,Math.min(40,s.beatErr));
    tgx.globalAlpha=0.5+0.5*(i/N);
    tgx.beginPath();tgx.arc(x,Math.max(4,Math.min(h-4,yMid-be*beScale)),2,0,7);tgx.fill();
    tgx.beginPath();tgx.arc(x,Math.max(4,Math.min(h-4,yMid+be*beScale)),2,0,7);tgx.fill();
  }
  tgx.globalAlpha=1;tgx.shadowBlur=0;
  tgx.fillStyle=CV('--axis');tgx.font='11px monospace';
  tgx.fillText('slope = rate \u2022 line gap = beat error',8,14);
  requestAnimationFrame(drawTGraph);
}
requestAnimationFrame(drawTGraph);

async function pollTick(){
  try{
    const t=await (await fetch('/api/tick')).json();
    document.getElementById('tickEn').value=String(t.enabled);
    if(t.valid){
      document.getElementById('tRate').textContent=(t.rate>=0?'+':'')+t.rate.toFixed(1)+' s/day';
      document.getElementById('tBeat').textContent=t.beatErrorMs.toFixed(1)+' ms';
      document.getElementById('tPeriod').textContent=t.beatPeriod.toFixed(3)+' s';
      document.getElementById('tickInfo').textContent=t.nOnsets+' onsets'+
        (t.nominal>0?' \u2022 target '+t.nominal.toFixed(3)+'s':' \u2022 auto');
      tgBuf.push({rate:t.rate,beatErr:t.beatErrorMs});
      if(tgBuf.length>TGN)tgBuf.shift();
    }else if(t.enabled){
      document.getElementById('tickInfo').textContent='listening\u2026 ('+t.nOnsets+' onsets last window)';
    }
  }catch(e){}
  setTimeout(pollTick, 5000);
}
pollTick();

document.getElementById('tickArm').onclick=()=>{
  document.getElementById('tickInfo').textContent='measuring (~12s)\u2026';
  fetch('/api/tick/arm',{method:'POST'});
};
document.getElementById('tickCal').onclick=()=>{
  fetch('/api/tick/calibrate',{method:'POST'}).then(r=>r.json()).then(j=>{
    document.getElementById('tickInfo').textContent=j.ok?'target set':'need a valid measurement first';
  });
};
document.getElementById('tickEn').onchange=function(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'tickEnabled='+this.value});
};

// ---- tick sensitivity listener ----
document.getElementById('tickListen').onclick=function(){
  const secs=document.getElementById('tickWin').value;
  const st=document.getElementById('tlState');
  this.disabled=true; st.textContent='listening '+secs+'s\u2026';
  fetch('/api/tick/listen?s='+secs,{method:'POST'}).then(r=>r.json()).then(d=>{
    st.textContent='';
    const v=document.getElementById('tlVerdict');
    v.textContent=d.verdict.charAt(0).toUpperCase()+d.verdict.slice(1);
    v.className='gverdict '+(d.verdict==='good'?'good':(d.verdict==='noisy'?'hot':'low'));
    document.getElementById('tlAdvice').textContent=d.advice;
    document.getElementById('tlN').textContent=d.onsets;
    document.getElementById('tlI').textContent=d.medianIntvl>0?d.medianIntvl.toFixed(3)+' s':'\u2014';
    document.getElementById('tlB').textContent=d.bph>0?Math.round(d.bph):'\u2014';
    document.getElementById('tlR').textContent=(d.regularity*100).toFixed(0)+'%';
  }).catch(()=>{st.textContent='failed';})
    .finally(()=>{document.getElementById('tickListen').disabled=false;});
};


// ===== XLSX export (no dependencies) =====
// The device serves plain CSV; the browser turns it into a properly typed
// workbook. Dates/times become real Excel date/time values (ms preserved),
// numbers become numbers - so sorting, charting and formatting all work.
const _CRCT=(()=>{const t=new Uint32Array(256);for(let n=0;n<256;n++){let c=n;
 for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[n]=c>>>0;}return t;})();
function _crc32(u){let c=0xFFFFFFFF;for(let i=0;i<u.length;i++)c=_CRCT[(c^u[i])&255]^(c>>>8);return (c^0xFFFFFFFF)>>>0;}
const _enc=s=>new TextEncoder().encode(s);
const _xe=s=>String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
function _dser(y,m,d){return Date.UTC(y,m-1,d)/86400000+25569;}
function _zip(fs){const ch=[],cd=[];let off=0;
 for(const f of fs){const nm=_enc(f.name),da=_enc(f.data),cr=_crc32(da);
  const l=new DataView(new ArrayBuffer(30));l.setUint32(0,0x04034b50,true);l.setUint16(4,20,true);
  l.setUint32(14,cr,true);l.setUint32(18,da.length,true);l.setUint32(22,da.length,true);
  l.setUint16(26,nm.length,true);ch.push(new Uint8Array(l.buffer),nm,da);
  const c=new DataView(new ArrayBuffer(46));c.setUint32(0,0x02014b50,true);c.setUint16(4,20,true);
  c.setUint16(6,20,true);c.setUint32(16,cr,true);c.setUint32(20,da.length,true);
  c.setUint32(24,da.length,true);c.setUint16(28,nm.length,true);c.setUint32(42,off,true);
  cd.push(new Uint8Array(c.buffer),nm);off+=30+nm.length+da.length;}
 let cl=0;for(const c of cd)cl+=c.length;
 const e=new DataView(new ArrayBuffer(22));e.setUint32(0,0x06054b50,true);
 e.setUint16(8,fs.length,true);e.setUint16(10,fs.length,true);e.setUint32(12,cl,true);e.setUint32(16,off,true);
 const all=[...ch,...cd,new Uint8Array(e.buffer)];let tot=0;for(const a of all)tot+=a.length;
 const o=new Uint8Array(tot);let p=0;for(const a of all){o.set(a,p);p+=a.length;}return o;}
function _xlsx(cols,rows,name){
 const CN=n=>{let s='';n++;while(n>0){const m=(n-1)%26;s=String.fromCharCode(65+m)+s;n=(n-m-1)/26;}return s;};
 const SS={date:2,time:3,num:4,num4:5,int:6,num1:7,text:0};
 let sd='<row r="1">';
 cols.forEach((c,i)=>{sd+=`<c r="${CN(i)}1" s="1" t="inlineStr"><is><t>${_xe(c.h)}</t></is></c>`;});
 sd+='</row>';
 rows.forEach((r,ri)=>{sd+=`<row r="${ri+2}">`;
  cols.forEach((c,i)=>{const v=r[i];if(v===null||v===undefined||v==='')return;
   const rf=`${CN(i)}${ri+2}`,s=SS[c.t]!==undefined?SS[c.t]:0;
   sd+= c.t==='text' ? `<c r="${rf}" s="${s}" t="inlineStr"><is><t>${_xe(v)}</t></is></c>`
                     : `<c r="${rf}" s="${s}"><v>${v}</v></c>`;});
  sd+='</row>';});
 const cx=cols.map((c,i)=>`<col min="${i+1}" max="${i+1}" width="${c.w||14}" customWidth="1"/>`).join('');
 const sheet=`<?xml version="1.0" encoding="UTF-8" standalone="yes"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1"/><sheetViews><sheetView workbookViewId="0" tabSelected="1"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews><sheetFormatPr defaultRowHeight="15"/><cols>${cx}</cols><sheetData>${sd}</sheetData><autoFilter ref="A1:${CN(cols.length-1)}${rows.length+1}"/></worksheet>`;
 const styles=`<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="5"><numFmt numFmtId="164" formatCode="yyyy\-mm\-dd"/><numFmt numFmtId="165" formatCode="hh:mm:ss.000"/><numFmt numFmtId="166" formatCode="0.000"/><numFmt numFmtId="167" formatCode="0.0000"/><numFmt numFmtId="168" formatCode="0.0"/></numFmts><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FFF2E8D9"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="8"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/><xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/><xf numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/><xf numFmtId="166" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/><xf numFmtId="167" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/><xf numFmtId="1" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/><xf numFmtId="168" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>`;
 return _zip([
  {name:'[Content_Types].xml',data:'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>'},
  {name:'_rels/.rels',data:'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'},
  {name:'xl/workbook.xml',data:`<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="${_xe(name)}" sheetId="1" r:id="rId1"/></sheets></workbook>`},
  {name:'xl/_rels/workbook.xml.rels',data:'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>'},
  {name:'xl/styles.xml',data:styles},
  {name:'xl/worksheets/sheet1.xml',data:sheet}]);}

// Column schemas keyed by CSV header name -> Excel type
const XSPEC={
 date:{h:'Date',t:'date',w:12}, time:{h:'Time',t:'time',w:14},
 epoch_ms:{h:'Epoch (ms)',t:'text',w:16}, epoch:{h:'Epoch',t:'text',w:14},
 sec_from_hour:{h:'Sec from hour',t:'num',w:15},
 offset_mmss:{h:'Offset (m:ss)',t:'text',w:14},
 peak:{h:'Peak',t:'num4',w:10}, strikes:{h:'Strikes',t:'int',w:9},
 expected:{h:'Expected',t:'int',w:10}, valid:{h:'Valid',t:'text',w:8},
 tempC:{h:'Temp (C)',t:'num1',w:10}, half_hour:{h:'Half hour',t:'text',w:11}};

function csvToXlsx(csv,sheetName,fname){
 const lines=csv.trim().split('\n');
 if(lines.length<2){alert('Log is empty - nothing to export.');return;}
 const head=lines[0].split(',');
 const cols=head.map(h=>XSPEC[h]||{h:h,t:'text',w:14});
 const rows=lines.slice(1).filter(l=>l.length>2).map(l=>{
  const c=l.split(',');
  return head.map((h,i)=>{
   const raw=(c[i]===undefined?'':c[i]).trim(), ty=cols[i].t;
   if(ty==='date'){const m=raw.match(/^(\d{4})-(\d{2})-(\d{2})$/);
     return m?_dser(+m[1],+m[2],+m[3]):raw;}
   if(ty==='time'){const m=raw.match(/^(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?$/);
     if(!m)return raw;
     const ms=m[4]?parseInt((m[4]+'00').slice(0,3),10):0;
     return ((+m[1])*3600+(+m[2])*60+(+m[3])+ms/1000)/86400;}
   if(ty==='num'||ty==='num4'||ty==='num1'||ty==='int'){
     const v=parseFloat(raw); return isNaN(v)?'':v;}
   if(h==='valid')     return raw==='1'?'yes':'no';
   if(h==='half_hour') return raw==='1'?'yes':'no';
   return raw;});});
 const blob=new Blob([_xlsx(cols,rows,sheetName)],
   {type:'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'});
 const a=document.createElement('a');
 a.href=URL.createObjectURL(blob); a.download=fname;
 document.body.appendChild(a); a.click();
 setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove();},2000);
}
function dlXlsx(kind){
 const url = kind==='drift' ? '/api/drift' : '/api/log';
 const info=document.getElementById('logInfo');
 if(info) info.textContent='building workbook\u2026';
 fetch(url).then(r=>r.text()).then(t=>{
   csvToXlsx(t, kind==='drift'?'Drift':'Chimes',
             kind==='drift'?'drift.xlsx':'chimes.xlsx');
   if(info) info.textContent='';
 }).catch(()=>{if(info) info.textContent='export failed';});
}

// ---- log viewer / editor ----
const MAXROWS=250;
function renderLog(kind,text){
  const lines=text.trim().split('\n');
  if(lines.length<2){
    document.getElementById('logWrap').style.display='none';
    fetch('/api/logstat').then(r=>r.json()).then(d=>{
      document.getElementById('logInfo').textContent='empty \u2014 '+d.why;
    }).catch(()=>{document.getElementById('logInfo').textContent='log is empty';});
    return;}
  const head=lines[0].split(',');
  // Resolve columns BY NAME, never by fixed index: the CSV layout has changed
  // before (date/time split into two columns) and hardcoded offsets silently
  // sent the wrong epoch to the delete endpoint.
  const iEpoch=head.indexOf('epoch'), iValid=head.indexOf('valid');
  let rows=lines.slice(1).filter(l=>l.length>2);
  const total=rows.length;
  rows=rows.slice(-MAXROWS).reverse();          // newest first
  const t=document.getElementById('logTable');
  let html='<tr>'+head.map(h=>'<th>'+h+'</th>').join('')
          +(kind==='drift'?'<th></th>':'')+'</tr>';
  for(const r of rows){
    const c=r.split(',');
    const invalid=(kind==='drift'&&iValid>=0&&c[iValid]==='0');
    html+='<tr class="'+(invalid?'bad':'')+'">'+c.map(x=>'<td>'+x+'</td>').join('');
    if(kind==='drift'&&iEpoch>=0)
      html+='<td><button class="del" data-ep="'+c[iEpoch]+'">delete</button></td>';
    else if(kind==='drift') html+='<td></td>';
    html+='</tr>';
  }
  t.innerHTML=html;
  document.getElementById('logWrap').style.display='';
  document.getElementById('logInfo').textContent =
    total+' rows'+(total>MAXROWS?' (showing newest '+MAXROWS+')':'');
  if(kind==='drift') t.querySelectorAll('.del').forEach(b=>{
    b.onclick=()=>{
      if(!confirm('Delete this measurement? It will no longer affect the drift rate.'))return;
      fetch('/api/drift/delete',{method:'POST',
        headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'epoch='+b.dataset.ep}).then(r=>r.json()).then(j=>{
          if(j.ok) loadLog(); else alert('Row not found');
        });
    };
  });
}
function loadLog(){
  const kind=document.getElementById('logPick').value;
  document.getElementById('logInfo').textContent='loading\u2026';
  fetch(kind==='drift'?'/api/drift':'/api/log').then(r=>r.text())
    .then(t=>renderLog(kind,t))
    .catch(()=>{document.getElementById('logInfo').textContent='failed';});
}
document.getElementById('logLoad').onclick=loadLog;

// ---- environment + alerts (Phase A/B/C) ----
async function pollEnv(){
  try{
    const e=await (await fetch('/api/env')).json();
    if(e.present){
      document.getElementById('envTile').style.display='';
      document.getElementById('temp').textContent=e.tempC.toFixed(1)+'\u00B0C';
    }
  }catch(x){}
  setTimeout(pollEnv,30000);
}
pollEnv();

function setAlert(h){
  const bar=document.getElementById('alertBar');
  if(h.stopped){
    bar.style.display='';bar.className='alertbar stop';
    const mins=Math.floor((h.secsSinceStrike||0)/60);
    bar.textContent='\u26A0 CLOCK STOPPED \u2014 no strike heard for '+mins+' min';
  }else if(h.windDue){
    bar.style.display='';bar.className='alertbar wind';
    bar.textContent='\u23F3 Time to wind the clock ('+h.daysSinceWind.toFixed(1)+' days since last wind)';
  }else{bar.style.display='none';}
  const wi=document.getElementById('windInfo');
  wi.textContent = (h.daysSinceWind>=0)? h.daysSinceWind.toFixed(1)+' days since wind' : 'no wind logged';
}

document.getElementById('windBtn').onclick=()=>{
  fetch('/api/wind',{method:'POST'}).then(()=>{});
};
const halfEn=document.getElementById('halfEn');
halfEn.onchange=function(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'halfHour='+this.value});
};

// ---- Help mode: hover (desktop) or tap (mobile) reveals explanations ----
(function(){
  const tip=document.createElement('div'); tip.className='tip'; document.body.appendChild(tip);
  let helpOn=false;
  function parse(raw){
    // format: "tt:Title|Body text"
    let title='', body=raw;
    if(raw.indexOf('tt:')===0){ const bar=raw.indexOf('|');
      title=raw.slice(3,bar); body=raw.slice(bar+1); }
    return {title,body};
  }
  function showTip(el,x,y){
    const p=parse(el.getAttribute('data-help'));
    tip.innerHTML=(p.title?'<span class="tt">'+p.title+'</span>':'')+p.body;
    tip.classList.add('show');
    const r=tip.getBoundingClientRect();
    let nx=x+14, ny=y+14;
    if(nx+r.width>innerWidth-8) nx=innerWidth-r.width-8;
    if(ny+r.height>innerHeight-8) ny=y-r.height-14;
    tip.style.left=nx+'px'; tip.style.top=ny+'px';
  }
  function hideTip(){ tip.classList.remove('show'); }
  document.addEventListener('mousemove',e=>{
    if(!helpOn){return;}
    const el=e.target.closest('[data-help]');
    if(el) showTip(el,e.clientX,e.clientY); else hideTip();
  });
  document.addEventListener('click',e=>{
    if(!helpOn) return;
    const el=e.target.closest('[data-help]');
    if(el){ e.preventDefault(); e.stopPropagation();
      const r=el.getBoundingClientRect(); showTip(el,r.left,r.bottom);
      setTimeout(hideTip,4000);
    } else hideTip();
  },true);
  const btn=document.getElementById('helpToggle');
  btn.onclick=()=>{ helpOn=!helpOn; btn.classList.toggle('on',helpOn);
    document.body.classList.toggle('help',helpOn);
    btn.textContent=helpOn?'\u2713 Help':'? Help';
    if(!helpOn) hideTip();
  };
})();

// ---- Microphone gain calibration ----
async function pollGain(){
  try{
    const g=await (await fetch('/api/gain')).json();
    document.getElementById('gMin').textContent=g.rawMin;
    document.getElementById('gMax').textContent=g.rawMax;
    document.getElementById('gSwing').textContent=g.swing;
    document.getElementById('gClip').textContent=g.clip;
    // bar: map raw 0..1023 to 0..100%, show the min..max band
    const lo=g.rawMin/1023*100, hi=g.rawMax/1023*100;
    const fill=document.getElementById('gFill');
    fill.style.left=lo+'%'; fill.style.width=Math.max(0,hi-lo)+'%';
    const v=document.getElementById('gVerdict');
    v.textContent=g.verdict.charAt(0).toUpperCase()+g.verdict.slice(1);
    v.className='gverdict '+g.verdict.replace(' ','').replace('too','too');
    document.getElementById('gAdvice').textContent=g.advice;
  }catch(e){}
  setTimeout(pollGain, 400);
}
pollGain();

document.getElementById('reset').onclick=()=>{
  if(!confirm('Forget WiFi and return to setup mode?'))return;
  fetch('/api/reset',{method:'GET'}).then(()=>{
    document.body.innerHTML='<div class="wrap" style="padding-top:40px">'+
      '<h1 style="font-family:Orbitron;color:#39ff9e">Resetting…</h1>'+
      '<p>The device is rebooting into setup mode. Reconnect to its WiFi network.</p></div>';
  });
};
poll();
</script>
</body></html>
)HTML";

#endif // WEBPAGES_H
