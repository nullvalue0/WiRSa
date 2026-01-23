#include "web_ui.h"

// Complete SPA HTML page stored in PROGMEM
const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiRSa Configuration</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#0d0d1a;color:#eee;min-height:100vh}
.container{max-width:800px;margin:0 auto;padding:10px}
header{background:linear-gradient(90deg,#ff3377,#5566ff);padding:15px;text-align:center}
header h1{color:#fff;font-size:1.5em;text-shadow:2px 2px 4px rgba(0,0,0,0.5)}
nav{display:flex;background:#1a1a2e;border-radius:8px;margin:15px 0;overflow:hidden;border:1px solid #2a2a4e}
nav button{flex:1;padding:12px;border:none;background:transparent;color:#eee;cursor:pointer;font-size:1em;transition:background 0.2s}
nav button:hover{background:#2a2a4e}
nav button.active{background:linear-gradient(90deg,#ff3377,#5566ff);color:#fff}
.tab{display:none;background:#1a1a2e;border-radius:8px;padding:15px;border:1px solid #2a2a4e}
.tab.active{display:block}
.section{margin-bottom:20px;padding:15px;background:#12121f;border-radius:6px;border:1px solid #2a2a4e}
.section h3{color:#ff5599;margin-bottom:10px;font-size:1.1em;border-bottom:1px solid #2a2a4e;padding-bottom:5px}
.row{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:10px}
.field{flex:1;min-width:200px}
.field label{display:block;margin-bottom:4px;color:#99aacc;font-size:0.9em}
.field input,.field select{width:100%;padding:8px;border:1px solid #2a2a4e;border-radius:4px;background:#0d0d1a;color:#eee}
.field input:focus,.field select:focus{outline:none;border-color:#ff5599}
.toggle{display:flex;align-items:center;gap:8px;padding:8px 0}
.toggle input[type="checkbox"]{width:18px;height:18px;accent-color:#ff3377}
.btn{padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:1em;transition:opacity 0.2s}
.btn:hover{opacity:0.85}
.btn-primary{background:linear-gradient(90deg,#ff3377,#cc3388);color:#fff}
.btn-secondary{background:#2a2a4e;color:#eee}
.btn-danger{background:#aa2244;color:#fff}
.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px}
.status-item{padding:10px;background:#12121f;border-radius:4px;border:1px solid #2a2a4e}
.status-item .label{color:#99aacc;font-size:0.85em}
.status-item .value{color:#7799ff;font-size:1.1em;word-break:break-all}
.file-list{width:100%;border-collapse:collapse}
.file-list th,.file-list td{padding:10px;text-align:left;border-bottom:1px solid #2a2a4e}
.file-list th{color:#99aacc;font-size:0.9em}
.file-actions{display:flex;gap:5px}
.upload-area{border:2px dashed #2a2a4e;border-radius:8px;padding:30px;text-align:center;margin-bottom:15px;cursor:pointer;transition:border-color 0.2s}
.upload-area:hover,.upload-area.dragover{border-color:#ff5599}
.upload-area input{display:none}
.progress{height:20px;background:#2a2a4e;border-radius:10px;overflow:hidden;margin-top:10px;display:none}
.progress-bar{height:100%;background:linear-gradient(90deg,#ff3377,#5566ff);transition:width 0.3s}
.msg{padding:10px;border-radius:4px;margin:10px 0;display:none}
.msg.success{display:block;background:#227755;color:#fff}
.msg.error{display:block;background:#aa2244;color:#fff}
@media(max-width:600px){.row{flex-direction:column}.field{min-width:100%}}
</style>
</head>
<body>
<header><h1>RetroDisks WiRSa WiFi Modem</h1></header>
<div class="container">
<nav>
<button onclick="showTab('status')" id="btn-status" class="active">Status</button>
<button onclick="showTab('settings')" id="btn-settings">Settings</button>
<button onclick="showTab('files')" id="btn-files">Files</button>
</nav>

<div id="status" class="tab active">
<div class="section">
<h3>Device Status</h3>
<div id="status-content" class="status-grid">Loading...</div>
</div>
</div>

<div id="settings" class="tab">
<div id="settings-msg" class="msg"></div>

<div class="section">
<h3>WiFi Configuration</h3>
<div class="row">
<div class="field"><label>SSID</label><input type="text" id="wifi-ssid" maxlength="32"></div>
<div class="field"><label>Password</label><input type="password" id="wifi-pass" maxlength="63" placeholder="Leave empty to keep current"></div>
</div>
</div>

<div class="section">
<h3>Serial Configuration</h3>
<div class="row">
<div class="field"><label>Baud Rate</label><select id="serial-baud"></select></div>
<div class="field"><label>Data/Parity/Stop</label>
<select id="serial-config">
<option value="0">5-N-1</option><option value="1">6-N-1</option><option value="2">7-N-1</option><option value="3">8-N-1</option>
<option value="4">5-N-2</option><option value="5">6-N-2</option><option value="6">7-N-2</option><option value="7">8-N-2</option>
<option value="8">5-E-1</option><option value="9">6-E-1</option><option value="10">7-E-1</option><option value="11">8-E-1</option>
<option value="12">5-E-2</option><option value="13">6-E-2</option><option value="14">7-E-2</option><option value="15">8-E-2</option>
<option value="16">5-O-1</option><option value="17">6-O-1</option><option value="18">7-O-1</option><option value="19">8-O-1</option>
<option value="20">5-O-2</option><option value="21">6-O-2</option><option value="22">7-O-2</option><option value="23">8-O-2</option>
</select></div>
</div>
</div>

<div class="section">
<h3>Modem Settings</h3>
<div class="row">
<div class="toggle"><input type="checkbox" id="modem-echo"><label for="modem-echo">Echo (E)</label></div>
<div class="toggle"><input type="checkbox" id="modem-verbose"><label for="modem-verbose">Verbose Results (V)</label></div>
<div class="toggle"><input type="checkbox" id="modem-autoanswer"><label for="modem-autoanswer">Auto Answer (S0)</label></div>
</div>
<div class="row">
<div class="toggle"><input type="checkbox" id="modem-telnet"><label for="modem-telnet">Telnet Protocol (NET)</label></div>
<div class="toggle"><input type="checkbox" id="modem-petscii"><label for="modem-petscii">PETSCII Translation (PET)</label></div>
</div>
</div>

<div class="section">
<h3>Flow Control</h3>
<div class="row">
<div class="field"><label>Flow Control Mode</label>
<select id="flow-control">
<option value="0">None</option>
<option value="1">Hardware (RTS/CTS)</option>
<option value="2">Software (XON/XOFF)</option>
</select></div>
<div class="field"><label>Pin Polarity</label>
<select id="flow-polarity">
<option value="0">Inverted (LOW active)</option>
<option value="1">Normal (HIGH active)</option>
</select></div>
</div>
</div>

<div class="section">
<h3>Network</h3>
<div class="row">
<div class="field"><label>Telnet Server Port</label><input type="number" id="net-port" min="1" max="65535"></div>
<div class="field"><label>Busy Message</label><input type="text" id="net-busy" maxlength="80"></div>
</div>
</div>

<div class="section">
<h3>Speed Dials</h3>
<div id="speed-dials"></div>
</div>

<div class="section">
<h3>Display</h3>
<div class="row">
<div class="field"><label>Orientation</label>
<select id="disp-orient">
<option value="0">Normal</option>
<option value="1">Flipped (180)</option>
</select></div>
<div class="field"><label>Default Mode</label>
<select id="disp-mode">
<option value="0">Main Menu</option>
<option value="1">Modem Mode</option>
</select></div>
</div>
</div>

<div class="row" style="justify-content:flex-end;gap:10px;margin-top:20px">
<button class="btn btn-secondary" onclick="loadSettings()">Reset</button>
<button class="btn btn-primary" onclick="saveSettings()">Save Settings</button>
<button class="btn btn-danger" onclick="rebootDevice()">Reboot</button>
</div>
</div>

<div id="files" class="tab">
<div id="files-msg" class="msg"></div>
<div class="section">
<h3>SD Card Files</h3>
<div id="sd-info" style="margin-bottom:15px;color:#aaa"></div>
<div class="upload-area" id="upload-area" onclick="document.getElementById('file-input').click()">
<p>Click or drag files here to upload</p>
<input type="file" id="file-input" onchange="uploadFile(this.files[0])">
</div>
<div class="progress" id="upload-progress"><div class="progress-bar" id="progress-bar"></div></div>
<table class="file-list">
<thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead>
<tbody id="file-tbody"></tbody>
</table>
</div>
</div>
</div>

<script>
let statusInterval;
function showTab(t){
document.querySelectorAll('.tab').forEach(e=>e.classList.remove('active'));
document.querySelectorAll('nav button').forEach(e=>e.classList.remove('active'));
document.getElementById(t).classList.add('active');
document.getElementById('btn-'+t).classList.add('active');
if(t==='status'){loadStatus();statusInterval=setInterval(loadStatus,5000);}
else{clearInterval(statusInterval);}
if(t==='settings')loadSettings();
if(t==='files')loadFiles();
}

function loadStatus(){
fetch('/api/status').then(r=>r.json()).then(d=>{
let h='';
h+='<div class="status-item"><div class="label">Firmware Version</div><div class="value">'+d.version+'</div></div>';
h+='<div class="status-item"><div class="label">WiFi Status</div><div class="value">'+d.wifiStatus+'</div></div>';
h+='<div class="status-item"><div class="label">SSID</div><div class="value">'+d.ssid+'</div></div>';
h+='<div class="status-item"><div class="label">IP Address</div><div class="value">'+d.ip+'</div></div>';
h+='<div class="status-item"><div class="label">MAC Address</div><div class="value">'+d.mac+'</div></div>';
h+='<div class="status-item"><div class="label">Gateway</div><div class="value">'+d.gateway+'</div></div>';
h+='<div class="status-item"><div class="label">Signal (RSSI)</div><div class="value">'+d.rssi+' dBm</div></div>';
h+='<div class="status-item"><div class="label">Server Port</div><div class="value">'+d.serverPort+'</div></div>';
h+='<div class="status-item"><div class="label">Connection</div><div class="value">'+(d.callConnected?'Connected to '+d.remoteIp:'Not connected')+'</div></div>';
if(d.callConnected)h+='<div class="status-item"><div class="label">Call Length</div><div class="value">'+d.callLength+'</div></div>';
h+='<div class="status-item"><div class="label">Free Heap</div><div class="value">'+(d.freeHeap/1024).toFixed(1)+' KB</div></div>';
h+='<div class="status-item"><div class="label">Uptime</div><div class="value">'+formatUptime(d.uptime)+'</div></div>';
document.getElementById('status-content').innerHTML=h;
}).catch(e=>document.getElementById('status-content').innerHTML='<p style="color:#ff5599">Error loading status</p>');
}

function formatUptime(s){
let d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60),sec=s%60;
return (d>0?d+'d ':'')+(h>0?h+'h ':'')+(m>0?m+'m ':'')+(sec+'s');
}

function loadSettings(){
fetch('/api/settings').then(r=>r.json()).then(d=>{
document.getElementById('wifi-ssid').value=d.wifi.ssid;
document.getElementById('wifi-pass').value='';
let bs=document.getElementById('serial-baud');
bs.innerHTML='';
d.baudOptions.forEach((b,i)=>{let o=document.createElement('option');o.value=i;o.text=b;bs.add(o);});
bs.value=d.serial.baudIndex;
document.getElementById('serial-config').value=d.serial.configIndex;
document.getElementById('modem-echo').checked=d.modem.echo;
document.getElementById('modem-verbose').checked=d.modem.verbose;
document.getElementById('modem-autoanswer').checked=d.modem.autoAnswer;
document.getElementById('modem-telnet').checked=d.modem.telnet;
document.getElementById('modem-petscii').checked=d.modem.petscii;
document.getElementById('flow-control').value=d.flow.control;
document.getElementById('flow-polarity').value=d.flow.pinPolarity;
document.getElementById('net-port').value=d.network.serverPort;
document.getElementById('net-busy').value=d.network.busyMsg;
let sd=document.getElementById('speed-dials');
sd.innerHTML='';
d.speedDials.forEach((v,i)=>{
sd.innerHTML+='<div class="row"><div class="field"><label>Dial '+i+'</label><input type="text" id="dial-'+i+'" value="'+v+'" maxlength="50"></div></div>';
});
document.getElementById('disp-orient').value=d.display.orientation;
document.getElementById('disp-mode').value=d.display.defaultMode;
});
}

function saveSettings(){
let data={
wifi:{ssid:document.getElementById('wifi-ssid').value},
serial:{baudIndex:parseInt(document.getElementById('serial-baud').value),configIndex:parseInt(document.getElementById('serial-config').value)},
modem:{echo:document.getElementById('modem-echo').checked,verbose:document.getElementById('modem-verbose').checked,autoAnswer:document.getElementById('modem-autoanswer').checked,telnet:document.getElementById('modem-telnet').checked,petscii:document.getElementById('modem-petscii').checked},
flow:{control:parseInt(document.getElementById('flow-control').value),pinPolarity:parseInt(document.getElementById('flow-polarity').value)},
network:{serverPort:parseInt(document.getElementById('net-port').value),busyMsg:document.getElementById('net-busy').value},
speedDials:[],
display:{orientation:parseInt(document.getElementById('disp-orient').value),defaultMode:parseInt(document.getElementById('disp-mode').value)}
};
let pw=document.getElementById('wifi-pass').value;
if(pw.length>0)data.wifi.password=pw;
for(let i=0;i<10;i++)data.speedDials.push(document.getElementById('dial-'+i).value);
fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
.then(r=>r.json()).then(d=>{
if(d.success){alert('Settings saved successfully!'+(d.needsRestart?'\n\nWiFi settings changed. Please reboot for changes to take effect.':''));}
else{let msg=document.getElementById('settings-msg');msg.className='msg error';msg.textContent='Error saving settings';setTimeout(()=>msg.className='msg',3000);}
});
}

function rebootDevice(){
if(confirm('Reboot the device?')){
fetch('/api/reboot',{method:'POST'}).then(()=>{
alert('Device is rebooting. Please wait...');
setTimeout(()=>location.reload(),10000);
});
}
}

function loadFiles(){
fetch('/api/files').then(r=>r.json()).then(d=>{
if(d.error){document.getElementById('file-tbody').innerHTML='<tr><td colspan="3">'+d.error+'</td></tr>';return;}
document.getElementById('sd-info').innerHTML='Used: '+(d.usedBytes/1024/1024).toFixed(2)+' MB / Total: '+(d.totalBytes/1024/1024).toFixed(2)+' MB';
let h='';
if(d.files.length===0)h='<tr><td colspan="3">No files</td></tr>';
else d.files.forEach(f=>{
h+='<tr><td>'+f.name+'</td><td>'+formatSize(f.size)+'</td><td class="file-actions">';
h+='<button class="btn btn-secondary" onclick="downloadFile(\''+f.name+'\')">Download</button>';
h+='<button class="btn btn-danger" onclick="deleteFile(\''+f.name+'\')">Delete</button></td></tr>';
});
document.getElementById('file-tbody').innerHTML=h;
}).catch(()=>document.getElementById('file-tbody').innerHTML='<tr><td colspan="3">Error loading files</td></tr>');
}

function formatSize(b){
if(b<1024)return b+' B';
if(b<1048576)return (b/1024).toFixed(1)+' KB';
return (b/1048576).toFixed(2)+' MB';
}

function downloadFile(n){window.location.href='/api/download?name='+encodeURIComponent(n);}

function deleteFile(n){
if(confirm('Delete '+n+'?')){
fetch('/api/files?name='+encodeURIComponent(n),{method:'DELETE'}).then(r=>r.json()).then(d=>{
let msg=document.getElementById('files-msg');
if(d.success){msg.className='msg success';msg.textContent='File deleted';loadFiles();}
else{msg.className='msg error';msg.textContent=d.error||'Error deleting file';}
setTimeout(()=>msg.className='msg',3000);
});
}
}

function uploadFile(f){
if(!f)return;
let fd=new FormData();
fd.append('file',f);
let xhr=new XMLHttpRequest();
let prog=document.getElementById('upload-progress');
let bar=document.getElementById('progress-bar');
prog.style.display='block';
xhr.upload.onprogress=e=>{if(e.lengthComputable)bar.style.width=(e.loaded/e.total*100)+'%';};
xhr.onload=()=>{
prog.style.display='none';bar.style.width='0%';
let msg=document.getElementById('files-msg');
if(xhr.status===200){msg.className='msg success';msg.textContent='File uploaded';loadFiles();}
else{msg.className='msg error';msg.textContent='Upload failed';}
setTimeout(()=>msg.className='msg',3000);
};
xhr.onerror=()=>{prog.style.display='none';alert('Upload failed');};
xhr.open('POST','/api/upload');
xhr.send(fd);
}

let ua=document.getElementById('upload-area');
['dragenter','dragover'].forEach(e=>ua.addEventListener(e,ev=>{ev.preventDefault();ua.classList.add('dragover');}));
['dragleave','drop'].forEach(e=>ua.addEventListener(e,ev=>{ev.preventDefault();ua.classList.remove('dragover');}));
ua.addEventListener('drop',e=>{if(e.dataTransfer.files.length)uploadFile(e.dataTransfer.files[0]);});

loadStatus();statusInterval=setInterval(loadStatus,5000);
</script>
</body>
</html>)rawliteral";
