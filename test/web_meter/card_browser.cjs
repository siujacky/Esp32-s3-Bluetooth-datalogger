// Isolated Chromium TF-card UI checks. All device data and downloads are fixtures.
// No hardware, external network, npm packages, or browser recording DB are used.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),web=path.join(root,'src/esp32_touch/meter/web');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
assert(chrome,'Set METER_TEST_CHROME to a Chromium executable.');
const filename='meter-boot_1-session-2.csv',csv='deviceId,seq,numeric,value,text\r\nfixture,1,true,12.5,"12.5"\r\nfixture,2,false,,"OL"\r\n';
const malicious='meter-<img src=x onerror="window.injected=true">.csv';
const base={supported:true,mounted:true,recording:false,activeFile:'',cardBytes:2147483648,totalBytes:2000000000,usedBytes:1000000,writtenRecords:2,droppedRecords:0,error:'',queueDepth:0,bufferedRecords:0,downloads:0,formatting:false,formatResult:'idle',formatMessage:'',formatOperationId:''};
let storage={...base},files=[{name:filename,bytes:csv.length,active:false},{name:'meter-empty.csv',bytes:0,active:false},{name:malicious,bytes:123,active:false}],filesError='',filesNotice=null,statusError='',remountError='',remountRequests=[],downloadRequests=[],fileRequests=0,statusRequests=0,statusDelay=0,filesDelay=0,mountPendingPolls=0;
let formatRequests=[],formatStages=[],formatRequestError='',formatAckDelay=0;
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost'),json=(value,code=200)=>{res.statusCode=code;res.setHeader('Content-Type','application/json');res.end(JSON.stringify(value));};
  if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/style.css"><link rel="stylesheet" href="/card.css"><main><section id="panel-sessions"><div id="card-storage-root"></div><section aria-label="Browser recordings"><h2>Browser recordings</h2><div id="session-list">Browser session sentinel</div></section></section></main><script src="/core.js"></script><script src="/card_ui.js"></script>');return;}
  if(url.pathname==='/api/storage/status'){statusRequests++;if(formatStages.length){storage={...storage,...formatStages.shift()};if(storage.formatResult==='success')files=[];}const value=statusError?{error:statusError}:{...storage,remounting:mountPendingPolls>0},code=statusError?503:200;if(mountPendingPolls>0)mountPendingPolls--;if(statusDelay)setTimeout(()=>json(value,code),statusDelay);else json(value,code);return;}
  if(url.pathname==='/api/storage/files'){fileRequests++;const value=filesError?{error:filesError}:filesNotice || {files},code=filesError?503:200;if(filesDelay)setTimeout(()=>json(value,code),filesDelay);else json(value,code);return;}
  if(url.pathname==='/api/storage/remount'){
    let body='';req.on('data',part=>body+=part);req.on('end',()=>{remountRequests.push({method:req.method,body,contentType:req.headers['content-type']});if(remountError){json({error:remountError},409);return;}storage={...base};json({ok:true,queued:true},202);});return;
  }
  if(url.pathname==='/api/storage/format'){
    let body='';req.on('data',part=>body+=part);req.on('end',()=>{formatRequests.push({method:req.method,body,contentType:req.headers['content-type']});if(formatRequestError){json({error:formatRequestError},409);return;}const operation=JSON.parse(body);storage={...storage,formatting:true,formatResult:'queued',formatMessage:'TF format queued',formatOperationId:operation.operationId};if(formatAckDelay)setTimeout(()=>json({ok:true,queued:true},202),formatAckDelay);else json({ok:true,queued:true},202);});return;
  }
  if(url.pathname==='/api/storage/download'){downloadRequests.push(req.url);if(url.searchParams.get('name')!==filename){res.statusCode=400;res.end('Invalid filename');return;}res.setHeader('Content-Type','text/csv; charset=utf-8');res.setHeader('Content-Disposition','attachment; filename="'+filename+'"');res.end(csv);return;}
  const name=url.pathname.slice(1);
  if(!['core.js','style.css','card_ui.js','card.css'].includes(name)){res.statusCode=404;res.end();return;}
  try{res.setHeader('Content-Type',name.endsWith('.css')?'text/css; charset=utf-8':'text/javascript; charset=utf-8');res.end(fs.readFileSync(path.join(web,name)));}catch(_){res.statusCode=404;res.end();}
});
let browser,ws,messageId=0,profile;const pending=new Map();
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const result=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;}
async function waitFor(expression){for(let i=0;i<70;i++){if(await evaluate('Boolean('+expression+')'))return;await sleep(100);}assert.fail('Timed out: '+expression);}
async function emit(name,detail){await evaluate('window.dispatchEvent(new CustomEvent('+JSON.stringify(name)+',{detail:'+JSON.stringify(detail)+'}))');}
async function publish(value){storage={...base,...value};await emit('meter:status',{status:{storage},receivedAt:Date.now()});}
const text=id=>evaluate('document.getElementById('+JSON.stringify(id)+').textContent');
const click=id=>evaluate('document.getElementById('+JSON.stringify(id)+').click()');
async function formatPhrase(value){await evaluate('(()=>{const input=document.getElementById("card-format-phrase");input.value='+JSON.stringify(value)+';input.dispatchEvent(new Event("input",{bubbles:true}));})()');}
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
  profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-card-ui-'));const downloads=path.join(profile,'downloads');fs.mkdirSync(downloads);
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port;for(let i=0;i<80;i++){try{port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(port))break;}catch(_){}await sleep(100);}assert(port,'Chromium did not start');
  const tab=await(await fetch('http://127.0.0.1:'+port+'/json/new?'+encodeURIComponent(origin),{method:'PUT'})).json();
  ws=new WebSocket(tab.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.id&&pending.has(message.id)){const item=pending.get(message.id);pending.delete(message.id);message.error?item.reject(new Error(JSON.stringify(message.error))):item.resolve(message.result);}});
  await cdp('Page.enable');await cdp('Browser.setDownloadBehavior',{behavior:'allow',downloadPath:downloads});
  await waitFor('location.origin==='+JSON.stringify(origin)+' && document.readyState==="complete"');
  assert(await evaluate('!!document.getElementById("card-storage-state")'),'TF-card panel must be built by the production card_ui.js module');
  await waitFor('document.querySelectorAll("#card-files .card-file").length===3');
  assert.match(await text('card-storage-state'),/mounted/i);
  assert.match(await text('card-storage-root'),/TF.*tab closed/i);
  assert.match(await text('card-storage-root'),/Browser.*page.*open/i);
  assert.equal(await text('session-list'),'Browser session sentinel');
  assert.deepEqual(await evaluate('(async()=>{const dbs=await indexedDB.databases();return dbs.map(db=>db.name);})()'),[],'Device files must not create the browser recording database');
  assert.equal(await evaluate('window.injected'),undefined);
  assert((await text('card-files')).includes(malicious),'Untrusted filenames must be displayed as text');
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),2,'Unsafe basenames must not become download links');
  assert.match(await text('card-files'),/0 B/,'Empty files must show their real zero size');
  assert(!await evaluate('!!document.querySelector("#card-storage-root [data-action=delete],#card-storage-root [data-action=format]")'));
  console.log('PASS device files stay separate from browser sessions; safe text, basenames, sizes, and offline help');

  files=files.map(file=>({...file,active:file.name===filename}));
  await publish({recording:true,activeFile:filename,writtenRecords:40,droppedRecords:3});await emit('meter:panel',{name:'sessions'});
  await waitFor('document.querySelector("#card-files .card-file-active")');
  assert.match(await text('card-storage-state'),/recording/i);assert(await evaluate('document.getElementById("card-remount").disabled'));
  assert.match(await text('card-storage-gaps'),/3.*dropped.*gap/i);
  assert.match(await text('card-files'),/snapshot/i,'An active download must explain its fixed snapshot');
  assert.equal(await evaluate('document.querySelector("#card-files a[download]").getAttribute("href")'),'/api/storage/download?name=meter-boot_1-session-2.csv');
  await evaluate('document.querySelector("#card-files a[download]").click()');
  for(let i=0;i<70&&!fs.existsSync(path.join(downloads,filename));i++)await sleep(100);
  assert(fs.existsSync(path.join(downloads,filename)),'Clicking Download must create the named CSV attachment');
  assert.equal(fs.readFileSync(path.join(downloads,filename),'utf8'),csv,'Attachment body must preserve invalid readings as non-numeric');
  assert.deepEqual(downloadRequests,['/api/storage/download?name=meter-boot_1-session-2.csv']);
  console.log('PASS active snapshot downloads use the encoded filename endpoint and preserve CSV body');

  await emit('meter:offline',{});
  assert.match(await text('card-storage-state'),/offline/i);assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0,'Offline files must not look downloadable');
  assert(await evaluate('document.getElementById("card-remount").disabled'));
  await publish({usedBytes:2000000000,error:'Card full <img src=x onerror="window.injected=true">'});
  assert.match(await text('card-storage-state'),/full/i);assert.match(await text('card-storage-error'),/Card full <img/);assert.equal(await evaluate('window.injected'),undefined);
  console.log('PASS offline and full-card states cannot masquerade as successful recording');

  await publish({mounted:false,error:'No card detected'});
  assert.match(await text('card-storage-state'),/not mounted/i);assert.match(await text('card-storage-error'),/No card detected/);
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0);
  assert(!await evaluate('document.getElementById("card-remount").disabled'));await click('card-remount');
  await waitFor('/mounted/i.test(document.getElementById("card-storage-state").textContent) && !document.getElementById("card-remount").disabled');
  assert.deepEqual(remountRequests,[{method:'POST',body:'{}',contentType:'application/json'}]);
  assert.equal(await evaluate('document.getElementById("card-storage-error").hidden'),true);
  console.log('PASS no-card retry sends JSON POST and waits for the queued mount result');

  await publish({mounted:false,error:'Mount failed'});remountError='Storage is busy';await click('card-remount');
  await waitFor('!document.getElementById("card-remount").disabled');
  assert.match(await text('card-storage-error'),/Storage is busy/);remountError='';
  await publish({supported:false,mounted:false,error:'',cardBytes:0,totalBytes:0,usedBytes:0});
  assert.match(await text('card-storage-state'),/not supported/i);assert(!/ready|recording/i.test(await text('card-storage-state')));
  assert(await evaluate('document.getElementById("card-remount").disabled'));assert.equal(await evaluate('document.getElementById("card-storage-error").hidden'),true);
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0);
  console.log('PASS busy errors remain visible and unsupported targets never claim card readiness');

  await publish({});filesError='Cannot read card directory';await click('card-refresh');
  await waitFor('!document.getElementById("card-refresh").disabled');assert.match(await text('card-storage-error'),/Cannot read card directory/);
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0,'An unreadable listing must not keep stale download links');
  filesError='';await click('card-refresh');await waitFor('document.querySelectorAll("#card-files a[download]").length===2');
  statusError='Card status unavailable';await click('card-refresh');await waitFor('!document.getElementById("card-refresh").disabled');
  assert.match(await text('card-storage-error'),/Card status unavailable/);assert(!/mounted|recording/i.test(await text('card-storage-state')),'A failed status request must not leave a success badge');
  statusError='';await click('card-refresh');await waitFor('document.querySelectorAll("#card-files a[download]").length===2');
  const before=fileRequests;await emit('meter:panel',{name:'sessions'});await waitFor('!document.getElementById("card-refresh").disabled');assert(fileRequests>before,'Opening Sessions must refresh the device listing');
  console.log('PASS listing/status errors invalidate stale success and recover with Refresh');

  filesNotice={files:[],error:'TF filesystem busy; retry the listing'};await click('card-refresh');
  await waitFor('!document.getElementById("card-refresh").disabled');
  assert.match(await text('card-storage-error'),/TF filesystem busy/,'HTTP 200 application errors must remain visible');
  assert.equal(await evaluate('document.getElementById("card-storage-error").hidden'),false);
  assert.equal(await evaluate('document.querySelectorAll("#card-files .card-file").length'),3,'A busy filesystem must retain its last known file listing');
  assert.match(await text('card-files'),/last known/i);assert(!(await text('card-files')).includes('No TF recordings yet'),'A busy response is not an empty directory');
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0,'Last known rows must not masquerade as a confirmed downloadable listing');
  filesNotice={files:[],message:'TF filesystem busy; try again'};filesDelay=300;const retryBefore=fileRequests;await click('card-refresh');
  for(let i=0;i<20&&fileRequests===retryBefore;i++)await sleep(20);
  assert(fileRequests>retryBefore);await publish({});
  assert.equal(await evaluate('document.getElementById("card-storage-error").hidden'),false,'A pending retry must keep its last error visible through unrelated status updates');
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0,'A status update cannot confirm a stale directory while its retry is pending');
  await waitFor('!document.getElementById("card-refresh").disabled');filesDelay=0;
  assert.match(await text('card-storage-error'),/TF filesystem busy; try again/,'Application error messages must be handled as well as error fields');
  assert.equal(await evaluate('document.querySelectorAll("#card-files .card-file").length'),3);
  filesNotice=null;await click('card-refresh');await waitFor('document.querySelectorAll("#card-files a[download]").length===2');
  assert.equal(await evaluate('document.getElementById("card-storage-error").hidden'),true,'A successful retry clears the application error');
  console.log('PASS HTTP 200 busy errors preserve labelled last-known files without claiming an empty card');

  mountPendingPolls=1;await click('card-remount');
  await waitFor('document.getElementById("card-storage-state").textContent.includes("Mounting")');await sleep(150);
  assert(await evaluate('document.getElementById("card-remount").disabled'),'A queued mount must wait until remounting clears, even if mounted still reflects the previous card');
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0,'Queued mounting must invalidate downloads from the previous mount');
  await waitFor('!document.getElementById("card-remount").disabled');
  assert.match(await text('card-storage-state'),/mounted/i);
  console.log('PASS queued remount cannot reuse the previous mounted snapshot');

  statusDelay=300;const statusBefore=statusRequests;await click('card-refresh');
  for(let i=0;i<20&&statusRequests===statusBefore;i++)await sleep(20);
  assert(statusRequests>statusBefore,'Delayed status request must be in flight before disconnect');await emit('meter:offline',{});
  await waitFor('!document.getElementById("card-refresh").disabled');
  assert.match(await text('card-storage-state'),/offline/i,'A response started before the offline event cannot restore a mounted badge');
  assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0);
  statusDelay=0;await click('card-refresh');await waitFor('document.querySelectorAll("#card-files a[download]").length===2');
  statusDelay=300;statusError='Old status failure';const failedBefore=statusRequests;await click('card-refresh');
  for(let i=0;i<20&&statusRequests===failedBefore;i++)await sleep(20);
  await publish({writtenRecords:77});await waitFor('!document.getElementById("card-refresh").disabled');
  assert.match(await text('card-storage-state'),/mounted/i,'An old failed status request cannot override a newer status event');
  assert.equal(await text('card-storage-written'),'77');statusDelay=0;statusError='';

  await emit('meter:status',{status:{device:{id:'old-device',bootId:'old-boot'},storage:{...base}},receivedAt:Date.now()});
  filesDelay=300;const filesBefore=fileRequests;await click('card-refresh');
  for(let i=0;i<20&&fileRequests===filesBefore;i++)await sleep(20);
  assert(fileRequests>filesBefore,'Delayed file request must be in flight before a new boot');
  files=[{name:'meter-newboot-session-3.csv',bytes:400,active:false}];
  await emit('meter:status',{status:{device:{id:'new-device',bootId:'new-boot'},storage:{...base}},receivedAt:Date.now()});
  await waitFor('document.querySelector("#card-files .card-file-name") && document.querySelector("#card-files .card-file-name").textContent==="meter-newboot-session-3.csv"');
  assert(!(await text('card-files')).includes(filename),'Old-device directory responses must not restore prior-device downloads');filesDelay=0;
  console.log('PASS offline, newer status, and device reboot invalidate older in-flight responses');

  assert(await evaluate('!!document.getElementById("card-format-open")'),'A TF format action must open a separate destructive confirmation');
  await click('card-format-open');assert.equal(await evaluate('document.getElementById("card-format-confirm").hidden'),false);
  assert.match(await text('card-format-confirm'),/deletes ALL.*data.*TF card/i);
  assert(await evaluate('document.getElementById("card-format-submit").disabled'));
  await formatPhrase('format tf card');assert(await evaluate('document.getElementById("card-format-submit").disabled'));
  await evaluate('document.getElementById("card-format-confirm").dispatchEvent(new Event("submit",{bubbles:true,cancelable:true}))');
  assert.equal(formatRequests.length,0,'A wrong confirmation phrase must never POST');
  await formatPhrase('FORMAT TF CARD');assert(!await evaluate('document.getElementById("card-format-submit").disabled'));
  await click('card-format-cancel');assert.equal(await evaluate('document.getElementById("card-format-confirm").hidden'),true);assert.equal(formatRequests.length,0);
  for(const busy of [{supported:false,mounted:false},{recording:true},{queueDepth:1},{bufferedRecords:1},{downloads:1},{remounting:true},{formatting:true},{formatResult:'queued'},{formatResult:'running'}]){
    await publish(busy);assert(await evaluate('document.getElementById("card-format-open").disabled'),'Unsafe format guard missing for '+JSON.stringify(busy));
    await click('card-format-open');assert.equal(await evaluate('document.getElementById("card-format-confirm").hidden'),true);
  }
  await emit('meter:offline',{});assert(await evaluate('document.getElementById("card-format-open").disabled'));await publish({});assert.equal(formatRequests.length,0);
  console.log('PASS TF formatting requires two steps, exact typed consent, cancellation, and all busy guards');

  formatStages=[{formatting:true,formatResult:'queued',formatMessage:'TF format queued'},{formatting:true,formatResult:'running',formatMessage:'Formatting TF card'},{formatting:false,formatResult:'success',formatMessage:'TF card format completed'}];
  await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  for(let i=0;i<50&&formatRequests.length<1;i++)await sleep(20);
  assert.equal(formatRequests.length,1);assert.equal(formatRequests[0].method,'POST');assert.equal(formatRequests[0].contentType,'application/json');const firstFormat=JSON.parse(formatRequests[0].body);
  assert.deepEqual(Object.keys(firstFormat).sort(),['confirm','operationId']);assert.equal(firstFormat.confirm,'FORMAT TF CARD');assert.match(firstFormat.operationId,/^[A-Za-z0-9_-]{16,64}$/);
  assert(!/completed|successful/i.test(await text('card-format-status')),'A 202 queued acknowledgement is not completion');
  assert(await evaluate('document.getElementById("card-format-open").disabled'));assert.equal(await evaluate('document.querySelectorAll("#card-files a[download]").length'),0);
  formatStages=[{formatting:true,formatResult:'running',formatMessage:'Formatting TF card'},{formatting:false,formatResult:'success',formatMessage:'Other client format completed',formatOperationId:'other_client_operation_1234'},{formatting:false,formatResult:'success',formatMessage:'TF card format completed',formatOperationId:firstFormat.operationId}];
  for(let i=0;i<150&&storage.formatOperationId!=='other_client_operation_1234';i++)await sleep(20);
  assert.equal(storage.formatOperationId,'other_client_operation_1234');await sleep(100);
  assert(!/completed|successful/i.test(await text('card-format-status')),'Another client operation cannot confirm this request');
  assert(await evaluate('document.getElementById("card-format-open").disabled'));
  await waitFor('document.getElementById("card-format-status").textContent.includes("format completed")');
  await click('card-refresh');await waitFor('!document.getElementById("card-refresh").disabled');assert.equal(formatRequests.length,1,'Refresh after formatting must never repeat the destructive POST');
  console.log('PASS queued/running TF format is observed until actual success without automatic resubmission');

  formatStages=[{formatting:true,formatResult:'queued'},{formatting:false,formatResult:'failed',formatMessage:'TF format failed: write protected <img src=x onerror="window.injected=true">'}];
  await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  await waitFor('document.getElementById("card-format-status").textContent.includes("write protected")');assert.equal(await evaluate('window.injected'),undefined);
  assert.match(await text('card-storage-error'),/write protected/);assert(!/completed|successful/i.test(await text('card-format-status')));
  await click('card-refresh');await waitFor('!document.getElementById("card-refresh").disabled');assert.equal(formatRequests.length,2);
  await publish({});formatRequestError='TF card is busy';await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  await waitFor('document.getElementById("card-format-status").textContent.includes("TF card is busy")');formatRequestError='';
  assert.equal(formatRequests.length,3);await click('card-refresh');await waitFor('!document.getElementById("card-refresh").disabled');assert.equal(formatRequests.length,3);
  console.log('PASS TF format failure and request rejection remain visible without a retry POST');

  await publish({});formatStages=[{formatting:false,formatResult:'success',formatMessage:'TF card format completed'}];statusDelay=300;
  const formatStatusBefore=statusRequests;await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  for(let i=0;i<50&&statusRequests===formatStatusBefore;i++)await sleep(20);
  assert(statusRequests>formatStatusBefore);await emit('meter:offline',{});await sleep(350);
  assert.match(await text('card-storage-state'),/offline/i);assert.match(await text('card-format-status'),/unconfirmed|unknown/i,'Disconnect must invalidate an in-flight completion result');
  assert(!/completed|successful/i.test(await text('card-format-status')));statusDelay=0;
  await click('card-refresh');await waitFor('document.getElementById("card-format-status").textContent.includes("format completed")');assert.equal(formatRequests.length,4);
  console.log('PASS disconnected TF format results stay unconfirmed until a fresh status read, never a repeated POST');

  await publish({});const timeoutBefore=statusRequests;await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  for(let i=0;i<50&&statusRequests===timeoutBefore;i++)await sleep(20);
  assert(statusRequests>timeoutBefore);await evaluate('window.fixtureRealDateNow=Date.now;Date.now=()=>window.fixtureRealDateNow()+61000');
  await waitFor('document.getElementById("card-format-status").textContent.includes("still unconfirmed")');
  await evaluate('Date.now=window.fixtureRealDateNow');assert.equal(formatRequests.length,5);assert(await evaluate('document.getElementById("card-format-open").disabled'));
  storage={...storage,formatting:false,formatResult:'success',formatMessage:'TF card format completed'};await click('card-refresh');
  await waitFor('document.getElementById("card-format-status").textContent.includes("format completed")');assert.equal(formatRequests.length,5);
  const operationIds=formatRequests.map(request=>JSON.parse(request.body).operationId);assert.equal(new Set(operationIds).size,operationIds.length,'Every explicit format attempt needs a fresh operation ID');
  console.log('PASS a 60s observation timeout remains unconfirmed and Refresh resolves only the matching operation');

  await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(100);
  assert(await evaluate('document.body.scrollWidth<=innerWidth'),'390px TF-card panel must not overflow');
  await click('card-format-open');await formatPhrase('FORMAT TF CARD');
  assert(await evaluate('document.body.scrollWidth<=innerWidth'),'390px typed format confirmation must not overflow');
  const tapSizes=await evaluate('[...document.querySelectorAll("#card-storage-root button,#card-storage-root a[download]")].filter(element=>element.getClientRects().length).map(element=>element.getBoundingClientRect().height)');
  assert(tapSizes.every(height=>height>=44),'Card controls and download links must have 44px tap targets');
  if(process.env.METER_CARD_SCREENSHOT){const shot=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(process.env.METER_CARD_SCREENSHOT,Buffer.from(shot.data,'base64'));}
  await click('card-format-cancel');assert.equal(formatRequests.length,5,'Inspecting or cancelling the confirmation never formats the card');
  console.log('PASS 390px responsive panel and 44px controls');
  await publish({});formatAckDelay=800;
  await click('card-format-open');await formatPhrase('FORMAT TF CARD');await click('card-format-submit');
  for(let i=0;i<50&&formatRequests.length<6;i++)await sleep(20);
  assert.equal(formatRequests.length,6);
  await publish({...storage,formatting:false,formatResult:'success',formatMessage:'TF card format completed before acknowledgement'});
  assert.match(await text('card-format-status'),/completed before acknowledgement/);
  await sleep(1000);
  assert.match(await text('card-format-status'),/completed before acknowledgement/,'A late queued acknowledgement must preserve an already verified matching completion');
  assert.equal(formatRequests.length,6);formatAckDelay=0;
  console.log('PASS matching completion survives a delayed POST acknowledgement without repeating format');
  console.log('ALL CARD BROWSER CHECKS PASSED');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();server.close();});
