// Isolated Chromium integration runner. Fixture data is confined to this test server.
// No npm packages are needed (Node 22+ and an installed Chrome/Edge).
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
if(!chrome)throw new Error('Set METER_TEST_CHROME to a Chromium executable.');
const profiles=[{id:'ut60bt',name:'UNI-T UT60BT',family:'UNI-T',experimental:false}];
let recording={active:false,id:'',label:''},frames=[],seq=0,liveValue=-1.25,deviceUptime=5000,meterState='connected';
let wifiScanRequests=0,wifiPolls=0,wifiJoin=null;
const scannedSsid='Lab <svg/onload=window.pwn=1>';
const header=fs.readFileSync(path.join(root,'src/esp32_touch/meter/meter_web_assets.h'),'utf8');
const html=header.slice(header.indexOf('R"METERWEB(')+11,header.lastIndexOf(')METERWEB";'));
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost');
  const json=data=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));};
  if(url.pathname==='/') {res.setHeader('Content-Type','text/html');res.end(html);return;}
  if(url.pathname==='/api/profiles'){json(profiles);return;}
  if(url.pathname==='/api/wifi/scan'){
    if(req.method==='POST'){wifiScanRequests++;wifiPolls=0;res.statusCode=202;json({ok:true,state:'queued',networks:[]});return;}
    json(++wifiPolls<2?{state:'scanning',networks:[]}:{state:'complete',finishedAt:5000,networks:[{ssid:scannedSsid,rssi:-45,channel:6,security:'WPA2',joinable:true},{ssid:'Open lab',rssi:-70,channel:1,security:'Open',joinable:true},{ssid:'Enterprise lab',rssi:-60,channel:11,security:'Enterprise',joinable:false}]});return;
  }
  if(url.pathname==='/api/wifi'){let body='';req.on('data',part=>body+=part);req.on('end',()=>{wifiJoin=JSON.parse(body);res.statusCode=202;json({ok:true,joining:true});});return;}
  if(url.pathname==='/api/status'){json({device:{id:'browser-test-device',bootId:'browser-test-boot',uptimeMs:deviceUptime},network:{apSsid:'Meter TEST',apIp:'192.168.4.1',staIp:'',connected:false},meters:[{slot:0,id:'m1',name:'UT60BT <script>window.injected=true</script>',address:'AA:BB:CC:DD:EE:FF',profile:'ut60bt',state:meterState,packets:1,readings:[{channel:0,value:liveValue,numeric:true,text:String(liveValue),unit:'V',mode:'DC',modeKey:1,flags:0,ageMs:50,fresh:true}]}],discovery:{scanning:false,devices:[]},recording,latestSeq:seq,oldestSeq:1,rules:[],webhook:{queued:0,dropped:0}});return;}
  if(url.pathname==='/api/frames'){const after=Number(url.searchParams.get('after')||0),page=frames.filter(f=>f.seq>after).slice(0,64);json({bootId:'browser-test-boot',oldestSeq:1,latestSeq:seq,nextSeq:page.length?page[page.length-1].seq:after,gap:false,frames:page});return;}
  if(url.pathname==='/api/recording') {let body='';req.on('data',part=>body+=part);req.on('end',()=>{const request=JSON.parse(body);if(request.action==='start'){recording={active:true,id:'server-session-42',label:request.label};frames.push({seq:++seq,kind:'event',event:'recording',message:'Recording started',sessionId:recording.id,timestampMs:Date.now(),uptimeMs:1000},{seq:++seq,kind:'reading',sessionId:recording.id,meter:0,channel:0,address:'AA:BB:CC:DD:EE:FF',profile:'ut60bt',packet:1,value:-1.25,numeric:true,text:'-1.25',unit:'V',mode:'DC',modeKey:1,flags:0,timestampMs:Date.now(),uptimeMs:1100});}else if(request.action==='stop'){recording.active=false;frames.push({seq:++seq,kind:'event',event:'recording',message:'Recording stopped',sessionId:recording.id,timestampMs:Date.now(),uptimeMs:1200});}json({ok:true});});return;}
  if(url.pathname==='/api/rules'){json({rules:[{id:1,enabled:true,meter:0,channel:0,unit:'V',operator:'above',threshold:1,hysteresis:.1,cooldownMs:1000,url:'http://example.test/hook',method:'POST',headers:{},body:'',caPem:'',armed:false,lastResult:'Delivered',lastCode:200,fireCount:1,deliveryCount:1}]});return;}
  if(url.pathname.startsWith('/api/')){json({ok:true});return;}
  const file=path.resolve(root,'.'+decodeURIComponent(url.pathname));if(!file.startsWith(root+path.sep)){res.statusCode=403;res.end();return;}
  try {res.setHeader('Content-Type',file.endsWith('.html')?'text/html':file.endsWith('.js')?'text/javascript':'text/plain');res.end(fs.readFileSync(file));}catch(_){res.statusCode=404;res.end();}
});
let browser,ws,pending=new Map(),messageId=0;
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const response=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(response.exceptionDetails)throw new Error(response.exceptionDetails.text+' '+JSON.stringify(response.exceptionDetails.exception));return response.result.value;}
async function waitFor(expression){for(let i=0;i<100;i++){try{if(await evaluate('Boolean('+expression+')'))return;}catch(error){if(!/Execution context was destroyed|Cannot find context/.test(error.message))throw error;}await sleep(100);}assert.fail('Timed out: '+expression);}
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-web-browser-'));
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  const active=path.join(profile,'DevToolsActivePort');let port='';for(let i=0;i<80;i++){try{const value=fs.readFileSync(active,'utf8').split('\n')[0];if(/^\d+$/.test(value)){port=value;break;}}catch(error){if(!['ENOENT','EBUSY'].includes(error.code))throw error;}await sleep(100);}assert(port,'Chromium did not start');
  const page=await (await fetch('http://127.0.0.1:'+port+'/json/new?'+encodeURIComponent(origin+'/test/web_meter/indexeddb.html'),{method:'PUT'})).json();
  ws=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.id&&pending.has(message.id)){const p=pending.get(message.id);pending.delete(message.id);if(message.error)p.reject(new Error(JSON.stringify(message.error)));else p.resolve(message.result);}});
  await cdp('Page.enable');
  await waitFor('document.title.startsWith("PASS") || document.title.startsWith("FAIL")');
  const native=await evaluate('({title:document.title,result:document.getElementById("results").textContent})');console.log(native.result);assert(native.title.startsWith('PASS'),native.result);
  await cdp('Page.navigate',{url:origin+'/'});await waitFor('document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"');await evaluate('(()=>{const e=document.getElementById("instrument-view");e.value="scope";e.dispatchEvent(new Event("change"));})()');
  await waitFor('document.getElementById("storage-state").textContent.includes("IndexedDB ready")');
  const live=await evaluate('({text:document.getElementById("big-value").textContent,unit:document.getElementById("big-unit").textContent,injected:window.injected,card:document.querySelector(".meter-card h2").textContent})');assert.equal(live.text,'-1.25');assert.equal(live.unit,'V');assert.equal(live.injected,undefined);assert(live.card.includes('<script>'));console.log('PASS live reading and untrusted Bluetooth name rendered as text');
  assert(await evaluate('!!document.getElementById("wifi-scan")'),'Wi-Fi scan control is missing');
  await evaluate('location.hash="network";document.getElementById("wifi-password").value="old-password";document.getElementById("wifi-scan").click()');
  await waitFor('document.querySelectorAll(".wifi-network").length===3');assert.equal(wifiScanRequests,1);
  assert.equal(await evaluate('window.pwn'),undefined,'Scanned SSID executed HTML');
  assert(await evaluate('Array.from(document.querySelectorAll(".wifi-network")).find(e=>e.textContent.includes("Enterprise lab")).disabled'),'Unsupported enterprise network should not offer a password-only join');
  await evaluate('Array.from(document.querySelectorAll(".wifi-network")).find(e=>e.textContent.includes('+JSON.stringify(scannedSsid)+')).click()');
  assert.deepEqual(await evaluate('({ssid:document.getElementById("wifi-ssid").value,password:document.getElementById("wifi-password").value,required:document.getElementById("wifi-password").required})'),{ssid:scannedSsid,password:'',required:true});assert.equal(wifiJoin,null,'Network selection must not auto-join');
  await evaluate('document.getElementById("wifi-password").value="test-only-password";document.getElementById("wifi-join").click()');await waitFor('document.getElementById("wifi-password").value===""');assert.deepEqual(wifiJoin,{ssid:scannedSsid,password:'test-only-password'});
  await evaluate('Array.from(document.querySelectorAll(".wifi-network")).find(e=>e.textContent.includes("Open lab")).click()');assert.equal(await evaluate('document.getElementById("wifi-password").required'),false);
  await evaluate('location.hash="live"');console.log('PASS Wi-Fi scan, untrusted SSID, explicit protected/open selection and join');
  await evaluate('document.getElementById("session-label").value="Browser integration";document.getElementById("record-start").click()');
  await waitFor('document.getElementById("storage-state").textContent.includes("1 samples committed")');
  await evaluate('document.getElementById("record-stop").click()');await waitFor('document.getElementById("recording-pill").textContent==="Idle"');
  await sleep(800);
  const stored=await evaluate('(async()=>{const store=await MeterCore.openDatabase(indexedDB);try{const sessions=await store.listSessions();return await store.readSession(sessions[0].id);}finally{store.db.close();}})()');
  assert.equal(stored.session.sessionId,'server-session-42');assert.equal(stored.session.label,'Browser integration');assert.equal(stored.session.sampleCount,1);assert.equal(stored.frames.length,3);assert.equal(stored.frames[1].value,-1.25);console.log('PASS start/stop control, real IndexedDB session tagging, counts and marker recovery');
  const previousDocument=await evaluate('performance.timeOrigin');await cdp('Page.reload',{ignoreCache:true});await waitFor('performance.timeOrigin!=='+previousDocument+' && document.getElementById("storage-state") && document.getElementById("storage-state").textContent.includes("1 samples committed")');
  const reloaded=await evaluate('(async()=>{const store=await MeterCore.openDatabase(indexedDB);try{return await store.listSessions();}finally{store.db.close();}})()');assert.equal(reloaded[0].sampleCount,1);assert.equal(reloaded[0].eventCount,2);console.log('PASS reload persistence and durable cursor dedupe');
  liveValue=9;deviceUptime=60000;frames.push({seq:++seq,kind:'reading',sessionId:'',meter:0,channel:0,address:'AA:BB:CC:DD:EE:FF',profile:'ut60bt',value:99,numeric:true,text:'99',unit:'V',uptimeMs:30000});await waitFor('document.getElementById("big-value").textContent==="9.00"');await sleep(600);assert.equal(await evaluate('document.getElementById("big-value").textContent'),'9.00','Old replay must not override the more recent status measurement');
  meterState='disconnected';await waitFor('document.getElementById("focus-state").textContent==="disconnected"');frames.push({seq:++seq,kind:'reading',sessionId:'',meter:0,channel:0,address:'AA:BB:CC:DD:EE:FF',profile:'ut60bt',value:77,numeric:true,text:'77',unit:'V',uptimeMs:60000});await sleep(600);assert.equal(await evaluate('document.getElementById("big-value").textContent'),'9.00');assert(await evaluate('document.getElementById("sample-age").textContent.includes("STALE")'));console.log('PASS wire replay freshness and disconnected live-frame rejection');
  meterState='connected';await waitFor('document.getElementById("focus-state").textContent==="connected"');await cdp('Network.enable');await cdp('Network.emulateNetworkConditions',{offline:true,latency:0,downloadThroughput:0,uploadThroughput:0});await waitFor('document.getElementById("sample-age").textContent.includes("STALE")');await cdp('Network.emulateNetworkConditions',{offline:false,latency:0,downloadThroughput:-1,uploadThroughput:-1});await waitFor('document.getElementById("api-state").textContent==="Device online"');console.log('PASS age keeps advancing during a device/network outage');
  await evaluate('location.hash="triggers"');await waitFor('document.querySelector(".rule-card .delivery")');assert.equal(await evaluate('document.querySelector(".rule-card .delivery").classList.contains("error")'),false,'A successful Delivered result must not be shown as an error');console.log('PASS successful webhook delivery status');
  await evaluate('location.hash="live"');await waitFor('!document.getElementById("panel-live").hidden');
  await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(100);const width=await evaluate('({body:document.body.scrollWidth,viewport:innerWidth})');assert(width.body<=width.viewport,'Mobile page overflows horizontally');console.log('PASS 390px responsive viewport');
  assert(await evaluate('document.getElementById("big-value").getBoundingClientRect().top<innerHeight*.75'),'Mobile live reading should be visible before setup cards');console.log('PASS mobile live reading priority');
  if(process.env.METER_TEST_SCREENSHOT){const shot=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(process.env.METER_TEST_SCREENSHOT,Buffer.from(shot.data,'base64'));console.log('Screenshot: '+process.env.METER_TEST_SCREENSHOT);}
  await cdp('Page.addScriptToEvaluateOnNewDocument',{source:'window.__meterInsecureApiCheck=true;Object.defineProperty(navigator,"storage",{value:undefined,configurable:true});if(window.crypto)Object.defineProperty(crypto,"randomUUID",{value:undefined,configurable:true});'});
  await cdp('Page.reload',{ignoreCache:true});await waitFor('window.__meterInsecureApiCheck===true && document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"');await evaluate('location.hash="sessions"');await waitFor('!document.getElementById("panel-sessions").hidden && document.querySelector(".session-card")');assert.equal(await evaluate('navigator.storage'),undefined);assert.equal(await evaluate('crypto.randomUUID'),undefined);assert.equal(await evaluate('document.getElementById("persist-storage").hidden'),true);console.log('PASS dashboard and sessions without navigator.storage or crypto.randomUUID');
  console.log('ALL BROWSER CHECKS PASSED');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();server.close();});
