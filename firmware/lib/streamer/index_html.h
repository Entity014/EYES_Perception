#pragma once
// Served at GET / . Single file, no external assets.
static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>XIAO Cam</title>
<style>
 body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#eee;text-align:center}
 img{max-width:100%;height:auto;display:block;margin:0 auto;background:#000}
 button{font-size:1.2rem;padding:.6rem 1.4rem;margin:.8rem;border:0;border-radius:.4rem;cursor:pointer}
 #rec{background:#c0392b;color:#fff}
 #rec.on{background:#27ae60}
 #st{font-size:.9rem;opacity:.85;min-height:1.2em}
</style></head><body>
<img src="/stream" alt="stream">
<div><button id="rec">Record</button></div>
<div id="st">&hellip;</div>
<script>
const rec=document.getElementById('rec'), st=document.getElementById('st');
async function refresh(){
 try{const r=await fetch('/status'); const j=await r.json();
  rec.textContent=j.recording?'Stop':'Record';
  rec.classList.toggle('on',j.recording);
  st.textContent=`${j.recording?'REC '+j.file:'idle'} | ${j.fps.toFixed(1)} fps | `
   +`${j.clients} client(s) | ${j.sdOk? j.sdFreeMB+' MB free':'no SD'}`;
 }catch(e){ st.textContent='disconnected'; }
}
rec.onclick=async()=>{ rec.disabled=true; try{await fetch('/record',{method:'POST'});}catch(e){}
 rec.disabled=false; refresh(); };
setInterval(refresh,1000); refresh();
</script></body></html>
)HTML";
