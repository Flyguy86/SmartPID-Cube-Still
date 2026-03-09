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
<label>SSR PWM: <b id="pv">0</b>%</label>
<input type="range" id="pw" min="0" max="100" value="0"
 oninput="document.getElementById('pv').textContent=this.value"
 onchange="gate(function(){fetch('/api/pwm?v='+Math.round(document.getElementById('pw').value*255/100))})">
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
<div class="c">
<h2>Recent Log</h2>
<div id="rl" style="font-size:.8em;color:#aaa;max-height:260px;overflow-y:auto;font-family:monospace"><span style="color:#666">No entries</span></div>
</div>
<div class="nav"><a href="/profiles">&#128295; Profiles</a> &middot; <a href="/settings">&#9881; Settings</a> &middot; <a href="/log">&#128202; Run Log</a></div>
<script>
var O=[0,0,0,0,0],RS=0,iv,_busy=0;
function gate(fn){if(_busy)return;_busy=1;setTimeout(function(){_busy=0},1000);fn();}
function tog(i){gate(function(){fetch('/api/output?id='+i+'&s='+(O[i]?0:1)).then(poll)})}
function rctl(){gate(function(){fetch('/api/'+(RS==0||RS==3?'start':'stop')).then(poll)})}
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
 var pp=Math.round(p*100/255);document.getElementById('pw').value=pp;document.getElementById('pv').textContent=pp;
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
  document.getElementById('rp').textContent=Math.round(p*100/255)+'%';
 }
}).catch(function(){})}
// Binary status: 17 bytes vs ~150 bytes JSON = ~9x less data per poll
iv=setInterval(poll,3000);poll();
var EN=['START','STOP','DONE','STEP','TARGET','HOLD','TEMP','ESTOP'];
var EC=['#ff6b35','#f44','#2196f3','#ff6b35','#4caf50','#4fc3f7','#aaa','#f44'];
function lfmt(s){var m=Math.floor(s/60),ss=s%60;return ('0'+m).slice(-2)+':'+('0'+ss).slice(-2);}
function logPoll(){fetch('/api/log/recent').then(r=>r.json()).then(d=>{
 if(!d.ok||!d.entries.length){document.getElementById('rl').innerHTML='<span style="color:#666">No entries</span>';return;}
 var h='';d.entries.forEach(e=>{
  h+='<div style="border-bottom:1px solid #222;padding:2px 0"><span style="color:#666">'+lfmt(e.t)+'</span> ';
  h+='<span style="color:'+EC[e.e]+'">'+EN[e.e]+'</span>';
  if(e.e==6)h+=' <b style="color:#4fc3f7">'+(e.x?'Up':'Lo')+' '+e.v.toFixed(1)+'\u00B0F</b>';
  else if(e.v)h+=' <span>'+e.v.toFixed(1)+'</span>';
  h+='</div>';});
 document.getElementById('rl').innerHTML=h;
 var el=document.getElementById('rl');el.scrollTop=el.scrollHeight;
}).catch(()=>{})}
setInterval(logPoll,5000);logPoll();
</script></body></html>)rawhtml";

// ─── Profiles Page ──────────────────────────────────────────────────────────

const char PAGE_PROFILES[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Profiles</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:12px;max-width:600px;margin:0 auto}
h1{text-align:center;color:#ff6b35;margin:12px 0;font-size:1.4em}
h2{font-size:1em;margin-bottom:6px;color:#aaa}
.c{background:#1a1a2e;border-radius:10px;padding:14px;margin:10px 0}
.btn{display:inline-block;padding:8px 14px;border:none;border-radius:6px;cursor:pointer;font-size:13px;font-weight:bold;margin:3px;text-align:center}
.on{background:#ff6b35;color:#fff}.off{background:#2a2a3e;color:#aaa}
.stp{background:#0f0f20;border-radius:8px;padding:10px;margin:8px 0}
.asgn{background:#151530;border-radius:6px;padding:8px;margin:6px 0;border-left:3px solid #ff6b35}
label{display:block;margin:4px 0 2px;font-size:.82em;color:#888}
input,select{padding:6px;border:1px solid #333;border-radius:4px;background:#0a0a1a;color:#eee;font-size:13px;width:100%}
.r2{display:flex;gap:8px}.r2>div{flex:1}
.save{display:block;width:100%;padding:12px;border:none;border-radius:6px;cursor:pointer;font-size:15px;font-weight:bold;background:#4caf50;color:#fff;margin:12px 0}
.del{color:#f44;cursor:pointer;float:right;font-weight:bold}
.add{display:inline-block;padding:6px 10px;border:none;border-radius:4px;cursor:pointer;font-size:12px;background:#2a2a3e;color:#ff6b35;margin:4px}
.nav{text-align:center;margin:16px 0}
a{color:#ff6b35}
.msg{padding:8px;border-radius:4px;text-align:center;margin:8px 0;display:none;background:#2e7d32;color:#fff}
</style></head><body>
<h1>&#128295; Profiles</h1>
<div class="c" style="text-align:center">
<button class="btn on" id="pb0" onclick="selP(0)">P1</button>
<button class="btn off" id="pb1" onclick="selP(1)">P2</button>
<button class="btn off" id="pb2" onclick="selP(2)">P3</button>
</div>
<div class="c">
<div class="r2">
<div><label>Profile Name</label><input id="pn" type="text" maxlength="15"></div>
<div><label>&nbsp;</label><button class="btn off" id="actb" onclick="setAct()" style="width:100%;padding:7px">Set Active</button></div>
</div></div>
<div id="steps"></div>
<div style="text-align:center"><button class="add" onclick="addStp()" id="asb">+ Add Step</button></div>
<button class="save" onclick="saveAll()">&#128190; Save Profile</button>
<div class="msg" id="msg">Saved!</div>
<div class="nav"><a href="/">&larr; Dashboard</a> &middot; <a href="/settings">&#9881; Settings</a> &middot; <a href="/log">&#128202; Run Log</a></div>
<script>
var CP=0,AP=0,P=[],D=[],ON=['SSR','RL1','RL2','DC1','DC2'],_busy=0;
function gate(fn){if(_busy)return;_busy=1;setTimeout(function(){_busy=0},1000);fn();}
function selP(p){gate(function(){CP=p;
 for(var i=0;i<3;i++)document.getElementById('pb'+i).className='btn '+(i==p?'on':'off');
 document.getElementById('pn').value=P[p]?P[p].name:'';
 updAct();loadP(p);});}
function updAct(){
 document.getElementById('actb').className='btn '+(CP==AP?'on':'off');
 document.getElementById('actb').textContent=CP==AP?'\u2713 Active':'Set Active';}
function setAct(){gate(function(){fetch('/api/profile/select?p='+CP).then(function(){AP=CP;updAct();});});}
function loadP(p){var m=document.getElementById('msg');
 m.style.display='block';m.textContent='Loading...';m.style.background='#1565c0';
 fetch('/api/profile/get?p='+p).then(function(r){
  if(!r.ok)throw new Error('HTTP '+r.status);
  return r.text();
 }).then(function(txt){
  console.log('profile raw:',txt);
  var d=JSON.parse(txt);
  D=d.steps||[];m.style.display='none';render();
 }).catch(function(e){D=[];render();
  console.error('loadP error:',e);
  m.textContent='Load error: '+e.message;m.style.background='#c62828';
  setTimeout(function(){m.style.display='none'},3000);});}
function render(){
 var c=document.getElementById('steps');c.innerHTML='';
 D.forEach(function(st,si){
  var d=document.createElement('div');d.className='c stp';
  var h='<div style="display:flex;justify-content:space-between;align-items:center"><b>Step '+(si+1)+'</b>';
  h+='<span class="del" onclick="delStp('+si+')">&times;</span></div>';
  h+='<div class="r2"><div><label>Hold (min)</label><input id="h'+si+'" type="number" value="'+st.hold+'"></div>';
  h+='<div><label>Direction</label><select id="cm'+si+'"><option value="0"'+(st.cool?'':' selected')+'>Heat &#8593;</option>';
  h+='<option value="1"'+(st.cool?' selected':'')+'>Cool &#8595;</option></select></div></div>';
  if(!st.a)st.a=[];
  st.a.forEach(function(a,ai){
   h+='<div class="asgn"><div style="display:flex;justify-content:space-between"><b style="font-size:.82em;color:#4fc3f7">Sensor '+(ai+1)+'</b>';
   h+='<span class="del" style="font-size:.9em" onclick="delA('+si+','+ai+')">&times;</span></div>';
   h+='<div class="r2"><div><label>Probe</label><select id="as'+si+'_'+ai+'">';
   h+='<option value="0"'+(a.s==0?' selected':'')+'>Lower</option><option value="1"'+(a.s==1?' selected':'')+'>Upper</option></select></div>';
   h+='<div><label>Output</label><select id="ao'+si+'_'+ai+'">';
   for(var oi=0;oi<5;oi++)h+='<option value="'+oi+'"'+(a.o==oi?' selected':'')+'>'+ON[oi]+'</option>';
   h+='</select></div></div>';
   h+='<div class="r2"><div><label>Target &deg;F</label><input id="at'+si+'_'+ai+'" type="number" step="0.1" value="'+a.t+'"></div>';
   h+='<div><label>Max PWM %</label><input id="am'+si+'_'+ai+'" type="number" min="0" max="100" value="'+Math.round(a.m*100/255)+'">';h+='</div></div></div>';
  });
  if(st.a.length<2)h+='<button class="add" onclick="addA('+si+')">+ Add Sensor</button>';
  d.innerHTML=h;c.appendChild(d);});
 document.getElementById('asb').style.display=D.length>=10?'none':'inline-block';}
function addStp(){if(D.length>=10)return;readUI();D.push({hold:60,cool:false,a:[]});render();}
function delStp(i){readUI();D.splice(i,1);render();}
function addA(si){if(D[si].a.length>=2)return;readUI();
 var la=D[si].a.length>0?D[si].a[0]:{s:0,o:0,m:100,t:175};
 D[si].a.push({s:la.s?0:1,o:la.o,m:la.m,t:la.t});render();}
function delA(si,ai){readUI();D[si].a.splice(ai,1);render();}
function readUI(){D.forEach(function(st,si){
 st.hold=parseInt(document.getElementById('h'+si).value)||0;
 st.cool=document.getElementById('cm'+si).value=='1';
 st.a.forEach(function(a,ai){
  a.s=parseInt(document.getElementById('as'+si+'_'+ai).value)||0;
  a.o=parseInt(document.getElementById('ao'+si+'_'+ai).value)||0;
  a.t=parseFloat(document.getElementById('at'+si+'_'+ai).value)||175;
  a.m=Math.round((parseInt(document.getElementById('am'+si+'_'+ai).value)||100)*255/100);
 });});}
function saveAll(){readUI();
 var name=document.getElementById('pn').value;
 var m=document.getElementById('msg'),sv=document.querySelector('.save');
 m.style.display='block';m.textContent='Saving...';m.style.background='#e65100';
 sv.disabled=true;sv.style.opacity='0.5';
 fetch('/api/profile/name?p='+CP+'&name='+encodeURIComponent(name)).then(function(){
  return new Promise(function(r){setTimeout(r,300)});
 }).then(function(){
  m.textContent='Saving layout...';
  return fetch('/api/profile/resize?p='+CP+'&n='+D.length);
 }).then(function(){
  var i=0;
  function next(){
   if(i>=D.length){
    m.textContent='\u2713 Saved!';m.style.background='#2e7d32';
    sv.disabled=false;sv.style.opacity='1';
    setTimeout(function(){m.style.display='none'},2000);
    P[CP].name=name;P[CP].n=D.length;
    for(var j=0;j<3;j++)document.getElementById('pb'+j).textContent=P[j].name||('P'+(j+1));
    return;}
   setTimeout(function(){
    m.textContent='Saving step '+(i+1)+'/'+D.length+'...';
    var st=D[i],u='/api/profile/step?p='+CP+'&s='+i+'&hold='+st.hold+'&cool='+(st.cool?1:0)+'&na='+st.a.length;
    st.a.forEach(function(a,ai){u+='&s'+ai+'='+a.s+'&o'+ai+'='+a.o+'&m'+ai+'='+a.m+'&t'+ai+'='+a.t;});
    fetch(u).then(function(){i++;next();}).catch(function(){
     m.textContent='Error saving step '+(i+1);m.style.background='#c62828';
     sv.disabled=false;sv.style.opacity='1';});},300);}
  next();
 }).catch(function(){m.textContent='Save error';m.style.background='#c62828';
  sv.disabled=false;sv.style.opacity='1';});}
fetch('/api/profiles').then(function(r){return r.json()}).then(function(d){
 AP=d.ap;P=d.p;
 for(var i=0;i<3;i++)document.getElementById('pb'+i).textContent=P[i].name||('P'+(i+1));
 setTimeout(function(){selP(AP);},200);});
</script></body></html>)rawhtml";

// ─── Run Log Page ───────────────────────────────────────────────────────────

const char PAGE_LOG[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Run Log</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:12px;max-width:600px;margin:0 auto}
h1{text-align:center;color:#ff6b35;margin:12px 0;font-size:1.4em}
h2{font-size:1.1em;margin-bottom:8px;color:#aaa}
.c{background:#1a1a2e;border-radius:10px;padding:14px;margin:10px 0}
.btn{display:inline-block;padding:10px 16px;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold;margin:4px;text-align:center;background:#ff6b35;color:#fff}
.btn2{background:#2a2a3e;color:#aaa}
.nav{text-align:center;margin:16px 0}
a{color:#ff6b35}
table{width:100%;border-collapse:collapse;font-size:.85em;margin-top:8px}
th{background:#2a2a3e;padding:6px 4px;text-align:left;position:sticky;top:0}
td{padding:4px;border-bottom:1px solid #222}
.ev{color:#4fc3f7}.tmp{color:#4caf50}
.tbl{max-height:400px;overflow-y:auto;border-radius:6px}
.info{display:flex;gap:16px;flex-wrap:wrap;margin-bottom:8px}
.info span{color:#aaa;font-size:.9em}
.info b{color:#4fc3f7}
</style></head><body>
<h1>&#128202; Run Log</h1>

<div class="c">
<div style="text-align:center;margin-bottom:10px">
<button class="btn" onclick="load('active')" id="ba">Active Run</button>
<button class="btn btn2" onclick="load('last')" id="bl">Last Run</button>
</div>
<div class="info" id="info"><span>No data loaded</span></div>
<div style="text-align:center;margin:8px 0">
<button class="btn btn2" onclick="dlCSV()" id="dc" style="display:none">&#11015; Download CSV</button>
</div>
<div class="tbl" id="tbl"></div>
</div>

<div class="nav"><a href="/">&larr; Dashboard</a> &middot; <a href="/settings">&#9881; Settings</a></div>
<script>
var W='active';
var EN=['RUN_START','RUN_STOP','RUN_DONE','STEP_START','TARGET_HIT','HOLD_DONE','TEMP','ESTOP'];

function fmt(s){var m=Math.floor(s/60),ss=s%60;return m+'m '+('0'+ss).slice(-2)+'s';}

function load(w){
 W=w;
 document.getElementById('ba').className='btn'+(w=='active'?'':' btn2');
 document.getElementById('bl').className='btn'+(w=='last'?'':' btn2');
 document.getElementById('tbl').innerHTML='<p style="text-align:center;color:#666">Loading...</p>';
 document.getElementById('info').innerHTML='';
 fetch('/api/log?which='+w).then(r=>r.json()).then(d=>{
  if(!d.ok){document.getElementById('tbl').innerHTML='<p style="text-align:center;color:#666">'+
   (d.msg||'No data')+'</p>';document.getElementById('dc').style.display='none';return;}
  // Info bar
  var inf='<span>Duration: <b>'+fmt(d.dur)+'</b></span>';
  inf+='<span>Steps: <b>'+d.steps+'</b></span>';
  inf+='<span>Entries: <b>'+d.count+'</b></span>';
  if(d.logging)inf+='<span style="color:#4caf50"><b>&#9679; Recording</b></span>';
  document.getElementById('info').innerHTML=inf;
  document.getElementById('dc').style.display=d.count>0?'inline-block':'none';
  // Table
  if(d.count==0){document.getElementById('tbl').innerHTML='<p style="text-align:center;color:#666">No entries yet</p>';return;}
  var h='<table><thead><tr><th>Time</th><th>Event</th><th>Idx</th><th>Value</th></tr></thead><tbody>';
  d.entries.forEach(function(e){
   var cls=e.e==6?'tmp':'ev';
   h+='<tr><td>'+fmt(e.t)+'</td><td class="'+cls+'">'+EN[e.e]+'</td><td>'+e.x+'</td>';
   h+='<td>'+(e.e==6?e.v.toFixed(1)+'\u00B0F':e.v.toFixed(1))+'</td></tr>';
  });
  h+='</tbody></table>';
  document.getElementById('tbl').innerHTML=h;
 }).catch(()=>{document.getElementById('tbl').innerHTML='<p style="color:#f44">Failed to load</p>';});
}

function dlCSV(){window.location='/api/log/csv?which='+W;}

load('active');
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

<div class="nav"><a href="/">&larr; Dashboard</a> &middot; <a href="/profiles">&#128295; Profiles</a> &middot; <a href="/log">&#128202; Run Log</a></div>
<script>

function saveW(){var s=document.getElementById('wn').value,p=document.getElementById('wp').value;
 if(!s){alert('Enter a network name');return;}
 var m=document.getElementById('wm');m.style.display='block';m.textContent='Saving & connecting...';
 fetch('/api/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))
  .then(()=>{m.textContent='Saved! Device is joining network. Check serial log for new IP.';})
  .catch(()=>{m.textContent='Saved! Device may be reconnecting — check serial log for new IP.';});}
function saveP(n){fetch('/api/pid?n='+n+'&kp='+document.getElementById('kp'+n).value
  +'&ki='+document.getElementById('ki'+n).value+'&kd='+document.getElementById('kd'+n).value
  +'&out='+document.getElementById('o'+n).value).then(()=>alert('Saved'));}
function atune(n){if(!confirm('Start Auto-Tune for '+(n?'Upper':'Lower')+' probe?\nHeater will cycle on/off. Ensure sensor is connected.'))return;
 fetch('/api/autotune?n='+n).then(()=>{document.getElementById('at'+n).textContent='Running...'});}
setTimeout(function(){fetch('/api/settings').then(r=>r.json()).then(d=>{
 if(d.ssid){document.getElementById('wn').value=d.ssid;}
 for(var i=0;i<2;i++){document.getElementById('kp'+i).value=d.sc[i].kp;
  document.getElementById('ki'+i).value=d.sc[i].ki;document.getElementById('kd'+i).value=d.sc[i].kd;
  document.getElementById('o'+i).value=d.sc[i].out;}
}).catch(()=>{});},500);
</script></body></html>)rawhtml";
