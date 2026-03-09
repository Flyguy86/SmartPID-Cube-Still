#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  HTML Pages — stored in flash as const strings
//  Served via ESP8266 AT commands through TCP port 80
//  Mobile-first dark theme, compact for fast transfer over AT+CIPSEND
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Main Dashboard Page ────────────────────────────────────────────────────

const char PAGE_MAIN[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartPID Still</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:12px;max-width:480px;margin:0 auto}
h1{text-align:center;color:#ff6b35;margin:12px 0;font-size:1.4em}
h2{font-size:1.1em;margin-bottom:8px;color:#aaa}
.c{background:#1a1a2e;border-radius:10px;padding:14px;margin:10px 0}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #2a2a3e}
.row:last-child{border:none}
.val{font-size:1.3em;font-weight:bold;color:#4fc3f7}
.nc{color:#666}
.btn{display:inline-block;padding:10px 16px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;margin:4px;min-width:70px;text-align:center}
.on{background:#ff6b35;color:#fff}
.off{background:#2a2a3e;color:#666}
.grp{display:flex;flex-wrap:wrap;gap:4px;justify-content:center}
input[type=range]{width:100%;margin:8px 0;accent-color:#ff6b35}
.st{padding:8px;border-radius:6px;text-align:center;font-weight:bold;margin-bottom:8px}
.si{background:#2a2a3e;color:#666}
.sh{background:#ff6b35;color:#fff}
.so{background:#4caf50;color:#fff}
.sd{background:#2196f3;color:#fff}
.nav{text-align:center;margin:16px 0}
a{color:#ff6b35}
.at{background:#e65100;color:#fff;padding:6px 10px;border-radius:4px;font-size:.85em;margin-top:4px;display:none}
</style></head><body>
<h1>&#x1F525; SmartPID Still</h1>
<div class="c">
<h2>Sensors</h2>
<div class="row"><span>Lower Probe</span><span class="val" id="t0">--</span></div>
<div class="row"><span>Upper Probe</span><span class="val" id="t1">--</span></div>
</div>
<div class="c">
<h2>Outputs</h2>
<div class="grp">
<button class="btn off" id="b0" onclick="tog(0)">SSR</button>
<button class="btn off" id="b1" onclick="tog(1)">RL1</button>
<button class="btn off" id="b2" onclick="tog(2)">RL2</button>
<button class="btn off" id="b3" onclick="tog(3)">DC1</button>
<button class="btn off" id="b4" onclick="tog(4)">DC2</button>
</div>
<div style="margin-top:12px">
<label>SSR PWM: <b id="pv">0</b>/255</label>
<input type="range" id="pw" min="0" max="255" value="0"
 oninput="document.getElementById('pv').textContent=this.value"
 onchange="fetch('/api/pwm?v='+this.value)">
</div></div>
<div class="c">
<h2>Run Profile</h2>
<div id="rs" class="st si">IDLE</div>
<div id="at" class="at">Auto-Tuning...</div>
<div id="ri" style="display:none">
<div class="row"><span>Step</span><span id="rn">--</span></div>
<div class="row"><span>Target</span><span id="rt">--</span></div>
<div class="row"><span>Current</span><span id="rc">--</span></div>
<div class="row"><span>Time Left</span><span id="rr">--</span></div>
<div class="row"><span>PWM Out</span><span id="rp">--</span></div>
</div>
<div style="text-align:center;margin-top:8px">
<button class="btn on" id="rb" onclick="rctl()">Start</button>
</div></div>
<div class="nav"><a href="/settings">&#9881; Settings</a></div>
<script>
var O=[0,0,0,0,0],RS=0,iv;
function tog(i){fetch('/api/output?id='+i+'&s='+(O[i]?0:1)).then(poll)}
function rctl(){fetch('/api/'+(RS==0||RS==3?'start':'stop')).then(poll)}
function poll(){fetch('/api/status').then(function(r){return r.arrayBuffer()}).then(function(b){
 var v=new DataView(b),t0=v.getInt16(0,1)/10,t1=v.getInt16(2,1)/10,fl=v.getUint8(4);
 var c0=fl&1,c1=fl&2,p=v.getUint8(5),st=v.getUint8(6);
 var tgt=v.getInt16(7,1)/10,cur=v.getInt16(9,1)/10;
 var rem=v.getUint16(11,1),mx=v.getUint8(13),cs=v.getUint8(14),ts=v.getUint8(15),at=v.getInt8(16);
 O=[fl&4?1:0,fl&8?1:0,fl&16?1:0,fl&32?1:0,fl&64?1:0];RS=st;
 var fmt=function(val,cn){return cn?val.toFixed(1)+'\u00B0F':'N/C'};
 document.getElementById('t0').textContent=fmt(t0,c0);
 document.getElementById('t0').className='val'+(c0?'':' nc');
 document.getElementById('t1').textContent=fmt(t1,c1);
 document.getElementById('t1').className='val'+(c1?'':' nc');
 for(var i=0;i<5;i++){var bn=document.getElementById('b'+i);bn.className='btn '+(O[i]?'on':'off')}
 document.getElementById('pw').value=p;document.getElementById('pv').textContent=p;
 var re=document.getElementById('rs'),ri=document.getElementById('ri'),rb=document.getElementById('rb');
 var L=['IDLE','HEATING','HOLDING','DONE'],C=['si','sh','so','sd'];
 re.textContent=L[st];re.className='st '+C[st];
 document.getElementById('at').style.display=at>=0?'block':'none';
 ri.style.display=st>0?'block':'none';
 rb.textContent=(st==0||st==3)?'Start':'Stop';
 if(st>0){
  document.getElementById('rn').textContent=(cs+1)+'/'+ts;
  document.getElementById('rt').textContent=tgt.toFixed(1)+'\u00B0F';
  document.getElementById('rc').textContent=cur<-900?'N/C':cur.toFixed(1)+'\u00B0F';
  var m=Math.floor(rem/60),s=rem%60;
  document.getElementById('rr').textContent=m+'m '+('0'+s).slice(-2)+'s';
  document.getElementById('rp').textContent=p+'/'+mx;
 }
}).catch(function(){})}
// Binary status: 17 bytes vs ~150 bytes JSON = ~9x less data per poll
iv=setInterval(poll,3000);poll();
</script></body></html>)rawhtml";

// ─── Settings Page ──────────────────────────────────────────────────────────

const char PAGE_SETTINGS[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:12px;max-width:480px;margin:0 auto}
h1{text-align:center;color:#ff6b35;margin:12px 0;font-size:1.4em}
h2{font-size:1.1em;margin-bottom:8px;color:#aaa}
.c{background:#1a1a2e;border-radius:10px;padding:14px;margin:10px 0}
label{display:block;margin:6px 0 2px;font-size:.9em;color:#888}
input,select{width:100%;padding:8px;border:1px solid #333;border-radius:4px;background:#0a0a1a;color:#eee;font-size:14px}
.btn{display:block;width:100%;padding:10px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;background:#ff6b35;color:#fff;margin-top:10px}
.btn2{background:#2196f3}
.at{background:#e65100}
.r2{display:flex;gap:8px}.r2>div{flex:1}
.r3{display:flex;gap:6px}.r3>div{flex:1}
.nav{text-align:center;margin:16px 0}
a{color:#ff6b35}
.msg{padding:8px;border-radius:4px;text-align:center;margin-top:8px;display:none;background:#2e7d32;color:#fff}
</style></head><body>
<h1>&#9881; Settings</h1>

<div class="c"><h2>WiFi Network</h2>
<label>Network Name (SSID)</label><input id="wn" type="text" placeholder="Enter WiFi network name">
<label>Password</label><input id="wp" type="password">
<button class="btn" onclick="saveW()">Save &amp; Connect</button>
<div class="msg" id="wm">Saved! Connecting...</div></div>

<div class="c"><h2>Lower Probe</h2>
<div class="r2"><div><label>Output</label>
<select id="o0"><option value="0">SSR</option><option value="1">Relay1</option><option value="2">Relay2</option><option value="3">DC1</option><option value="4">DC2</option></select></div>
<div><label>&nbsp;</label><button class="btn at" onclick="atune(0)" id="at0">Auto-Tune</button></div></div>
<div class="r3"><div><label>Kp</label><input id="kp0" type="number" step="0.1"></div>
<div><label>Ki</label><input id="ki0" type="number" step="0.01"></div>
<div><label>Kd</label><input id="kd0" type="number" step="0.1"></div></div>
<button class="btn" onclick="saveP(0)">Save Lower</button></div>

<div class="c"><h2>Upper Probe</h2>
<div class="r2"><div><label>Output</label>
<select id="o1"><option value="0">SSR</option><option value="1">Relay1</option><option value="2">Relay2</option><option value="3">DC1</option><option value="4">DC2</option></select></div>
<div><label>&nbsp;</label><button class="btn at" onclick="atune(1)" id="at1">Auto-Tune</button></div></div>
<div class="r3"><div><label>Kp</label><input id="kp1" type="number" step="0.1"></div>
<div><label>Ki</label><input id="ki1" type="number" step="0.01"></div>
<div><label>Kd</label><input id="kd1" type="number" step="0.1"></div></div>
<button class="btn" onclick="saveP(1)">Save Upper</button></div>

<div class="c"><h2>Run Profile</h2>
<div id="steps"></div>
<button class="btn btn2" onclick="addStep()" id="ab">+ Add Step</button>
<button class="btn" onclick="saveAll()">Save All Steps</button></div>

<div class="nav"><a href="/">&larr; Dashboard</a></div>
<script>

function saveW(){var s=document.getElementById('wn').value,p=document.getElementById('wp').value;
 fetch('/api/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))
  .then(()=>{document.getElementById('wm').style.display='block'});}
function saveP(n){fetch('/api/pid?n='+n+'&kp='+document.getElementById('kp'+n).value
  +'&ki='+document.getElementById('ki'+n).value+'&kd='+document.getElementById('kd'+n).value
  +'&out='+document.getElementById('o'+n).value).then(()=>alert('Saved'));}
function atune(n){if(!confirm('Start Auto-Tune for '+(n?'Upper':'Lower')+' probe?\nHeater will cycle on/off. Ensure sensor is connected.'))return;
 fetch('/api/autotune?n='+n).then(()=>{document.getElementById('at'+n).textContent='Running...'});}
var STP=[];
function renderSteps(){var c=document.getElementById('steps');c.innerHTML='';
 STP.forEach(function(s,i){var d=document.createElement('div');d.className='c';d.style.background='#0f0f20';d.style.padding='10px';d.style.margin='6px 0';
  var h='<div style="display:flex;justify-content:space-between;align-items:center"><b>Step '+(i+1)+'</b>';
  if(STP.length>1)h+='<span style="color:#f44;cursor:pointer" onclick="delStep('+i+')">&times;</span>';
  h+='</div>';
  h+='<div class="r2"><div><label>Target &deg;F</label><input id="st'+i+'" type="number" step="0.1" value="'+s.target+'"></div>';
  h+='<div><label>Hold (min)</label><input id="sh'+i+'" type="number" value="'+s.hold+'"></div></div>';
  h+='<div class="r2"><div><label>Sensor</label><select id="ss'+i+'"><option value="0"'+(s.sensor==0?' selected':'')+'>Lower</option><option value="1"'+(s.sensor==1?' selected':'')+'>Upper</option></select></div>';
  h+='<div><label>Output</label><select id="so'+i+'"><option value="0"'+(s.out==0?' selected':'')+'>SSR</option><option value="1"'+(s.out==1?' selected':'')+'>Relay1</option><option value="2"'+(s.out==2?' selected':'')+'>Relay2</option><option value="3"'+(s.out==3?' selected':'')+'>DC1</option><option value="4"'+(s.out==4?' selected':'')+'>DC2</option></select></div></div>';
  h+='<div class="r2"><div><label>Max PWM</label><input id="sm'+i+'" type="number" min="0" max="255" value="'+s.maxpwm+'"></div><div></div></div>';
  d.innerHTML=h;c.appendChild(d)});
 document.getElementById('ab').style.display=STP.length>=8?'none':'block';}
function addStep(){if(STP.length>=8)return;var l=STP.length>0?STP[STP.length-1]:{target:175,hold:60,sensor:0,maxpwm:255,out:0};
 STP.push({target:l.target,hold:l.hold,sensor:l.sensor,maxpwm:l.maxpwm,out:l.out});renderSteps();}
function delStep(i){if(STP.length<=1)return;STP.splice(i,1);renderSteps();}
function saveAll(){var p=[];for(var i=0;i<STP.length;i++){
  STP[i].target=parseFloat(document.getElementById('st'+i).value)||175;
  STP[i].hold=parseInt(document.getElementById('sh'+i).value)||60;
  STP[i].sensor=parseInt(document.getElementById('ss'+i).value)||0;
  STP[i].maxpwm=parseInt(document.getElementById('sm'+i).value)||255;
  STP[i].out=parseInt(document.getElementById('so'+i).value)||0;}
 var done=0,total=STP.length;
 function next(i){if(i>=total){alert('Saved '+total+' steps');return;}
  fetch('/api/profile?step='+i+'&target='+STP[i].target+'&hold='+STP[i].hold+'&sensor='+STP[i].sensor+'&maxpwm='+STP[i].maxpwm+'&out='+STP[i].out)
  .then(function(){next(i+1)});}
 // First sync numSteps by adding/deleting
 fetch('/api/settings').then(function(r){return r.json()}).then(function(d){
  var cur=d.prof.n;
  function sync(){
   if(cur<total){fetch('/api/profile/add').then(function(){cur++;sync()});}
   else if(cur>total){fetch('/api/profile/del').then(function(){cur--;sync()});}
   else{next(0);}
  }
  sync();
 });}
setTimeout(function(){fetch('/api/settings').then(r=>r.json()).then(d=>{
 if(d.ssid){document.getElementById('wn').value=d.ssid;}
 for(var i=0;i<2;i++){document.getElementById('kp'+i).value=d.sc[i].kp;
  document.getElementById('ki'+i).value=d.sc[i].ki;document.getElementById('kd'+i).value=d.sc[i].kd;
  document.getElementById('o'+i).value=d.sc[i].out;}
 STP=d.prof.steps||[];if(STP.length==0)STP=[{target:175,hold:60,sensor:0,maxpwm:255,out:0}];renderSteps();
}).catch(()=>{});},500);
</script></body></html>)rawhtml";
