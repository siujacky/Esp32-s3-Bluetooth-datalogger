// Real-device dashboard check. No scan, connect, profile, disconnect or focus commands.
// Usage: node test/web_meter/live.cjs http://192.168.4.1 [artifact-directory]
// Starts/stops one named device recording; retains its browser profile and exports.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawn}=require('node:child_process'),assert=require('node:assert/strict');
const origin=new URL(process.argv[2] || 'http://192.168.4.1').origin;
const eventsOnly=process.argv.includes('--events-only');
const artifacts=path.resolve(process.argv[3] || fs.mkdtempSync(path.join(os.tmpdir(),'meter-web-live-artifacts-')));fs.mkdirSync(artifacts,{recursive:true});
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
if(!chrome)throw new Error('Set METER_TEST_CHROME to Chromium.');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));let browser,ws,recordingStarted=false,messageId=0,sessionId='',profile='';const pending=new Map(),errors=[];
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const data=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(data.exceptionDetails)throw new Error(JSON.stringify(data.exceptionDetails));return data.result.value;}
async function waitFor(expression,timeout=15000){const end=Date.now()+timeout;while(Date.now()<end){try{if(await evaluate('Boolean('+expression+')'))return;}catch(error){if(!/Execution context was destroyed|Cannot find context/.test(error.message))throw error;}await sleep(200);}throw new Error('Timed out: '+expression);}
async function snapshot(name){const image=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(artifacts,name),Buffer.from(image.data,'base64'));}
const readSessions='(async()=>{const store=await MeterCore.openDatabase(indexedDB);try{return await store.listSessions();}finally{store.db.close();}})()';
async function readSession(id){return evaluate('(async()=>{const store=await MeterCore.openDatabase(indexedDB);try{return await store.readSession('+JSON.stringify(id)+');}finally{store.db.close();}})()');}
(async()=>{
  profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-web-live-browser-'));
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port='';for(let i=0;i<100;i++){try{const value=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(value)){port=value;break;}}catch(error){if(!['ENOENT','EBUSY'].includes(error.code))throw error;}await sleep(100);}assert(port,'Chromium did not start');
  const page=await (await fetch('http://127.0.0.1:'+port+'/json/new?about:blank',{method:'PUT'})).json();
  ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails);if(message.id && pending.has(message.id)){const p=pending.get(message.id);pending.delete(message.id);if(message.error)p.reject(new Error(JSON.stringify(message.error)));else p.resolve(message.result);}});
  await cdp('Runtime.enable');await cdp('Page.enable');await cdp('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:artifacts});
  await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1050,deviceScaleFactor:1,mobile:false});
  await cdp('Page.navigate',{url:origin+'/'});await waitFor('document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"',30000);
  await waitFor('document.getElementById("storage-state").textContent.includes("IndexedDB ready")');
  const before=await evaluate('(async()=>await (await fetch("/api/status")).json())()');
  assert(before.device && before.device.id,'Actual device identity missing');assert(!before.recording.active,'Another recording is active; real-device test will not interrupt it');
  const connected=(before.meters || []).filter(m=>m.state==='connected');assert(eventsOnly||connected.length,'No connected meter: parent must connect the real meter first');
  console.log('Actual device: '+before.device.id+' / '+before.device.bootId);console.log('Connected meters: '+connected.map(m=>m.name+' '+m.address+' '+m.profile).join(', '));
  const label='Browser hardware check '+new Date().toISOString();
  await evaluate('document.getElementById("session-label").value='+JSON.stringify(label)+';document.getElementById("record-start").click()');
  await waitFor('document.getElementById("recording-pill").textContent==="Recording"');recordingStarted=true;
  const running=await evaluate('(async()=>await (await fetch("/api/status")).json())()');sessionId=running.recording.id;
  const localKey=JSON.stringify([String(running.device.id),String(sessionId)]);
  let data;
  const end=Date.now()+45000;while(Date.now()<end){data=await readSession(localKey);if(data.session && (eventsOnly?data.session.eventCount>=1:data.session.sampleCount>=4))break;await sleep(500);}assert(data.session && (eventsOnly?data.session.eventCount>=1:data.session.sampleCount>=4),eventsOnly?'Recording start event did not commit':'Fewer than four real readings arrived in 45 seconds');
  await evaluate('document.getElementById("session-label").value="Browser hardware marker";document.getElementById("record-mark").click()');
  await sleep(800);await evaluate('document.getElementById("record-stop").click()');await waitFor('document.getElementById("recording-pill").textContent==="Idle"');recordingStarted=false;
  const stopEnd=Date.now()+15000;while(Date.now()<stopEnd){data=await readSession(localKey);if(data.frames.some(f=>f.event==='recording' && /stop/i.test(f.message || '')))break;await sleep(300);}
  assert(data.frames.some(f=>f.event==='mark' && f.message==='Browser hardware marker'),'Actual marker did not commit');assert(data.frames.some(f=>f.event==='recording' && /stop/i.test(f.message || '')),'Stop event did not commit');
  assert(data.frames.every(f=>f.sessionId===sessionId),'Saved a frame without the actual backend session tag');assert.equal(new Set(data.frames.map(f=>JSON.stringify([f.deviceId,f.bootId,f.seq]))).size,data.frames.length,'Duplicate saved frame');
  await snapshot('desktop-live.png');
  const count=data.session.sampleCount,eventCount=data.session.eventCount;
  const previousDocument=await evaluate('performance.timeOrigin');await cdp('Page.reload',{ignoreCache:true});await waitFor('performance.timeOrigin!=='+previousDocument+' && document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"');await waitFor('document.getElementById("storage-state").textContent.includes("IndexedDB ready")');
  const reloaded=await readSession(localKey);assert.equal(reloaded.session.sampleCount,count,'Reload changed saved sample count');assert.equal(reloaded.session.eventCount,eventCount,'Reload duplicated events');
  await evaluate('location.hash="sessions"');await waitFor('Array.from(document.querySelectorAll(".session-card h3")).some(e=>e.textContent==='+JSON.stringify(label)+')');
  for(const text of ['Export CSV','Export JSON'])await evaluate('(()=>{const row=Array.from(document.querySelectorAll(".session-card")).find(e=>e.querySelector("h3").textContent==='+JSON.stringify(label)+');Array.from(row.querySelectorAll("button")).find(e=>e.textContent==='+JSON.stringify(text)+').click();})()');
  let files=[];for(let i=0;i<50;i++){files=fs.readdirSync(artifacts);if(files.some(f=>f.endsWith('.csv')) && files.some(f=>f.endsWith('.json')))break;await sleep(200);}
  const jsonName=files.find(f=>f.endsWith('.json')),csvName=files.find(f=>f.endsWith('.csv'));assert(jsonName && csvName,'CSV/JSON downloads did not finish');
  const exported=JSON.parse(fs.readFileSync(path.join(artifacts,jsonName),'utf8'));assert.equal(exported.session.sampleCount,count);assert.equal(exported.frames.length,data.frames.length);assert(fs.readFileSync(path.join(artifacts,csvName),'utf8').includes('channel,timestampMs'),'CSV header missing expected schema');
  await snapshot('desktop-sessions.png');await evaluate('location.hash="live"');await waitFor('!document.getElementById("panel-live").hidden');
  await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(200);assert(await evaluate('document.body.scrollWidth<=innerWidth'),'Actual mobile dashboard overflows horizontally');await snapshot('mobile-live.png');
  assert.equal(errors.length,0,'Browser JavaScript exceptions occurred');
  const summary={origin,device:before.device,meters:connected.map(m=>({slot:m.slot,name:m.name,address:m.address,profile:m.profile})),sessionId,label,sampleCount:count,eventCount,gaps:data.gaps,channels:[...new Set(data.frames.filter(f=>f.kind==='reading').map(f=>f.meter+':'+f.channel+':'+f.unit))],firstReading:data.frames.find(f=>f.kind==='reading'),lastReading:[...data.frames].reverse().find(f=>f.kind==='reading'),csv:csvName,json:jsonName,browserProfile:profile,exceptions:errors};
  fs.writeFileSync(path.join(artifacts,'verification.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify(summary,null,2));console.log('PASS real device dashboard, native IndexedDB recording/marker/stop, reload dedupe, CSV/JSON UI download and mobile viewport. Artifacts: '+artifacts);
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){if(recordingStarted)try{await evaluate('fetch("/api/recording",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({action:"stop"})})');}catch(_){}try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();});
