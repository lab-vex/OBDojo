// OBDojo web page - served from the ESP32 at http://1.1.1.1
// Single file: HTML + CSS + JS. Edit freely, then re-upload the firmware.
#pragma once

const char ICON_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9be07a"/><stop offset=".55" stop-color="#6cc24a"/><stop offset="1" stop-color="#3e9a2a"/></linearGradient></defs>
<rect width="512" height="512" rx="96" fill="#0a0a0a"/>
<g transform="translate(256 256) rotate(15)">
<path d="M0 -190 L44 -44 L190 0 L44 44 L0 190 L-44 44 L-190 0 L-44 -44 Z" fill="url(#g)"/>
<circle r="38" fill="#0a0a0a"/>
</g>
</svg>)SVG";

const char WEB_UI[] PROGMEM = R"OBDOJO(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#6cc24a">
<link rel="manifest" href="/manifest.json">
<link rel="icon" href="/icon.svg">
<title>OBDojo</title>
<style>
:root{--o:#6cc24a;--o2:#8fd66e;--bg:#0a0a0a;--card:#151515;--card2:#1d1d1d;--line:#2b2b2b;--tx:#f2f2f2;--mut:#9a9a9a;--bad:#ff3b30;--ok:#30d158;--warn:#ffcc00}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg);color:var(--tx);font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif;-webkit-tap-highlight-color:transparent}
body{padding-bottom:calc(72px + env(safe-area-inset-bottom))}
header{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:12px;padding:12px 16px;background:#000;border-bottom:3px solid var(--o)}
.logo{font-weight:900;font-style:italic;font-size:24px;letter-spacing:1px;text-transform:uppercase}
.logo b{color:var(--o)}
.slash{width:14px;height:26px;background:var(--o);transform:skewX(-20deg)}
.pill{margin-left:auto;font-size:12px;font-weight:700;padding:5px 10px;border-radius:20px;background:var(--card2);color:var(--mut);text-transform:uppercase}
.pill.ok{background:rgba(48,209,88,.15);color:var(--ok)}
.pill.bad{background:rgba(255,59,48,.15);color:var(--bad)}
main{max-width:900px;margin:0 auto;padding:16px}
section{display:none}section.on{display:block}
h2{font-size:14px;font-weight:800;font-style:italic;text-transform:uppercase;letter-spacing:1px;color:var(--o);margin:18px 0 10px}
h2:first-child{margin-top:4px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px;position:relative;overflow:hidden}
.tile .k{font-size:11px;font-weight:700;color:var(--mut);text-transform:uppercase;letter-spacing:.5px}
.tile .v{font-size:30px;font-weight:800;font-variant-numeric:tabular-nums;margin-top:4px;line-height:1.1}
.tile .u{font-size:13px;color:var(--mut);font-weight:600;margin-left:3px}
.tile .bar{position:absolute;left:0;bottom:0;height:4px;background:var(--o);width:0;transition:width .3s}
.tile.big{grid-column:span 2}.tile.big .v{font-size:44px}
.tile.rpm{padding-bottom:10px}
.tile.rpm .bar{position:relative;width:100%!important;height:12px;border-radius:3px;margin-top:2px;background:linear-gradient(90deg,var(--o) 0 calc(var(--w,0)*1%),#262626 0)}
.scale{position:relative;height:26px;margin:6px 4px 0}
.scale em{position:absolute;right:0;top:-18px;font-size:10px;font-style:normal;color:var(--mut)}
.scale i{position:absolute;bottom:0;width:1px;height:8px;background:#555;font-style:normal}
.scale i b{position:absolute;bottom:10px;left:50%;transform:translateX(-50%);font-size:11px;font-weight:700;color:var(--mut)}
.scale i:nth-of-type(n+14){background:var(--bad)}.scale i:nth-of-type(n+14) b{color:var(--bad)}
.tile.warn{border-color:var(--bad)}.tile.warn .v{color:var(--bad)}.tile.warn .bar{background:var(--bad)}
.tile.na .v{color:#555}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:10px}
.row{display:flex;gap:10px;flex-wrap:wrap}
button,.btn{appearance:none;border:0;border-radius:10px;padding:12px 16px;font-size:15px;font-weight:800;text-transform:uppercase;letter-spacing:.5px;font-style:italic;background:var(--card2);color:var(--tx);border:1px solid var(--line);cursor:pointer;text-decoration:none;display:inline-block}
button.p,.btn.p{background:var(--o);border-color:var(--o);color:#000}
button.d{background:var(--bad);border-color:var(--bad);color:#fff}
button:disabled{opacity:.4}
button:active{transform:scale(.97)}
.code{display:flex;gap:12px;align-items:center;padding:12px 0;border-bottom:1px solid var(--line)}
.code:last-child{border:0}
.code .c{font-family:ui-monospace,Menlo,monospace;font-weight:800;font-size:20px;color:var(--o);min-width:44px}
.code .t{font-size:11px;font-weight:700;padding:3px 8px;border-radius:6px;background:var(--card2);color:var(--mut);text-transform:uppercase}
.big-status{display:flex;align-items:center;gap:14px}
.lamp{width:46px;height:46px;border-radius:50%;background:#222;border:3px solid #333;flex:none}
.lamp.on{background:var(--warn);border-color:#ffe066;box-shadow:0 0 18px rgba(255,204,0,.6)}
.lamp.off{background:#1a3d22;border-color:var(--ok)}
.mut{color:var(--mut);font-size:13px}
pre{background:#000;border:1px solid var(--line);border-radius:10px;padding:12px;font-size:12px;overflow:auto;white-space:pre-wrap;word-break:break-all;max-height:55vh;color:#ddd}
input,select{width:100%;background:#000;color:var(--tx);border:1px solid var(--line);border-radius:8px;padding:11px;font-size:15px}
input:focus,select:focus{outline:2px solid var(--o);border-color:transparent}
label{display:block;font-size:13px;color:var(--mut);margin:12px 0 6px}
.sw{display:flex;align-items:center;justify-content:space-between;margin-top:12px;gap:12px}
.sw label{margin:0}
.sw input{width:auto;transform:scale(1.4);accent-color:var(--o)}
.small{font-size:12px;padding:6px 10px}
nav{position:fixed;left:0;right:0;bottom:0;z-index:5;display:flex;background:#000;border-top:1px solid var(--line);padding-bottom:env(safe-area-inset-bottom)}
nav button{flex:1;border:0;border-radius:0;background:none;padding:12px 4px 14px;font-size:11px;color:var(--mut);font-style:normal}
nav button.on{color:var(--o);box-shadow:inset 0 3px 0 var(--o)}
nav svg{display:block;margin:0 auto 4px;width:22px;height:22px;fill:none;stroke:currentColor;stroke-width:2}
#toast{position:fixed;left:50%;bottom:90px;transform:translateX(-50%) translateY(20px);opacity:0;background:var(--o);color:#000;font-weight:800;padding:12px 18px;border-radius:10px;transition:.25s;z-index:9;max-width:90vw;text-align:center}
#toast.show{opacity:1;transform:translateX(-50%)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-size:14px}
.kv div:nth-child(odd){color:var(--mut)}
.spin{display:inline-block;width:14px;height:14px;border:2px solid #000;border-top-color:transparent;border-radius:50%;animation:s 1s linear infinite;vertical-align:-2px}
@keyframes s{to{transform:rotate(360deg)}}
.tile .pk{font-size:11px;color:var(--mut);font-weight:700;margin-top:2px;min-height:13px}
.mono{font-family:ui-monospace,Menlo,monospace;font-size:13px}
.sn{background:#000;border:1px solid var(--line);border-radius:10px;padding:8px 10px;max-height:60vh;overflow:auto}
.sr{padding:6px 0;border-bottom:1px solid #1a1a1a}
.sr:last-child{border:0}
.sr .id{display:inline-block;min-width:34px;color:var(--o);font-weight:800}
.sr .nm{color:var(--mut);font-size:11px;margin-left:34px}
.sr i{font-style:normal;padding:1px 2px;border-radius:3px;margin-right:2px}
.sr i.m{color:var(--o)}
.sr i.c{background:var(--o);color:#000}
.sr .rg{font-size:11px;color:var(--mut);margin:2px 0 0 34px}
.prog{height:8px;border-radius:4px;background:#262626;overflow:hidden;margin:8px 0}
.prog div{height:100%;background:var(--o);width:0;transition:width .4s}
.step{display:flex;gap:10px;padding:8px 0;border-bottom:1px solid var(--line);align-items:flex-start}
.step:last-child{border:0}
.step b{flex:none;width:22px;height:22px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:13px;color:#000}
.step b.y{background:var(--ok)}.step b.n{background:var(--bad);color:#fff}.step b.u{background:#444;color:#fff}
.banner{background:rgba(255,204,0,.12);border:1px solid #6b5a10;color:var(--warn);border-radius:10px;padding:10px 12px;margin-bottom:10px;font-size:13px;font-weight:700;display:none}
.chk{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--mut);margin:10px 0 0}
.chk input{width:auto;accent-color:var(--o);transform:scale(1.2)}
table{width:100%;border-collapse:collapse;font-size:13px}
td,th{text-align:left;padding:5px 6px;border-bottom:1px solid #1f1f1f;vertical-align:top}
th{color:var(--mut);font-size:11px;text-transform:uppercase}
td.r{color:var(--o);font-weight:800;font-family:ui-monospace,Menlo,monospace}
</style></head><body>
<header><div class="slash"></div><div class="logo">OBD<b>OJO</b></div><div id="conn" class="pill">…</div></header>
<main>

<section id="dash" class="on">
  <div class="banner" id="modebanner"></div>
  <div class="grid" id="tiles"></div>
  <p class="mut" style="margin-top:12px" id="dashnote"></p>
  <div class="row" style="margin-top:10px"><button class="small" onclick="cmd('peak reset').then(r=>toast(r.msg))">Reset peaks</button></div>
</section>

<section id="codes">
  <div class="card big-status">
    <div class="lamp" id="lamp"></div>
    <div><div style="font-weight:800;font-size:18px" id="fitext">Codes: not read yet</div>
    <div class="mut" id="fisub">Key on, then tap Read codes</div></div>
  </div>
  <div class="row" style="margin:12px 0">
    <button class="p" onclick="readCodes()">Read codes</button>
    <button onclick="freeze()">Failure records</button>
    <button class="d" onclick="clearCodes()">Clear codes</button>
  </div>
  <div class="card" id="codelist"><p class="mut">No codes read yet.</p></div>
  <div class="card" id="freezebox" style="display:none"></div>
  <p class="mut">Codes are Kawasaki's two-digit DFI service codes, the same numbers dealer mode shows on the dash (key on, odometer screen, hold the left button 2 s). "Current" and "stored" are the ECU's two code lists (13 01 and 13 02); which is which isn't confirmed on the EX300 yet.</p>
  <h2>ABS (EX300B)</h2>
  <div class="row"><button onclick="absRead()">Try the ABS ECU</button></div>
  <p class="mut" style="margin-top:6px">Experimental: asks ECU address 28 on the same K-line. The 300's ABS may only talk on its own connector.</p>
  <div class="card" id="absbox" style="display:none;margin-top:10px"></div>
</section>

<section id="tools">
  <h2>K-line check</h2>
  <div class="row"><button class="p" onclick="ktest()">Run K-line test</button><button onclick="kline()">Frame trace</button><button onclick="tool('ident')">ECU info</button></div>
  <p class="mut" style="margin-top:6px">Key on. Re-wakes the ECU and reports each step: the wake-up pulse, the transceiver echo, the ECU's answer and a register read.</p>
  <div class="card" id="ktbox" style="display:none;margin-top:10px"></div>

  <h2>Register sweep</h2>
  <div class="row"><button class="p" id="swbtn" onclick="sweepToggle()">Start sweep</button><a class="btn" href="/sweep.csv">CSV</a><button onclick="monFromSweep()">Monitor these</button></div>
  <p class="mut" style="margin-top:6px">Reads every register 00-FF once (about 35 s, key on, engine off is fine) and lists the ones the ECU answers. Live data pauses while it runs.</p>
  <div class="prog"><div id="swprog"></div></div>
  <p class="mut" id="swinfo"></p>
  <div class="sn" id="swlist" style="display:none"></div>

  <h2>Register monitor</h2>
  <div class="row"><input id="monin" class="mono" placeholder="all   or   04 05 07 5B" style="flex:1;min-width:160px"><button class="p" id="monbtn" onclick="monToggle()">Start</button><button onclick="monMark()">Mark</button></div>
  <p class="mut" style="margin:8px 0">Re-reads the registers over and over. Green box = changed just now, green text = changed since Mark (range below it). Tap Mark, then twist the throttle, warm the engine or pull the clutch, and watch what moves. Fewer registers = faster updates.</p>
  <div class="sn mono" id="mon"><span class="mut">Not running.</span></div>
  <p class="mut" style="margin-top:8px">Found one? Point a live value at it in Settings &gt; Live data, or add a Custom signal, e.g. <span class="mono">5E:0:1:b:0.01953:0:Tilt sensor:V</span></p>

  <h2>Watch a request</h2>
  <div class="row"><input id="watchin" class="mono" placeholder="21 04   or   @28 1A" style="flex:1;min-width:160px"><button class="p" id="wabtn" onclick="watchToggle()">Watch</button><button onclick="watchMark()">Mark</button></div>
  <p class="mut" style="margin:8px 0">Sends one read request over and over and highlights bytes that move. Read-only requests only.</p>
  <div class="sn mono" id="watch"><span class="mut">Not running.</span></div>

  <h2>Throttle calibration</h2>
  <p class="mut" style="margin-bottom:8px">Engine off, key on. Throttle shut: Learn closed. Hold it wide open: Learn open. Sets 0% and 100% on the Dash.</p>
  <div class="row"><button onclick="tps('closed')">Learn closed</button><button onclick="tps('open')">Learn open</button><button class="small" onclick="tps('reset')">Defaults</button></div>
  <p class="mut mono" style="margin-top:8px" id="tpsinfo"></p>

  <h2>Raw command</h2>
  <div class="row"><input id="rawin" placeholder="e.g. 21 04   or   @28 13 01" style="flex:1;min-width:160px"><button onclick="tool('raw '+$('#rawin').value)">Send</button></div>
  <p class="mut" style="margin-top:6px">Needs "Allow raw ECU commands" in Settings. Services that could reprogram the ECU are always blocked.</p>
  <h2>Output</h2>
  <pre id="out">Pick a tool.</pre>
</section>

<section id="log">
  <div class="card">
    <div class="kv" id="logkv"></div>
  </div>
  <div class="row">
    <button class="p" onclick="logc('start')">Start</button>
    <button onclick="logc('stop')">Stop</button>
    <button onclick="logc('clear')">Clear</button>
    <a class="btn" href="/log.csv">Download CSV</a>
  </div>
  <p class="mut" style="margin-top:12px">Logs RPM, throttle (% and volts), sub-throttle, MAP, atmospheric pressure, speed, temps, battery, gear and your custom signals. Stored in memory until power off. Interval is in Settings.</p>
</section>

<section id="set">
  <div id="setform"><p class="mut">Loading…</p></div>
  <div class="row" style="margin-top:16px">
    <button class="p" onclick="saveSettings()">Save</button>
    <button onclick="cmd('reboot').then(r=>toast(r.msg))">Reboot</button>
    <button class="d" onclick="factory()">Factory reset</button>
  </div>
  <h2>About</h2>
  <div class="card"><div class="kv" id="about"></div></div>
</section>

</main>

<nav>
  <button class="on" data-s="dash"><svg viewBox="0 0 24 24"><path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/></svg>Dash</button>
  <button data-s="codes"><svg viewBox="0 0 24 24"><path d="M12 3l9 16H3z"/><path d="M12 10v4M12 17v.5"/></svg>Codes</button>
  <button data-s="tools"><svg viewBox="0 0 24 24"><path d="M14 7l3-3 3 3-3 3M17 4L8 13l-4 7 7-4 9-9"/></svg>Tools</button>
  <button data-s="log"><svg viewBox="0 0 24 24"><path d="M4 20V4M4 20h16M8 16l4-5 3 3 5-7"/></svg>Log</button>
  <button data-s="set"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/></svg>Settings</button>
</nav>
<div id="toast"></div>

<script>
const $=s=>document.querySelector(s);
let S={units:0,live_ms:250,temp_warn:105,rpm_warn:12500,vbat_low:11.8},cur='dash',liveT=null,fields=[];
const imp=()=>S.units==1;
const T=[
 {k:'rpm',l:'RPM',u:'',max:14000,big:1,dp:0},
 {k:'gear',l:'Gear',u:'',max:6,dp:0},
 {k:'tps',l:'Throttle',u:'%',max:100,dp:1},
 {k:'spd',l:'Speed',u:'km/h',max:180,dp:0,c:v=>imp()?v*0.6214:v,cu:'mph'},
 {k:'ect',l:'Coolant',u:'°C',max:120,dp:0,c:v=>imp()?v*1.8+32:v,cu:'°F'},
 {k:'iat',l:'Intake air',u:'°C',max:80,dp:0,c:v=>imp()?v*1.8+32:v,cu:'°F'},
 {k:'map',l:'Intake pressure',u:'kPa',max:110,dp:0},
 {k:'vbat',l:'Battery',u:'V',max:15,dp:2},
 {k:'tpsv',l:'Throttle sensor',u:'V',max:5,dp:2},
 {k:'sub',l:'Sub-throttle',u:'V',max:5,dp:2},
 {k:'baro',l:'Atmospheric',u:'kPa',max:110,dp:1}
];
const PK={rpm:'rpm',spd:'spd',ect:'ect'};
function tilesList(){return T.concat((S.sigs||[]).map(g=>({k:g.k,l:g.n,u:g.u,max:0,dp:2,sig:1})))}
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('show');clearTimeout(t._t);t._t=setTimeout(()=>t.classList.remove('show'),2600)}
async function cmd(c){
  const r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c='+encodeURIComponent(c)});
  return r.json();
}
function busy(b,on){if(!b)return;if(on){b._h=b.innerHTML;b.innerHTML='<span class="spin"></span>';b.disabled=true}else{b.innerHTML=b._h;b.disabled=false}}
async function act(fn){const b=document.activeElement&&document.activeElement.tagName=='BUTTON'?document.activeElement:null;busy(b,1);try{await fn()}catch(e){toast('Lost connection to OBDojo')}busy(b,0)}
const esc=x=>String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const hx=v=>v.toString(16).toUpperCase().padStart(2,'0');

// ---------- tabs ----------
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x==b));
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id==b.dataset.s));
  cur=b.dataset.s;
  if(cur=='set')loadSettings();
  if(cur=='tools'){cmd('status').then(s=>{Object.assign(S,s);tpsUI(s.tps)});sweepPoll()}
  if(cur=='log')logc('status');
  live();
});

// ---------- dashboard ----------
function buildTiles(){
  const scale='<div class="scale"><em>×1000 rpm</em>'+Array.from({length:15},(_,i)=>`<i style="left:${i/14*100}%"><b>${i}</b></i>`).join('')+'</div>';
  $('#tiles').innerHTML=tilesList().map(t=>`<div class="tile na${t.big?' big':''}${t.k=='rpm'?' rpm':''}" id="t_${t.k}"><div class="k">${esc(t.l)}</div><div class="v"><span>--</span><span class="u">${esc(imp()&&t.cu?t.cu:t.u)}</span></div>${t.k=='rpm'?scale:''}${PK[t.k]?'<div class="pk"></div>':''}<div class="bar"></div></div>`).join('');
}
function showLive(d){
  setConn(d.ecu);
  tilesList().forEach(t=>{
    const el=$('#t_'+t.k);if(!el)return;
    let v=d[t.k];
    if(v===null||v===undefined){el.classList.add('na');el.querySelector('span').textContent='--';const b=el.querySelector('.bar');b.style.width='0';b.style.setProperty('--w',0);return}
    el.classList.remove('na');
    const raw=v; if(t.c)v=t.c(v);
    el.querySelector('span').textContent=t.k=='gear'?(v?v:'N'):v.toFixed(t.dp);
    const pk=el.querySelector('.pk');
    if(pk&&d.pk&&d.pk[t.k]!=null){let p=d.pk[t.k];if(t.c)p=t.c(p);pk.textContent='max '+Math.round(p)}
    const pct=t.max?Math.min(100,Math.max(0,raw/t.max*100)):0;
    if(t.k=='rpm')el.querySelector('.bar').style.setProperty('--w',pct);else el.querySelector('.bar').style.width=pct+'%';
    let w=false;
    if(t.k=='ect')w=raw>=S.temp_warn;
    if(t.k=='rpm')w=raw>=S.rpm_warn;
    if(t.k=='vbat')w=raw<S.vbat_low;
    el.classList.toggle('warn',w);
  });
  const mb=$('#modebanner');
  if(d.mode&&d.mode!='live'){mb.style.display='block';mb.textContent=(d.mode=='sweep'?'Register sweep running':'Register monitor running')+': live values are paused. Stop it in Tools.'}else mb.style.display='none';
  if(d.fi==1)$('#dashnote').innerHTML='<b style="color:var(--warn)">'+d.dtc+' current code(s)</b>. See Codes.';
  else $('#dashnote').textContent=d.ecu?(d.rate?d.rate+' ECU replies a second':''):'Waiting for the ECU: turn the key on.';
}
function setConn(ecu){const c=$('#conn');c.textContent=ecu?'ECU OK':'No ECU';c.className='pill '+(ecu?'ok':'bad')}
async function live(){
  clearTimeout(liveT);
  if(cur!='dash'||document.hidden)return;
  const t0=Date.now();
  try{showLive(await (await fetch('/live')).json())}catch(e){$('#conn').textContent='Offline';$('#conn').className='pill bad'}
  liveT=setTimeout(live,Math.max(10,Math.max(100,S.live_ms)-(Date.now()-t0)));
}
document.addEventListener('visibilitychange',live);

// ---------- codes ----------
function codeRows(list,tag){return (list||[]).map(c=>`<div class="code"><div class="c">${esc(c.code)}</div><div style="flex:1">${esc(c.desc||'Unknown code')}<div class="mut">status ${esc(c.st)}</div></div><div class="t">${tag}</div></div>`).join('')}
function readCodes(){act(async()=>{
  const r=await cmd('codes');
  if(!r.ok){toast(r.msg);return}
  const n=(r.current||[]).length,m=(r.stored||[]).length;
  $('#lamp').className='lamp '+(n?'on':'off');
  $('#fitext').textContent=n?n+' current code(s)':'No current codes';
  $('#fisub').textContent=(m+' stored')+(r.count!=null?' · failure count '+r.count:'');
  const h=codeRows(r.current,'Current')+codeRows(r.stored,'Stored');
  $('#codelist').innerHTML=(h||'<p style="color:var(--ok);font-weight:800">No codes.</p>')+`<p class="mut mono" style="margin-top:8px">13 01 → ${esc(r.raw_cur||'no reply')}<br>13 02 → ${esc(r.raw_sto||'no reply')}</p>`;
})}
function clearCodes(){
  if(!confirm('Clear the stored codes?\n\nKey on, engine off works best.'))return;
  act(async()=>{const r=await cmd('clear yes');toast(r.ok?'Codes cleared':r.msg);if(r.ok)readCodes()})
}
function freeze(){act(async()=>{
  const r=await cmd('freeze');const b=$('#freezebox');b.style.display='block';
  if(!r.ok){b.innerHTML='<p class="mut">'+esc(r.msg)+'</p>';return}
  b.innerHTML='<h2 style="margin-top:0">Failure records ('+r.count+')</h2>'+(r.list.length?r.list.map(x=>`<div class="code"><div class="c">${esc(x.code||'?')}</div><div class="mono" style="flex:1">${esc(x.hex)}</div><div class="t">#${x.n}</div></div>`).join(''):'<p class="mut">None stored.</p>')+'<p class="mut" style="margin-top:6px">Raw replies to 12 n 01 54. The layout of these records is still being worked out.</p>';
})}
function absRead(){act(async()=>{
  const r=await cmd('abs');const b=$('#absbox');b.style.display='block';
  if(!r.ok){b.innerHTML='<p class="mut">'+esc(r.msg)+'</p>';return}
  b.innerHTML=`<div class="kv"><div>ABS ECU</div><div>${esc(r.id)}</div></div>`+(codeRows(r.current,'Current')+codeRows(r.stored,'Stored')||'<p style="color:var(--ok);font-weight:800;margin-top:8px">No ABS codes.</p>')+`<p class="mut" style="margin-top:6px">${esc(r.msg)}</p>`;
})}

// ---------- tools ----------
function fmtTool(r){
  if(!r.ok)return 'Error: '+r.msg;
  switch(r.t){
    case 'ident':return r.items.map(i=>i.k.padEnd(26)+i.v).join('\n');
    case 'raw':return 'Sent: '+r.req+'\nGot:  '+r.hex+'\n      |'+r.ascii+'|';
  }
  return JSON.stringify(r,null,2);
}
function tool(c){act(async()=>{$('#out').textContent='Working…';const r=await cmd(c);$('#out').textContent=fmtTool(r)})}
function klKv(k){return `<div class="kv" style="margin-top:10px"><div>Session</div><div>${k.session?'open, tester '+k.tester+' → ECU '+k.target:'closed'}</div><div>Keywords</div><div>${k.kb}</div><div>Wake-up pulse</div><div>${k.init_ms} ms</div><div>Requests</div><div>${k.req} sent, ${k.okc} answered, ${k.tmo} no reply, ${k.cks} bad checksum, ${k.echo_bad} no echo</div><div>Wake-ups</div><div>${k.init_ok} of ${k.inits} answered</div><div>Speed</div><div>${k.rate} replies/s</div><div>Last error</div><div>${esc(k.err||'none')}</div></div>`}
function ktest(){act(async()=>{
  const r=await cmd('ktest');const b=$('#ktbox');b.style.display='block';
  b.innerHTML=r.steps.map(s=>`<div class="step"><b class="${s.ok===true?'y':s.ok===false?'n':'u'}">${s.ok===true?'✓':s.ok===false?'✗':'?'}</b><div><div style="font-weight:700">${esc(s.k)}</div><div class="mut">${esc(s.msg)}</div></div></div>`).join('')+klKv(r);
})}
function kline(){act(async()=>{
  const r=await cmd('kline');
  $('#out').textContent=r.trace.map(e=>String(e.ms).padStart(8)+'  '+(e.d=='T'?'→ ':e.d=='R'?'← ':'· ')+e.x).join('\n')||'Nothing yet.';
  const b=$('#ktbox');b.style.display='block';b.innerHTML=klKv(r);
})}

// ---------- register sweep ----------
let swT=null,swRun=false;
function sweepToggle(){act(async()=>{const r=await cmd(swRun?'sweep stop':'sweep start');if(!r.ok){toast(r.msg);return}sweepShow(r);sweepPoll()})}
async function sweepPoll(){
  clearTimeout(swT);
  try{const r=await cmd('sweep');sweepShow(r);if(r.on&&cur=='tools')swT=setTimeout(sweepPoll,1000)}catch(e){}
}
function sweepShow(r){
  swRun=r.on;$('#swbtn').textContent=r.on?'Stop':'Start sweep';
  const pos=Math.max(0,Math.min(256,r.pos));$('#swprog').style.width=(pos/256*100)+'%';
  if(r.pos<0){$('#swinfo').textContent='Not run yet.';$('#swlist').style.display='none';return}
  $('#swinfo').textContent=(r.on?'Reading register '+hx(Math.min(255,pos))+'… ':(r.done?'Done in '+Math.round(r.ms/1000)+' s. ':'Stopped. '))+r.found+' answered, '+r.refused+' refused, '+r.silent+' no reply.';
  const l=$('#swlist');l.style.display=r.items.length?'block':'none';
  l.innerHTML='<table><tr><th>Reg</th><th>Data</th><th>Decimal</th></tr>'+r.items.map(i=>`<tr><td class="r">${i.r}</td><td class="mono">${i.hex}<div class="mut">${esc(i.nm)}</div></td><td class="mono">${i.hex.split(' ').filter(x=>x).map(h=>parseInt(h,16)).join(' ')}</td></tr>`).join('')+'</table>';
}
function monFromSweep(){$('#monin').value='all';if(!monOn)monToggle()}

// ---------- register monitor ----------
let monOn=false,monT=null;
function monToggle(){act(async()=>{
  if(monOn){await cmd('mon stop');monOn=false;$('#monbtn').textContent='Start';return}
  const v=$('#monin').value.trim()||'all';
  const r=await cmd('mon start '+v);if(!r.ok){toast(r.msg);return}
  monOn=true;$('#monbtn').textContent='Stop';monDraw(r);monPoll();
})}
function monMark(){cmd('mon mark').then(r=>{monDraw(r);toast('Marked. Now move something.')})}
async function monPoll(){
  clearTimeout(monT);if(!monOn)return;
  if(cur=='tools'&&!document.hidden){try{const r=await cmd('mon');if(!r.on){monOn=false;$('#monbtn').textContent='Start'}monDraw(r)}catch(e){}}
  monT=setTimeout(monPoll,700);
}
function byteRow(hex,c,m){return hex.split(' ').map((h,i)=>`<i class="${c>>i&1?'c':(m>>i&1?'m':'')}">${h}</i>`).join('')}
function monDraw(r){
  const el=$('#mon');
  if(!r.regs||!r.regs.length){el.innerHTML='<span class="mut">Nothing to monitor.</span>';return}
  el.innerHTML=`<div class="mut" style="margin-bottom:6px">${r.regs.length} registers, ${r.rate} reads/s</div>`+r.regs.map(e=>{
    if(!e.n)return `<div class="sr"><span class="id">${e.r}</span><span class="mut">${e.nrc?esc(e.nrc):'waiting…'}</span><div class="nm">${esc(e.nm)}</div></div>`;
    let rg='';if(e.m){const lo=e.lo.split(' '),hi=e.hi.split(' ');rg=lo.map((l,i)=>e.m>>i&1?'byte '+i+': '+l+'–'+hi[i]+' ('+parseInt(l,16)+'–'+parseInt(hi[i],16)+')':'').filter(x=>x).join('<br>')}
    return `<div class="sr"><span class="id">${e.r}</span>${byteRow(e.d,e.c,e.m)}${e.nm?`<div class="nm">${esc(e.nm)}</div>`:''}${rg?`<div class="rg">${rg}</div>`:''}</div>`}).join('');
}

// ---------- watch a request ----------
let waOn=false,waT=null,waPrev=null,waLo=null,waHi=null;
function watchToggle(){waOn=!waOn;$('#wabtn').textContent=waOn?'Stop':'Watch';if(waOn){waPrev=null;watchMark();watchPoll()}}
function watchMark(){waLo=waHi=null}
async function watchPoll(){
  clearTimeout(waT);if(!waOn)return;
  if(cur=='tools'&&!document.hidden){
    try{
      const r=await cmd('peek '+$('#watchin').value);
      if(!r.ok){$('#watch').innerHTML='<span class="mut">'+esc(r.msg)+'</span>'}
      else{
        const b=r.hex.split(' ').map(h=>parseInt(h,16));let c=0;
        if(!waLo||waLo.length!=b.length){waLo=b.slice();waHi=b.slice()}
        b.forEach((v,i)=>{if(waPrev&&waPrev[i]!==v)c|=1<<Math.min(i,30);if(v<waLo[i])waLo[i]=v;if(v>waHi[i])waHi[i]=v});
        waPrev=b;
        const rg=[];for(let i=0;i<b.length;i++)if(waLo[i]!==waHi[i])rg.push('byte '+i+': '+hx(waLo[i])+'–'+hx(waHi[i])+' ('+waLo[i]+'–'+waHi[i]+')');
        $('#watch').innerHTML=`<div class="sr"><div class="mut" style="margin-bottom:4px">Byte 0 = reply code (61 = OK, 7F = refused). Data starts at byte 2 for 21 xx.</div>${b.map((v,i)=>`<i class="${c>>i&1?'c':(waLo[i]!==waHi[i]?'m':'')}">${hx(v)}</i>`).join('')}${rg.length?'<div class="rg" style="margin-left:0">'+rg.join('<br>')+'</div>':''}${r.nrc?'<div class="rg" style="margin-left:0">ECU says: '+esc(r.nrc)+'</div>':''}</div>`;
      }
    }catch(e){}
  }
  waT=setTimeout(watchPoll,250);
}

// ---------- throttle calibration ----------
function tpsUI(t){if(t)$('#tpsinfo').textContent='raw now '+t.raw+'   closed '+t.min+'   open '+t.max}
function tps(a){act(async()=>{const r=await cmd('tps '+a);if(r.ok){toast(a=='reset'?'Back to defaults':'Saved');tpsUI(r)}else toast(r.msg)})}

// ---------- log ----------
function logc(c){act(async()=>{
  const r=await cmd('log '+c);
  $('#logkv').innerHTML=`<div>Status</div><div style="font-weight:800;color:${r.on?'var(--ok)':'var(--mut)'}">${r.on?'LOGGING':'Stopped'}</div><div>Samples</div><div>${r.n} / ${r.max}</div><div>Interval</div><div>${r.ms} ms</div><div>Capacity</div><div>${Math.round(r.max*r.ms/60000)} min</div>`;
})}

// ---------- settings ----------
async function loadSettings(){
  const r=await cmd('settings');fields=r.fields;
  let g='',h='';
  fields.forEach(f=>{
    if(f.g!=g){h+=(g?'</div>':'')+'<h2>'+f.g+'</h2><div class="card">';g=f.g}
    const id='f_'+f.k;
    if(f.t=='bool')h+=`<div class="sw"><label for="${id}">${f.l}${f.r?' ↻':''}</label><input type="checkbox" id="${id}" ${f.v=='1'?'checked':''}></div>`;
    else if(f.t=='sel')h+=`<label for="${id}">${f.l}${f.r?' ↻':''}</label><select id="${id}">`+f.o.split('|').map(o=>{const[v,l]=o.split(':');return`<option value="${v}" ${v==f.v?'selected':''}>${l}</option>`}).join('')+'</select>';
    else if(f.t=='pass')h+=`<label for="${id}">${f.l}${f.r?' ↻':''}</label><div class="row" style="flex-wrap:nowrap"><input type="password" id="${id}" placeholder="${f.set?'•••••• (unchanged)':'not set'}" autocomplete="new-password">${f.set?`<button class="small" onclick="clearPass('${f.k}')">Remove</button>`:''}</div>`;
    else h+=`<label for="${id}">${f.l}${f.r?' ↻':''}</label><input id="${id}" value="${(f.v||'').replace(/"/g,'&quot;')}" ${f.t=='int'||f.t=='float'?'inputmode="decimal"':''}>`;
  });
  $('#setform').innerHTML=h+'</div><p class="mut" style="margin-top:8px">↻ = takes effect after reboot</p>';
  const s=await cmd('status');Object.assign(S,s);
  $('#about').innerHTML=`<div>Firmware</div><div>${s.fw}</div><div>ECU</div><div>${s.ecu?'Connected'+(s.ecu_id?' ('+esc(s.ecu_id)+')':''):'Not connected'}</div><div>K-line</div><div>${s.kl.session?'tester '+s.kl.tester+', '+s.kl.rate+' replies/s':esc(s.kl.err||'no session')}</div><div>Supported registers</div><div class="mono">${esc(s.supp||'not read yet')}</div><div>Bluetooth (Torque / tracker)</div><div>${s.bt_client?'Connected':'Not connected'}</div><div>Hotspot address</div><div>${s.ap_ip}</div><div>Bluetooth MAC</div><div>${s.bt_mac}</div><div>Wi-Fi clients</div><div>${s.wifi_clients}</div><div>Home Wi-Fi IP</div><div>${s.sta_ip||'—'}</div><div>Uptime</div><div>${Math.floor(s.uptime/60)} min</div><div>Free memory</div><div>${Math.round(s.heap/1024)} KB</div>`;
}
async function saveSettings(){
  let reboot=false,errs=[],n=0;
  for(const f of fields){
    const el=$('#f_'+f.k);if(!el)continue;
    let v=f.t=='bool'?(el.checked?'1':'0'):el.value.trim();
    if(f.t=='pass'){if(!v)continue}
    else if(v==f.v)continue;
    const r=await cmd('set '+f.k+' '+v);
    if(r.ok){n++;if(r.reboot)reboot=true}else errs.push(r.msg);
  }
  if(errs.length)alert(errs.join('\n'));
  else toast(n?'Saved '+n+' setting(s)':'Nothing changed');
  await loadSettings();buildTiles();
  if(reboot&&confirm('Some changes need a reboot. Reboot now?\n\nIf you changed Wi-Fi settings, rejoin the new hotspot afterwards.'))cmd('reboot');
}
async function clearPass(k){const r=await cmd('set '+k);toast(r.ok?'Removed':r.msg);loadSettings()}
function factory(){if(confirm('Reset ALL settings to defaults and reboot?'))cmd('factory yes').then(r=>toast(r.msg))}

// ---------- start ----------
(async()=>{try{Object.assign(S,await cmd('status'))}catch(e){}buildTiles();live()})();
</script>
</body></html>
)OBDOJO";
