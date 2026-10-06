#pragma once
// PROGMEM web assets. Every byte is charged against the app partition, so this
// is deliberately compact: no framework, no webfont, no
// external request of any kind. The board serves these pages on a LAN that may
// have no route to the internet, and a stylesheet that failed to load would
// leave the only interface unusable.
//
// The design is a status panel rather than a web app. A left edge-bar carries
// each device's trust state the way an equipment panel marks a channel, and
// the word "known" is never printed — it is the default, so saying it adds
// nothing and would drown out the exceptions.
#include <Arduino.h>

// Shared shell. Inlined into both pages rather than fetched separately: a
// second request is another chance at a half-rendered page on a board that is
// also mid-sweep.
#define NM_HEAD \
"<meta charset=utf-8>" \
"<meta name=viewport content=\"width=device-width,initial-scale=1\">" \
"<style>" \
":root{--bg:#f2f4f6;--panel:#fff;--edge:#dde3e9;--ink:#16202b;--mut:#5f7183;" \
"--ok:#1f9d57;--warn:#b8770c;--bad:#d33b38;--link:#0e7c93;--tint:#eaf1f7;" \
"--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}" \
"@media(prefers-color-scheme:dark){:root{--bg:#141a22;--panel:#1b242e;" \
"--edge:#2b3947;--ink:#e4eaf0;--mut:#8fa0b0;--ok:#4fd18b;--warn:#e5a83c;" \
"--bad:#f0605f;--link:#5bc8d6;--tint:#22303e}}" \
"*{box-sizing:border-box}[hidden]{display:none!important}" \
"body{margin:0;background:var(--bg);color:var(--ink);" \
"font:15px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Ubuntu,sans-serif}" \
".sh{max-width:44rem;margin:0 auto;background:var(--panel);min-height:100vh;" \
"border-inline:1px solid var(--edge)}" \
"header{display:flex;align-items:baseline;justify-content:space-between;" \
"gap:1rem;padding:.85rem 1rem;border-bottom:1px solid var(--edge)}" \
"header b{font-weight:600;letter-spacing:-.01em}" \
"a{color:var(--link)}a.nav{text-decoration:none;font-size:.9rem}" \
"a.nav:hover{text-decoration:underline}" \
":focus-visible{outline:2px solid var(--link);outline-offset:2px}" \
"input,select,button{font:inherit;color:inherit}" \
".foot{padding:1rem;font-size:.82rem;color:var(--mut)}" \
".brand{display:flex;align-items:center;gap:.45rem;text-decoration:none;" \
"color:inherit}" \
".brand svg{display:block;color:var(--ok)}" \
".brand b{font-weight:600;letter-spacing:-.01em}" \
".ver{font-size:.68rem;color:var(--mut);font-variant-numeric:tabular-nums;" \
"align-self:flex-end;padding-bottom:.12rem}" \
"nav{display:flex;flex-wrap:wrap;gap:.3rem .9rem;font-size:.9rem}" \
"header{flex-wrap:wrap;row-gap:.4rem}nav{margin-left:auto}" \
"@media(max-width:400px){header{gap:.6rem}nav{gap:.3rem .6rem;font-size:.84rem}}" \
"nav a{text-decoration:none}nav a:hover{text-decoration:underline}" \
"nav span{color:var(--mut)}" \
"section{padding:1rem;border-bottom:1px solid var(--edge)}" \
"h2{margin:0;font-size:.95rem;font-weight:600;letter-spacing:-.01em}" \
"label{display:block;margin:.75rem 0 .25rem;font-size:.85rem;color:var(--mut)}" \
"input,select{width:100%;padding:.5rem .6rem;border:1px solid var(--edge);" \
"border-radius:9px;background:var(--panel)}" \
"button{padding:.5rem .85rem;border:1px solid var(--edge);border-radius:9px;" \
"background:var(--bg);cursor:pointer}" \
"button.primary{width:100%;padding:.65rem;background:var(--ink);" \
"color:var(--panel);border-color:var(--ink);font-weight:600}" \
".hint{margin:.35rem 0 0;font-size:.78rem;color:var(--mut)}" \
"dl{display:grid;grid-template-columns:auto 1fr;gap:.32rem .9rem;" \
"margin:.9rem 0 0;font-size:.85rem}" \
"dt{color:var(--mut)}" \
"dd{margin:0;font-family:var(--mono);font-variant-numeric:tabular-nums;" \
"overflow-wrap:anywhere}" \
"</style>" \
"<script>" \
/* From 0.13 every request needs signing in. A session that ends while a */ \
/* page is open shows as a 401 marked X-Netmon-Login on whatever the page */ \
/* asks next; the page then goes to the sign-in page, which brings it back. */ \
"(function(){var f=window.fetch;if(!f)return;" \
"window.fetch=function(){return f.apply(this,arguments).then(function(r){" \
"if(r.status==401&&r.headers.get('X-Netmon-Login')){nmlogin();return new Promise(function(){})}" \
"return r})}})();" \
"function nmlogin(){location.href='/login?next='+encodeURIComponent(location.pathname+location.search)+location.hash}" \
"function nmsignout(){var go=function(){location.href='/login'};" \
"fetch('/api/logout',{method:'POST'}).then(go,go)}" \
"</script>"

// The one page served without a session. "Remember me" asks the board for a
// session that lasts 30 days and a cookie that outlives the browser; the
// browser's own password manager offers to save the password, which the
// autocomplete names below let it do.
static const char LOGIN_HTML[] PROGMEM =
"<!doctype html><title>Sign in - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
.in{max-width:23rem;margin:0 auto;padding:2rem 1rem 1.4rem}
h1{margin:0;font-size:1.3rem;font-weight:600;letter-spacing:-.02em}
.who{margin:.3rem 0 0;color:var(--mut);font-size:.88rem;overflow-wrap:anywhere}
.pw{display:flex;gap:.4rem}.pw input{flex:1;min-width:0}.pw button{flex:none}
label.keep{display:flex;align-items:flex-start;gap:.55rem;margin:1rem 0 1.1rem;
 font-size:.9rem;color:var(--ink);cursor:pointer}
.keep input{width:auto;margin:.2rem 0 0;padding:0;flex:none}
#err{margin:.85rem 0 0;font-size:.88rem;color:var(--bad)}#err:empty{display:none}
button.primary:disabled{opacity:.6;cursor:default}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a></header>
<form class=in id=f>
<h1>Sign in</h1>
<p class=who id=who>to this network monitor</p>
<input id=user name=username autocomplete=username value=netmon hidden>
<label for=pw>Password</label>
<div class=pw><input id=pw name=password type=password autocomplete=current-password
 autocapitalize=off spellcheck=false autofocus>
<button type=button id=peek aria-pressed=false>Show</button></div>
<label class=keep><input type=checkbox id=keep><span id=keeptext>Remember me on this
 browser for 30 days</span></label>
<button class=primary id=go>Sign in</button>
<p id=err role=alert></p>
<p class=hint>Your login password, or the board's update password from secrets.h,
which always works. Unticked, you stay signed in until the browser closes.</p>
</form>
</div>
<script>
var busy=false,waitT=0,nx='/';
// Back to the page that sent you here, but only to a page on this board:
// "//elsewhere" and "/\elsewhere" would leave it, and browsers drop tabs and
// line breaks from an address before reading it.
(function(){var m=location.search.match(/[?&]next=([^&#]*)/),v='';
 if(m)try{v=decodeURIComponent(m[1])}catch(e){}
 if(/^\/(?![\/\\])[^\\\x00-\x20\x7f]*$/.test(v))nx=v;
 if(/^\/login\b/.test(nx))nx='/';
 if(location.hash&&nx.indexOf('#')<0)nx+=location.hash})();
function say(m){err.textContent=m||''}
peek.onclick=function(){var s=pw.type=='password';pw.type=s?'text':'password';
 peek.textContent=s?'Hide':'Show';peek.setAttribute('aria-pressed',s);pw.focus()};
try{keep.checked=localStorage.getItem('nm.keep')=='1'}catch(e){}
fetch('/api/auth',{cache:'no-store'}).then(function(r){return r.json()}).then(function(a){
 if(a.signed_in){location.replace(nx);return}
 ver.textContent=a.version||'';who.textContent=(a.name||'netmon')+' at '+location.host;
 if(a.remember_days)keeptext.textContent='Remember me on this browser for '+a.remember_days+' days'})
 .catch(function(){say('Could not reach the board.')});
// After too many wrong passwords the board says how long to wait.
function hold(s){var end=Date.now()+s*1000;go.disabled=true;clearInterval(waitT);
 function tick(){var l=Math.ceil((end-Date.now())/1000);
  if(l>0){say('Too many wrong passwords. Try again in '+l+' s.');return}
  clearInterval(waitT);go.disabled=false;say('')}
 tick();waitT=setInterval(tick,1000)}
f.onsubmit=function(e){e.preventDefault();if(busy||go.disabled)return;
 if(!pw.value){say('Enter the password.');pw.focus();return}
 busy=true;go.disabled=true;go.textContent='Signing in…';say('');
 try{localStorage.setItem('nm.keep',keep.checked?'1':'0')}catch(x){}
 fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({password:pw.value,remember:keep.checked,unix:Math.floor(Date.now()/1000)})})
 .then(function(r){return r.json().catch(function(){return {}}).then(function(j){return {s:r.status,j:j}})})
 .then(function(x){busy=false;go.textContent='Sign in';
  if(x.s==200){location.replace(nx);return}
  go.disabled=false;
  if(x.s==429){hold(x.j.retry_s||30);return}
  say(x.j.error||'The board refused ('+x.s+').');pw.select()})
 .catch(function(){busy=false;go.disabled=false;go.textContent='Sign in';
  say('Could not reach the board.')})};
</script>
)HTML";

static const char DASHBOARD_HTML[] PROGMEM =
"<!doctype html><title>Network Monitor</title>"
NM_HEAD
R"HTML(<style>
.top{padding:1.05rem 1rem .9rem}
.count{margin:0;font-size:1.35rem;font-weight:600;letter-spacing:-.02em;
 font-variant-numeric:tabular-nums}
.verdict{margin:.2rem 0 0;font-size:.95rem}
.verdict.clear{color:var(--mut)}
.verdict.flag{color:var(--bad);font-weight:500}
.verdict.idle{color:var(--warn);font-weight:500}
.log{margin:.55rem 0 0;font-size:.82rem;color:var(--mut);
 font-variant-numeric:tabular-nums}
.find{padding:0 1rem .85rem}
.find input{width:100%;padding:.55rem .7rem;border:1px solid var(--edge);
 border-radius:9px;background:var(--panel)}
.hits{margin:.45rem 0 0;font-size:.8rem;color:var(--mut)}
.wrap{overflow-x:auto;border-top:1px solid var(--edge)}
table{border-collapse:collapse;table-layout:fixed;font-size:.84rem}
@media(max-width:640px){table{font-size:.78rem}}
th,td{text-align:left;padding:.42rem .5rem;border-bottom:1px solid var(--edge);
 overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
th{position:relative;font-weight:500;color:var(--mut);cursor:pointer;
 user-select:none}
th:hover{color:var(--ink)}
.ar{font-size:.68rem;margin-left:.2rem}
/* The handle overhangs its own header so the grab area straddles the border.
   Later headers paint after earlier ones, so without a z-index the next
   column's cell sits on top of it and swallows the pointer. */
.rz{position:absolute;top:0;right:-5px;width:11px;height:100%;z-index:2;
 cursor:col-resize;touch-action:none}
.rz:hover::after,.rz.on::after{content:'';position:absolute;left:5px;top:0;
 width:1px;height:100%;background:var(--link)}
td.ip,td.mac,td.up{font-family:var(--mono);font-variant-numeric:tabular-nums}
td.st{box-shadow:inset 3px 0 var(--ok)}
tr.private td.st{box-shadow:inset 3px 0 var(--warn);color:var(--warn)}
tr.unknown td.st{box-shadow:inset 3px 0 var(--bad);color:var(--bad);
 font-weight:600}
tr.self{background:var(--tint)}
tr.off{opacity:.55}
tr.off td.st{box-shadow:inset 3px 0 var(--mut)}
.tag{font-size:.72rem;color:var(--mut);margin-left:.35rem}
.none{padding:1.6rem 1rem;color:var(--mut)}
</style>
<div class=sh>
<header><a class=brand href="/">
<svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg>
<b>netmon</b><span class=ver id=ver></span></a>
<nav><span>Devices</span><a href="/map">Map</a><a href="/nearby">Nearby</a><a href="/events">Events</a><a href="/isp">Internet</a>
<a href="/settings">Settings</a></nav></header>
<div class=top>
 <p class=count id=count>Loading</p>
 <p class=verdict id=verdict></p>
 <p class=log id=log></p>
</div>
<div class=find>
 <input id=q type=search autocomplete=off
  placeholder="Search address, vendor or MAC">
 <p class=hits id=hits></p>
</div>
<div class=wrap id=wrap><table id=tbl><colgroup id=cg></colgroup>
<thead><tr id=hrow></tr></thead><tbody id=rows></tbody></table></div>
<p class=none id=none hidden></p>
<p class=foot><a href="/events">Event history</a> · <a href="/api/health">Health data</a> · <a href="/login" onclick="nmsignout();return false">Sign out</a></p>
</div>
<script>
// Floors are measured from the widest real value each column has to hold:
// a 15-character address, twelve MAC digits, "Hangzhou Huacheng Network".
// Five columns of this cannot fit a phone, so the table scrolls inside its own
// container rather than shrinking values into ellipses.
var all=[],cols=[{k:'status',t:'State',f:.13,m:84},{k:'ip',t:'IP',f:.19,m:140},
 {k:'mac',t:'MAC',f:.17,m:116},{k:'hostname',t:'Hostname',f:.24,m:150},
 {k:'vendor',t:'Vendor',f:.20,m:150},{k:'up',t:'Uptime',f:.10,m:74}];
var sortk='ip',sortd=1;
function put(k,v){try{localStorage.setItem('nm.'+k,v)}catch(e){}}
function get(k){try{return localStorage.getItem('nm.'+k)}catch(e){return null}}
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;')
 .replace(/"/g,'&quot;')}
function age(s){if(s<60)return s+'s';if(s<3600)return((s/60)|0)+'m';
 if(s<86400)return((s/3600)|0)+'h';return((s/86400)|0)+'d'}
function dur(s){if(!s)return '';if(s<60)return s+'s';
 if(s<3600)return((s/60)|0)+'m';
 if(s<86400)return((s/3600)|0)+'h '+(((s%3600)/60)|0)+'m';
 return((s/86400)|0)+'d '+(((s%86400)/3600)|0)+'h'}
// Registry names carry legal boilerplate that costs a column's width and tells
// you nothing. Display only; /api/devices keeps the full string.
function shortv(v){if(!v)return '';
 var x=v.replace(/,/g,' ').replace(
  /\b(corporation|corp|company|technologies|technology|electronics|international|holdings|limited|ltd|inc|co|llc|gmbh|plc|pte|bv|nv|srl|spa)\b\.?/gi,' ')
  .replace(/\s+/g,' ').replace(/^[\s.]+|[\s.]+$/g,'');
 return x||v}
function vendor(x){return x.vendor?shortv(x.vendor)
 :x.randomised?'randomised address':'not in registry'}
function ipnum(s){var p=String(s||'').split('.');
 return p.length==4?(+p[0])*16777216+(+p[1])*65536+(+p[2])*256+(+p[3]):0}
var rank={unknown:0,private:1,known:2};
function cmp(a,b){var r;
 if(sortk=='ip')r=ipnum(a.ip)-ipnum(b.ip);
 else if(sortk=='up')r=(a.up_s||0)-(b.up_s||0);
 else if(sortk=='status')r=(rank[a.status]-rank[b.status])||
  (ipnum(a.ip)-ipnum(b.ip));
 else if(sortk=='hostname'){var av=a.hostname||'',bv=b.hostname||'';
  r=av.toLowerCase()<bv.toLowerCase()?-1:av.toLowerCase()>bv.toLowerCase()?1:0}
 else if(sortk=='vendor'){var av=vendor(a),bv=vendor(b);
  r=av.toLowerCase()<bv.toLowerCase()?-1:av.toLowerCase()>bv.toLowerCase()?1:0}
 else r=a.mac<b.mac?-1:a.mac>b.mac?1:0;
 return r*sortd||ipnum(a.ip)-ipnum(b.ip)}
function widths(){var sum=0;
 cols.forEach(function(c,i){cg.children[i].style.width=c.px+'px';sum+=c.px});
 tbl.style.width=sum+'px'}
function initWidths(){var saved=null,avail=Math.max(320,wrap.clientWidth-1);
 try{saved=JSON.parse(get('w'))}catch(e){}
 cols.forEach(function(c,i){
  c.px=(saved&&saved[i]>=46)?saved[i]:Math.max(c.m,Math.round(avail*c.f))})}
 // No widths() here: the <col> elements it writes to do not exist until
 // head() builds them.
function head(){
 hrow.innerHTML='';cg.innerHTML='';
 cols.forEach(function(c,i){
  cg.appendChild(document.createElement('col'));
  var th=document.createElement('th');
  th.innerHTML=c.t+(sortk==c.k?'<span class=ar>'+(sortd>0?'▲':'▼')
   +'</span>':'');
  th.title='Sort by '+c.t.toLowerCase();
  th.onclick=function(){sortd=(sortk==c.k)?-sortd:1;sortk=c.k;
   put('s',c.k);put('d',sortd);head();render()};
  var h=document.createElement('span');h.className='rz';
  h.onclick=function(e){e.stopPropagation()};
  h.addEventListener('pointerdown',function(e){
   e.preventDefault();e.stopPropagation();
   h.setPointerCapture(e.pointerId);h.classList.add('on');
   var x0=e.clientX,w0=th.offsetWidth;
   function mv(ev){c.px=Math.max(46,w0+(ev.clientX-x0));widths()}
   function up(){h.classList.remove('on');
    h.removeEventListener('pointermove',mv);h.removeEventListener('pointerup',up);
    put('w',JSON.stringify(cols.map(function(z){return z.px})))}
   h.addEventListener('pointermove',mv);h.addEventListener('pointerup',up)});
  th.appendChild(h);hrow.appendChild(th)});
 widths()}
function render(){
 var f=q.value.trim().toLowerCase(),show=all;
 if(f)show=all.filter(function(x){
  return (x.ip+' '+x.mac+' '+(x.vendor||'')+' '+(x.hostname||'')+' '+x.status)
   .toLowerCase().indexOf(f)>-1});
 hits.textContent=f?show.length+' of '+all.length+' devices':'';
 none.hidden=show.length>0;wrap.hidden=!show.length;
 if(!show.length){none.textContent=f?'No device matches '+q.value+'.'
  :'No devices found yet. The first sweep runs a minute after start-up.';
  return}
 rows.innerHTML=show.slice().sort(cmp).map(function(x){
  return '<tr class="'+x.status+(x.self?' self':'')+(x.online?'':' off')+'">'
   +'<td class=st>'+x.status+'</td>'
   +'<td class=ip>'+esc(x.ip||'')
   +(x.self?'<span class=tag>this</span>':'')+'</td>'
   +'<td class=mac title="'+esc(x.mac)+'">'+esc(x.mac.replace(/:/g,''))+'</td>'
   +'<td title="'+esc(x.hostname||'')+'">'+esc(x.hostname||'—')+'</td>'
   +'<td title="'+esc(x.vendor||'')+'">'+esc(vendor(x))+'</td>'
   +'<td class=up title="last seen '+age(x.last_seen_s)+' ago">'
   +(x.online?dur(x.up_s):'—')+'</td></tr>'}).join('')}
// The board has no clock of its own; it dates saved reports by the first one
// it is given (firmware 0.12).
var clockSent=false;
function sendclock(){if(clockSent)return;clockSent=true;
 fetch('/api/clock',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({unix:Math.floor(Date.now()/1000)})}).catch(function(){})}
function load(){
 fetch('/api/health').then(function(r){return r.json()}).then(function(h){
  ver.textContent=h.version;
  if(h.clock===false)sendclock();
  count.textContent=h.devices+(h.devices==1?' device on ':' devices on ')+h.subnet;
  log.textContent=(h.last_pass_ms
   ?'Swept in '+(h.last_pass_ms/1000).toFixed(1)+'s, reached '+h.pass_seen+'. '
   :'First sweep has not finished yet. ')
   +'Signal '+h.rssi+' dBm.'
   +(h.latency_valid?' · Gateway '+h.latency_ms+' ms':'');
  if(!h.sweepable){verdict.className='verdict idle';
   verdict.textContent='Not scanning. No usable subnet, so the board is in setup mode.';
   return}
  return fetch('/api/devices').then(function(r){return r.json()})
   .then(function(d){all=d;
    var u=d.filter(function(x){return x.status=='unknown'}).length;
    if(u){verdict.className='verdict flag';
     verdict.textContent=u==1?'1 device is not recognised.'
      :u+' devices are not recognised.'}
    else{verdict.className='verdict clear';
     verdict.textContent=h.baseline_open
      ?'Still learning. Everything seen so far counts as known for '
       +Math.max(1,Math.round(h.baseline_closes_in_s/60))+' more minutes.'
      :'Everything here is recognised.'}
    render()})})
 .catch(function(){verdict.className='verdict flag';
  verdict.textContent='Cannot reach the monitor.'})}
sortk=get('s')||'ip';sortd=+get('d')||1;
initWidths();head();
q.addEventListener('input',render);
// /?q=192.168.2.45 opens with that search, as the Map page links to it.
var qq=location.search.match(/[?&]q=([^&]*)/);
if(qq)try{q.value=decodeURIComponent(qq[1].replace(/\+/g,' '))}catch(e){}
load();setInterval(load,15000);
</script>
)HTML";

static const char SETTINGS_HTML[] PROGMEM =
"<!doctype html><title>Settings - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
#msg,#fwmsg,#netmsg{margin:.75rem 0 0;font-size:.85rem;min-height:1.2em}
#msg.err,#fwmsg.err,#netmsg.err{color:var(--bad)}
#msg.ok,#fwmsg.ok{color:var(--ok)}
#netmsg:empty{display:none}
button:disabled{opacity:.5;cursor:default}
.nets{width:100%;border-collapse:collapse;margin:.8rem 0 0;font-size:.85rem}
.nets th{text-align:left;font-weight:500;font-size:.76rem;color:var(--mut);
 padding:.3rem .4rem;border-bottom:1px solid var(--edge)}
.nets td{padding:.55rem .4rem;border-bottom:1px solid var(--edge);vertical-align:top}
.nets td.n{width:1.6rem;color:var(--mut);font-variant-numeric:tabular-nums}
.nets td.a{width:1%;text-align:right;white-space:nowrap}
.nets .ss{font-weight:500;overflow-wrap:anywhere}
.nets .sub{display:block;margin-top:.1rem;font-size:.76rem;color:var(--mut)}
.nets .good{color:var(--ok)}.nets .bad{color:var(--bad)}.nets .warn{color:var(--warn)}
.nets button{padding:.28rem .6rem;font-size:.8rem}
.nets button.arm{border-color:var(--bad);color:var(--bad)}
.pill{font-size:.72rem;color:var(--ok);border:1px solid var(--ok);border-radius:99px;
 padding:.08rem .5rem}
#fwbtn{margin-top:.9rem}
.bar{height:.45rem;margin:.9rem 0 0;border-radius:99px;background:var(--bg);
 border:1px solid var(--edge);overflow:hidden}
.bar i{display:block;height:100%;width:0;background:var(--link);transition:width .2s}
input[type=file]{padding:.4rem}
input[type=file]::file-selector-button{font:inherit;color:inherit;margin-right:.7rem;
 padding:.3rem .7rem;border:1px solid var(--edge);border-radius:7px;background:var(--bg)}
.btrow{display:flex;flex-wrap:wrap;gap:.5rem;margin-top:.9rem}
.btrow button.arm{border-color:var(--bad);color:var(--bad)}
#btcode{margin:.9rem 0 0;padding:.7rem .9rem;border:1px solid var(--edge);border-radius:12px;
 background:var(--tint)}
#btcode b{display:block;font:600 2rem/1.15 var(--mono);letter-spacing:.14em;
 font-variant-numeric:tabular-nums}
#btmsg{margin:.75rem 0 0;font-size:.85rem}#btmsg:empty{display:none}
#btmsg.err{color:var(--bad)}#btmsg.ok{color:var(--ok)}
#reprows td{vertical-align:top}#reprows .acts{display:flex;flex-wrap:wrap;gap:.35rem;justify-content:flex-end}
@media(min-width:560px){#reprows .acts{flex-wrap:nowrap}}
#reprows a.dl{font-size:.8rem;padding:.28rem .6rem;border:1px solid var(--edge);border-radius:9px;
 text-decoration:none;background:var(--bg);color:inherit;white-space:nowrap}
#repmsg{margin:.75rem 0 0;font-size:.85rem}#repmsg:empty{display:none}
#repmsg.err{color:var(--bad)}#repmsg.ok{color:var(--ok)}
#btcodemsg,#authmsg{margin:.75rem 0 0;font-size:.85rem}#btcodemsg:empty,#authmsg:empty{display:none}
#btcodemsg.err,#authmsg.err{color:var(--bad)}#btcodemsg.ok,#authmsg.ok{color:var(--ok)}
#btcodemsg.warn{color:var(--warn)}
#btown{max-width:10rem;font:600 1.1rem var(--mono);letter-spacing:.14em}
#macin{font-family:var(--mono)}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a><nav><a href="/">Devices</a><a href="/map">Map</a><a href="/nearby">Nearby</a><a href="/events">Events</a><a href="/isp">Internet</a>
<span>Settings</span></nav></header>
<p class=hint id=setupnote hidden></p>
<section>
 <h2>Wi-Fi</h2>
 <button type=button onclick=doscan()>Scan for networks</button>
 <label for=ssidsel id=sellab hidden>Networks in range</label>
 <select id=ssidsel hidden onchange="ssid.value=this.value"></select>
 <label for=ssid>Network name</label>
 <input id=ssid autocomplete=off>
 <label for=pass>Password</label>
 <input id=pass type=password autocomplete=off>
 <p class=hint id=passhint>Leave blank to keep the current password.</p>
</section>
<section>
 <h2>Network history</h2>
 <p class=hint>Networks this board has been set up on, newest first. At start-up
 it tries them in this order, waiting up to 12 seconds on each one that does not
 answer.</p>
 <table class=nets id=nettbl hidden><thead><tr><th>#</th><th>Network</th>
 <th>At start-up</th><th aria-label="Action"></th></tr></thead>
 <tbody id=netrows></tbody></table>
 <p class=hint id=netnote></p>
 <p id=netmsg></p>
</section>
<section>
 <h2>Address</h2>
 <label for=mode>How this board gets its address</label>
 <select id=mode onchange=togglestatic()>
  <option value=dhcp>Automatically, from the router</option>
  <option value=static>Fixed address</option>
 </select>
 <div id=st hidden>
  <label for=ip>Address</label><input id=ip autocomplete=off>
  <label for=mask>Netmask</label><input id=mask value=255.255.255.0>
  <label for=gw>Gateway</label><input id=gw autocomplete=off>
  <label for=dns>DNS</label><input id=dns value=1.1.1.1>
 </div>
 <p class=hint id=acthead>In use right now</p>
 <dl id=act></dl>
 <div id=macbox hidden>
  <label for=macin>MAC address on Wi-Fi</label>
  <input id=macin autocomplete=off spellcheck=false autocapitalize=characters oninput=machint()>
  <div class=btrow><button type=button onclick=macrand()>Random address</button>
  <button type=button onclick=macown()>The chip's own</button></div>
  <p class=hint id=machelp></p>
  <p class=hint>A new address is taken at the restart after saving. The router
  then sees a new device and may give the board a new IP address: if this page
  does not come back, find netmon in the router's list of devices, or try
  http://netmon.local. The Bluetooth address stays the same, so paired phones
  stay paired.</p>
 </div>
</section>
<section>
 <h2>DHCP information</h2>
 <dl id=dhcp><dt>Status</dt><dd>Waiting for DHCP traffic…</dd></dl>
 <p class=hint>Learned from the DHCP requests devices broadcast when they join the network, which is also where hostnames come from. A device that stays connected shows up here only after it reconnects or restarts. Names are remembered across restarts.</p>
</section>
<section>
 <h2>Advanced monitoring</h2>
 <label for=scan>Scan interval (seconds)</label>
 <input id=scan type=number min=10 max=3600>
 <label for=probe>Gateway latency interval (seconds)</label>
 <input id=probe type=number min=10 max=3600>
 <label for=offline>Offline after (seconds)</label>
 <input id=offline type=number min=30 max=86400>
 <label for=learning>Learning window (seconds)</label>
 <input id=learning type=number min=0 max=86400>
 <p class=hint>Changes take effect after restart. Short intervals increase Wi-Fi traffic and power use.</p>
</section>
<section>
 <button class=primary onclick=save()>Save and restart</button>
 <p id=msg></p>
 <p class=hint>Saving reconnects the board. If it cannot rejoin, it opens a
 setup network called netmon-setup at 192.168.4.1, and goes back to trying its
 saved networks whenever nobody has been joined to that for three minutes.</p>
</section>
<section>
 <h2>Bluetooth</h2>
 <p class=hint>A phone paired with this board can use the netmon app over
 Bluetooth, out of reach of this Wi-Fi: while you walk with the Finder, say, or
 when the board is in setup mode. Each phone pairs once, with a code shown here.</p>
 <dl id=btinfo><dt>Link</dt><dd>Reading&hellip;</dd></dl>
 <div id=btcode hidden aria-live=polite><b id=btdigits></b>
 <p class=hint id=btsteps></p></div>
 <div class=btrow><button type=button id=btpair onclick=btpairing()>Pair a phone</button>
 <button type=button id=btonoff onclick=btswitch()>Switch off</button>
 <button type=button id=btforget onclick=btforgetall()>Forget paired phones</button></div>
 <p id=btmsg></p>
 <div id=btcodebox hidden>
  <label for=btmode>Pairing code</label>
  <select id=btmode onchange=btmodeset()>
   <option value=random>A new random code for each pairing</option>
   <option value=own>My own code, the same every time</option>
  </select>
  <div id=btownbox hidden><label for=btown>Your code: six digits</label>
  <input id=btown inputmode=numeric autocomplete=off maxlength=6 placeholder="000000" oninput="btedit=true"></div>
  <p class=hint>Whichever you choose, a phone can pair only while a pairing
  window is open, and three wrong codes close it. Your own code is easier to
  enter on a phone that cannot see this page; keep it to yourself, and avoid
  ones like 000000 or 123456.</p>
  <div class=btrow><button type=button id=btcodebtn onclick=btcodesave()>Save pairing code</button></div>
  <p id=btcodemsg></p>
 </div>
</section>
<section>
 <h2>Saved reports</h2>
 <p class=hint>For each network this board has been on, up to four, the device
 list as it last stood: saved every 15 minutes, before every restart, and when
 you ask. Devices not seen since the board last started are kept from the
 report before. Download one as JSON or as CSV for a spreadsheet.</p>
 <table class=nets id=reptbl hidden><thead><tr><th>Network</th><th>Devices</th>
 <th>Saved</th><th aria-label="Action"></th></tr></thead><tbody id=reprows></tbody></table>
 <p class=hint id=repnote>Reading&hellip;</p>
 <div class=btrow><button type=button id=repsave onclick=repsavenow()>Save this network now</button></div>
 <p id=repmsg></p>
</section>
<section>
 <h2>Signing in</h2>
 <p class=hint>These pages and the netmon app ask for a password: your own
 login password once you set one here, and always the update password from
 secrets.h, as the way back in if you forget yours.</p>
 <dl id=authinfo><dt>This browser</dt><dd>Reading&hellip;</dd></dl>
 <div class=btrow><button type=button onclick=nmsignout()>Sign out</button>
 <button type=button id=soall onclick=signoutall()>Sign out everywhere</button></div>
 <label for=pwcur>Current password</label>
 <input id=pwcur type=password autocomplete=current-password>
 <label for=pwnew>New login password</label>
 <input id=pwnew type=password autocomplete=new-password>
 <label for=pwnew2>New login password again</label>
 <input id=pwnew2 type=password autocomplete=new-password>
 <p class=hint>8 to 64 characters. Every other browser and phone signed in is
 signed out, so they sign in again with the new one.</p>
 <div class=btrow><button type=button id=pwbtn onclick=setpw()>Set login password</button>
 <button type=button id=pwdel onclick=delpw() hidden>Remove it</button></div>
 <p id=authmsg></p>
</section>
<section>
 <h2>Firmware update</h2>
 <p class=hint>Running <span id=fwver>this build</span>. In Arduino IDE use
 Sketch &rarr; Export Compiled Binary, then choose the file ending in
 <b>.ino.bin</b> &mdash; not the bootloader, partitions or merged one.</p>
 <label for=fw>Firmware file</label>
 <input id=fw type=file>
 <p class=hint id=fwinfo></p>
 <label for=fwkey>Update password</label>
 <input id=fwkey type=password autocomplete=off>
 <button type=button id=fwbtn onclick=doupload()>Upload and install</button>
 <div class=bar id=fwbar hidden><i id=fwfill></i></div>
 <p id=fwmsg></p>
</section>
</div>
<script>
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;')}
function togglestatic(){st.hidden=mode.value!='static'}
function show(m,ok){msg.textContent=m;msg.className=ok?'ok':(m?'err':'')}
function row(k,v){return '<dt>'+k+'</dt><dd>'+esc(v||'-')+'</dd>'}
// A field the last DHCP message did not carry comes back as 0.0.0.0.
function ipv(v){return v=='0.0.0.0'?'':v}
function listentext(s){return s=='listening'?'Listening on port 67'
 :s=='paused'?'Paused for a Wi-Fi scan or an update'
 :s=='failed'?'Not listening: port 67 could not be opened'
 :s=='off'?'Not listening in setup mode':'Unknown'}
function loaddhcp(){fetch('/api/dhcp').then(function(r){return r.json()})
 .then(function(d){
  var head=row('Listener',listentext(d.listener))+row('Packets received',String(d.received||0));
  if(!d.valid){dhcp.innerHTML=head+row('Status','No DHCP request heard yet');return}
  dhcp.innerHTML=head+row('Client MAC',d.client_mac)+row('Requested IP',ipv(d.assigned_ip))
   +row('DHCP server',ipv(d.server_ip))+row('Lease',d.lease_s?d.lease_s+' seconds':'-')
   +row('Message',d.message_type)+row('Hostname',d.hostname||'-')
   +row('Last packet',d.last_seen_s+' seconds ago')+row('DHCP messages',String(d.capture_packets||0))})
 .catch(function(){dhcp.innerHTML='<dt>Status</dt><dd>DHCP data unavailable</dd>'})}
function showactive(a){
 if(!a){acthead.hidden=true;act.innerHTML='';return}
 acthead.textContent=a.source=='dhcp'?'Assigned by the router right now'
  :a.source=='static'?'Fixed address, in use right now'
  :'Setup network, in use right now';
 act.innerHTML=row('Address',a.ip)+row('Netmask',a.mask)+row('Gateway',a.gw)
  +row('DNS',a.dns)+row('Connected to',a.ssid)+row('Signal',a.rssi+' dBm')
  +row('This board',a.mac)}
function loadcfg(){fetch('/api/config').then(function(r){return r.json()})
 .then(function(c){
  ver.textContent=c.version||'';fwold=c.version||'';fwmax=c.update_max||0;
  fwver.textContent=c.version||'this build';
  ssid.value=c.ssid||'';
  pass.placeholder=c.has_password?'unchanged':'none, this is an open network';
  passhint.textContent=c.has_password
   ?'Leave blank to keep the current password.'
   :'This network is stored without a password.';
  mode.value=c.use_dhcp?'dhcp':'static';togglestatic();
  if(!c.use_dhcp){ip.value=c.ip;mask.value=c.mask;gw.value=c.gw;dns.value=c.dns}
  scan.value=c.scan_interval_s;probe.value=c.probe_interval_s;offline.value=c.offline_after_s;learning.value=c.learning_window_s;
  if(c.active&&c.active.source=='softap'){
   setupnote.hidden=false;
   setupnote.textContent='This board could not join any network it knows, so '
    +'it opened this one. Pick a network below and save. Keep the address on '
    +'automatic unless you know the new network uses fixed addresses — a '
    +'fixed address from somewhere else will leave the board unreachable. '
    +'Left alone for three minutes, it restarts and tries its saved networks again.'}
  showactive(c.active);macshow(c.mac)})
 .catch(function(){show('Could not read the current settings.')})}
// The board's own Wi-Fi address (0.13): the chip's, or one the owner sets.
// Empty or the chip's own both mean the chip's own.
var macnow=null;
function macnorm(v){var h=v.trim().replace(/[:.\-]/g,'');
 return /^[0-9a-f]{12}$/i.test(h)?h.toUpperCase().match(/../g).join(':'):''}
function macshow(m){macnow=m||null;macbox.hidden=!m;if(!m)return;
 macin.value=m.custom||m.factory;machint()}
function machint(){var m=macnow;if(!m)return;
 var v=macin.value.trim()?macnorm(macin.value):m.factory,saved=m.custom||m.factory;
 machelp.textContent=!v?'Six pairs of hex digits, like 02:1A:2B:3C:4D:5E. Empty means the chip\'s own.'
  :v!=saved?'Not saved yet: the board takes '+v+' when you save and it restarts.'
  :m.custom&&m.active==m.custom?'Your own address, in use now. The chip\'s own is '+m.factory+'.'
  :m.custom?'Saved, and taken at the next restart. In use now: '+m.active+'.'
  :'The chip\'s own address. Enter another, or make a random one, and the router sees this board as a new device.'}
function macrand(){var b=new Uint8Array(6),i;
 try{crypto.getRandomValues(b)}catch(e){for(i=0;i<6;i++)b[i]=Math.random()*256|0}
 // Locally administered (bit 1 set) and for one device (bit 0 clear), so it
 // cannot be any manufacturer's.
 b[0]=b[0]&0xFC|2;
 macin.value=[].map.call(b,function(x){return ('0'+x.toString(16)).slice(-2)}).join(':').toUpperCase();
 machint()}
function macown(){if(macnow){macin.value=macnow.factory;machint()}}
// What save() sends to /api/mac: null for no change, '' for the chip's own,
// or the new address; a string starting with '!' is what is wrong with it.
function macwant(){var m=macnow;if(!m)return null;
 var t=macin.value.trim(),v=t?macnorm(t):m.factory;
 if(!v)return '!Enter a MAC address like 02:1A:2B:3C:4D:5E, or leave it empty for the chip\'s own.';
 if(parseInt(v.slice(0,2),16)&1)return '!That is a group (multicast) address; the first pair of digits must be even.';
 if(v=='00:00:00:00:00:00')return '!00:00:00:00:00:00 is not a usable address.';
 if(v==(m.custom||m.factory))return null;
 return v==m.factory?'':v}
function doscan(){show('Scanning');
 fetch('/api/scan').then(function(r){return r.json()}).then(function(n){
  ssidsel.innerHTML='';
  n.forEach(function(x){var o=document.createElement('option');
   o.value=x.ssid;o.textContent=x.ssid+'  '+x.rssi+' dBm';ssidsel.add(o)});
  sellab.hidden=ssidsel.hidden=!n.length;
  if(n.length){ssid.value=n[0].ssid}
  show(n.length?n.length+' networks in range':'No networks found',n.length>0)})
 .catch(function(){show('Wi-Fi scan failed or is busy. DHCP capture is paused during the scan; try again in a few seconds.')})}
function save(){var mac=macwant();
 if(mac&&mac.charAt(0)=='!'){show(mac.slice(1));macin.focus();return}
 show('Saving');
 fetch('/api/config',{method:'POST',
  headers:{'Content-Type':'application/json'},
  body:JSON.stringify({ssid:ssid.value,pass:pass.value,
   use_dhcp:mode.value=='dhcp',ip:ip.value,mask:mask.value,gw:gw.value,
   dns:dns.value,scan_interval_s:+scan.value,probe_interval_s:+probe.value,
   offline_after_s:+offline.value,learning_window_s:+learning.value})})
 .then(function(r){return r.json().then(function(j){return {ok:r.ok,j:j}})})
 .then(function(res){
  if(!res.ok){show(res.j.error);return}
  if(mac===null)return res;
  // The address goes second: a setting the board refused leaves it as it was.
  return fetch('/api/mac',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({mac:mac})})
  .then(function(r){return r.json().then(function(j){return {ok:r.ok,j:j}})})})
 .then(function(res){
  if(!res)return;
  if(!res.ok){show(res.j.error);return}
  show(mac===null?'Saved. Restarting now.':'Saved. Restarting now with '
   +(mac||'the chip\'s own address')+'. If this page does not come back within a minute, the board has a new IP address.',true);
  fetch('/api/reboot',{method:'POST'})})
 .catch(function(){show('Could not save. The board may have restarted already.')})}
var nets=[],armed=null,armT=0;
// The Wi-Fi stack's own words for why a join failed, and its code, so a
// label can always be checked against what the board was actually told.
var why={2:'authentication timed out',14:'key check failed',15:'handshake timed out',
 23:'802.1X sign-in failed',200:'lost the access point',201:'not found',
 202:'authentication failed',203:'association refused',204:'handshake timed out',
 205:'connection failed',210:'no compatible security',211:'security weaker than WPA2',
 212:'signal too weak'};
function reason(n){return n.reason?(why[n.reason]||'reason')+' (code '+n.reason+')':''}
function jointext(n){var s=(n.boot_ms/1000).toFixed(1)+' s',r=reason(n);
 switch(n.boot){
  case 'joined':return '<span class=good>Joined</span><span class=sub>in '+s+'</span>';
  case 'not_found':return '<span class=bad>Not in range</span><span class=sub>waited '+s+'</span>';
  case 'refused':return '<span class=bad>Refused</span><span class=sub>check the password'
   +(r?'; '+r:'')+'</span>';
  case 'failed':return '<span class=warn>Could not join</span><span class=sub>in range; '
   +(r||'connection failed')+'</span>';
  case 'no_answer':return '<span class=warn>Did not join</span><span class=sub>waited '+s+'</span>';
  default:return '<span class=sub>Not tried</span><span class=sub>an earlier one joined</span>'}}
function loadnets(){fetch('/api/networks').then(function(r){return r.json()})
 .then(function(list){nets=list;clearTimeout(armT);armed=null;nettbl.hidden=!list.length;
  netrows.innerHTML=list.map(function(n,i){
   return '<tr><td class=n>'+n.order+'</td><td><span class=ss>'+esc(n.ssid)+'</span>'
    +'<span class=sub>'+(n.has_password?'password saved':'open network')+'</span></td>'
    +'<td>'+jointext(n)+'</td><td class=a>'
    +(n.active?'<span class=pill>In use</span>'
     :'<button type=button data-i='+i+'>Forget</button>')+'</td></tr>'}).join('');
  // Start-up time spent on networks tried before the one that answered.
  var a=-1,lost=0,slow=[];
  list.forEach(function(n,i){if(n.active)a=i});
  list.forEach(function(n,i){
   if((a<0||i<a)&&n.boot!='joined'&&n.boot!='not_tried'){lost+=n.boot_ms;slow.push(n.ssid)}});
  netnote.textContent=!list.length?'No networks remembered yet.'
   :a<0&&slow.length?'None of these answered at start-up, so the board opened netmon-setup.'
   :lost?slow.join(', ')+(slow.length>1?' were':' was')+' tried first and cost '
    +(lost/1000).toFixed(1)+' s at start-up. Saving settings moves '+list[a].ssid
    +' to the top.':''})
 .catch(function(){netnote.textContent='Network history unavailable.'})}
function disarm(){clearTimeout(armT);
 if(armed){armed.textContent='Forget';armed.className=''}armed=null}
// Forget takes two taps: the first arms the button for four seconds.
netrows.onclick=function(e){
 var b=e.target.closest&&e.target.closest('button[data-i]');if(!b)return;
 var n=nets[+b.getAttribute('data-i')];if(!n)return;
 if(armed!==b){disarm();armed=b;b.textContent='Confirm';b.className='arm';
  armT=setTimeout(disarm,4000);return}
 disarm();b.disabled=true;b.textContent='Forgetting';netmsg.textContent='';
 fetch('/api/networks/forget',{method:'POST',
  headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:n.ssid})})
 .then(function(r){return r.json().then(function(j){return {ok:r.ok,j:j}})})
 .then(function(res){if(!res.ok){netmsg.textContent=res.j.error||'Could not forget that network.';
  netmsg.className='err'}loadnets()})
 .catch(function(){netmsg.textContent='Could not reach the board.';netmsg.className='err';
  loadnets()})};
// The largest file the board can take, from /api/config. It depends on the
// partition scheme the board was flashed with, so the board says.
var fwfile=null,fwbad='',fwold='',busy=false,fwmax=0;
function fsize(n){return n>=1048576?(n/1048576).toFixed(2)+' MB':Math.round(n/1024)+' KB'}
function fwsay(m,c){fwmsg.textContent=m;fwmsg.className=c||''}
// Catch the wrong file here rather than after a megabyte of upload. The
// board checks again: the updater refuses an image without the 0xE9 magic.
fw.onchange=function(){var f=fw.files&&fw.files[0];
 fwfile=null;fwbad='';fwbar.hidden=true;fwsay('');
 if(!f){fwinfo.textContent='';return}
 fwinfo.textContent=f.name+' \u00b7 '+fsize(f.size);
 var nm=f.name.toLowerCase(),part=nm.match(/bootloader|partitions|merged/);
 var bad=!/\.bin$/.test(nm)?'That is not a .bin file.'
  :part?'That is the '+part[0]+' image, not the firmware. Choose the file ending in .ino.bin.'
  :fwmax&&f.size>fwmax?'Too large: this board takes firmware up to '+fwmax.toLocaleString('en')+' bytes.'
  :f.size<262144?'Too small to be netmon firmware.':'';
 if(bad){fwbad=bad;fwsay(bad,'err');return}
 fwbad='Still reading that file. Try again in a moment.';
 f.slice(0,1).arrayBuffer().then(function(buf){
  if(fw.files[0]!==f)return;
  if(new Uint8Array(buf)[0]!==0xE9){fwbad='That file is not an ESP32 firmware image.';fwsay(fwbad,'err');return}
  fwbad='';fwfile=f})
 .catch(function(){if(fw.files[0]===f){fwbad='';fwfile=f}})};
// The button stays usable and says what is missing, rather than sitting greyed
// out with no reason given; it is disabled only while a request is in flight.
function doupload(){if(busy)return;
 if(!fwfile){fwsay(fwbad||'Choose the firmware file first: the one ending in .ino.bin.','err');return}
 var key=fwkey.value;
 if(!key){fwsay('Enter the update password.','err');fwkey.focus();return}
 fwbtn.disabled=true;fwsay('Checking the password\u2026');
 fetch('/api/update/check',{method:'POST',headers:{'X-Netmon-Key':key}})
 .then(function(r){
  if(r.status==401){fwbtn.disabled=false;fwsay('Wrong update password.','err');return}
  if(!r.ok)throw 0;sendfw(key)})
 .catch(function(){fwbtn.disabled=false;fwsay('Could not reach the board.','err')})}
function sendfw(key){var x=new XMLHttpRequest(),fd=new FormData(),t0=Date.now();
 fd.append('firmware',fwfile,fwfile.name);busy=true;
 x.open('POST','/api/update');x.setRequestHeader('X-Netmon-Key',key);
 fwbar.hidden=false;fwfill.style.width='0';fwsay('Uploading 0%');
 x.upload.onprogress=function(e){if(!e.lengthComputable)return;
  var p=Math.round(e.loaded*100/e.total);fwfill.style.width=p+'%';
  fwsay(p<100?'Uploading '+p+'%':'Installing\u2026')};
 x.onload=function(){var j={};try{j=JSON.parse(x.responseText)}catch(e){}
  if(x.status==200){fwfill.style.width='100%';
   fwsay('Installed. The board is restarting\u2026','ok');waitback(t0);return}
  // Signed out meanwhile: XMLHttpRequest goes past the fetch() wrapper.
  if(x.status==401&&x.getResponseHeader('X-Netmon-Login')){nmlogin();return}
  busy=false;fwbtn.disabled=false;fwbar.hidden=true;
  fwsay(x.status==401?'Wrong update password.':(j.error||'The board rejected the file.'),'err')};
 x.onerror=function(){
  fwsay('The connection dropped. Checking whether the board restarted\u2026');waitback(t0)};
 x.send(fd)}
// The board answers, then restarts half a second later, so the first look
// waits four seconds. Its uptime says whether it really restarted since the
// upload began; the version says whether the new image is the one running.
function waitback(t0){setTimeout(function poll(){
  var c=window.AbortController?new AbortController():null,
   tm=setTimeout(function(){if(c)c.abort()},3000);
  fetch('/api/health',{cache:'no-store',signal:c?c.signal:undefined})
  .then(function(r){return r.json()}).then(function(h){clearTimeout(tm);
   busy=false;fwbtn.disabled=false;
   var restarted=(h.uptime_s||0)*1000<Date.now()-t0;
   ver.textContent=fwver.textContent=h.version;
   if(!restarted)fwsay('The board did not restart and is still running '+h.version
    +', so nothing was installed.','err');
   else if(h.version!=fwold)fwsay('Updated. Now running '+h.version+'.','ok');
   else fwsay('Restarted, still running '+h.version
    +' \u2014 the file carries the same version.','ok');
   fwold=h.version})
  .catch(function(){clearTimeout(tm);
   if(Date.now()-t0>150000){busy=false;fwbtn.disabled=false;
    fwsay('No answer from the board after two and a half minutes. If it does not '
     +'come back, reflash it over USB.','err');return}
   setTimeout(poll,2000)})},4000)}
// Bluetooth. Polled every two seconds while a pairing window is open, so the
// countdown runs and "paired" shows as soon as the phone is done.
var bt=null,btT=0,btarm=null,btarmT=0,btauto=false,btedit=false;
function btsay(m,c){btmsg.textContent=m;btmsg.className=c||'';btauto=false}
function mmss(s){return Math.floor(s/60)+':'+('0'+s%60).slice(-2)}
// How the last pairing attempt went. From 0.13 the board says why one failed:
// a wrong code, the phone giving up, the connection dropping.
function btlast(l){return l.text.replace(/\.$/,'')+' \u00b7 '+agetext(l.age_s)
 +(l.status?' \u00b7 code 0x'+(l.status>>>0).toString(16):'')}
function btshow(b){bt=b;
 var state=!b.available?'Not running: Bluetooth could not start on this board'
  :!b.enabled?'Off':'On, advertising as '+b.name;
 btinfo.innerHTML=row('Link',state)+row('Address',b.addr||'-')
  +row('Paired phones',b.bonds+' of '+b.max_bonds)
  +row('Connected now',b.connected?b.connected+(b.secure<b.connected?' ('+b.secure+' paired)':''):'none')
  +(b.own_code==null?'':row('Pairing code',b.own_code?'Your own':'New random code each time'))
  +(b.last?row('Last attempt',btlast(b.last)):'');
 btonoff.textContent=b.enabled?'Switch off':'Switch on';
 btpair.disabled=!b.available||!b.enabled;
 btpair.textContent=b.pairing?'Cancel pairing':'Pair a phone';
 btcode.hidden=!b.pairing;
 var m='',c='';
 if(b.pairing){btdigits.textContent=b.code.slice(0,3)+' '+b.code.slice(3);
  btsteps.textContent='In the netmon app, tap this board under Bluetooth when finding a monitor, '
   +'or Pair this phone in Settings, Bluetooth, and enter this code when the app asks for it. '
   +'It works for one phone, for the next '+mmss(b.left_s)+'.'
   +(b.tries_left!=null&&b.tries_left<3?' '+b.tries_left+(b.tries_left==1?' try':' tries')+' left.':'');
  if(b.result=='failed'){m='That try did not pair: '+(b.why_text||'pairing failed.')
   +' The window is still open with the same code.';c='err'}}
 else if(b.result=='paired'&&b.result_age_s<120){m='Paired. That phone can now use the app over Bluetooth.';c='ok'}
 else if(b.result=='failed'&&b.result_age_s<120){m='Pairing failed: '
  +(b.why_text||'a wrong code, or the phone gave up.')
  +(b.tries_left===0?' Three tries failed, so the window closed.':'')+' Pair again to try again.';c='err'}
 // Messages of its own replace one the page put up after a tap; only the
 // ones from here go away again by themselves.
 if(m){btsay(m,c);btauto=true}else if(btauto)btsay('');
 btcodebox.hidden=b.own_code==null;
 if(!btedit){btmode.value=b.own_code?'own':'random';btown.value=b.own||'';btownbox.hidden=!b.own_code}
 clearTimeout(btT);btT=setTimeout(loadble,b.pairing?2000:10000)}
// The owner's own pairing code (0.13), or a new random one each window.
function btmodeset(){btedit=true;btownbox.hidden=btmode.value!='own';
 if(btmode.value=='own'){btown.focus()}}
function btweak(c){return /^(\d)\1{5}$/.test(c)||'0123456789'.indexOf(c)>=0||'9876543210'.indexOf(c)>=0}
function btcodesay(m,c){btcodemsg.textContent=m;btcodemsg.className=c||''}
function btcodesave(){var own=btmode.value=='own'?btown.value.replace(/\s/g,''):'';
 if(btmode.value=='own'&&!/^\d{6}$/.test(own)){btcodesay('The code needs exactly six digits.','err');btown.focus();return}
 btcodebtn.disabled=true;btcodesay('');
 btpost('/api/ble',{own:own}).then(function(b){btcodebtn.disabled=false;btedit=false;btshow(b);
  if(!own)btcodesay('Saved. Each pairing window gets a new random code.','ok');
  else if(btweak(own))btcodesay('Saved, though '+own+' is easy to guess. A code nobody would try first is safer.','warn');
  else btcodesay('Saved. Pairing windows use your code from now on.','ok')})
 .catch(function(e){btcodebtn.disabled=false;btcodesay(typeof e=='string'?e:'Could not reach the board.','err')})}
function loadble(){fetch('/api/ble').then(function(r){if(!r.ok)throw r.status;return r.json()})
 .then(btshow)
 .catch(function(e){btinfo.innerHTML=row('Link',e==404?'Needs firmware 0.12 or later':'Unavailable');
  btpair.disabled=true})}
function btpost(path,body){return fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(body||{})})
 .then(function(r){return r.json().then(function(j){if(!r.ok)throw j.error||'The board refused.';return j})})}
function btpairing(){btsay('');
 btpost('/api/ble/pair',bt&&bt.pairing?{stop:true}:{}).then(btshow)
 .catch(function(e){btsay(typeof e=='string'?e:'Could not reach the board.','err')})}
function btswitch(){btsay('');if(!bt)return;
 btpost('/api/ble',{enabled:!bt.enabled}).then(btshow)
 .catch(function(e){btsay(typeof e=='string'?e:'Could not reach the board.','err')})}
// Two taps, like forgetting a network: the first arms the button for four seconds.
function btforgetall(){
 if(btarm!==btforget){btarm=btforget;btforget.textContent='Confirm: every phone pairs again';
  btforget.className='arm';btarmT=setTimeout(btdisarm,4000);return}
 btdisarm();btsay('');
 btpost('/api/ble/forget').then(function(b){btshow(b);btsay('Forgot every paired phone.','ok')})
 .catch(function(e){btsay(typeof e=='string'?e:'Could not reach the board.','err')})}
function btdisarm(){clearTimeout(btarmT);btarm=null;btforget.textContent='Forget paired phones';
 btforget.className=''}
// Saved reports. CSV is made here from the board's JSON, the same columns as
// the app's export.
var reps=[],reparm=null,reparmT=0,repclock=false;
function repsay(m,c){repmsg.textContent=m;repmsg.className=c||''}
function agetext(s){return s<0?'before the last restart':s<90?'just now'
 :s<5400?Math.round(s/60)+' min ago':s<172800?Math.round(s/3600)+' h ago':Math.round(s/86400)+' days ago'}
function whentext(r){var d=r.saved_unix?new Date(r.saved_unix*1000):null;
 return (d?d.toLocaleString([], {dateStyle:'medium',timeStyle:'short'})+' \u00b7 ':'')+agetext(r.age_s)}
function fname(r,ext){return 'netmon-'+(r.ssid||'network').replace(/[^A-Za-z0-9._-]+/g,'_')+'-'
 +(r.saved_unix?new Date(r.saved_unix*1000).toISOString().slice(0,10):'report')+'.'+ext}
function showreps(l){reps=l.reports||[];repclock=!!l.clock;
 if(l.clock===false)sendclock();
 reptbl.hidden=!reps.length;
 reps.sort(function(a,b){return (b.current-a.current)||((b.saved_unix||0)-(a.saved_unix||0))});
 reprows.innerHTML=reps.map(function(r,i){
  return '<tr><td><span class=ss>'+esc(r.ssid)+'</span>'+(r.current?' <span class=pill>On it now</span>':'')
   +'<span class=sub>'+esc(r.subnet)+(r.gateway?' \u00b7 router '+esc(r.gateway):'')+'</span></td>'
   +'<td>'+r.count+'<span class=sub>'+r.online+' online</span></td>'
   +'<td>'+esc(whentext(r))+'</td>'
   +'<td class=a><div class=acts><a class=dl href="/api/reports/get?slot='+r.slot+'" download="'+esc(fname(r,'json'))+'">JSON</a>'
   +'<button type=button data-csv='+i+'>CSV</button><button type=button data-del='+i+'>Delete</button></div></td></tr>'}).join('');
 repnote.textContent=(reps.length?'':'No report saved yet. ')
  +(l.network?'This board is on '+l.network.ssid+'.':'This board is not on a network now.')
  +' Room for '+l.max+' networks; a fifth replaces the one saved longest ago.'
  +(l.clock?'':' The board does not know the time yet, so reports say how long ago devices were seen rather than when.');
 repsave.disabled=!l.network}
function loadreps(){fetch('/api/reports').then(function(r){if(!r.ok)throw r.status;return r.json()})
 .then(showreps)
 .catch(function(e){repnote.textContent=e==404?'Saved reports need firmware 0.12 or later.':'Saved reports unavailable.';
  repsave.disabled=true})}
var clockSent=false;
function sendclock(){if(clockSent)return;clockSent=true;
 fetch('/api/clock',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({unix:Math.floor(Date.now()/1000)})}).catch(function(){})}
function repsavenow(){repsay('Saving\u2026');
 btpost('/api/reports/save').then(function(l){showreps(l);repsay('Saved.','ok')})
 .catch(function(e){repsay(typeof e=='string'?e:'Could not reach the board.','err')})}
// One value for a CSV cell. A cell a spreadsheet would run as a formula is
// written as text: names come from what devices broadcast about themselves.
function csvcell(v){v=v==null?'':String(v);
 if(/^[=+\-@\t\r]/.test(v))v="'"+v;
 return /[",\r\n]/.test(v)?'"'+v.replace(/"/g,'""')+'"':v}
function iso(t){if(!t)return '';var d=new Date(t*1000),p=function(n){return ('0'+n).slice(-2)};
 return d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+' '+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds())}
function tocsv(j){
 var rows=[['network','subnet','saved','mac','ip','hostname','vendor','status','private_mac','online',
  'last_seen','last_seen_s_before_save','first_seen','uptime_s','carried_over']];
 (j.devices||[]).forEach(function(d){
  var seen=d.seen_unix||(j.saved_unix?j.saved_unix-d.last_seen_s:0);
  rows.push([j.ssid,j.subnet,iso(j.saved_unix),d.mac,d.ip,d.hostname,d.vendor,d.status,
   d.randomised?'yes':'no',d.online?'yes':'no',iso(seen),d.last_seen_s,iso(d.first_unix),d.up_s,d.carried?'yes':'no'])});
 return rows.map(function(r){return r.map(csvcell).join(',')}).join('\r\n')+'\r\n'}
function repcsv(r,b){b.disabled=true;
 fetch('/api/reports/get?slot='+r.slot).then(function(x){if(!x.ok)throw 0;return x.json()})
 .then(function(j){var a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob(['\ufeff'+tocsv(j)],{type:'text/csv'}));a.download=fname(r,'csv');
  document.body.appendChild(a);a.click();setTimeout(function(){URL.revokeObjectURL(a.href);a.remove()},1000);
  b.disabled=false})
 .catch(function(){b.disabled=false;repsay('Could not read that report.','err')})}
function repdisarm(){clearTimeout(reparmT);if(reparm){reparm.textContent='Delete';reparm.className=''}reparm=null}
reprows.onclick=function(e){var t=e.target.closest&&e.target.closest('button');if(!t)return;
 if(t.hasAttribute('data-csv')){repcsv(reps[+t.getAttribute('data-csv')],t);return}
 var r=reps[+t.getAttribute('data-del')];if(!r)return;
 if(reparm!==t){repdisarm();reparm=t;t.textContent='Confirm';t.className='arm';reparmT=setTimeout(repdisarm,4000);return}
 repdisarm();repsay('');
 btpost('/api/reports/delete',{slot:r.slot}).then(function(l){showreps(l);repsay('Deleted.','ok')})
 .catch(function(x){repsay(typeof x=='string'?x:'Could not reach the board.','err')})};
// Signing in (0.13).
var soarm=false,soarmT=0,pwarm=false,pwarmT=0;
function authsay(m,c){authmsg.textContent=m;authmsg.className=c||''}
function loadauth(){fetch('/api/auth',{cache:'no-store'}).then(function(r){return r.json()})
 .then(function(a){
  authinfo.innerHTML=row('This browser',a.remembered?'Signed in, remembered for '+a.remember_days+' days'
    :'Signed in until the browser closes, or 12 hours unused')
   +row('Password',a.own_password?'Your login password, or the update password'
    :'The update password from secrets.h')
   +row('Signed in now',a.sessions+(a.sessions==1?' browser or phone':' browsers and phones'));
  pwbtn.textContent=a.own_password?'Change login password':'Set login password';
  pwdel.hidden=!a.own_password})
 .catch(function(){authinfo.innerHTML=row('Status','Unavailable')})}
function signoutall(){
 if(!soarm){soarm=true;soall.textContent='Confirm: this browser too';soall.className='arm';
  soarmT=setTimeout(function(){soarm=false;soall.textContent='Sign out everywhere';soall.className=''},4000);return}
 clearTimeout(soarmT);
 var go=function(){location.href='/login'};
 fetch('/api/auth/signout',{method:'POST'}).then(go,go)}
function pwsend(cur,nw){pwbtn.disabled=pwdel.disabled=true;authsay('');
 fetch('/api/auth/password',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({current:cur,new:nw})})
 .then(function(r){return r.json().catch(function(){return {}}).then(function(j){return {s:r.status,j:j}})})
 .then(function(x){pwbtn.disabled=pwdel.disabled=false;
  if(x.s!=200){authsay(x.j.error||'The board refused ('+x.s+').','err');return}
  pwcur.value=pwnew.value=pwnew2.value='';
  authsay((nw?'Login password set.':'Login password removed: the update password signs in.')
   +(x.j.ended?' '+x.j.ended+(x.j.ended==1?' other browser or phone was':' other browsers and phones were')
    +' signed out.':''),'ok');
  loadauth()})
 .catch(function(){pwbtn.disabled=pwdel.disabled=false;authsay('Could not reach the board.','err')})}
function setpw(){
 if(!pwcur.value){authsay('Enter the current password first: your login password or the update password.','err');pwcur.focus();return}
 if(!pwnew.value){authsay('Enter the new password.','err');pwnew.focus();return}
 if(pwnew.value!==pwnew2.value){authsay('The two new passwords are not the same.','err');pwnew2.focus();return}
 pwsend(pwcur.value,pwnew.value)}
// Two taps, like the other buttons that take something away.
function delpw(){
 if(!pwcur.value){authsay('Enter the current password first.','err');pwcur.focus();return}
 if(!pwarm){pwarm=true;pwdel.textContent='Confirm';pwdel.className='arm';
  pwarmT=setTimeout(function(){pwarm=false;pwdel.textContent='Remove it';pwdel.className=''},4000);return}
 clearTimeout(pwarmT);pwarm=false;pwdel.textContent='Remove it';pwdel.className='';
 pwsend(pwcur.value,'')}
loadcfg();loadnets();loaddhcp();loadble();loadreps();loadauth();
setInterval(function(){if(!busy)loaddhcp()},10000);
setInterval(function(){if(!busy)loadreps()},60000);
</script>
)HTML";

static const char ISP_HTML[] PROGMEM =
"<!doctype html><title>Internet - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
.hero{padding:1.1rem 1rem}
.big{margin:0;font-family:var(--mono);font-size:1.45rem;font-weight:600;
 letter-spacing:-.02em;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.big.pending{font-family:inherit;font-weight:500;color:var(--mut)}
.who{margin:.3rem 0 0;font-size:.98rem}
.who.bad{color:var(--bad)}
#again{margin-top:1rem}
/* Monospace earns its place on addresses, where digit alignment is the point.
   Company names in it just look like a mistake. */
dd.t{font-family:inherit}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a><nav><a href="/">Devices</a><a href="/map">Map</a><a href="/nearby">Nearby</a><a href="/events">Events</a><span>Internet</span>
<a href="/settings">Settings</a></nav></header>
<div class=hero>
 <p class="big pending" id=pubip>Checking</p>
 <p class=who id=who></p>
 <p class=hint id=meta></p>
</div>
<section>
 <h2>Connection</h2>
 <dl id=det></dl>
 <button type=button id=again onclick=check(1)>Check again</button>
</section>
<section>
 <h2>Router</h2>
 <dl id=rt></dl>
</section>
<p class=foot>The public address and provider come from a lookup service
reached over plain HTTP, so treat them as reported rather than proven. Asking
the question means telling that service this connection's public address,
which is the one thing it needs in order to answer. The board checks every six
hours and caches the result.</p>
</div>
<script>
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;')}
function age(s){if(s<60)return s+' seconds ago';
 if(s<3600)return((s/60)|0)+' minutes ago';
 if(s<86400)return((s/3600)|0)+' hours ago';return((s/86400)|0)+' days ago'}
function row(k,v,mono){
 return v?'<dt>'+k+'</dt><dd'+(mono?'':' class=t')+'>'+esc(v)+'</dd>':''}
// City and region are often the same word — "Lisbon, Lisbon, Portugal" reads as
// a bug rather than a location.
function place(c){var a=[c.city,c.region,c.country].filter(Boolean);
 return a.filter(function(v,i){return a.indexOf(v)==i}).join(', ')}
function check(force){
 again.disabled=true;again.textContent='Checking';
 fetch('/api/isp'+(force?'?force=1':'')).then(function(r){return r.json()})
 .then(function(c){
  again.disabled=false;again.textContent='Check again';
  ver.textContent=c.version||'';
  rt.innerHTML=row('Address',c.gateway,1)+row('Hardware',c.gateway_vendor)
   +row('MAC',c.gateway_mac,1);
  if(!c.valid){
   pubip.className='big pending';pubip.textContent='Not known';
   who.className='who bad';
   who.textContent=c.error||'The lookup did not return an answer.';
   meta.textContent='';det.innerHTML='';return}
  pubip.className='big';pubip.textContent=c.ip;
  who.className='who';who.textContent=c.isp||'Provider not reported';
  meta.textContent=(place(c)?'Seen from '+place(c)+'. ':'')
   +(c.ever_checked?'Checked '+age(c.checked_age_s)+'.':'');
  det.innerHTML=row('Organisation',c.org)+row('Network',c.asn)
   +row('Location',place(c))+row('Time zone',c.timezone,1)
   +row('Lookup round trip',c.rtt_ms+' ms',1)})
 .catch(function(){again.disabled=false;again.textContent='Check again';
  who.className='who bad';who.textContent='Cannot reach the monitor.'})}
check(0);
</script>
)HTML";


static const char EVENTS_HTML[] PROGMEM =
"<!doctype html><title>Events - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
.event{display:grid;grid-template-columns:5.5rem 5.8rem 1fr;gap:.55rem;
 padding:.65rem 1rem;border-bottom:1px solid var(--edge);font-size:.82rem}
.event .time,.event .mac{font-family:var(--mono);font-variant-numeric:tabular-nums}
.event .type{font-weight:600}
.event.offline .type{color:var(--warn)}
.event.hostname .type{color:var(--link)}
.empty{padding:1.5rem 1rem;color:var(--mut)}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a><nav><a href="/">Devices</a><a href="/map">Map</a><a href="/nearby">Nearby</a><span>Events</span><a href="/isp">Internet</a><a href="/settings">Settings</a></nav></header>
<section><h2>Recent events</h2><p class=hint>Stored in RAM; history is cleared when the monitor restarts.</p></section>
<div id=list></div><p class=empty id=empty hidden>No events yet.</p>
<p class=foot><a href="/api/events">Event JSON</a> · <a href="/api/latency">Latency JSON</a></p>
</div>
<script>
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;')}
function ago(s){if(!s)return 'now';if(s<60)return s+'s ago';if(s<3600)return((s/60)|0)+'m ago';
 if(s<86400)return((s/3600)|0)+'h ago';return((s/86400)|0)+'d ago'}
function load(){fetch('/api/health').then(function(r){return r.json()}).then(function(h){
 ver.textContent=h.version||'';return fetch('/api/events').then(function(r){return r.json()})
 .then(function(es){var now=h.uptime_s||0;empty.hidden=es.length>0;
 list.innerHTML=es.map(function(e){var age=now>=e.at_s?now-e.at_s:0;
 return '<div class="event '+esc(e.type)+'"><div class=time>'+age+'s</div>'
  +'<div class=type>'+esc(e.type)+'</div><div><span class=mac>'+esc(e.mac)
  +'</span> · '+esc(e.ip)+'<br>'+esc(e.text)+'</div></div>'}).join('')})})
 .catch(function(){list.innerHTML='<p class=empty>Cannot reach the monitor.</p>'})}
load();setInterval(load,10000);
</script>
)HTML";

// The Wi-Fi + BLE scanner's page, rebuilt in this firmware's own style: a
// radar for Wi-Fi networks and one for Bluetooth devices, a live log of what
// comes and goes, then the lists the radars are drawn from.
//
// Several ideas come from BlueWatch (github.com/PolarPatch/BlueWatch, MIT): a
// radar scale of plain signal strength beside the metre estimate, dots that
// glow as the beam passes and pulse when they arrive, a fading ring where one
// left, trend arrows, device kinds grouped by colour with a legend that hides
// a group, the hide-private-addresses switch and the arrivals log.
//
// Distance comes from signal strength alone and the page says so; the bearing
// is made up, fixed per device so a dot can be followed, and it says that too.
// Calibration, ranges and switches live in this browser, like the dashboard's
// column widths.
static const char NEARBY_HTML[] PROGMEM =
"<!doctype html><title>Nearby - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
:root{--wifi:#0e7c93;--ble:#8a3fbf;--scope:#f6f9fb;--ring:#d3dde6;
 --gp:#2a78d6;--gt:#eb6834;--gh:#1baf7a;
 --h0:#2f6fbf;--h1:#1693a3;--h2:#279a52;--h3:#c49a0c;--h4:#e0682a;--h5:#cf3337}
@media(prefers-color-scheme:dark){:root{--wifi:#5bc8d6;--ble:#c99af2;
 --scope:#111920;--ring:#2b3947;--gp:#3987e5;--gt:#d95926;--gh:#199e70;
 --h0:#5b9be6;--h1:#2bb5c4;--h2:#43c27a;--h3:#e0bb38;--h4:#f08447;--h5:#f2585c}}
.top{padding:1.05rem 1rem .9rem}
.count{margin:0;font-size:1.35rem;font-weight:600;letter-spacing:-.02em;
 font-variant-numeric:tabular-nums}
.state{margin:.2rem 0 0;font-size:.92rem;color:var(--mut)}
.state.bad{color:var(--bad)}
.ctl{display:flex;flex-wrap:wrap;align-items:center;gap:.5rem 1rem;margin-top:.8rem}
.ctl label,.opt{display:flex;align-items:center;gap:.35rem;margin:0;color:var(--ink);
 font-size:.86rem}
input[type=checkbox]{width:auto;margin:0;accent-color:var(--link)}
.ctl select{width:auto;padding:.3rem .45rem}
#scan{font-weight:600}
#ctlmsg{flex-basis:100%;margin:0;font-size:.8rem;color:var(--bad)}
#ctlmsg:empty{display:none}
.tabs{display:flex;gap:.2rem;padding:0 .6rem;border-top:1px solid var(--edge);
 border-bottom:1px solid var(--edge);overflow-x:auto}
.tabs button{display:flex;align-items:center;gap:.4rem;padding:.72rem .65rem .6rem;border:0;
 border-bottom:2.5px solid transparent;border-radius:0;background:none;color:var(--mut);
 font-size:.93rem;white-space:nowrap}
.tabs button:hover{color:var(--ink)}
.tabs button[aria-selected=true]{color:var(--ink);font-weight:600;border-bottom-color:var(--link)}
.tabs .n{font-weight:400;color:var(--mut);font-variant-numeric:tabular-nums}
.tabs .on{width:.5rem;height:.5rem;border-radius:50%;background:var(--h5)}
.scope{padding:1rem;border-bottom:1px solid var(--edge)}
figure{margin:0;min-width:0}
figcaption{display:flex;align-items:center;gap:.5rem;max-width:420px;margin:0 auto .5rem;
 font-size:.9rem}
figcaption b{font-weight:600}
figcaption .n{color:var(--mut);font-variant-numeric:tabular-nums}
figcaption select{margin-left:auto;width:auto;padding:.22rem .4rem;font-size:.8rem}
.sw{display:inline-block;width:.62rem;height:.62rem;border-radius:50%;flex:none}
.sw.w{background:var(--wifi)}.sw.p{background:var(--gp)}.sw.t{background:var(--gt)}
.sw.h{background:var(--gh)}.sw.u{background:var(--mut)}
.sw.o{background:none;box-shadow:inset 0 0 0 1.5px var(--wifi)}
.sw.j{background:var(--wifi);box-shadow:0 0 0 2px var(--panel),0 0 0 3.5px var(--wifi)}
canvas{display:block;width:100%;max-width:420px;margin:0 auto;aspect-ratio:1;
 touch-action:manipulation;cursor:crosshair}
.legend{display:flex;flex-wrap:wrap;justify-content:center;gap:.25rem .9rem;margin:.55rem 0 0;
 font-size:.76rem;color:var(--mut)}
.legend span{display:flex;align-items:center;gap:.3rem}
.legend button{display:flex;align-items:center;gap:.3rem;padding:.1rem .2rem;border:0;
 background:none;font-size:.76rem;color:var(--mut)}
.legend button[aria-pressed=false]{opacity:.4;text-decoration:line-through}
.legend .opt{font-size:.76rem;color:var(--mut)}
.pick{margin:.45rem auto 0;max-width:420px;min-height:2.5em;font-size:.8rem;color:var(--mut);
 text-align:center;overflow-wrap:anywhere}
.pick b{color:var(--ink);font-weight:600}
button.fb{padding:.12rem .55rem;font-size:.78rem;border-radius:7px;background:var(--panel);
 color:var(--link);border-color:var(--edge);font-weight:600}
.pick button.fb{margin-left:.35rem}
.feed h2{display:flex;align-items:baseline;gap:.5rem}
.feed h2 span{font-weight:400;font-size:.78rem;color:var(--mut)}
#feed{list-style:none;margin:.6rem 0 0;padding:0;max-height:13.5rem;overflow-y:auto;
 font-size:.82rem}
#feed li{display:grid;grid-template-columns:4.6rem 1rem 1fr auto;gap:.5rem;align-items:center;
 padding:.32rem 0;border-bottom:1px solid var(--edge)}
#feed li:last-child{border-bottom:none}
#feed .t,#feed .s{font-family:var(--mono);font-variant-numeric:tabular-nums;color:var(--mut);
 font-size:.76rem}
#feed .gone{color:var(--mut)}
#feed .none{display:block;color:var(--mut);padding:.3rem 0}
.note{margin:0;padding:.8rem 1rem;font-size:.78rem;color:var(--mut);
 border-bottom:1px solid var(--edge)}
.find{padding:.85rem 1rem}
.find input{padding:.55rem .7rem}
.list{padding:0;border-bottom:none}
.list h2{padding:.2rem 1rem .5rem}
.list h2 .n{color:var(--mut);font-weight:400;font-variant-numeric:tabular-nums}
.list .hint{padding:0 1rem .5rem;margin:0}
.list+.list h2{padding-top:1.2rem}
.wrap{overflow-x:auto;border-top:1px solid var(--edge);border-bottom:1px solid var(--edge)}
table{width:100%;border-collapse:collapse;font-size:.84rem}
@media(max-width:640px){table{font-size:.78rem}}
th,td{text-align:left;padding:.42rem .5rem;border-bottom:1px solid var(--edge);white-space:nowrap}
tr:last-child td{border-bottom:none}
th{font-weight:500;color:var(--mut);cursor:pointer;user-select:none}
th.ns{cursor:default}
th:hover{color:var(--ink)}
.ar{font-size:.68rem;margin-left:.2rem}
td.m{font-family:var(--mono);font-variant-numeric:tabular-nums}
td.k{box-shadow:inset 3px 0 var(--edge-c,var(--wifi))}
tbody.h td.k{box-shadow:inset 3px 0 var(--edge)}
tr.old td:not(.fc){opacity:.55}
tr.hl,tr.hl td.fc{background:var(--tint)}
td.fc,th.fc{position:sticky;right:0;padding:.3rem .5rem;background:var(--panel);
 box-shadow:inset 1px 0 var(--edge)}
.tag{font-size:.72rem;color:var(--mut);margin-left:.35rem}
.hid{color:var(--mut);font-style:italic}
.open{color:var(--warn)}
.tr{color:var(--mut);font-size:.75em}
.none{padding:1rem;color:var(--mut);font-size:.85rem;margin:0}
details{padding:1rem;border-top:1px solid var(--edge)}
summary{cursor:pointer;font-size:.9rem;font-weight:600}
.cal{display:grid;grid-template-columns:1fr 1fr;gap:0 1rem}
.cal label{margin-top:.6rem}
#calreset{margin-top:.9rem}
details .opt{margin-top:1rem}
.fpick{padding:1rem 1rem 0}
.fpick .hint{font-size:.82rem;margin:.4rem 0 0}
.chips{display:flex;flex-wrap:wrap;gap:.4rem;margin:.85rem 0 0}
.chips button{padding:.3rem .75rem;border-radius:999px;font-size:.84rem;background:var(--panel)}
.chips button[aria-pressed=true]{background:var(--ink);color:var(--panel);border-color:var(--ink)}
.chips .n{opacity:.75;font-variant-numeric:tabular-nums}
.flist{list-style:none;margin:0;padding:0;border-top:1px solid var(--edge)}
.flist button{display:grid;grid-template-columns:.7rem minmax(0,1fr) auto auto;gap:.75rem;
 align-items:center;width:100%;padding:.62rem 1rem;border:0;border-bottom:1px solid var(--edge);
 border-radius:0;background:none;text-align:left}
.flist button:hover{background:var(--tint)}
.flist b{display:block;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.flist small{display:block;color:var(--mut);font-size:.76rem;overflow:hidden;
 text-overflow:ellipsis;white-space:nowrap}
.flist .s{text-align:right;font-family:var(--mono);font-variant-numeric:tabular-nums;
 font-size:.78rem;color:var(--mut)}
.flist .go{color:var(--link);font-weight:600;font-size:.84rem}
.ftop{display:flex;align-items:flex-start;gap:.8rem;padding:1rem 1rem .3rem}
.ftop .who{flex:1;min-width:0}
.ftop b{display:block;font-size:1.15rem;font-weight:600;letter-spacing:-.01em;
 overflow-wrap:anywhere}
.ftop .what{font-size:.82rem;color:var(--mut);overflow-wrap:anywhere}
.fstate{margin:0;padding:0 1rem;font-size:.85rem;color:var(--mut)}
.fstate.bad{color:var(--bad)}
.fmain{display:grid;grid-template-columns:minmax(0,1fr) 12.5rem;gap:1rem 1.2rem;
 align-items:center;padding:.9rem 1rem}
.fmain #cf{grid-column:1;grid-row:1;max-width:380px;cursor:default}
.fmain .fread{grid-column:2;grid-row:1}
.frow{display:flex;flex-wrap:wrap;align-items:baseline;justify-content:space-between;gap:.2rem 1rem}
.fmeta{margin:.7rem 0 0;font-size:.76rem;color:var(--mut);font-family:var(--mono);
 font-variant-numeric:tabular-nums}
@media(max-width:620px){.fmain{grid-template-columns:1fr;gap:.8rem}
 .fmain #cf,.fmain .fread{grid-column:1;grid-row:auto}.fmain #cf{max-width:300px}
 .frow .trend{margin:0}}
.big{margin:0;font-size:2.3rem;font-weight:650;letter-spacing:-.03em;line-height:1.05;
 font-variant-numeric:tabular-nums}
.fsub{margin:.15rem 0 0;font-size:.78rem;color:var(--mut)}
.prox{margin:.55rem 0 .45rem;font-size:1.02rem;font-weight:600}
.meter{position:relative;height:.55rem;border-radius:999px;
 background:linear-gradient(90deg,var(--h0),var(--h1),var(--h2),var(--h3),var(--h4),var(--h5));
 background:linear-gradient(90deg in hsl,var(--h0),var(--h1),var(--h2),var(--h3),var(--h4),var(--h5))}
.meter i{position:absolute;top:-.3rem;left:0;width:.24rem;height:1.15rem;margin-left:-.12rem;
 border-radius:2px;background:var(--ink);box-shadow:0 0 0 2px var(--panel);transition:left .6s}
.meter i[hidden]{display:none}
.ends{display:flex;justify-content:space-between;margin-top:.3rem;font-size:.7rem;color:var(--mut)}
.trend{margin:.8rem 0 0;font-size:.95rem;font-weight:600}
.found .big{color:var(--h5)}
.trace{display:block;width:100%;max-width:none;height:84px;aspect-ratio:auto;margin:0;cursor:default}
.tracebox{padding:0 1rem .3rem}
.tracebox p{margin:.25rem 0 0;font-size:.72rem;color:var(--mut);display:flex;
 justify-content:space-between}
.fbtn{display:flex;flex-wrap:wrap;gap:.5rem;padding:.6rem 1rem 1rem}
.fbtn .primary{width:auto;flex:1 1 15rem}
.turnmsg{margin:0;padding:0 1rem 1rem;font-size:.88rem}
.turnmsg:empty{display:none}
.fmsg{margin:.8rem 0 0;font-size:.85rem;color:var(--bad)}
.fmsg:empty{display:none}
.how ol{margin:.6rem 0 0;padding-left:1.2rem;font-size:.84rem}
.how li{margin:.35rem 0}
.how p{font-size:.84rem;color:var(--mut)}
.vh{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a><nav><a href="/">Devices</a><a href="/map">Map</a><span>Nearby</span><a href="/events">Events</a><a href="/isp">Internet</a><a href="/settings">Settings</a></nav></header>
<div class=top id=topbox>
 <p class=count id=count>Listening</p>
 <p class=state id=state></p>
 <div class=ctl>
  <button type=button id=scan>Scan now</button>
  <label><input type=checkbox id=onw> Wi-Fi</label>
  <label><input type=checkbox id=onb> Bluetooth</label>
  <label>With this page closed
   <select id=bg><option value=0>off</option><option value=60>every minute</option>
   <option value=120>every 2 minutes</option><option value=300>every 5 minutes</option>
   <option value=900>every 15 minutes</option><option value=3600>every hour</option>
   </select></label>
  <p id=ctlmsg></p>
 </div>
</div>
<div class=tabs role=tablist aria-label="What to show" id=tabs>
 <button type=button role=tab id=t-wifi aria-controls=p-wifi aria-selected=true data-t=wifi><span class="sw w"></span>Wi-Fi <span class=n id=nw></span></button>
 <button type=button role=tab id=t-bluetooth aria-controls=p-bluetooth aria-selected=false tabindex=-1 data-t=bluetooth><span class="sw p"></span>Bluetooth <span class=n id=nb></span></button>
 <button type=button role=tab id=t-finder aria-controls=p-finder aria-selected=false tabindex=-1 data-t=finder>Finder <span class=on id=fon hidden title="Finding a device"></span></button>
</div>
<div role=tabpanel id=p-wifi aria-labelledby=t-wifi>
 <div class=scope><figure><figcaption><span class="sw w"></span><b>Wi-Fi networks</b>
  <select id=rw aria-label="Wi-Fi radar scale"></select></figcaption>
  <canvas id=cw role=img aria-label="Wi-Fi networks by signal or estimated distance; the same networks are listed below"></canvas>
  <div class=legend><span><i class="sw w"></i>secured</span><span><i class="sw o"></i>open</span>
   <span><i class="sw j"></i>this board's network</span></div>
  <p class=pick id=pw>Point at a dot to see which network it is.</p></figure></div>
 <div class=qslot></div>
 <section class=list><h2>In range <span class=n id=cnw></span></h2>
 <div class=wrap><table><thead><tr id=hw></tr></thead><tbody id=tw></tbody></table></div>
 <p class=none id=ew hidden></p></section>
 <section class=list><h2>History <span class=n id=cnh></span></h2>
 <p class=hint>Heard since the board started, and not in range now.</p>
 <div class=wrap><table><thead><tr id=hh></tr></thead><tbody id=th class=h></tbody></table></div>
 <p class=none id=eh hidden></p></section>
 <div class=logslot></div>
</div>
<div role=tabpanel id=p-bluetooth aria-labelledby=t-bluetooth hidden>
 <div class=scope><figure><figcaption><span class="sw p"></span><b>Bluetooth devices</b>
  <select id=rb aria-label="Bluetooth radar scale"></select></figcaption>
  <canvas id=cb role=img aria-label="Bluetooth devices by signal or estimated distance; the same devices are listed below"></canvas>
  <div class=legend id=lg></div>
  <div class=legend><label class=opt><input type=checkbox id=hidep> Hide private addresses
   <span id=np></span></label></div>
  <p class=pick id=pb>Point at a dot to see which device it is.</p></figure></div>
 <div class=qslot></div>
 <section class=list><h2>Devices <span class=n id=cnb></span></h2>
 <p class=hint>Most phones and earbuds change their address every quarter hour or
 so, which is why one phone can show up more than once. Those say <i>private</i>.
 The kind comes from what a device advertises and is a best guess.</p>
 <div class=wrap><table><thead><tr id=hb></tr></thead><tbody id=tb></tbody></table></div>
 <p class=none id=eb hidden></p></section>
 <div class=logslot></div>
</div>
<div role=tabpanel id=p-finder aria-labelledby=t-finder hidden>
 <div id=fpick>
  <div class=fpick><h2>What are you looking for?</h2>
  <p class=hint>The board does the listening, not your phone, so take it with you: run it
  from a USB power bank and keep this page open. Pick a device here, or press Find beside
  it in the Wi-Fi or Bluetooth list.</p>
  <div class=chips role=group aria-label="Show" id=chips></div>
  <p class=fmsg id=fmsg role=alert></p></div>
  <div class=qslot></div>
  <ul class=flist id=flist></ul>
  <p class=none id=fnone hidden></p>
 </div>
 <div id=fgo hidden>
  <div class=ftop><div class=who><b id=fname></b><span class=what id=fwhat></span></div>
   <button type=button id=fchange>Change</button></div>
  <p class=fstate id=fstate></p>
  <p class=fstate id=fpriv hidden>It uses a private address, which it changes every so often (phones and
  many trackers do, some every quarter hour). When it does, it drops out here: pick it again from the list.</p>
  <div class=fmain id=fmain>
  <div class=fread>
   <div class=frow><div><p class=big id=fbd>&ndash;</p><p class=fsub>away, by signal strength</p></div>
    <p class=trend id=ftrend>&nbsp;</p></div>
   <p class=prox id=fprox>&nbsp;</p>
   <div class=meter><i id=fmark hidden></i></div>
   <div class=ends><span>far</span><span>here</span></div>
   <p class=fmeta id=fmeta>&ndash;</p>
  </div>
  <canvas id=cf role=img aria-label="Where the device is: the ring is the estimated distance; an arrow appears after a turn"></canvas>
 </div>
 <div class=tracebox><canvas class=trace id=cs role=img aria-label="Signal strength over the last minute"></canvas>
   <p><span>a minute ago</span><span>signal, dBm</span><span>now</span></p></div>
  <div class=fbtn>
   <button type=button class=primary id=fturn>Turn to find the direction</button>
   <button type=button id=fsound aria-pressed=false>Sound off</button>
   <button type=button id=fstop>Stop finding</button>
  </div>
  <p class=turnmsg id=fturnmsg></p>
  <p class=vh aria-live=polite id=fsay></p>
  <details class=how><summary>How to find it</summary>
  <ol>
  <li>Take the board with you. It is the board that hears the device, so a board left on a shelf
  will not get any closer. Run it from a USB power bank, keep this page open on your phone, and stay
  within reach of your Wi-Fi.</li>
  <li>Walk slowly, a few steps at a time, and pause. One reading can jump several dB from the
  last, so go by the trend over a few seconds: <i>warmer</i> means closer, <i>colder</i> means turn
  back or try another way.</li>
  <li>For a direction, hold the board flat against your chest and press <i>Turn to find the
  direction</i>, then turn on the spot to your right in step with the hand. Your body blocks the
  signal from behind you, so it is strongest when you face the device.</li>
  <li>Close in, look and listen rather than trust the figure. Walls, furniture and bags make
  things seem farther than they are: a tracker in a bag can read a few metres away when it is
  next to you.</li>
  </ol>
  <p>A device that changes its address (most phones, and some trackers every quarter hour or so)
  drops out when it does. Pick it again from the list.</p>
  </details>
 </div>
</div>
<section class=feed id=logbox><h2>Live log <span>arrivals and departures while this page is open</span></h2>
<ol id=feed><li class=none>Watching for arrivals and departures&hellip;</li></ol></section>
<div class=find id=qbox><input id=q type=search autocomplete=off title="Press / to search"
 placeholder="Search name, kind, address or maker"></div>
<p class=note id=note>Distance is estimated from signal strength, so walls and bodies make
things look farther than they are; the Signal scale shows the strength itself.
Direction on the radars is not measured: each dot keeps a fixed bearing so you can follow
it, nothing more. The Finder gets a direction from you turning with the board.
&#9650; and &#9660; mean the signal is getting stronger or weaker.</p>
<details><summary>Distance calibration and display</summary>
<p class=hint>Signal strength one metre from the board, and how fast it falls
off: 2 in open air, 3 or more through walls. Hold a phone a metre away and
read its signal off the list to set the first. Kept in this browser.</p>
<div class=cal>
 <label for=cwp>Wi-Fi at 1 m (dBm)</label><label for=cbp>Bluetooth at 1 m (dBm)</label>
 <input id=cwp type=number step=1 min=-100 max=-10>
 <input id=cbp type=number step=1 min=-100 max=-10>
 <label for=cwn>Wi-Fi falloff</label><label for=cbn>Bluetooth falloff</label>
 <input id=cwn type=number step=0.1 min=1.5 max=6>
 <input id=cbn type=number step=0.1 min=1.5 max=6>
</div>
<button type=button id=calreset>Reset to defaults</button>
<label class=opt><input type=checkbox id=mask> Mask addresses and names, for screenshots</label>
</details>
<p class=foot><a href="/api/nearby">Nearby data</a> &middot; <a href="/api/nearby/find">Finder data</a>
&middot; Some ideas on this page come from
<a href="https://github.com/PolarPatch/BlueWatch">BlueWatch</a>.</p>
</div>
<script>
var D=null,busyT=0,pollT=0,lastOk=0,masked=false;
var CAL0={wp:-45,wn:2.7,bp:-59,bn:2.2},cal={};
var SCALES=[0,5,10,20,30,50,100];
var sorts={w:{k:'rssi',d:-1},b:{k:'rssi',d:-1},h:{k:'age_s',d:1}};
var KIND={phone:'Phone',tablet:'Tablet',computer:'Computer',watch:'Watch',
 fitness:'Fitness',audio:'Headphones',speaker:'Speaker',tv:'TV',tracker:'Tracker',
 beacon:'Beacon',smarthome:'Smart home',sensor:'Sensor',input:'Input device',
 glasses:'Smart glasses',vehicle:'Vehicle',lock:'Lock',printer:'Printer',flipper:'Flipper Zero'};
var GROUP={phone:'p',tablet:'p',computer:'p',watch:'p',fitness:'p',audio:'p',glasses:'p',
 tracker:'t',flipper:'t'};
var GNAME={p:'personal',t:'trackers',h:'home and things',u:'not identified'};
var hideg={};
function put(k,v){try{localStorage.setItem('nm.nb.'+k,v)}catch(e){}}
function get(k){try{return localStorage.getItem('nm.nb.'+k)}catch(e){return null}}
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;')}
function since(s){return s<3?'just now':ago(s)+' ago'}
function ago(s){if(s<0)return 'never';if(s<3)return 'now';if(s<60)return s+'s';
 if(s<3600)return((s/60)|0)+'m';if(s<86400)return((s/3600)|0)+'h';return((s/86400)|0)+'d'}
function metres(rssi,t){var p=t=='b'?cal.bp:cal.wp,n=t=='b'?cal.bn:cal.wn;
 return Math.max(.1,Math.min(200,Math.pow(10,(p-rssi)/(10*n))))}
function fdist(m){return '~'+(m<10?m.toFixed(1):Math.round(m))+' m'}
function bearing(k){var h=0;for(var i=0;i<k.length;i++)h=(h*31+k.charCodeAt(i))>>>0;
 return h%3600/3600*2*Math.PI}
function group(x){return x.type&&x.type!='unknown'?GROUP[x.type]||'h':'u'}
function maddr(a){return masked?a.slice(0,5)+':XX:XX:XX:XX':a}
function mname(n){return masked&&n.length>2?n.slice(0,2)+new Array(n.length-1).join('*'):n}
function wtext(x){return x.ssid?mname(x.ssid):'hidden network'}
function btext(x){return x.name?mname(x.name):x.model?x.model
 :x.vendor?x.vendor+' device':KIND[x.type]?KIND[x.type]:'unnamed device'}
function wname(x){return x.ssid?esc(mname(x.ssid)):'<span class=hid>hidden network</span>'}
function bname(x){return x.name?esc(mname(x.name)):'<span class=hid>'+esc(btext(x))+'</span>'}
function key(x){return x.bssid||x.addr}
function matches(x,f){if(!f)return true;
 return((x.ssid||'')+' '+(x.name||'')+' '+(x.model||'')+' '+(x.vendor||'')+' '
  +(KIND[x.type]||'')+' '+key(x)+' '+(x.security||'')).toLowerCase().indexOf(f)>-1}
function shown(x){return !hideg[group(x)]&&!(hidep.checked&&x.kind=='private')}
function findBtn(t,k,txt){return '<button type=button class=fb data-ft='+t+' data-fa="'+esc(k)+'">'
 +(txt||'Find')+'</button>'}

// Signal trend over the last few readings, per device, kept by this page.
var hist={};
function remember(list){list.forEach(function(x){var h=hist[key(x)]||(hist[key(x)]=[]);
 h.push(x.rssi);if(h.length>6)h.shift()})}
function trend(x){var h=hist[key(x)];if(!h||h.length<4)return 0;var n=h.length,
 d=(h[n-1]+h[n-2])/2-(h[0]+h[1])/2;return d>=4?1:d<=-4?-1:0}
function arrow(x){var t=trend(x);return t?' <span class=tr>'+(t>0?'▲':'▼')+'</span>':''}
function arrowtxt(x){var t=trend(x);return t>0?' ▲':t<0?' ▼':''}

// ---- radars ----
var reduce=window.matchMedia&&matchMedia('(prefers-reduced-motion: reduce)').matches;
var C={},H=[];
function rgb(h){h=h.replace('#','');if(h.length==3)h=h.replace(/./g,'$&$&');var n=parseInt(h,16)||0;
 return[n>>16&255,n>>8&255,n&255]}
function readColors(){var cs=getComputedStyle(document.documentElement);
 ['--wifi','--gp','--gt','--gh','--mut','--scope','--ring','--ok','--ink','--panel']
 .forEach(function(k){C[k]=cs.getPropertyValue(k).trim()});
 H=[0,1,2,3,4,5].map(function(i){return rgb(cs.getPropertyValue('--h'+i).trim())})}
// Cold to hot, 0 to 1: the Finder's colour for how close it is. Between
// two stops by hue, not by red, green and blue, which would turn green and
// gold into olive.
function hsl(c){var r=c[0]/255,g=c[1]/255,b=c[2]/255,mx=Math.max(r,g,b),mn=Math.min(r,g,b),
 l=(mx+mn)/2,d=mx-mn,h=0,s=0;
 if(d){s=d/(1-Math.abs(2*l-1));h=mx==r?((g-b)/d+6)%6:mx==g?(b-r)/d+2:(r-g)/d+4}return[h*60,s,l]}
function hc(v){v=Math.max(0,Math.min(1,v))*5;var i=Math.min(4,Math.floor(v)),f=v-i,a=hsl(H[i]),
 b=hsl(H[i+1]),dh=((b[0]-a[0]+540)%360)-180;
 return 'hsl('+((a[0]+dh*f+360)%360).toFixed(1)+','+((a[1]+(b[1]-a[1])*f)*100).toFixed(1)+'%,'
  +((a[2]+(b[2]-a[2])*f)*100).toFixed(1)+'%)'}
function Radar(id,t,pick){this.cv=document.getElementById(id);this.t=t;
 this.pick=document.getElementById(pick);this.pts=[];this.ghosts=[];this.born={};this.sel=null;
 this.hl=null;
 var r=get('r'+t);this.scale=r!=null&&r!==''&&SCALES.indexOf(+r)>-1?+r:(t=='w'?30:20);
 var me=this;
 function at(e){var b=me.cv.getBoundingClientRect();return me.near(e.clientX-b.left,e.clientY-b.top)}
 this.cv.addEventListener('pointermove',function(e){if(e.pointerType=='mouse')me.choose(at(e),false)});
 this.cv.addEventListener('pointerleave',function(e){if(e.pointerType=='mouse')me.choose(null,false)});
 this.cv.addEventListener('click',function(e){me.choose(at(e),true)})}
Radar.prototype.radius=function(x){if(!this.scale)return Math.max(0,Math.min(1,(-20-x.rssi)/80));
 return Math.min(metres(x.rssi,this.t)/this.scale,1)};
Radar.prototype.beyond=function(x){return this.scale>0&&metres(x.rssi,this.t)>this.scale};
Radar.prototype.near=function(x,y){var best=null,bd=14*14,w=this.cv.clientWidth,c=w/2,R=c-12;
 this.pts.forEach(function(p){if(!p.on)return;var dx=c+p.r*R*Math.cos(p.a)-x,
  dy=c+p.r*R*Math.sin(p.a)-y,d=dx*dx+dy*dy;if(d<bd){bd=d;best=p}});return best};
Radar.prototype.choose=function(p,stick){
 if(!p&&!stick&&this.sel)p=this.sel;else if(stick)this.sel=p;
 this.hl=p?p.k:null;mark(this.t,this.hl);redraw();
 this.pick.innerHTML=p?p.label+(this.sel&&this.sel.k==p.k?' '+findBtn(this.t,p.k,'Find it'):'')
  :(this.t=='w'?'Point at a dot to see which network it is.':'Point at a dot to see which device it is.')};
Radar.prototype.set=function(list,f){var me=this,keep=null,born=this.born;
 this.pts=list.filter(shown).map(function(x){var k=key(x),m=metres(x.rssi,me.t);
  var p={k:k,a:bearing(k),r:me.radius(x),out:me.beyond(x),on:matches(x,f),j:!!x.joined,
   open:me.t=='w'&&x.security=='Open',
   col:me.t=='w'?C['--wifi']:C[{p:'--gp',t:'--gt',h:'--gh',u:'--mut'}[group(x)]],
   f:me.t=='b'?Math.max(.35,1-Math.max(0,x.age_s-10)/60):1,rssi:x.rssi,born:born[k],
   text:me.t=='w'?wtext(x):btext(x),sub:x.rssi+' dBm'+arrowtxt(x),
   label:'<b>'+esc(me.t=='w'?wtext(x):btext(x))+'</b> · '+x.rssi+' dBm'+arrowtxt(x)
    +' · '+fdist(m)+(me.t=='w'?' · channel '+x.ch+' · '+esc(x.security)
     :(KIND[x.type]?' · '+esc(KIND[x.type]):'')+(x.vendor?' · '+esc(x.vendor):''))
    +'<br>'+esc(maddr(k))+(x.joined?' · this board is joined to it':'')
    +(me.t=='b'&&x.kind!='public'?' · '+x.kind+' address':'')};
  if(me.sel&&me.sel.k==k)keep=p;return p});
 if(this.sel){this.sel=keep;if(!keep)this.choose(null,true);
  else if(this.hl==keep.k)this.pick.innerHTML=keep.label+' '+findBtn(this.t,keep.k,'Find it')}};
Radar.prototype.draw=function(now){var cv=this.cv,w=cv.clientWidth;if(!w)return;
 var dpr=window.devicePixelRatio||1,px=Math.round(w*dpr);
 if(cv.width!=px){cv.width=px;cv.height=px}
 var x=cv.getContext('2d'),c=w/2,R=c-12,i,me=this,sweep=reduce?null:now/4200*2*Math.PI%(2*Math.PI);
 x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,w);
 scope(x,c,R,sweep);
 x.fillStyle=C['--mut'];x.font='10px system-ui,sans-serif';x.textBaseline='top';x.lineWidth=1;
 var placed=[];
 for(i=1;i<=4;i++){var rl=this.scale?(this.scale*i/4%1?(this.scale*i/4).toFixed(1)
   :this.scale*i/4)+' m':(-20-20*i)+' dBm';x.fillText(rl,c+3,c-R*i/4+2);
  placed.push({x:c+1,y:c-R*i/4+1,w:x.measureText(rl).width+4,h:12})}
 var t=Date.now(),drawn=[];
 this.pts.forEach(function(p){var px=c+p.r*R*Math.cos(p.a),py=c+p.r*R*Math.sin(p.a),rad=4.2;
  var glow=1;if(sweep!=null){var behind=(sweep-p.a+4*Math.PI)%(2*Math.PI);
   glow=.4+.6*Math.exp(-behind/2.4);if(behind<.5)rad+=1.6*(1-behind/.5)}
  x.globalAlpha=p.on?p.f*glow:.1;x.fillStyle=p.col;x.strokeStyle=p.col;
  if(p.born&&t-p.born<6000&&p.on){var ph=((t-p.born)%1500)/1500;
   x.globalAlpha=.85*(1-ph);x.lineWidth=2;x.beginPath();x.arc(px,py,rad+4+22*ph,0,7);x.stroke();
   x.globalAlpha=p.f*glow}
  x.lineWidth=1.6;x.beginPath();x.arc(px,py,p.out?3:rad,0,7);
  if(p.open)x.stroke();else x.fill();
  if(p.out){x.lineWidth=1;x.beginPath();x.arc(px,py,5.5,0,7);x.stroke()}
  if(p.j){x.lineWidth=1.5;x.beginPath();x.arc(px,py,8,0,7);x.stroke()}
  if(me.hl==p.k){x.globalAlpha=1;x.strokeStyle=C['--ink'];x.lineWidth=2;
   x.beginPath();x.arc(px,py,10,0,7);x.stroke()}
  x.globalAlpha=1;if(p.on)drawn.push({p:p,x:px,y:py})});
 for(i=this.ghosts.length-1;i>=0;i--){var g=this.ghosts[i],ga=t-g.at;
  if(ga>4000){this.ghosts.splice(i,1);continue}
  var gx=c+g.r*R*Math.cos(g.a),gy=c+g.r*R*Math.sin(g.a);
  x.globalAlpha=.7*(1-ga/4000);x.strokeStyle=g.col;x.lineWidth=1.4;
  if(x.setLineDash)x.setLineDash([3,3]);x.beginPath();x.arc(gx,gy,6+10*ga/4000,0,7);x.stroke();
  if(x.setLineDash)x.setLineDash([])}
 // Direct labels for the strongest, the selected first, never overlapping.
 x.globalAlpha=1;var shownN=0,max=w<300?4:6;
 drawn.sort(function(a,b){return(b.p.k==me.hl)-(a.p.k==me.hl)||b.p.rssi-a.p.rssi});
 drawn.forEach(function(d){if(shownN>=max)return;x.font='10px system-ui,sans-serif';
  var tw=Math.min(x.measureText(d.p.text).width,96),bx=d.x+8,by=d.y-8,bw=Math.max(tw,46),bh=21;
  if(bx+bw>w-2)bx=d.x-8-bw;if(by<2||by+bh>w-2)return;
  if(placed.some(function(b){return bx<b.x+b.w+3&&bx+bw+3>b.x&&by<b.y+b.h+1&&by+bh+1>b.y}))return;
  placed.push({x:bx,y:by,w:bw,h:bh});shownN++;
  x.fillStyle=C['--ink'];x.fillText(clip(x,d.p.text,96),bx,by);
  x.font='9px system-ui,sans-serif';x.fillStyle=C['--mut'];x.fillText(d.p.sub,bx,by+11)});
 x.fillStyle=C['--ink'];x.beginPath();x.arc(c,c,2.5,0,7);x.fill()};
// The scope itself: face, rings, cross and, unless motion is reduced, the beam.
function scope(x,c,R,sweep){var i;
 x.fillStyle=C['--scope'];x.beginPath();x.arc(c,c,R,0,7);x.fill();
 x.strokeStyle=C['--ring'];x.lineWidth=1;
 for(i=1;i<=4;i++){x.beginPath();x.arc(c,c,R*i/4,0,7);x.stroke()}
 x.beginPath();x.moveTo(c-R,c);x.lineTo(c+R,c);x.moveTo(c,c-R);x.lineTo(c,c+R);x.stroke();
 if(sweep==null)return;x.save();x.translate(c,c);
 for(i=0;i<20;i++){x.globalAlpha=.14*Math.pow(1-i/20,1.3);x.fillStyle=C['--ok'];
  x.beginPath();x.moveTo(0,0);x.arc(0,0,R,sweep-(i+1)*.032,sweep-i*.032);x.fill()}
 x.globalAlpha=.75;x.strokeStyle=C['--ok'];x.lineWidth=1.5;x.beginPath();x.moveTo(0,0);
 x.lineTo(R*Math.cos(sweep),R*Math.sin(sweep));x.stroke();x.restore();x.globalAlpha=1}
function clip(x,s,max){if(x.measureText(s).width<=max)return s;
 while(s.length>1&&x.measureText(s+'…').width>max)s=s.slice(0,-1);return s+'…'}
var RW,RB;
function frame(t){RW.draw(t);RB.draw(t);fdraw(t);if(!reduce&&!document.hidden)requestAnimationFrame(frame)}
function redraw(){if(reduce||document.hidden){var t=performance.now();RW.draw(t);RB.draw(t);fdraw(t)}}

// ---- live log ----
// The first 20 seconds only learn what is already here: a page opened after
// a quiet spell would otherwise log everything the first quick scans find.
var here={w:{},b:{}},last={w:{},b:{}},events=[],opened=Date.now();
function track(){var now=new Date(),add=[],gone=[];
 function pass(t,list,isHere){var cur={};list.forEach(function(x){if(isHere(x))cur[key(x)]=x});
  Object.keys(cur).forEach(function(k){if(!here[t][k])add.push({t:t,x:cur[k]})});
  Object.keys(here[t]).forEach(function(k){if(!cur[k])gone.push({t:t,x:last[t][k]||here[t][k]})});
  here[t]=cur;Object.keys(cur).forEach(function(k){last[t][k]=cur[k]})}
 // Nothing comes or goes while the scans wait for the Finder, so nothing is logged.
 if(D.finding)return;
 pass('w',D.wifi,function(x){return x.live});
 pass('b',D.ble,function(x){return x.age_s<=60});
 if(Date.now()-opened>20000){var vis=function(e){return e.t=='w'||shown(e.x)};
  var a=add.filter(vis),g=gone.filter(vis);
  ['w','b'].forEach(function(t){var at=a.filter(function(e){return e.t==t});
   if(at.length>12)events.unshift({at:now,t:t,batch:at.length});
   else at.forEach(function(e){events.unshift({at:now,t:t,x:e.x,on:1});
    (t=='w'?RW:RB).born[key(e.x)]=Date.now()})});
  g.forEach(function(e){events.unshift({at:now,t:e.t,x:e.x,on:0});var R=e.t=='w'?RW:RB;
   R.ghosts.push({a:bearing(key(e.x)),r:R.radius(e.x),at:Date.now(),
    col:e.t=='w'?C['--wifi']:C[{p:'--gp',t:'--gt',h:'--gh',u:'--mut'}[group(e.x)]]})});
  if(events.length>80)events.length=80}
 feedRender()}
function feedRender(){var f=feed,t=tab=='bluetooth'?'b':'w',ev=events.filter(function(e){return e.t==t});
 if(!ev.length){f.innerHTML='<li class=none>Watching for '+(t=='w'?'networks':'devices')
  +' arriving and leaving…</li>';return}
 f.innerHTML=ev.map(function(e){var tm=e.at.toTimeString().slice(0,8);
  if(e.batch)return '<li><span class=t>'+tm+'</span><span></span><span>'+e.batch
   +(t=='w'?' networks':' devices')+' appeared at once</span><span></span></li>';
  var x=e.x,sw=e.t=='w'?'w':group(x);
  return '<li'+(e.on?'':' class=gone')+'><span class=t>'+tm+'</span><span class="sw '+sw
   +'" title="'+(e.t=='w'?'Wi-Fi':'Bluetooth, '+GNAME[sw])+'"></span><span>'
   +esc(e.t=='w'?wtext(x):btext(x))+(e.on?' appeared':' left')+'</span><span class=s>'
   +(e.on?'':'last ')+x.rssi+' dBm</span></li>'}).join('')}

// ---- tables ----
var COLS={
 w:[{k:'ssid',t:'Network',f:function(x){return wname(x)+(x.joined?'<span class=tag>this board</span>'
   :x.twin?'<span class=tag>same name as yours</span>':'')}},
  {k:'rssi',t:'Signal',n:1,m:1,f:function(x){return x.rssi+' dBm'+arrow(x)}},
  {k:'dist',t:'Distance',n:1,m:1,f:function(x){return fdist(metres(x.rssi,'w'))}},
  {k:'ch',t:'Ch',n:1,m:1,f:function(x){return x.ch}},
  {k:'security',t:'Security',f:function(x){return x.security=='Open'?'<span class=open>Open</span>'
   :esc(x.security)}},
  {k:'bssid',t:'BSSID',m:1,f:function(x){return esc(maddr(x.bssid))}},
  {k:'age_s',t:'Heard',n:1,m:1,f:function(x){return ago(x.age_s)}},
  {k:'',t:'Find',ns:1,f:function(x){return findBtn('w',x.bssid)}}],
 b:[{k:'name',t:'Device',f:function(x){return bname(x)
   +(x.name&&x.vendor?'<span class=tag>'+esc(x.vendor)+'</span>':'')}},
  {k:'rssi',t:'Signal',n:1,m:1,f:function(x){return x.rssi+' dBm'+arrow(x)}},
  {k:'dist',t:'Distance',n:1,m:1,f:function(x){return fdist(metres(x.rssi,'b'))}},
  {k:'type',t:'Kind',f:function(x){return KIND[x.type]?esc(KIND[x.type])
   :'<span class=hid>not known</span>'}},
  {k:'addr',t:'Address',m:1,f:function(x){return esc(maddr(x.addr))
   +(x.kind!='public'?'<span class=tag>'+esc(x.kind)+'</span>':'')}},
  {k:'age_s',t:'Heard',n:1,m:1,f:function(x){return ago(x.age_s)}},
  {k:'',t:'Find',ns:1,f:function(x){return findBtn('b',x.addr)}}],
 h:[{k:'ssid',t:'Network',f:function(x){return wname(x)}},
  {k:'rssi',t:'Last signal',n:1,m:1,f:function(x){return x.rssi+' dBm'}},
  {k:'ch',t:'Ch',n:1,m:1,f:function(x){return x.ch}},
  {k:'security',t:'Security',f:function(x){return esc(x.security)}},
  {k:'bssid',t:'BSSID',m:1,f:function(x){return esc(maddr(x.bssid))}},
  {k:'age_s',t:'Last heard',n:1,m:1,f:function(x){return ago(x.age_s)+' ago'}}]};
function val(x,k,t){if(k=='dist')return -x.rssi;
 if(k=='name'&&t=='b')return btext(x).toLowerCase();
 if(k=='type')return KIND[x.type]?KIND[x.type].toLowerCase():'￿';
 if(k=='ssid')return(x.ssid||'￿').toLowerCase();
 var v=x[k];return typeof v=='string'?v.toLowerCase():v}
function head(t){var h=document.getElementById('h'+t),s=sorts[t];h.innerHTML='';
 COLS[t].forEach(function(c){var th=document.createElement('th');
  if(c.ns){th.className='ns fc';th.innerHTML='<span class=vh>'+c.t+'</span>';h.appendChild(th);return}
  th.innerHTML=c.t+(s.k==c.k?'<span class=ar>'+(s.d>0?'▲':'▼')+'</span>':'');
  th.title='Sort by '+c.t.toLowerCase();
  th.onclick=function(){s.d=s.k==c.k?-s.d:(c.n&&c.k!='age_s'&&c.k!='ch'?-1:1);
   s.k=c.k;put('s'+t,s.k+','+s.d);head(t);render()};h.appendChild(th)})}
// Rows are kept by key and moved, not rebuilt, and a cell is only rewritten
// when it changes. A list refreshes every few seconds and re-sorts as signals
// move; rebuilt, a tap that straddled a refresh could miss, or land on the
// row that had just moved under the finger.
function keyed(parent,tag,items,keyOf,build){var old={},i,k,ae=document.activeElement,at;
 for(i=0;i<parent.children.length;i++){var n0=parent.children[i];old[n0.getAttribute('data-k')]=n0}
 at=parent.firstElementChild;
 items.forEach(function(x){var k2=keyOf(x),n=old[k2];
  if(n)delete old[k2];else{n=document.createElement(tag);n.setAttribute('data-k',k2)}
  build(n,x);
  // Only what is out of place moves: a node taken out and put back loses focus.
  if(n===at)at=at.nextElementSibling;else parent.insertBefore(n,at)});
 for(k in old)parent.removeChild(old[k]);
 if(ae&&ae!==document.activeElement&&parent.contains(ae))ae.focus()}
function sethtml(n,h){if(n._h!==h){n.innerHTML=h;n._h=h}}
function rows(t,list,f){var s=sorts[t],tb=document.getElementById('t'+t),
 e=document.getElementById('e'+t),show=list.filter(function(x){return matches(x,f)});
 show.sort(function(a,b){var u=val(a,s.k,t),v=val(b,s.k,t);
  var r=u<v?-1:u>v?1:0;return r*s.d||b.rssi-a.rssi});
 tb.parentNode.parentNode.hidden=!show.length;e.hidden=!!show.length;
 keyed(tb,'tr',show,key,function(tr,x){var edge=t!='b'?'':'--edge-c:var('
   +(group(x)=='u'?'--mut':'--g'+group(x))+')';
  tr.classList.toggle('old',t=='b'&&x.age_s>30);
  if(tr.cells.length!=COLS[t].length)tr.innerHTML=new Array(COLS[t].length+1).join('<td></td>');
  COLS[t].forEach(function(c,i){var td=tr.cells[i],cl=(i?'':'k ')+(c.m?'m':'')+(c.ns?'fc':''),st=i?'':edge;
   sethtml(td,String(c.f(x)));if(td.className!=cl)td.className=cl;
   if((td.getAttribute('style')||'')!=st){if(st)td.setAttribute('style',st);else td.removeAttribute('style')}})});
 return show.length}
function mark(t,k){var tb=document.getElementById(t=='w'?'tw':'tb');
 [].forEach.call(tb.rows,function(r){r.classList.toggle('hl',r.getAttribute('data-k')==k)})}
function hover(t,R){var tb=document.getElementById('t'+t);
 tb.addEventListener('mouseover',function(e){var r=e.target.closest&&e.target.closest('tr');
  R.hl=r?r.getAttribute('data-k'):null;redraw()});
 tb.addEventListener('mouseleave',function(){R.hl=R.sel?R.sel.k:null;redraw()});
 tb.addEventListener('click',function(e){if(e.target.closest&&e.target.closest('.fb'))return;
  var r=e.target.closest&&e.target.closest('tr');
  if(!r)return;var k=r.getAttribute('data-k');
  R.choose(R.pts.filter(function(p){return p.k==k})[0]||null,true)})}
function legend(){var n={p:0,t:0,h:0,u:0},priv=0;D.ble.forEach(function(x){n[group(x)]++;
  if(x.kind=='private')priv++});
 sethtml(lg,['p','t','h','u'].map(function(g){return '<button type=button data-g='+g
  +' aria-pressed='+!hideg[g]+'><i class="sw '+g+'"></i>'+GNAME[g]+' '+n[g]+'</button>'}).join(''));
 np.textContent='('+priv+')'}
function render(){if(!D)return;var f=q.value.trim().toLowerCase();
 var live=D.wifi.filter(function(x){return x.live}),gone=D.wifi.filter(function(x){return !x.live});
 var mine=D.wifi.filter(function(x){return x.joined})[0];
 D.wifi.forEach(function(x){x.twin=!!(mine&&x.ssid&&x.ssid==mine.ssid&&!x.joined)});
 var bl=D.ble.filter(shown);
 var nw=rows('w',live,f),nb=rows('b',bl,f),nh=rows('h',gone,f);
 cnw.textContent=nw!=live.length?nw+' of '+live.length:live.length;
 cnb.textContent=nb!=D.ble.length?nb+' of '+D.ble.length:D.ble.length;
 cnh.textContent=nh!=gone.length?nh+' of '+gone.length:gone.length;
 ew.textContent=f&&live.length?'No network matches.':D.wifi_scan.enabled
  ?(D.wifi_scan.scans?'No networks heard in the last scan.':'The first Wi-Fi scan has not finished.')
  :'Wi-Fi scanning is off.';
 eb.textContent=(f||bl.length<D.ble.length)&&D.ble.length?'Nothing matches, or it is hidden above.'
  :!D.ble_scan.enabled?'Bluetooth is off.':D.ble_scan.state=='unavailable'
  ?'Bluetooth could not start on this board.':'No Bluetooth devices heard yet.';
 eh.textContent=f&&gone.length?'No network matches.':'Nothing yet: every network heard is still in range.';
 document.getElementById('nw').textContent=D.wifi_scan.enabled?live.length:'off';
 document.getElementById('nb').textContent=D.ble_scan.enabled?D.ble.length:'off';
 legend();RW.set(live,f);RB.set(D.ble,f);redraw();
 if(RW.sel)mark('w',RW.sel.k);if(RB.sel)mark('b',RB.sel.k);
 if(tab=='finder'){if(F.t)findRender();else pickRender()}}

// ---- status and controls ----
function showState(){var w=D.wifi_scan,b=D.ble_scan,p=[],bad=false;
 var nW=D.wifi.filter(function(x){return x.live}).length,nB=D.ble.length,c=[];
 if(w.enabled)c.push(nW+' Wi-Fi network'+(nW==1?'':'s'));
 if(b.enabled)c.push(nB+' Bluetooth device'+(nB==1?'':'s'));
 count.textContent=c.length?c.join(' and ')+' nearby':'Scanning is off';
 if(D.finding){var fx=D.finding;
  p.push('Finding '+(fx.name?mname(fx.name):fx.type=='ble'?'a Bluetooth device':'a hidden network')
   +'. The other scans wait until that stops, so these lists stand still')}
 else{
  if(w.enabled)p.push(w.state=='scanning'?'Scanning Wi-Fi now':w.state=='queued'?'Wi-Fi scan queued'
   :w.age_s<0?'Wi-Fi not scanned yet':'Wi-Fi scanned '+since(w.age_s));
  if(b.enabled){if(b.state=='unavailable'){p.push('Bluetooth could not start');bad=true}
   else p.push(b.state=='listening'?'listening for Bluetooth now':b.state=='queued'
    ?'Bluetooth queued':b.age_s<0?'Bluetooth not listened for yet'
    :'Bluetooth listened '+since(b.age_s))}
  if(D.sweeping&&(w.state=='queued'||b.state=='queued'))p.push('waiting for the network sweep to finish')}
 if(!D.on_lan)p.push('setup mode: scanning only while this page is open');
 state.textContent=p.join(' · ');state.className='state'+(bad?' bad':'');
 ver.textContent=D.version||''}
function controls(c){onw.checked=c.wifi;onb.checked=c.ble;
 var v=String(c.background_s),o=[].some.call(bg.options,function(x){return x.value==v});
 if(!o){var n=document.createElement('option');n.value=v;n.textContent='every '+v+' s';bg.add(n)}
 bg.value=v}
function setcfg(body){ctlmsg.textContent='';
 fetch('/api/nearby/config',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(body)}).then(function(r){return r.json().then(function(j){return {ok:r.ok,j:j}})})
 .then(function(res){if(!res.ok){ctlmsg.textContent=res.j.error||'Could not change that.';loadcfg();return}
  controls(res.j);if(res.j.ble&&!res.j.ble_ready)ctlmsg.textContent='Bluetooth could not start on this board.';
  load()})
 .catch(function(){ctlmsg.textContent='Could not reach the board.';loadcfg()})}
function loadcfg(){fetch('/api/nearby/config').then(function(r){return r.json()}).then(controls)
 .catch(function(){})}
// Every 3 s, or every 10 s while a device is being found: then the lists stand
// still anyway, and the Finder's own requests are what matter.
function load(){clearTimeout(pollT);
 fetch('/api/nearby',{cache:'no-store'}).then(function(r){return r.json()})
 .then(function(d){D=d;lastOk=Date.now();remember(d.wifi);remember(d.ble);showState();track();render()})
 .catch(function(){if(Date.now()-lastOk>9000){state.textContent='Cannot reach the monitor.';
  state.className='state bad'}})
 .then(function(){if(!document.hidden)pollT=setTimeout(load,tab=='finder'&&F.t?10000:3000)})}
scan.onclick=function(){scan.disabled=true;scan.textContent='Scanning…';
 fetch('/api/nearby/scan',{method:'POST'}).then(load).catch(function(){});
 clearTimeout(busyT);busyT=setTimeout(function(){scan.disabled=false;scan.textContent='Scan now'},6000)};
onw.onchange=function(){setcfg({wifi:onw.checked})};
onb.onchange=function(){setcfg({ble:onb.checked})};
bg.onchange=function(){setcfg({background_s:+bg.value})};
lg.onclick=function(e){var b=e.target.closest&&e.target.closest('button[data-g]');if(!b)return;
 var g=b.getAttribute('data-g');hideg[g]=!hideg[g];put('hide',JSON.stringify(hideg));render()};
hidep.onchange=function(){put('hidep',hidep.checked?'1':'');render()};
mask.onchange=function(){masked=mask.checked;put('mask',masked?'1':'');render();feedRender()};

// ---- tabs ----
var tab='',TABN=['wifi','bluetooth','finder'];
function tabFrom(h){h=(h||'').replace('#','');
 return h=='bt'?'bluetooth':h=='find'?'finder':TABN.indexOf(h)>-1?h:''}
function showTab(t){if(TABN.indexOf(t)<0)t='wifi';var prev=tab;tab=t;
 TABN.forEach(function(k){var b=document.getElementById('t-'+k),on=k==t;
  b.setAttribute('aria-selected',on);b.tabIndex=on?0:-1;document.getElementById('p-'+k).hidden=!on});
 put('tab',t);if(location.hash!='#'+t&&history.replaceState)history.replaceState(null,'','#'+t);
 var qs=document.querySelector('#p-'+t+' .qslot');if(qs)qs.appendChild(qbox);
 var ls=document.querySelector('#p-'+t+' .logslot');logbox.hidden=!ls;if(ls)ls.appendChild(logbox);
 q.placeholder=t=='wifi'?'Search name, address or security':t=='bluetooth'
  ?'Search name, kind, address or maker':'Search what to find';
 if(prev=='finder'&&t!='finder')findLeave();
 if(t=='finder'&&prev!='finder')findEnter();
 qbox.hidden=topbox.hidden=t=='finder'&&!!F.t;
 render();feedRender();redraw();
 if(prev&&prev!=t&&D)load()}
tabs.onclick=function(e){var b=e.target.closest&&e.target.closest('[role=tab]');
 if(b)showTab(b.getAttribute('data-t'))};
tabs.onkeydown=function(e){var i=TABN.indexOf(tab),k=e.key;
 if(k=='ArrowRight')i=(i+1)%3;else if(k=='ArrowLeft')i=(i+2)%3;else if(k=='Home')i=0;
 else if(k=='End')i=2;else return;
 e.preventDefault();showTab(TABN[i]);document.getElementById('t-'+TABN[i]).focus()};
window.addEventListener('hashchange',function(){var t=tabFrom(location.hash);if(t&&t!=tab)showTab(t)});

// ---- finder ----
// One device, listened for closely while you walk up to it with the board.
// The board sends every reading raw; this page smooths them, works out
// warmer and colder, and, from a slow turn on the spot with the board held
// against you, which way the signal is strongest.
var F={t:null,info:null,rd:[],after:0,pollT:0,lastOk:0,err:'',ema:null,emaT:0,since:0,
 turn:null,dir:null,found:false,sound:false,beepT:0,said:''};
var FSC=[1,2,4,8,20,40,100];
function fdm(r){return metres(r,F.t&&F.t.type=='wifi'?'w':'b')}
// 30 m is cold, 30 cm is hot, evenly by the logarithm in between.
function hot(d){return Math.max(0,Math.min(1,(Math.log(30)-Math.log(d))/Math.log(100)))}
function prox(d){return d<.7?'Very close':d<2?'Close':d<5?'Near':d<15?'Some way off':'Far off'}
function fbig(m){return '≈ '+(m<10?m.toFixed(1):Math.round(m))+' m'}
function freset(){F.rd=[];F.ema=null;F.emaT=0;F.turn&&clearTimeout(F.turn.timer);F.turn=null;
 F.dir=null;F.found=false;F.info=null;F.after=0;F.said='';fturnmsg.textContent='';
 fturn.textContent='Turn to find the direction'}
// A reading in: a median of three against single deep fades, then an
// average over about three seconds whatever the rate readings arrive at.
function addReading(t,r){var rd=F.rd,a=[r],i;
 for(i=rd.length-1;i>=0&&a.length<3;i--){if(t-rd[i].t<6000)a.push(rd[i].r);else break}
 a.sort(function(x,y){return x-y});var m=a.length==2?(a[0]+a[1])/2:a[(a.length-1)>>1],e;
 if(F.ema==null||t-F.emaT>15000)e=m;else e=F.ema+(1-Math.exp(-Math.max(0,t-F.emaT)/3000))*(m-F.ema);
 F.ema=e;F.emaT=t;rd.push({t:t,r:r,m:m,e:e});
 while(rd.length&&t-rd[0].t>180000)rd.shift();
 var u=F.turn;if(u&&u.t0&&t>=u.t0&&t<=u.t0+u.T)u.pts.push({a:(t-u.t0)/u.T*2*Math.PI,r:r})}
function fnow(){return F.ema==null?null:Math.round(F.ema)}
function heardAgo(){return F.rd.length?Date.now()-F.rd[F.rd.length-1].t:Infinity}
// Readings a minute, over what has been heard since finding started.
function rate(){var now=Date.now(),W=Math.min(60000,now-F.since),n=0,i;if(W<5000)return null;
 for(i=F.rd.length-1;i>=0&&now-F.rd[i].t<=W;i--)n++;return Math.round(n*60000/W)}
// dB gained over the last 8 seconds or so, by least squares over the filtered
// readings: a straight line through them shrugs off the odd jumpy one.
function trendOf(){var now=Date.now(),pts=[],W,i;
 for(W=8000;W<=16000;W+=4000){pts=F.rd.filter(function(x){return now-x.t<=W});if(pts.length>=4)break}
 if(pts.length<4)return null;var n=pts.length,sx=0,sy=0,sxx=0,sxy=0;
 for(i=0;i<n;i++){var X=(pts[i].t-now)/1000,Y=pts[i].m;sx+=X;sy+=Y;sxx+=X*X;sxy+=X*Y}
 var dn=n*sxx-sx*sx;if(dn<=0)return null;return(n*sxy-sx*sy)/dn*W/1000}
function ftarget(){var t=F.t,x=null,i=F.info;
 if(D)x=(t.type=='ble'?D.ble:D.wifi).filter(function(y){return key(y)==t.addr})[0]||null;
 if(!x&&i&&i.active)x=t.type=='ble'?{addr:i.addr,name:i.name,model:i.model,vendor:i.vendor,
  type:i.dtype,kind:i.kind}:{bssid:i.addr,ssid:i.name,ch:i.ch,security:i.security};
 return x||(t.type=='ble'?{addr:t.addr,name:'',type:'unknown',kind:''}:{bssid:t.addr,ssid:''})}
function pickFind(type,addr){if(!F.t||F.t.type!=type||F.t.addr!=addr)freset();
 F.t={type:type,addr:addr};F.err='';
 if(tab!='finder')showTab('finder');else{fview();findStart()}}
function findStart(){var t=F.t;if(!t)return;clearTimeout(F.pollT);F.lastOk=Date.now();
 fetch('/api/nearby/find',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({type:t.type,addr:t.addr})})
 .then(function(r){return r.json().then(function(j){return {ok:r.ok,j:j}})})
 .then(function(res){if(F.t!==t)return;
  if(!res.ok){F.err=res.j.error||'Could not start finding that.';F.t=null;put('ft','');
   freset();fview();return}
  F.err='';F.info=res.j;F.after=res.j.seq;F.lastOk=Date.now();
  if(!F.rd.length||Date.now()-F.rd[F.rd.length-1].t>5000)F.since=Date.now();
  put('ft',JSON.stringify(t));fview();findPoll()})
 .catch(function(){if(F.t!==t)return;findRender();F.pollT=setTimeout(findStart,3000)})}
function findPoll(){clearTimeout(F.pollT);if(!F.t||tab!='finder'||document.hidden)return;
 var t=F.t,again=true;
 fetch('/api/nearby/find?after='+F.after,{cache:'no-store'}).then(function(r){return r.json()})
 .then(function(d){if(F.t!==t)return;F.lastOk=Date.now();
  // The board let it go (asked too seldom, or restarted): ask again. Another
  // page finding something else has it now: leave it be, rather than the two
  // taking it from each other every second.
  if(!d.active){again=false;findStart();return}
  if(d.addr!=t.addr){again=false;F.t=null;put('ft','');freset();
   F.err='Another page is using the Finder on this board now, for '
    +(d.name?mname(d.name):maddr(d.addr))+'. Pick yours again to take it back.';fview();return}
  if(d.seq<F.after)F.after=0;var now=Date.now();
  d.readings.forEach(function(x){if(x[0]>F.after)addReading(now-x[1],x[2])});
  F.after=Math.max(F.after,d.seq);F.info=d;findRender()})
 .catch(function(){findRender()})
 .then(function(){if(again&&F.t===t&&tab=='finder'&&!document.hidden)F.pollT=setTimeout(findPoll,1000)})}
function stopReq(){fetch('/api/nearby/find',{method:'POST',headers:{'Content-Type':'application/json'},
 body:'{"stop":true}'}).catch(function(){})}
function findLeave(){clearTimeout(F.pollT);clearTimeout(F.beepT);fturnCancel();if(F.t)stopReq()}
function findEnter(){fview();if(F.t){findStart();if(F.sound)beeper()}}
function findStop(){findLeave();F.t=null;put('ft','');freset();fview();load()}
function fview(){var on=!!F.t;fpick.hidden=on;fgo.hidden=!on;fon.hidden=!on;
 fmsg.textContent=F.err||'';if(tab=='finder'){qbox.hidden=on;topbox.hidden=on}
 if(on)findRender();else pickRender()}
var pickc=get('pickc')||'';
function pickRender(){var f=q.value.trim().toLowerCase();
 if(!D){fnone.hidden=false;fnone.textContent='Listening…';return}
 var tr=D.ble.filter(function(x){return group(x)=='t'}),wl=D.wifi.filter(function(x){return x.live});
 var c=pickc;if(c!='t'&&c!='b'&&c!='w')c=tr.length?'t':'b';
 sethtml(chips,[['t','Trackers',tr.length],['b','Bluetooth',D.ble.length],['w','Wi-Fi',wl.length]]
  .map(function(o){return '<button type=button data-c='+o[0]+' aria-pressed='+(o[0]==c)+'>'+o[1]
   +' <span class=n>'+o[2]+'</span></button>'}).join(''));
 var list=(c=='t'?tr:c=='b'?D.ble:wl).filter(function(x){return matches(x,f)}).slice()
  .sort(function(a,b){return b.rssi-a.rssi});
 keyed(flist,'li',list,key,function(li,x){var w=c=='w',g=w?'w':group(x),sub=w
   ?['channel '+x.ch,x.security].join(' · ')
   :[KIND[x.type]||'kind not known',x.name?x.vendor:'',x.kind!='public'?x.kind+' address':'']
    .filter(String).join(' · ');
  var b=li.firstChild;if(!b){li.innerHTML='<button type=button></button>';b=li.firstChild}
  b.setAttribute('data-ft',w?'w':'b');b.setAttribute('data-fa',key(x));
  sethtml(b,'<span class="sw '+g+'"></span><span><b>'+esc(w?wtext(x):btext(x))+'</b><small>'+esc(sub)
   +'</small></span><span class=s>'+x.rssi+' dBm<br>'+fdist(metres(x.rssi,w?'w':'b'))+'</span>'
   +'<span class=go aria-hidden=true>Find</span>')});
 fnone.hidden=list.length>0;
 fnone.textContent=f?'Nothing matches.':c=='t'?'No trackers heard. AirTags, Tiles, SmartTags and the '
  +'like show here when the board hears them; choose Bluetooth to pick from everything.'
  :c=='b'?(D.ble_scan.enabled?'No Bluetooth devices heard yet.':'Bluetooth is off.')
  :(D.wifi_scan.enabled?'No networks in range.':'Wi-Fi scanning is off.')}
chips.onclick=function(e){var b=e.target.closest&&e.target.closest('button[data-c]');if(!b)return;
 pickc=b.getAttribute('data-c');put('pickc',pickc);pickRender()};
flist.onclick=function(e){var b=e.target.closest&&e.target.closest('button[data-fa]');if(!b)return;
 pickFind(b.getAttribute('data-ft')=='w'?'wifi':'ble',b.getAttribute('data-fa'))};
document.addEventListener('click',function(e){var b=e.target.closest&&e.target.closest('button.fb');
 if(!b)return;e.preventDefault();
 pickFind(b.getAttribute('data-ft')=='w'?'wifi':'ble',b.getAttribute('data-fa'))});
function findRender(){if(!F.t)return;var t=F.t,x=ftarget(),ble=t.type=='ble',i=F.info,now=Date.now();
 fname.textContent=ble?btext(x):wtext(x);
 fwhat.textContent=(ble?[KIND[x.type]||'',x.name?x.vendor||'':'',x.kind?x.kind+' address':'']
   :['Wi-Fi network',x.ch?'channel '+x.ch:'',x.security||'']).filter(String).concat(maddr(t.addr))
   .join(' · ');
 var r=fnow(),ago2=heardAgo(),have=r!=null&&ago2<30000,d=have?fdm(r):null,v=have?hot(d):0,
  s='',bad=false;
 if(F.lastOk&&now-F.lastOk>6000){s='Cannot reach the board. If you have carried it out of reach '
  +'of your Wi-Fi, come back a little.';bad=true}
 else if(!i)s='Starting…';
 else if(i.state=='off'){s=ble?'Bluetooth is switched off above, so the board cannot listen for it.'
  :'Wi-Fi scanning is switched off above, so the board cannot look for it.';bad=true}
 else if(i.state=='unavailable'){s='Bluetooth could not start on this board.';bad=true}
 else if(i.state=='paused')s={sweep:'Paused for a few seconds while netmon checks your network.',
  probe:'Paused for a moment while netmon times its connection.',
  scan:'Waiting a moment for a scan to finish.',update:'Paused while the firmware updates.',
  start:'Could not start listening just now. Trying again.'}[i.why]
  ||'Paused for a moment.';
 else if(!F.rd.length)s=i.for_s>20?'Listening, and nothing heard from it yet. It may be out of '
  +'range, switched off, or have changed its address.':'Listening for it, and nothing else…';
 else if(ago2>20000)s='Not heard for '+Math.round(ago2/1000)+' s. Keep still a moment; if it stays '
  +'quiet it may be out of range, or have changed its address.';
 else s=ble?'Listening for it, and nothing else.':'Looking for it on channel '+(i.ch||x.ch||'?')
  +', about once a second.';
 fstate.textContent=s;fstate.className='fstate'+(bad?' bad':'');
 fpriv.hidden=!(ble&&x.kind=='private');
 fbd.textContent=have?fbig(d):'–';
 var tr=have?trendOf():null,found=have&&d<.7&&ago2<10000;
 fprox.textContent=found?'Very close: within arm’s reach. Look around here.'
  :have?prox(d):F.rd.length?'Lost it for now':'Waiting for a reading';
 fmark.hidden=!have;if(have)fmark.style.left=(v*100).toFixed(1)+'%';
 ftrend.textContent=!have?' ':tr==null?'…':tr>=3?'▲ Warmer':tr<=-3?'▼ Colder':'● Steady';
 ftrend.title=tr==null?'Learning the trend':tr>=3?'Getting closer':tr<=-3?'Getting farther'
  :'About the same';
 var pm=rate();
 fmeta.textContent=[have?r+' dBm':'no signal',pm==null?'counting readings':pm>=90?Math.round(pm/60)
  +' readings a second':pm+' a minute',F.rd.length?'heard '+(ago2<1500?'just now':Math.round(ago2/1000)
  +' s ago'):'not heard yet'].join(' · ');
 // A buzz on arriving, where the phone allows it: only after a tap on the page.
 if(found&&!F.found&&navigator.vibrate&&(!navigator.userActivation||navigator.userActivation.hasBeenActive)){
  try{navigator.vibrate([90,60,90])}catch(e){}}
 F.found=found;fmain.className='fmain'+(found?' found':'');
 var say=have?(found?'Very close':prox(d))+(tr==null?'':tr>=3?', warmer':tr<=-3?', colder':''):'';
 if(say!=F.said){F.said=say;fsay.textContent=say}
 drawTrace();redraw()}
// ---- the Finder's radar: you in the middle, the distance as a ring, and
// after a turn, an arrow where the signal was strongest ----
function fdraw(now){var cv=cf,w=cv.clientWidth;if(!w||!F.t)return;
 var dpr=window.devicePixelRatio||1,px=Math.round(w*dpr);if(cv.width!=px){cv.width=px;cv.height=px}
 var x=cv.getContext('2d'),c=w/2,R=c-16,TAU=2*Math.PI,i,u=F.turn,turning=u&&u.t0;
 x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,w);
 var r=fnow(),have=r!=null&&heardAgo()<30000,d=have?fdm(r):null,col=hc(have?hot(d):0),sc=10;
 if(have){sc=100;for(i=0;i<FSC.length;i++)if(FSC[i]>=d*1.6){sc=FSC[i];break}}
 scope(x,c,R,turning||reduce?null:now/4200*TAU%TAU);
 var f=turning?Math.min(1,(Date.now()-u.t0)/u.T):0,ha=-Math.PI/2+f*TAU;
 if(turning){x.globalAlpha=.13;x.fillStyle=C['--ok'];x.beginPath();x.moveTo(c,c);
  x.arc(c,c,R,-Math.PI/2,ha);x.closePath();x.fill();x.globalAlpha=1}
 x.fillStyle=C['--mut'];x.font='10px system-ui,sans-serif';x.textBaseline='top';x.textAlign='left';
 for(i=1;i<=4;i++)x.fillText(+(sc*i/4).toFixed(2)+' m',c+3,c-R*i/4+2);
 // What the turn heard: each reading at the angle the hand was at, farther
 // out the stronger it was; after the turn, the smoothed shape of it.
 var dir=F.dir,pts=u?u.pts:dir&&dir.pts;
 if(pts&&pts.length){var lo=1e9,hi=-1e9;pts.forEach(function(p){lo=Math.min(lo,p.r);hi=Math.max(hi,p.r)});
  var span=Math.max(6,hi-lo),rad=function(v){return R*(.22+.72*(v-lo)/span)};
  if(!u&&dir&&dir.curve){x.beginPath();var st=0;dir.curve.forEach(function(v,k){if(v==null)return;
    var a=k*TAU/36-Math.PI/2,rr=rad(v);if(!st++)x.moveTo(c+rr*Math.cos(a),c+rr*Math.sin(a));
    else x.lineTo(c+rr*Math.cos(a),c+rr*Math.sin(a))});
   x.closePath();x.globalAlpha=.1;x.fillStyle=col;x.fill();x.globalAlpha=.45;x.strokeStyle=col;
   x.lineWidth=1.2;x.stroke()}
  x.fillStyle=col;pts.forEach(function(p){var rr=rad(p.r),a=p.a-Math.PI/2;x.globalAlpha=.85;
   x.beginPath();x.arc(c+rr*Math.cos(a),c+rr*Math.sin(a),2.6,0,TAU);x.fill()});x.globalAlpha=1}
 if(have){var rr=Math.min(d/sc,1)*R,lo2=Math.min(d/1.5/sc,1)*R,hi2=Math.min(d*1.5/sc,1)*R;
  x.globalAlpha=dir&&dir.ok?.11:.17;x.fillStyle=col;x.beginPath();x.arc(c,c,hi2,0,TAU);x.arc(c,c,lo2,TAU,0,true);
  x.fill();x.globalAlpha=1;x.strokeStyle=col;x.lineWidth=2.4;if(x.setLineDash)x.setLineDash([6,5]);
  x.beginPath();x.arc(c,c,rr,0,TAU);x.stroke();if(x.setLineDash)x.setLineDash([]);
  if(dir&&dir.ok&&!u){var age=Date.now()-dir.at,fade=Math.max(.3,1-age/180000),a=dir.a-Math.PI/2,
    ca=Math.cos(a),sa=Math.sin(a),tip=Math.max(rr,30),bx=c+tip*ca,by=c+tip*sa;
   x.globalAlpha=fade;x.strokeStyle=col;x.fillStyle=col;x.lineWidth=3;x.beginPath();x.moveTo(c,c);
   x.lineTo(c+(tip-14)*ca,c+(tip-14)*sa);x.stroke();x.beginPath();x.moveTo(c+(tip-4)*ca,c+(tip-4)*sa);
   x.lineTo(c+(tip-16)*ca-6*sa,c+(tip-16)*sa+6*ca);x.lineTo(c+(tip-16)*ca+6*sa,c+(tip-16)*sa-6*ca);
   x.closePath();x.fill();x.beginPath();x.arc(bx,by,6.5,0,TAU);x.fill();x.globalAlpha=1}}
 if(turning){x.strokeStyle=C['--ink'];x.fillStyle=C['--ink'];x.lineWidth=2.5;x.beginPath();x.moveTo(c,c);
  x.lineTo(c+R*Math.cos(ha),c+R*Math.sin(ha));x.stroke();x.beginPath();
  x.arc(c+R*Math.cos(ha),c+R*Math.sin(ha),5.5,0,TAU);x.fill()}
 if(u||(dir&&dir.ok)){x.fillStyle=C['--mut'];x.textAlign='center';x.textBaseline='bottom';
  x.fillText('ahead, where you started',c,c-R-3);x.textAlign='left'}
 if(F.found){var ph=reduce?.3:(Date.now()%1200)/1200;x.globalAlpha=.55*(1-ph);x.strokeStyle=col;
  x.lineWidth=3;x.beginPath();x.arc(c,c,8+34*ph,0,TAU);x.stroke();x.globalAlpha=1}
 x.fillStyle=C['--ink'];x.beginPath();x.arc(c,c,3.6,0,TAU);x.fill()}
// The last minute of signal: each reading a dot, the smoothed figure a line.
function drawTrace(){var cv=cs,w=cv.clientWidth,h=cv.clientHeight;if(!w||!h)return;
 var dpr=window.devicePixelRatio||1;if(cv.width!=Math.round(w*dpr)||cv.height!=Math.round(h*dpr)){
  cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr)}
 var x=cv.getContext('2d'),now=Date.now(),lo=-100,hi=-30,g;x.setTransform(dpr,0,0,dpr,0,0);
 x.clearRect(0,0,w,h);
 function X(t){return w-(now-t)/60000*w}
 function Y(r){return 4+(h-8)*(1-(Math.max(lo,Math.min(hi,r))-lo)/(hi-lo))}
 x.strokeStyle=C['--ring'];x.fillStyle=C['--mut'];x.lineWidth=1;x.font='9px system-ui,sans-serif';
 x.textBaseline='bottom';
 for(g=-40;g>=-90;g-=25){x.beginPath();x.moveTo(0,Y(g)+.5);x.lineTo(w,Y(g)+.5);x.stroke();
  x.fillText(g,2,Y(g)-1)}
 var pts=F.rd.filter(function(p){return now-p.t<=60000});
 x.fillStyle=C['--mut'];x.globalAlpha=.6;
 pts.forEach(function(p){x.beginPath();x.arc(X(p.t),Y(p.r),2,0,7);x.fill()});x.globalAlpha=1;
 x.strokeStyle=C['--ink'];x.lineWidth=1.8;x.beginPath();
 pts.forEach(function(p,k){if(!k||p.t-pts[k-1].t>15000)x.moveTo(X(p.t),Y(p.e));
  else x.lineTo(X(p.t),Y(p.e))});x.stroke()}
// ---- turning on the spot for a direction ----
// Asks the board to hold its network sweep off for the turn, which would
// otherwise leave a blind sector in it; 0 lets it go again.
function holdReq(s){var t=F.t;if(!t)return Promise.resolve(null);
 return fetch('/api/nearby/find',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({type:t.type,addr:t.addr,hold_s:s})})
 .then(function(r){return r.ok?r.json():null}).catch(function(){return null})}
function fturnStart(){if(!F.t)return;var pm=rate(),T=pm?Math.max(20,Math.min(45,Math.round(18*60/pm))):30;
 F.dir=null;var u=F.turn={at:Date.now(),T:T*1000,pts:[],t0:0,tick:-1,held:null};
 holdReq(T+6).then(function(j){if(F.turn===u)u.held=!!(j&&j.hold_ms>0)});
 fturn.textContent='Cancel the turn';turnTick()}
function turnTick(){var u=F.turn;if(!u)return;var now=Date.now();clearTimeout(u.timer);
 if(!u.t0){var left=3-Math.floor((now-u.at)/1000);
  if(left>0){fturnmsg.textContent='Hold the board flat against your chest and face ahead. Turning in '
   +left+'…'+(u.held===false?' (The board may stop listening for a few seconds of this turn to check '
   +'your network; it did so for a turn not long ago.)':'');u.timer=setTimeout(turnTick,250);return}
  u.t0=now;if(F.sound)tone(880,90,.2)}
 var f=(now-u.t0)/u.T;
 if(f>=1){if(now-u.t0<u.T+1500){fturnmsg.textContent='Hold still a moment…';
   u.timer=setTimeout(turnTick,250);return}
  turnDone();return}
 var q8=Math.floor(f*8);if(F.sound&&q8!=u.tick){u.tick=q8;tone(1250,25,.12)}
 fturnmsg.textContent='Turn slowly to your right, in step with the hand: once round in '
  +Math.round(u.T/1000)+' seconds. '+Math.round(f*100)+'%';
 if(reduce)redraw();u.timer=setTimeout(turnTick,250)}
function direction(pts){var n=pts.length,TAU=2*Math.PI,res={a:0,contrast:0,n:n,ok:false,curve:[]},
 best=-1e9,worst=1e9,k;if(n<5)return res;
 for(k=0;k<36;k++){var a=k*TAU/36,s=0,sw=0;
  pts.forEach(function(p){var dd=Math.abs(((p.a-a)%TAU+TAU+Math.PI)%TAU-Math.PI);
   if(dd<Math.PI/3){var wt=Math.cos(dd*1.5);s+=wt*p.r;sw+=wt}});
  var m=sw>.3?s/sw:null;res.curve.push(m);if(m==null)continue;
  if(m>best){best=m;res.a=a}if(m<worst)worst=m}
 res.contrast=best>-1e9?best-worst:0;res.ok=res.contrast>=3.5;
 // The widest part of the turn with no reading in it.
 var as=pts.map(function(p){return p.a}).sort(function(x,y){return x-y});
 res.gap=TAU-as[n-1]+as[0];for(k=1;k<n;k++)res.gap=Math.max(res.gap,as[k]-as[k-1]);
 return res}
var CLOCK={12:'straight ahead of where you started',1:'a little to the right',2:'a little to the right',
 3:'to your right',4:'behind you, on the right',5:'behind you, on the right',6:'behind you',
 7:'behind you, on the left',8:'behind you, on the left',9:'to your left',10:'a little to the left',
 11:'a little to the left'};
function dirText(d){if(d.n<5)return 'Too few readings during the turn to tell. Turn more slowly, '
  +'or move a little closer, and try again.';
 if(!d.ok)return 'No clear direction: the signal was much the same all the way round. It may be '
  +'very close, or bouncing off walls. Move a few steps and turn again.';
 var h=Math.round(d.a/(Math.PI/6))%12||12;
 return(d.contrast<6?'A faint lead: s':'S')+'trongest at about '+h+' o’clock, '+CLOCK[h]
  +' ('+Math.round(d.contrast)+' dB above the weakest). Face that way and walk slowly: it should '
  +'get warmer.'+(d.gap>Math.PI/3?' Part of the turn, about '+Math.round(d.gap*180/Math.PI)
  +'°, heard nothing, so turn again if this looks wrong.':'')}
function turnDone(){var u=F.turn;F.turn=null;fturn.textContent='Turn to find the direction';holdReq(0);
 var d=direction(u.pts);d.at=Date.now();d.pts=u.pts;F.dir=d;fturnmsg.textContent=dirText(d);
 if(F.sound)tone(d.ok?1320:440,160,.2);redraw()}
function fturnCancel(){if(!F.turn)return;clearTimeout(F.turn.timer);F.turn=null;holdReq(0);
 fturn.textContent='Turn to find the direction';fturnmsg.textContent=''}
fturn.onclick=function(){if(F.turn)fturnCancel();else fturnStart()};
// ---- sound: beeps that quicken and rise as it gets closer ----
var AC=null;
function audio(){if(!AC){var A=window.AudioContext||window.webkitAudioContext;if(A)try{AC=new A()}catch(e){}}
 if(AC&&AC.state=='suspended')AC.resume();return AC}
function tone(f,ms,vol){var a=audio();if(!a)return;var o=a.createOscillator(),g=a.createGain(),
 t=a.currentTime;o.type='sine';o.frequency.value=f;g.gain.setValueAtTime(.0001,t);
 g.gain.exponentialRampToValueAtTime(vol,t+.01);g.gain.exponentialRampToValueAtTime(.0001,t+ms/1000);
 o.connect(g);g.connect(a.destination);o.start(t);o.stop(t+ms/1000+.03)}
function beeper(){clearTimeout(F.beepT);if(!F.sound||!F.t||tab!='finder'||document.hidden)return;
 var r=fnow();if(r==null||heardAgo()>10000){F.beepT=setTimeout(beeper,1000);return}
 var v=hot(fdm(r));if(!F.turn)tone(330+990*v,70,.18);F.beepT=setTimeout(beeper,Math.round(1500-1340*v))}
fsound.onclick=function(){F.sound=!F.sound;fsound.setAttribute('aria-pressed',F.sound);
 fsound.textContent=F.sound?'Sound on':'Sound off';if(F.sound){audio();beeper()}else clearTimeout(F.beepT)};
fstop.onclick=findStop;fchange.onclick=findStop;
window.addEventListener('pagehide',function(){if(F.t&&tab=='finder'&&navigator.sendBeacon)
 try{navigator.sendBeacon('/api/nearby/find','{"stop":true}')}catch(e){}});

// ---- calibration and scales ----
function loadcal(){var s=null;try{s=JSON.parse(get('cal'))}catch(e){}
 ['wp','wn','bp','bn'].forEach(function(k){cal[k]=s&&isFinite(s[k])?+s[k]:CAL0[k];
  document.getElementById('c'+k).value=cal[k]})}
['cwp','cwn','cbp','cbn'].forEach(function(id){document.getElementById(id).onchange=function(){
 var k=id.slice(1),v=parseFloat(this.value),ok=k[1]=='p'?v<=-10&&v>=-100:v>=1.5&&v<=6;
 if(!ok){this.value=cal[k];return}cal[k]=v;put('cal',JSON.stringify(cal));render()}});
calreset.onclick=function(){put('cal','');loadcal();render()};
function scales(sel,R){SCALES.forEach(function(v){var o=document.createElement('option');
 o.value=v;o.textContent=v?v+' m':'Signal';sel.add(o)});sel.value=R.scale;
 sel.onchange=function(){R.scale=+sel.value;put('r'+R.t,R.scale);render()}}

readColors();
if(window.matchMedia)matchMedia('(prefers-color-scheme: dark)').addListener(function(){
 readColors();render();drawTrace()});
RW=new Radar('cw','w','pw');RB=new Radar('cb','b','pb');
scales(rw,RW);scales(rb,RB);hover('w',RW);hover('b',RB);
['w','b','h'].forEach(function(t){var s=(get('s'+t)||'').split(',');
 if(s.length==2&&s[0]){sorts[t].k=s[0];sorts[t].d=+s[1]||1}head(t)});
try{hideg=JSON.parse(get('hide'))||{}}catch(e){hideg={}}
hidep.checked=get('hidep')=='1';mask.checked=masked=get('mask')=='1';
q.addEventListener('input',render);
// '/' jumps to the search box, unless the key is being typed into a field.
document.addEventListener('keydown',function(e){var a=document.activeElement;
 if(e.key!='/'||e.ctrlKey||e.metaKey||e.altKey||qbox.hidden)return;
 if(a&&(a.tagName=='TEXTAREA'||a.tagName=='SELECT'||(a.tagName=='INPUT'
  &&!/^(checkbox|radio|button|submit|reset|file)$/.test(a.type))))return;
 e.preventDefault();q.focus()});
window.addEventListener('resize',function(){redraw();drawTrace()});
document.addEventListener('visibilitychange',function(){
 if(!document.hidden){load();if(tab=='finder'&&F.t){findStart();beeper()}
  if(!reduce)requestAnimationFrame(frame)}});
loadcal();
try{var ft=JSON.parse(get('ft'));if(ft&&ft.addr&&(ft.type=='ble'||ft.type=='wifi'))F.t=ft}catch(e){}
showTab(tabFrom(location.hash)||get('tab')||'wifi');
loadcfg();load();
if(!reduce)requestAnimationFrame(frame);else redraw();
</script>
)HTML";

static const char MAP_HTML[] PROGMEM =
"<!doctype html><title>Map - Network Monitor</title>"
NM_HEAD
R"HTML(<style>
:root{--scope:#f6f9fb;--ring:#d3dde6;--line:#c4d0db}
@media(prefers-color-scheme:dark){:root{--scope:#111920;--ring:#2b3947;--line:#34475a}}
.top{padding:1.05rem 1rem .9rem}
.count{margin:0;font-size:1.35rem;font-weight:600;letter-spacing:-.02em;
 font-variant-numeric:tabular-nums}
.state{margin:.2rem 0 0;font-size:.92rem;color:var(--mut)}
.state.bad{color:var(--bad)}
.state.flag{color:var(--bad);font-weight:500}
.ctl{display:flex;flex-wrap:wrap;align-items:center;gap:.5rem 1.1rem;margin-top:.8rem}
.ctl label{display:flex;align-items:center;gap:.35rem;margin:0;color:var(--ink);font-size:.86rem}
.ctl select{width:auto;padding:.3rem .45rem}
input[type=checkbox]{width:auto;margin:0;accent-color:var(--link)}
.mapbox{position:relative;background:var(--scope);border-top:1px solid var(--edge);
 border-bottom:1px solid var(--edge)}
#map{display:block;width:100%;height:auto;touch-action:manipulation;user-select:none;
 -webkit-user-select:none}
#map text{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Ubuntu,sans-serif;
 paint-order:stroke;stroke:var(--scope);stroke-width:3px;stroke-linejoin:round}
#map .t1{font-size:11.5px;font-weight:600;fill:var(--ink)}
#map .t2{font-size:10px;fill:var(--mut)}
#map .lk{stroke:var(--line);fill:none}
#map .lk.on{stroke:var(--link)}
#map .air{stroke:var(--mut);stroke-dasharray:3 3;fill:none}
#map .cl{fill:var(--panel);stroke:var(--edge);stroke-width:1.2;cursor:pointer}
#map .cl.on{stroke:var(--link);stroke-width:2}
#map .hub{fill:var(--panel);stroke:var(--ink);stroke-width:1.6;cursor:pointer}
#map .hub.on{stroke:var(--link);stroke-width:2.6}
#map .gl{fill:none;stroke:var(--ink);stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round;
 pointer-events:none}
#map .dv{stroke-width:1.6;cursor:pointer}
#map .known{fill:var(--ok);stroke:var(--ok)}
#map .private{fill:var(--warn);stroke:var(--warn)}
#map .unknown{fill:var(--bad);stroke:var(--bad)}
#map .off{fill:var(--panel);stroke:var(--mut);opacity:.7}
#map .me{fill:var(--link);stroke:var(--link)}
#map .ap{fill:var(--panel);stroke:var(--link);stroke-width:1.8;cursor:pointer}
#map .ap.gone{stroke:var(--mut);stroke-dasharray:2 2}
#map .sel{fill:none;stroke:var(--ink);stroke-width:2;pointer-events:none}
.tip{position:absolute;z-index:3;padding:.28rem .5rem;border-radius:7px;background:var(--ink);
 color:var(--panel);font-size:.76rem;line-height:1.3;white-space:nowrap;pointer-events:none;
 transform:translate(-50%,calc(-100% - 12px))}
.legend{display:flex;flex-wrap:wrap;justify-content:center;gap:.3rem 1rem;padding:.6rem 1rem;
 font-size:.76rem;color:var(--mut);border-bottom:1px solid var(--edge)}
.legend span{display:flex;align-items:center;gap:.3rem}
.sw{display:inline-block;width:.62rem;height:.62rem;border-radius:50%;flex:none}
.sw.known{background:var(--ok)}.sw.private{background:var(--warn)}.sw.unknown{background:var(--bad)}
.sw.off{box-shadow:inset 0 0 0 1.5px var(--mut)}.sw.me{background:var(--link)}
.sw.ap{box-shadow:inset 0 0 0 1.8px var(--link)}
.info{padding:1rem;border-bottom:1px solid var(--edge);min-height:6rem}
.info h2{display:flex;align-items:baseline;gap:.5rem;flex-wrap:wrap}
.info h2 span{font-weight:400;font-size:.8rem;color:var(--mut)}
.info p{margin:.4rem 0 0;font-size:.86rem}
.info .mut{color:var(--mut)}
.info .acts{display:flex;flex-wrap:wrap;gap:.4rem 1rem;margin-top:.8rem;font-size:.86rem}
.info ul{list-style:none;margin:.7rem -1rem 0;padding:0;border-top:1px solid var(--edge)}
.info li button{display:grid;grid-template-columns:.62rem minmax(0,1fr) auto;gap:.65rem;
 align-items:center;width:100%;padding:.5rem 1rem;border:0;border-bottom:1px solid var(--edge);
 border-radius:0;background:none;text-align:left;font-size:.86rem}
.info li:last-child button{border-bottom:0}
.info li button:hover{background:var(--tint)}
.info li small{display:block;color:var(--mut);font-size:.76rem;overflow:hidden;
 text-overflow:ellipsis;white-space:nowrap}
.info li b{display:block;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.info li .r{font-family:var(--mono);font-size:.78rem;color:var(--mut);text-align:right}
.warn{color:var(--bad)}
.note{margin:0;padding:.8rem 1rem;font-size:.78rem;color:var(--mut);border-bottom:1px solid var(--edge)}
</style>
<div class=sh>
<header><a class=brand href="/"><svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true"><circle cx="4" cy="10" r="2.4" fill="currentColor"/><path d="M8.4 5.6a6 6 0 0 1 0 8.8" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/><path d="M12.3 2.9a9.8 9.8 0 0 1 0 14.2" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity=".5"/></svg><b>netmon</b><span class=ver id=ver></span></a><nav><a href="/">Devices</a><span>Map</span><a href="/nearby">Nearby</a><a href="/events">Events</a><a href="/isp">Internet</a><a href="/settings">Settings</a></nav></header>
<div class=top>
 <p class=count id=count>Drawing the map</p>
 <p class=state id=state></p>
 <div class=ctl>
  <label>Group by <select id=by><option value=kind>what they are</option>
   <option value=status>recognised or not</option></select></label>
  <label><input type=checkbox id=off> Show offline devices <span id=noff></span></label>
 </div>
</div>
<div class=mapbox id=box><svg id=map role=img aria-label="Map of the local network: the router in the middle, devices grouped around it. The same devices are listed under the map when a group is chosen."></svg>
<div class=tip id=tip hidden></div></div>
<div class=legend><span><i class="sw known"></i>online</span><span><i class="sw private"></i>private address</span>
 <span><i class="sw unknown"></i>not recognised</span><span><i class="sw off"></i>offline</span>
 <span><i class="sw me"></i>this board</span><span><i class="sw ap"></i>access point</span></div>
<section class=info id=info aria-live=polite></section>
<p class=note>The board sees the network from where it sits: who answers on it, and what the
router and the devices say about themselves. It cannot see the cables, or which access point a
device is joined to, so every device hangs off the router here. Groups are guesses from names and
makers. Access points come from the Nearby page's Wi-Fi scans: other access points with your
network's name are mesh nodes or extenders, or something pretending to be one.</p>
<p class=foot><a href="/api/map">Map data</a> &middot; <a href="/api/devices">Device data</a></p>
</div>
<script>
var M=null,DV=[],sel=null,lastOk=0,pollT=0,W=0,L=null;
var NS='http://www.w3.org/2000/svg';
function put(k,v){try{localStorage.setItem('nm.map.'+k,v)}catch(e){}}
function get(k){try{return localStorage.getItem('nm.map.'+k)}catch(e){return null}}
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;')}
function ago(s){if(s<60)return s+' s';if(s<3600)return((s/60)|0)+' min';
 if(s<86400)return((s/3600)|0)+' h '+(((s%3600)/60)|0)+' min';
 return((s/86400)|0)+' d '+(((s%86400)/3600)|0)+' h'}
function ipnum(s){var p=String(s||'').split('.');
 return p.length==4?(+p[0])*16777216+(+p[1])*65536+(+p[2])*256+(+p[3]):0}
function shortv(v){if(!v)return '';
 var x=v.replace(/,/g,' ').replace(
  /\b(corporation|corp|company|technologies|technology|electronics|international|holdings|limited|ltd|inc|co|llc|gmbh|plc|pte|bv|nv|srl|spa|trading|incorporated|observed)\b\.?/gi,' ')
  .replace(/[()]/g,' ').replace(/\s+/g,' ').replace(/^[\s.]+|[\s.]+$/g,'');
 return x||v}
function dname(d){return d.hostname||(d.vendor?shortv(d.vendor)+' device':d.randomised
 ?'Private address':d.ip)}

// What a device probably is: its name first, which says the most, then its
// maker, which is often ambiguous and so only used where it is not.
var KINDS=[
 ['net','Network gear',/\b(router|gateway|modem|switch|access ?point|extender|repeater|mesh|deco|eero|orbi|unifi|ubnt|mikrotik|openwrt|fritz|velop|powerline|devolo)\b/,
  /routerboard|ubiquiti|mikrotik|netgear|tp-link|zyxel|avm|eero|plume|cisco|aruba|ruckus|draytek|sagemcom|technicolor|arris|linksys|devolo|schneider|apc by/],
 ['nas','Servers and storage',/(\bnas\b|server|synology|diskstation|qnap|truenas|unraid|proxmox|esxi|docker|homelab|pihole|pi-hole|home-?assistant)/,
  /synology|qnap|vmware|pcs systemtechnik|western digital|seagate/],
 ['pc','Computers',/(macbook|imac|mac-?mini|mac-?pro|mac-?studio|desktop|laptop|notebook|\bpc\b|thinkpad|latitude|\bxps\b|surface|ubuntu|debian|fedora|linux|raspberry|raspberrypi|\bpi\d?\b|\bnuc\b|workstation|chromebook)/,
  /raspberry|intel corp|dell|lenovo|micro-star|gigabyte|microsoft/],
 ['phone','Phones and tablets',/(iphone|ipad|android|galaxy|pixel|oneplus|xiaomi|redmi|poco|oppo|vivo|realme|honor|motorola|\bmoto\b|nokia|phone|tablet|\bsm-[a-z]\d)/,null],
 ['media','TV, media and games',/(\btv\b|-tv|tv-|roku|chromecast|fire ?tv|firestick|apple ?tv|appletv|shield|sonos|bose|denon|marantz|yamaha|webos|bravia|vizio|hisense|\btcl\b|kodi|plex|xbox|playstation|\bps[345]\b|nintendo|steam ?deck|homepod)/,
  /sonos|roku|bose|denon|vizio|hisense|nintendo|sony interactive|nvidia|valve/],
 ['print','Printers',/(printer|\bprint|epson|canon|brother|laserjet|officejet|deskjet|kyocera|xerox|lexmark|ricoh)/,
  /seiko epson|canon|brother|kyocera|xerox|lexmark|ricoh/],
 ['cam','Cameras',/(camera|\bcam\b|\bipc\b|doorbell|dahua|hikvision|ezviz|reolink|imou|wyze ?cam|eufy ?cam)/,
  /dahua|hangzhou huacheng|hikvision|ezviz|reolink|axis comm/],
 ['iot','Smart home',/(\besp[-_]?|esp32|esp8266|tuya|shelly|tasmota|sonoff|wled|\bhue\b|nest|\bring\b|arlo|tapo|kasa|meross|govee|ecobee|tado|netatmo|roborock|ecovacs|roomba|dreame|plug|bulb|light|sensor|thermostat|\becho\b|alexa|google-?home|broadlink|yeelight|aqara|lifx|nanoleaf|switchbot|zigbee|matter)/,
  /espressif|tuya|allterco|shelly|itead|signify|philips lighting|nest labs|ecobee|tado|netatmo|roborock|ecovacs|irobot|broadlink|yeelight|lumi united|lifx|nanoleaf|wiz connected|texas instruments|usr iot/]];
var EXTRA={priv:'Private addresses',other:'Not identified'};
var STATUS=[['unknown','Not recognised'],['private','Private addresses'],['known','Recognised'],['off','Offline']];
function kindOf(d){var n=(d.hostname||'').toLowerCase(),v=(d.vendor||'').toLowerCase(),i;
 for(i=0;i<KINDS.length;i++)if(n&&KINDS[i][2].test(n))return KINDS[i][0];
 for(i=0;i<KINDS.length;i++)if(v&&KINDS[i][3]&&KINDS[i][3].test(v))return KINDS[i][0];
 return d.randomised?'priv':'other'}
function title(k){for(var i=0;i<KINDS.length;i++)if(KINDS[i][0]==k)return KINDS[i][1];
 for(i=0;i<STATUS.length;i++)if(STATUS[i][0]==k)return STATUS[i][1];return EXTRA[k]||k}
function cls(d){return d.online?d.status:'off'}

// ---- layout ----
// The router in the middle, a bubble per group on a ring around it, the way
// out straight up. The ring grows until nothing overlaps; on a narrow screen
// it is squeezed sideways and stretched down, so text stays readable rather
// than everything shrinking to fit.
var DOT=6.5,GAP=26,HUB=21;
function packed(n,c){var out=[],k;for(k=0;k<n;k++){if(n==1){out.push([0,0]);break}
 var r=c*Math.sqrt(k+.5),a=k*2.39996;out.push([r*Math.cos(a),r*Math.sin(a)])}return out}
function groups(){var by=document.getElementById('by').value,showOff=off.checked,gw=M.gateway,g={},order=[];
 DV.forEach(function(d){if(d.self||d.ip==gw)return;if(!d.online&&!showOff)return;
  var k=by=='status'?cls(d):kindOf(d);(g[k]||(g[k]=[])).push(d)});
 if(by=='status')order=STATUS.map(function(s){return s[0]});
 else order=KINDS.map(function(k){return k[0]}).concat(['priv','other']);
 var out=[];
 // Your Wi-Fi first, up and to the right: its access points and this board.
 var aps=(M.aps||[]).slice().sort(function(a,b){return b.joined-a.joined||b.rssi-a.rssi});
 var me=DV.filter(function(d){return d.self})[0]||{ip:M.ip,mac:M.mac,hostname:M.hostname,self:true,
  online:true,status:'known'};
 if(M.wifi=='connected')out.push({key:'wifi',title:M.ssid?'Wi-Fi “'+M.ssid+'”':'Wi-Fi',wifi:true,aps:aps,me:me,
  items:aps.map(function(a){return {ap:a}}).concat([{me:me}])});
 order.forEach(function(k){if(g[k])out.push({key:k,title:title(k),items:g[k].sort(function(a,b){
  return(b.online-a.online)||ipnum(a.ip)-ipnum(b.ip)})})});
 // On a narrow screen long names go on two lines, so the bubbles at the
 // edges do not need the width of a whole name each.
 var maxc=W<560?12:34;
 out.forEach(function(c){var n=c.items.length;c.lines=wrap(c.title,maxc);
  c.lw=Math.max(54,Math.max.apply(null,c.lines.map(function(s){return s.length}))*6.6);
  c.lh=c.lines.length*13+16;
  if(!c.wifi){c.at=packed(n,9.6);c.r=Math.max(24,9.6*Math.sqrt(n)+12);return}
  // This board in the middle, its network's access points round it.
  var k=c.aps.length,rho=k<2?0:Math.max(30,13/Math.sin(Math.PI/k));
  c.at=c.aps.map(function(a,i){if(k==1)return[-15,0];var t=-Math.PI/2+i*2*Math.PI/k;
   return[rho*Math.cos(t),rho*Math.sin(t)]}).concat([k==1?[15,0]:[0,0]]);
  c.r=k==1?36:k==0?22:rho+18});
 return out}
function wrap(s,n){if(s.length<=n)return[s];var w=s.split(' '),out=[],line='';
 w.forEach(function(x){if(line&&(line+' '+x).length>n){out.push(line);line=x}else line=line?line+' '+x:x});
 if(line)out.push(line);if(out.length>2)out=[out[0],clip(out.slice(1).join(' '),n)];
 return out.map(function(l){return clip(l,n+4)})}
function boxes(c){return {x0:c.x-c.lw/2,x1:c.x+c.lw/2,y0:c.y+c.r+2,y1:c.y+c.r+2+c.lh}}
function hitRect(a,b){return a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1}
function hitCircle(c,b){var x=Math.max(b.x0,Math.min(c.x,b.x1)),y=Math.max(b.y0,Math.min(c.y,b.y1));
 return Math.hypot(c.x-x,c.y-y)<c.r+4}
function layout(cs,ex,ey){var n=cs.length,TAU=2*Math.PI,IW=134,IH=38,R,tries,i,j,ok;
 var total=0;cs.forEach(function(c){total+=2*c.r+GAP});
 R=Math.max(HUB+70,total/TAU*1.05);
 for(tries=0;tries<90;tries++){
  // Leave room straight up for the line out to the internet.
  var keep=Math.asin(Math.min(.95,(IW/2+12)/R)),span=TAU-2*keep,a=-Math.PI/2+keep;
  var w=cs.map(function(c){return 2*c.r+GAP}),sum=w.reduce(function(s2,v){return s2+v},0)||1;
  cs.forEach(function(c,k){var share=span*w[k]/sum;c.a=a+share/2;a+=share;
   c.x=R*ex*Math.cos(c.a);c.y=R*ey*Math.sin(c.a)});
  ok=true;
  for(i=0;i<n&&ok;i++){var c=cs[i],bc=boxes(c);
   if(Math.hypot(c.x,c.y)<HUB+c.r+36)ok=false;
   if(Math.abs(c.x)<HUB+c.lw/2&&bc.y0<HUB+34&&bc.y1>-HUB)ok=false;
   if(c.y<0&&Math.abs(c.x)<c.r+8)ok=false;
   if(bc.y0<0&&Math.abs(c.x)<c.lw/2+4)ok=false;
   for(j=0;j<n&&ok;j++){if(j==i)continue;var e=cs[j],be=boxes(e);
    if(j>i&&Math.hypot(c.x-e.x,c.y-e.y)<c.r+e.r+10)ok=false;
    if(hitCircle(e,bc)||(j>i&&hitRect(bc,be)))ok=false}}
  if(ok)break;R*=1.06}
 var top=-HUB;cs.forEach(function(c){top=Math.min(top,c.y-c.r)});
 var iy=Math.min(top-IH/2-16,-HUB-64),x0=-IW/2,x1=IW/2,y0=iy-IH/2,y1=HUB+34;
 cs.forEach(function(c){var h=Math.max(c.r,c.lw/2);x0=Math.min(x0,c.x-h);x1=Math.max(x1,c.x+h);
  y0=Math.min(y0,c.y-c.r);y1=Math.max(y1,c.y+c.r+c.lh+4)});
 return {R:R,iy:iy,IW:IW,IH:IH,box:[x0-12,y0-12,x1-x0+24,y1-y0+24]}}
function plan(){var best=null,k;
 var tries=W<560?[[1,1],[.86,1.18],[.74,1.36],[.62,1.6],[.5,1.9],[.42,2.2]]:[[1,1],[1.15,.9]];
 for(k=0;k<tries.length;k++){var cs=groups(),l=layout(cs,tries[k][0],tries[k][1]);
  l.cs=cs;l.scale=W/l.box[2];
  if(!best||l.scale>best.scale+.04)best=l;if(l.scale>=.9)break}
 // Never drawn larger than 1.25 times: wider screens get more room round it.
 var b=best.box;if(W/b[2]>1.25){var extra=W/1.25-b[2];b[0]-=extra/2;b[2]+=extra}
 return best}

// ---- drawing ----
function el(name,attrs,parent){var e=document.createElementNS(NS,name);
 for(var k in attrs)e.setAttribute(k,attrs[k]);if(parent)parent.appendChild(e);return e}
function txt(x,y,s,c,parent,anchor){var t=el('text',{x:x,y:y,'class':c,'text-anchor':anchor||'middle'},parent);
 t.textContent=s;return t}
function clip(s,n){return s.length>n?s.slice(0,n-1)+'…':s}
var GLYPH={router:'M-9 1h18v7h-18zM-5 1v-6M5 1v-6M-5 4.5h1M-1 4.5h1',
 globe:'M0 -9a9 9 0 1 0 0.01 0zM-9 0h18M0 -9c-4.5 4 -4.5 14 0 18M0 -9c4.5 4 4.5 14 0 18',
 wifi:'M-6 -1a8.5 8.5 0 0 1 12 0M-3.4 2a4.8 4.8 0 0 1 6.8 0M0 5.2v.1'};
function draw(){var svg=document.getElementById('map');W=svg.clientWidth||box.clientWidth;
 while(svg.firstChild)svg.removeChild(svg.firstChild);
 if(!M)return;
 if(M.wifi!='connected'){svg.setAttribute('viewBox','0 0 320 120');svg.style.height='120px';
  txt(160,64,M.wifi=='softap'?'In setup mode: no network to map yet.':'Joining the network…','t2',svg);L=null;return}
 L=plan();var b=L.box;svg.setAttribute('viewBox',b.join(' '));
 svg.style.height=Math.round(b[3]*W/b[2])+'px';
 var gl=el('g',{},svg),gc=el('g',{},svg),gn=el('g',{},svg);
 // the way out
 el('line',{x1:0,y1:-HUB,x2:0,y2:L.iy+L.IH/2,'class':'lk'+(sel&&sel.t=='net'?' on':''),
  'stroke-width':2.2},gl);
 var ib=el('g',{'data-k':'net',tabindex:0,role:'button','aria-label':'The internet'},gn);
 el('rect',{x:-L.IW/2,y:L.iy-L.IH/2,width:L.IW,height:L.IH,rx:12,'class':'hub'+(sel&&sel.t=='net'?' on':'')},ib);
 el('path',{d:GLYPH.globe,transform:'translate('+(-L.IW/2+20)+','+L.iy+') scale(.8)','class':'gl'},ib);
 txt(-L.IW/2+36,L.iy-2,'Internet','t1',ib,'start');
 txt(-L.IW/2+36,L.iy+11,clip(M.isp&&M.isp.checked&&M.isp.valid?shortv(M.isp.isp||M.isp.org):'not looked up yet',18),'t2',ib,'start');
 L.cs.forEach(function(c){var on=sel&&sel.t=='g'&&sel.k==c.key,
  wd=c.wifi?2.4:Math.min(5,1.2+Math.sqrt(c.items.length)*.7);
  el('line',{x1:0,y1:0,x2:c.x,y2:c.y,'class':'lk'+(on?' on':''),'stroke-width':wd},gl);
  var g=el('g',{'data-k':c.key,tabindex:0,role:'button','aria-label':c.title+', '+c.items.length
   +(c.wifi?'':' devices')},gc);
  el('circle',{cx:c.x,cy:c.y,r:c.r,'class':'cl'+(on?' on':'')},g);
  var n=c.wifi?c.aps.length+' access point'+(c.aps.length==1?'':'s'):c.items.length+' device'
   +(c.items.length==1?'':'s');
  c.lines.forEach(function(s,k){txt(c.x,c.y+c.r+14+13*k,s,'t1',g)});
  txt(c.x,c.y+c.r+14+13*c.lines.length,n,'t2',g);
  var joined=null;
  c.items.forEach(function(it,k){var px=c.x+c.at[k][0],py=c.y+c.at[k][1];it.x=px;it.y=py;
   if(it.ap){var a=it.ap;if(a.joined)joined=it;
    var ag=el('g',{'class':'ap'+(a.live?'':' gone')},gn);el('circle',{cx:px,cy:py,r:10},ag);
    el('path',{d:GLYPH.wifi,transform:'translate('+px+','+py+') scale(.85)','class':'gl'},gn)}
   else if(it.me)el('circle',{cx:px,cy:py,r:DOT+.5,'class':'dv me'},gn);
   else el('circle',{cx:px,cy:py,r:DOT,'class':'dv '+cls(it)},gn)});
  if(c.wifi){var me=c.items[c.items.length-1];
   if(joined)el('line',{x1:joined.x,y1:joined.y,x2:me.x,y2:me.y,'class':'air'},gc)}});
 // the router
 var rg=el('g',{'data-k':'router',tabindex:0,role:'button','aria-label':'Router, '+M.gateway},gn);
 el('circle',{cx:0,cy:0,r:HUB,'class':'hub'+(sel&&sel.t=='router'?' on':'')},rg);
 el('path',{d:GLYPH.router,transform:'scale(1)','class':'gl'},rg);
 txt(0,HUB+14,'Router','t1',rg);txt(0,HUB+26,M.gateway,'t2',rg);
 // what is selected
 var s=locate(sel);if(s&&s.x!=null)el('circle',{cx:s.x,cy:s.y,r:s.ap?14:11,'class':'sel'},gn);
 if(s&&s.x!=null&&!s.ap&&!s.me)txt(s.x,s.y-15,clip(dname(s),24),'t1',gn)}
// The device or access point a selection points at, with its place on the map.
function locate(s){if(!s||!L||(s.t!='d'&&s.t!='ap'&&s.t!='me'))return null;var hit=null;
 L.cs.forEach(function(c){c.items.forEach(function(it){
  if(s.t=='d'&&!it.ap&&!it.me&&it.mac==s.k)hit=it;
  if(s.t=='ap'&&it.ap&&it.ap.bssid==s.k)hit={x:it.x,y:it.y,ap:it.ap};
  if(s.t=='me'&&it.me)hit={x:it.x,y:it.y,me:it.me}})});return hit}

// ---- pointing ----
// Dots are small and packed, so a tap goes to the nearest within reach
// rather than needing to land on one.
function at(e){var svg=document.getElementById('map'),r=svg.getBoundingClientRect();if(!L)return null;
 var b=L.box,k=b[2]/r.width,x=b[0]+(e.clientX-r.left)*k,y=b[1]+(e.clientY-r.top)*k,best=null,bd=(18*k)*(18*k);
 L.cs.forEach(function(c){c.items.forEach(function(it){var dx=it.x-x,dy=it.y-y,d=dx*dx+dy*dy;
  if(d<bd){bd=d;best=it.ap?{t:'ap',k:it.ap.bssid,it:it}:it.me?{t:'me',k:'me',it:it}:{t:'d',k:it.mac,it:it}}})});
 if(best)return best;
 if(x*x+y*y<(HUB+6)*(HUB+6))return {t:'router'};
 if(Math.abs(x)<L.IW/2&&Math.abs(y-L.iy)<L.IH/2)return {t:'net'};
 for(var i=0;i<L.cs.length;i++){var c=L.cs[i];if(Math.hypot(c.x-x,c.y-y)<c.r+4||
  (Math.abs(c.x-x)<c.lw/2&&y>c.y+c.r&&y<c.y+c.r+c.lh))return {t:'g',k:c.key}}
 return null}
function choose(s){sel=s?{t:s.t,k:s.k}:null;tip.hidden=true;draw();info()}
map.addEventListener('click',function(e){choose(at(e))});
map.addEventListener('pointermove',function(e){if(e.pointerType!='mouse')return;var s=at(e);
 if(!s||!s.it){tip.hidden=true;map.style.cursor=s?'pointer':'default';return}
 map.style.cursor='pointer';var it=s.it,r=box.getBoundingClientRect(),m=map.getBoundingClientRect(),
  k=m.width/L.box[2];
 tip.innerHTML=it.ap?'<b>'+(it.ap.joined?'This board joins here':'Access point')+'</b><br>channel '+it.ap.ch+' · '+it.ap.rssi+' dBm'
  :it.me?'<b>netmon</b> (this board)<br>'+esc(M.ip):'<b>'+esc(dname(it))+'</b><br>'+esc(it.ip)+(it.online?'':' · offline');
 tip.style.left=(m.left-r.left+(it.x-L.box[0])*k)+'px';tip.style.top=(m.top-r.top+(it.y-L.box[1])*k)+'px';
 tip.hidden=false});
map.addEventListener('pointerleave',function(){tip.hidden=true});
map.addEventListener('keydown',function(e){if(e.key!='Enter'&&e.key!=' ')return;
 var g=e.target.closest&&e.target.closest('[data-k]');if(!g)return;e.preventDefault();
 var k=g.getAttribute('data-k');choose(k=='net'||k=='router'?{t:k}:{t:'g',k:k})});

// ---- the panel under the map ----
var STAT={known:'Recognised: first seen while the board was learning the network after it started',
 private:'Private address: the device makes up its own address, as phones and laptops do for privacy, '
  +'so the board cannot tell whether it has seen it before',
 unknown:'Not recognised: first seen after the board finished learning the network. Worth a look if '
  +'you do not know what it is'};
function devLine(d){return '<li><button type=button data-mac="'+esc(d.mac)+'"><i class="sw '+cls(d)
 +'"></i><span><b>'+esc(dname(d))+'</b><small>'+esc([d.ip,d.vendor?shortv(d.vendor):''].filter(String).join(' · '))
 +'</small></span><span class=r>'+(d.online?(d.up_s?'up '+ago(d.up_s):'online'):'seen '+ago(d.last_seen_s)+' ago')
 +'</span></button></li>'}
function info(){var o=document.getElementById('info'),h='';if(!M){o.innerHTML='';return}
 var online=DV.filter(function(d){return d.online}).length,offn=DV.length-online,
  bad=DV.filter(function(d){return d.status=='unknown'}),gwd=DV.filter(function(d){return d.ip==M.gateway})[0];
 if(!sel){h='<h2>Your network <span>'+esc(M.subnet)+'</span></h2><p>'+online+' of '+DV.length
  +' devices online'+(offn&&!off.checked?', '+offn+' offline not shown':'')+'. Tap a group, a dot or the router for details.</p>'
  +(bad.length?'<p class=warn>'+(bad.length==1?'One device is':bad.length+' devices are')+' not recognised: the red dots.</p>':'')}
 else if(sel.t=='net'){var i=M.isp||{};h='<h2>Internet</h2><p>'+(i.checked&&i.valid
   ?'Through '+esc(i.isp||i.org)+(i.org&&i.org!=i.isp?' ('+esc(i.org)+')':'')+', as of '+ago(i.age_s)+' ago.'
   :'Not looked up yet. The Internet page asks who your connection belongs to when you open it.')
  +'</p><div class=acts><a href="/isp">Internet page</a></div>'}
 else if(sel.t=='router'){h='<h2>Router <span>'+esc(M.gateway)+'</span></h2><p>'
  +(gwd?esc(gwd.vendor?shortv(gwd.vendor):'Maker not in the board’s list')+' · '+esc(gwd.mac):'Not answered the sweep yet.')
  +'</p><p class=mut>'+(M.latency_valid?'Answers this board in '+M.latency_ms+' ms.':'No round trip measured yet.')
  +' Everything on the map reaches the internet through it.</p>'}
 else if(sel.t=='me'){h='<h2>netmon <span>this board</span></h2><p>'+esc(M.ip)+' · '+esc(M.mac)+'</p><p class=mut>Joined to “'
  +esc(M.ssid)+'” on channel '+M.channel+' at '+M.rssi+' dBm. Firmware '+esc(M.version)+', up '+ago(M.uptime_s)+'.</p>'}
 else if(sel.t=='ap'){var a=(M.aps||[]).filter(function(x){return x.bssid==sel.k})[0];
  if(a)h='<h2>Access point <span>'+esc(a.bssid)+'</span></h2><p>“'+esc(M.ssid)+'” on channel '+a.ch+', '+a.rssi+' dBm'
   +(a.joined?'. This board is joined to it.':', heard '+(a.age_s<5?'just now':ago(a.age_s)+' ago')+(a.live?'':', and not in range now')+'.')+'</p>'
   +(a.joined?'':'<p class=mut>Another access point with your network’s name: a mesh node or an extender, or something pretending to be one.</p>')
   +'<div class=acts><a href="/nearby#wifi">Wi-Fi nearby</a><a href="/nearby#finder" data-find="'+esc(a.bssid)+'">Find it with the board</a></div>'}
 else if(sel.t=='d'){var d=DV.filter(function(x){return x.mac==sel.k})[0];
  if(d)h='<h2>'+esc(dname(d))+' <span>'+esc(title(kindOf(d)))+'</span></h2><p>'+esc(d.ip)+' · '+esc(d.mac)
   +(d.vendor?' · '+esc(d.vendor):'')+'</p><p class=mut>'+(STAT[d.status]||d.status)+'. '+(d.online
   ?'Online'+(d.up_s?' for '+ago(d.up_s):'')+'.':'Offline, last seen '+ago(d.last_seen_s)+' ago.')+'</p>'
   +'<div class=acts><a href="/?q='+encodeURIComponent(d.ip)+'">Show in Devices</a></div>'}
 else if(sel.t=='g'){var c=L&&L.cs.filter(function(x){return x.key==sel.k})[0];
  if(c&&c.wifi){h='<h2>'+esc(c.title)+' <span>'+c.aps.length+' access point'+(c.aps.length==1?'':'s')+'</span></h2>'
   +(M.nearby_wifi?'':'<p class=mut>Wi-Fi scanning is off on the Nearby page, so only the access point this board is joined to is known.</p>')
   +'<ul>'+c.aps.map(function(a){return '<li><button type=button data-ap="'+esc(a.bssid)+'"><i class="sw ap"></i><span><b>'
    +(a.joined?'This board joins here':'Another access point')+'</b><small>'+esc(a.bssid)+' · channel '+a.ch
    +'</small></span><span class=r>'+a.rssi+' dBm</span></button></li>'}).join('')+'</ul>'}
  else if(c)h='<h2>'+esc(c.title)+' <span>'+c.items.length+'</span></h2><ul>'+c.items.map(devLine).join('')+'</ul>'}
 if(!h){sel=null;return info()}
 if(o._h!==h){o.innerHTML=h;o._h=h}}
document.getElementById('info').addEventListener('click',function(e){
 var b=e.target.closest&&e.target.closest('button[data-mac],button[data-ap],a[data-find]');if(!b)return;
 if(b.hasAttribute('data-find')){try{localStorage.setItem('nm.nb.ft',JSON.stringify({type:'wifi',
  addr:b.getAttribute('data-find')}))}catch(x){}return}
 e.preventDefault();
 if(b.hasAttribute('data-mac'))choose({t:'d',k:b.getAttribute('data-mac')});
 else choose({t:'ap',k:b.getAttribute('data-ap')})});

// ---- data ----
function summary(){var online=DV.filter(function(d){return d.online}).length,
 bad=DV.filter(function(d){return d.status=='unknown'&&d.online}).length,offn=DV.length-online;
 count.textContent=M.wifi!='connected'?'No network yet':online+' of '+DV.length+' devices online';
 state.className='state'+(bad?' flag':'');
 state.textContent=M.wifi!='connected'?(M.wifi=='softap'?'The board is in setup mode.':'Joining the network.')
  :bad?(bad==1?'One device online is not recognised.':bad+' devices online are not recognised.')
  :(M.ssid?'“'+M.ssid+'” · ':'')+M.subnet+' · router '+M.gateway;
 noff.textContent=offn?'('+offn+')':'';ver.textContent=M.version||''}
function load(){clearTimeout(pollT);
 Promise.all([fetch('/api/map',{cache:'no-store'}).then(function(r){return r.json()}),
  fetch('/api/devices',{cache:'no-store'}).then(function(r){return r.json()})])
 .then(function(v){M=v[0];DV=v[1];lastOk=Date.now();summary();draw();info()})
 .catch(function(){if(Date.now()-lastOk>20000){count.textContent='Cannot reach the monitor';
  state.textContent='';}})
 .then(function(){if(!document.hidden)pollT=setTimeout(load,15000)})}
by.value=get('by')=='status'?'status':'kind';off.checked=get('off')=='1';
by.onchange=function(){put('by',by.value);sel=null;draw();info()};
off.onchange=function(){put('off',off.checked?'1':'');draw();info()};
var rt=0;window.addEventListener('resize',function(){clearTimeout(rt);rt=setTimeout(draw,120)});
document.addEventListener('visibilitychange',function(){if(!document.hidden)load()});
load();
</script>
)HTML";
