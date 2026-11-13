// Real Chromium + native IndexedDB regression for TF-only live feed/recovery.
// All device readings are fixtures. No hardware or external service is used.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),web=path.join(root,'src/esp32_touch/meter/web'),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(file=>fs.existsSync(file));
assert(chrome,'Set METER_TEST_CHROME to a Chromium executable.');
const deviceId='tf-feed-fixture',bootId='tf-feed-boot',sessionId='tf-feed-session';
let total=192,tf=true,recording=false,blockOpen=true,abortWrites=false,holdAfter=null,held=null;
const requests=[];
function frame(seq){return {seq,kind:'reading',sessionId,meter:0,channel:0,address:'AA:00:00:00:00:01',profile:'ut60bt',numeric:true,value:seq,text:String(seq),unit:'V',mode:'DC',modeKey:1,flags:0,timestampMs:1700000000000+seq,uptimeMs:199000+seq};}
function storage(){return {supported:tf,mounted:tf,remounting:false,recording:tf&&recording,activeFile:recording?'meter-tf-feed-session.csv':'',cardBytes:2147483648,totalBytes:2000000000,usedBytes:1000000,writtenRecords:total,droppedRecords:0,error:'',queueDepth:0,bufferedRecords:0};}
function status(){return {device:{id:deviceId,bootId,uptimeMs:200000},network:{apSsid:'TF fixture',apIp:'192.168.4.1',connected:false},storage:storage(),recording:{active:recording,id:sessionId,label:'TF feed fixture'},meters:[{slot:0,name:'Voltage fixture',address:'AA:00:00:00:00:01',profile:'ut60bt',state:'connected',readings:[{channel:0,numeric:true,value:-1,text:'-1',unit:'V',mode:'DC',modeKey:1,flags:0,ageMs:1000,fresh:true}]}],discovery:{scanning:false,devices:[]},webhook:{queued:0,dropped:0},rules:[]};}
function fixtureHtml(){
  let html=fs.readFileSync(path.join(web,'index.html'),'utf8');
  const files={'METER_CSS':'style.css','METER_CALC_CSS':'calculations.css','METER_CARD_CSS':'card.css','METER_CORE':'core.js','METER_CALCULATIONS':'calculations.js','METER_CALC_UI':'calculations_ui.js','METER_CARD_UI':'card_ui.js','METER_VALUE_FORMAT':'value_format.js','METER_VALUE_TILES':'value_tiles.js','METER_APP':'app.js'};
  for(const [marker,file] of Object.entries(files))html=html.replace('/* '+marker+' */',()=>fs.readFileSync(path.join(web,file),'utf8'));
  const setup='<script>window.fixtureNativeIdb=window.indexedDB;window.fixtureBlockDb='+JSON.stringify(blockOpen)+';window.fixtureAbortWrites='+JSON.stringify(abortWrites)+';window.fixtureSeen=[];window.addEventListener("meter:frames",event=>{for(const frame of event.detail.frames || [])window.fixtureSeen.push(frame.seq);});Object.defineProperty(window,"indexedDB",{configurable:true,get(){if(window.fixtureBlockDb)throw new DOMException("Fixture browser storage is blocked","SecurityError");return window.fixtureNativeIdb;}});const fixtureNativeTransaction=IDBDatabase.prototype.transaction;IDBDatabase.prototype.transaction=function(...args){const tx=fixtureNativeTransaction.apply(this,args);if(this.name==="meter-workbench"&&args[1]==="readwrite"&&window.fixtureAbortWrites)queueMicrotask(()=>tx.abort());return tx;};</script>';
  return html.replace('</head>','</head>'+setup);
}
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost'),json=value=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(value));};
  if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fixtureHtml());return;}
  if(url.pathname==='/api/status'){json(status());return;}
  if(url.pathname==='/api/profiles'){json([{id:'ut60bt',name:'UT60BT fixture'}]);return;}
  if(url.pathname==='/api/storage/status'){json(storage());return;}
  if(url.pathname==='/api/storage/files'){json({files:[]});return;}
  if(url.pathname==='/api/frames'){
    const after=Number(url.searchParams.get('after')||0),frames=Array.from({length:Math.min(64,Math.max(0,total-after))},(_,index)=>frame(after+index+1));
    const reply={bootId,oldestSeq:1,latestSeq:total,nextSeq:frames.length?frames.at(-1).seq:after,gap:false,frames};requests.push(after);
    if(after===holdAfter&&!held){held={res,reply};holdAfter=null;return;}json(reply);return;
  }
  if(url.pathname==='/api/recording'){let body='';req.on('data',part=>body+=part);req.on('end',()=>{const value=JSON.parse(body);if(value.action==='start')recording=true;if(value.action==='stop')recording=false;json({ok:true});});return;}
  if(url.pathname==='/api/rules'){json({rules:[]});return;}
  if(url.pathname==='/api/time'){json({ok:true});return;}
  res.statusCode=404;res.end();
});
let browser,ws,messageId=0;const pending=new Map();
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const result=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;}
async function waitFor(expression){for(let i=0;i<100;i++){try{if(await evaluate('Boolean('+expression+')'))return;}catch(error){if(!/Execution context was destroyed|Cannot find context/.test(error.message))throw error;}await sleep(100);}assert.fail('Timed out: '+expression);}
async function readStore(){return evaluate('(async()=>{const store=await MeterCore.openDatabase(window.fixtureNativeIdb);try{const cursor=await store.cursor('+JSON.stringify(deviceId)+','+JSON.stringify(bootId)+');const sessions=await store.listSessions();const data=sessions.length?await store.readSession(sessions[0].id):{session:null,frames:[]};return {cursor,session:data.session,seqs:data.frames.map(frame=>frame.seq)};}finally{store.db.close();}})()');}
async function waitCursor(want){for(let i=0;i<100;i++){if((await readStore()).cursor===want)return;await sleep(100);}assert.fail('Durable cursor did not reach '+want);}
async function waitRequests(after,count=1){for(let i=0;i<100;i++){if(requests.filter(value=>value===after).length>=count)return;await sleep(100);}assert.fail('Feed did not request after='+after+'; requests='+JSON.stringify(requests));}
const click=id=>evaluate('document.getElementById('+JSON.stringify(id)+').click()');
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-tf-feed-'));
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port;for(let i=0;i<80;i++){try{port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(port))break;}catch(_){}await sleep(100);}assert(port,'Chromium did not start');
  const tab=await(await fetch('http://127.0.0.1:'+port+'/json/new?'+encodeURIComponent(origin),{method:'PUT'})).json();
  ws=new WebSocket(tab.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const value=JSON.parse(event.data);if(value.id&&pending.has(value.id)){const item=pending.get(value.id);pending.delete(value.id);value.error?item.reject(new Error(JSON.stringify(value.error))):item.resolve(value.result);}});
  await cdp('Page.enable');await waitFor('document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"');
  assert.match(await evaluate('document.getElementById("storage-state").textContent'),/browser storage/i,'Storage errors must identify the browser copy');
  assert(!await evaluate('document.getElementById("record-start").disabled'),'A mounted TF card must permit recording with blocked IndexedDB');
  await click('record-start');await waitFor('document.getElementById("recording-pill").textContent==="Recording"');
  await waitRequests(192);assert((await evaluate('Math.max(0,...window.fixtureSeen)'))>=192,'Tagged frames beyond the first page must reach live/calculation consumers');
  assert.match(await evaluate('document.getElementById("notice").textContent'),/TF.*browser|browser.*TF/i,'Recording notices must explain both independent destinations');
  await waitFor('document.querySelector("#value-tiles .value-number")?.dataset.formatted?.trim()==="192.00"');
  assert(await evaluate('document.getElementById("record-start").disabled'),'The active/card-busy start gate must remain enforced');
  console.log('PASS blocked browser storage leaves TF recording and multi-page live feed usable');

  const prefix=Array.from({length:16},(_,index)=>frame(index+1));
  await evaluate('(async()=>{const store=await MeterCore.openDatabase(window.fixtureNativeIdb);try{await store.commitBatch({deviceId:'+JSON.stringify(deviceId)+',bootId:'+JSON.stringify(bootId)+',after:0,nextSeq:16,oldestSeq:1,gap:false,frames:'+JSON.stringify(prefix)+',recording:{id:'+JSON.stringify(sessionId)+',label:"TF feed fixture"}});}finally{store.db.close();}})()');
  assert.equal((await readStore()).cursor,16,'The fixture durable cursor must differ from the volatile 192 cursor');
  total=256;holdAfter=192;for(let i=0;i<100&&!held;i++)await sleep(100);assert(held,'A pre-retry feed response must be in flight');
  const recoveredRequests=requests.length;await evaluate('window.fixtureBlockDb=false');await click('retry-storage');
  await waitFor('document.getElementById("storage-state").textContent.includes("IndexedDB ready")');
  held.res.setHeader('Content-Type','application/json');held.res.end(JSON.stringify(held.reply));held=null;
  await waitCursor(256);assert(requests.slice(recoveredRequests).includes(16),'Retry must resume at durable 16, not volatile 192 or the old in-flight response');
  let stored=await readStore();assert.equal(stored.session.sampleCount,256);assert.deepEqual(stored.seqs,Array.from({length:256},(_,index)=>index+1));
  console.log('PASS retry resets to the real durable cursor and rejects an older in-flight feed response');

  await evaluate('window.fixtureAbortWrites=true');total=384;
  await waitRequests(384);assert((await evaluate('Math.max(0,...window.fixtureSeen)'))>=384,'TF-backed live feed must survive a native browser transaction abort');
  assert.equal((await readStore()).cursor,256,'Volatile feed progress must never move the failed browser durable cursor');
  assert.match(await evaluate('document.getElementById("storage-state").textContent'),/browser storage/i);
  await evaluate('window.fixtureAbortWrites=false');const abortRecovery=requests.length;await click('retry-storage');await waitCursor(384);
  assert(requests.slice(abortRecovery).includes(256),'Browser retry must replay the failed range from durable 256');stored=await readStore();assert.equal(stored.session.sampleCount,384);
  console.log('PASS TF-backed transaction failure advances live only and later replays every missed browser sample');

  tf=false;blockOpen=false;abortWrites=true;total=448;recording=true;const noTfStart=requests.length;
  await cdp('Page.navigate',{url:origin+'/?without-tf'});await waitFor('document.getElementById("api-state") && document.getElementById("api-state").textContent==="Device online"');
  for(let i=0;i<100&&requests.slice(noTfStart).filter(value=>value===384).length<3;i++)await sleep(100);
  const stopped=requests.slice(noTfStart);assert(stopped.filter(value=>value===384).length>=3,'Without TF, failed transactions must retry the same durable page');
  assert(!stopped.some(value=>value>384),'Without TF, a transaction failure must not advance the live cursor past uncommitted tagged frames');
  assert.equal((await readStore()).cursor,384);assert.match(await evaluate('document.getElementById("feed-state").textContent'),/not advanced|paused/i);
  await evaluate('window.fixtureAbortWrites=false');await click('retry-storage');await waitCursor(448);stored=await readStore();assert.equal(stored.session.sampleCount,448);
  console.log('PASS original no-TF failed-transaction cursor semantics remain commit-only');
  console.log('ALL TF FEED BROWSER CHECKS PASSED');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(held){held.res.end('{}');held=null;}if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();server.close();});
