// Isolated calculation UI integration tests. All readings are test-server fixtures.
// Exercises real Chromium, production calculation modules and native IndexedDB.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),web=path.join(root,'src/esp32_touch/meter/web');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
assert(chrome,'Set METER_TEST_CHROME to a Chromium executable.');
const server=http.createServer((req,res)=>{
  const name=new URL(req.url,'http://localhost').pathname.slice(1);
  if(!name){res.setHeader('Content-Type','text/html; charset=utf-8');res.end('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/style.css"><link rel="stylesheet" href="/calculations.css"><main><section id="panel-calculations"><div id="calculator-root"></div></section></main><script src="/core.js"></script><script src="/calculations.js"></script><script src="/calculations_ui.js"></script>');return;}
  if(!['core.js','calculations.js','calculations_ui.js','style.css','calculations.css'].includes(name)){res.statusCode=404;res.end();return;}
  try{res.setHeader('Content-Type',name.endsWith('.css')?'text/css; charset=utf-8':'text/javascript; charset=utf-8');res.end(fs.readFileSync(path.join(web,name)));}catch(_){res.statusCode=404;res.end();}
});
let browser,ws,messageId=0;const pending=new Map();
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const result=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;}
async function waitFor(expression){for(let i=0;i<70;i++){if(await evaluate('Boolean('+expression+')'))return;await sleep(100);}assert.fail('Timed out: '+expression);}
function fixture(seq,uptime,extra={}){return {seq,kind:'reading',meter:extra.meter??0,channel:0,address:extra.meter===1?'BB:00:00:00:00:01':'AA:00:00:00:00:01',profile:'ut60bt',numeric:true,value:extra.meter===1?1000:12,text:extra.meter===1?'1000':'12',unit:extra.meter===1?'mA':'V',mode:'DC',modeKey:1,flags:0,uptimeMs:uptime,...extra};}
function status(uptime=10000,extra={}){return {device:{id:'calculator-fixture',bootId:'calc-boot-1',uptimeMs:uptime},meters:[0,1].map(slot=>({slot,name:slot?'Current <img src=x onerror="window.injected=true">':'Voltage',address:slot?'BB:00:00:00:00:01':'AA:00:00:00:00:01',profile:'ut60bt',state:'connected',readings:[{channel:0,numeric:true,value:slot?1000:12,text:slot?'1000':'12',unit:slot?'mA':'V',mode:'DC',modeKey:1,flags:0,ageMs:0,fresh:true}]})),...extra};}
async function emit(name,detail){await evaluate('window.dispatchEvent(new CustomEvent('+JSON.stringify(name)+',{detail:'+JSON.stringify(detail)+'}))');}
async function pair(seq,uptime,receivedAt){await emit('meter:status',{status:status(uptime),receivedAt});await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt,gap:false,frames:[fixture(seq,uptime),fixture(seq+1,uptime,{meter:1})]});}
async function field(id,value){await evaluate('(()=>{const e=document.getElementById('+JSON.stringify(id)+');e.value='+JSON.stringify(value)+';e.dispatchEvent(new Event("change",{bubbles:true}));})()');}
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-calculations-'));
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port;for(let i=0;i<80;i++){try{port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(port))break;}catch(_){}await sleep(100);}assert(port,'Chromium did not start');
  const tab=await(await fetch('http://127.0.0.1:'+port+'/json/new?'+encodeURIComponent(origin),{method:'PUT'})).json();
  ws=new WebSocket(tab.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const m=JSON.parse(event.data);if(m.id&&pending.has(m.id)){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);}});
  await cdp('Page.enable');
  await waitFor('document.getElementById("calc-start")');
  await emit('meter:status',{status:status(),receivedAt:Date.now()});
  await waitFor('document.getElementById("calc-power-value").textContent==="12"');
  assert.equal(await evaluate('document.getElementById("calc-power-unit").textContent'),'W');
  assert.equal(await evaluate('document.getElementById("calc-resistance-value").textContent'),'12');
  assert.equal(await evaluate('window.injected'),undefined);
  assert((await evaluate('document.getElementById("calc-current-source").textContent')).includes('<img'));
  console.log('PASS selected sources normalize mA, calculate DC power/resistance, and render names safely');
  const skewed=status();skewed.meters[1].readings[0].ageMs=2000;
  await emit('meter:status',{status:skewed,receivedAt:Date.now()});
  assert.equal(await evaluate('document.getElementById("calc-power-value").textContent'),'—','Fresh status channels sampled too far apart must not produce combined power');
  assert(await evaluate('document.getElementById("calc-start").disabled'));
  console.log('PASS status-only previews enforce inter-channel sample skew');
  await field('calc-mode','ac');
  await emit('meter:status',{status:status(10000,{meters:status().meters.map(m=>({...m,readings:m.readings.map(r=>({...r,mode:'AC'}))}))}),receivedAt:Date.now()});
  await waitFor('document.getElementById("calc-power-unit").textContent==="VA"');
  assert.equal(await evaluate('document.getElementById("calc-power-value").textContent'),'12');
  assert.equal(await evaluate('document.getElementById("calc-real-power-value").textContent'),'—');
  await field('calc-power-factor','0.8');
  assert.equal(await evaluate('document.getElementById("calc-real-power-value").textContent'),'9.6');
  assert(await evaluate('document.getElementById("calc-start").disabled'));
  console.log('PASS AC apparent power remains VA, configured PF supplies W, and battery integration is DC-only');
  await field('calc-mode','dc');await field('calc-rated-ah','10');await field('calc-soc','80');
  const t=Date.now();await pair(1,10000,t);await evaluate('document.getElementById("calc-start").click()');
  await pair(3,11000,t+1000);await pair(5,12000,t+2000);
  const charge=Number(await evaluate('document.getElementById("calc-charge-value").textContent'));
  assert(charge>0 && charge<0.001,'Live current must integrate a small measured Ah amount');
  assert(Number(await evaluate('document.getElementById("calc-energy-value").textContent'))>0);
  assert((await evaluate('document.getElementById("calc-runtime-value").textContent')).includes('h'));
  await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:t+3000,gap:false,frames:[fixture(3,11000),fixture(4,11000,{meter:1})]});
  assert.equal(Number(await evaluate('document.getElementById("calc-charge-value").textContent')),charge,'Backlog must not increase measured Ah');
  assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')));
  console.log('PASS device sample-time integration, runtime assumptions, and backlog gap pause');
  await pair(7,13000,t+3000);await evaluate('document.getElementById("calc-start").click()');
  await emit('meter:offline',{});
  assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')));
  const offlineCharge=Number(await evaluate('document.getElementById("calc-charge-value").textContent'));
  await pair(9,14000,t+4000);
  assert.equal(Number(await evaluate('document.getElementById("calc-charge-value").textContent')),offlineCharge,'Offline pauses must require an explicit resume');
  console.log('PASS offline pause requires explicit resume');
  await evaluate('document.getElementById("calc-save").click()');
  await waitFor('document.querySelector("#calc-sessions .calc-saved-session")');
  const databases=await evaluate('(async()=>{const rows=await indexedDB.databases();return rows.map(r=>r.name);})()');
  assert(databases.includes('meter-calculations'));assert(!databases.includes('meter-workbench'),'Calculations must not open the raw recording database');
  const summaries=await evaluate('(async()=>{const db=await new Promise((r,j)=>{const q=indexedDB.open("meter-calculations");q.onsuccess=()=>r(q.result);q.onerror=()=>j(q.error);});const result=await new Promise((r,j)=>{const q=db.transaction("sessions").objectStore("sessions").getAll();q.onsuccess=()=>r(q.result);q.onerror=()=>j(q.error);});db.close();return result;})()');
  assert.equal(summaries.length,1);assert(!JSON.stringify(summaries).includes('frames'),'Saved calculations must contain summaries rather than raw frames');
  await cdp('Page.reload',{ignoreCache:true});
  await waitFor('document.querySelector("#calc-sessions .calc-saved-session")');
  assert.equal(await evaluate('document.getElementById("calc-rated-ah").value'),'10');
  assert.equal(await evaluate('document.getElementById("calc-soc").value'),'80');
  assert(/idle|paused/i.test(await evaluate('document.getElementById("calc-session-state").textContent')));
  console.log('PASS separate native IndexedDB summary/settings persistence with reload-safe inactive session');
  await field('calc-rated-ah','');await field('calc-soc','');await field('calc-direction','negative-discharge');
  const negativeStatus=status(20000);negativeStatus.meters[1].readings[0].value=-1000;negativeStatus.meters[1].readings[0].text='-1000';
  const n=Date.now()+100;await emit('meter:status',{status:negativeStatus,receivedAt:n});
  await evaluate('document.getElementById("calc-start").click()');
  await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:n,gap:false,frames:[fixture(11,20000)]});
  assert.equal(await evaluate('document.getElementById("calc-session-state").textContent'),'Running','First asynchronous source should wait for its pair without pausing');
  await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:n+50,gap:false,frames:[fixture(12,20000,{meter:1,value:-1000,text:'-1000'})]});
  negativeStatus.device.uptimeMs=21000;
  await emit('meter:status',{status:negativeStatus,receivedAt:n+1000});
  await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:n+1000,gap:false,frames:[fixture(13,21000),fixture(14,21000,{meter:1,value:-1000,text:'-1000'})]});
  assert(Number(await evaluate('document.getElementById("calc-charge-value").textContent'))>0);
  assert.equal(await evaluate('document.getElementById("calc-runtime-value").textContent'),'—');
  assert.equal(await evaluate('document.getElementById("calc-remaining-value").textContent'),'—');
  console.log('PASS asynchronous source pairing, entered discharge polarity, and measured-only tests without inferred rating');
  await emit('meter:status',{status:negativeStatus,receivedAt:Date.now()-4000});
  assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')));
  await field('calc-direction','positive-discharge');
  assert(await evaluate('document.getElementById("calc-start").disabled'),'Changed assumptions must not mix into existing measured totals');
  assert(/reset/i.test(await evaluate('document.getElementById("calc-message").textContent')));
  console.log('PASS stale-source pause and assumption-change reset requirement');
  const replacement=status();replacement.meters[0].address='CC:00:00:00:00:01';replacement.meters[0].name='Replacement voltage meter';
  await emit('meter:status',{status:replacement,receivedAt:Date.now()});
  await cdp('Browser.setDownloadBehavior',{behavior:'deny'});
  await evaluate('(()=>{const original=URL.createObjectURL;URL.createObjectURL=blob=>{window.__exportedCalculation=blob;return original(blob);};document.getElementById("calc-export").click();})()');
  const exported=await evaluate('(async()=>JSON.parse(await window.__exportedCalculation.text()))()');
  assert.equal(exported.assumptions.direction,'negative-discharge');assert.equal(exported.assumptions.ratedCapacityAh,null);assert.equal(exported.sources.voltage.address,'AA:00:00:00:00:01');assert(exported.summary.dischargeAh>0);assert(!('frames' in exported));
  console.log('PASS exported summaries retain original assumptions and measured totals without raw readings');
  await evaluate('document.getElementById("calc-reset").click()');
  await emit('meter:status',{status:status(),receivedAt:Date.now()});
  await evaluate('document.getElementById("calc-start").click()');
  await emit('meter:status',{status:replacement,receivedAt:Date.now()});
  assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')),'Physical source replacement must pause even before the first paired sample');
  assert(await evaluate('document.getElementById("calc-start").disabled'));
  console.log('PASS physical source replacement cannot change a newly started test identity');
  const regressionFailures=[];
  async function regression(name,body){try{await body();console.log('PASS '+name);}catch(error){regressionFailures.push(name+': '+error.message);console.error('FAIL '+name+': '+error.message);}}
  async function beginRegression(uptime,current,seq,rated=false,currentFirst=false){
    await evaluate('document.getElementById("calc-reset").click()');await field('calc-rated-ah',rated?'1':'');await field('calc-soc',rated?'100':'');await field('calc-direction','positive-discharge');
    const s=status(uptime);s.meters[1].readings[0].value=current*1000;s.meters[1].readings[0].text=String(current*1000);const at=Date.now()+100;
    await emit('meter:status',{status:s,receivedAt:at});await evaluate('document.getElementById("calc-start").click()');
    const voltage=fixture(currentFirst?seq+1:seq,uptime),amps=fixture(currentFirst?seq:seq+1,uptime,{meter:1,value:current*1000,text:String(current*1000)});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at,gap:false,frames:currentFirst?[amps,voltage]:[voltage,amps]});return at;
  }
  await regression('every current observation in a batched pulse contributes Ah and Wh',async()=>{
    const at=await beginRegression(30000,0,100);
    const ahead=status(31000);ahead.meters[1].readings[0].value=0;ahead.meters[1].readings[0].text='0';
    await emit('meter:status',{status:ahead,receivedAt:at+1000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+1000,gap:false,frames:[fixture(102,30500,{meter:1,value:2000,text:'2000'}),fixture(103,31000),fixture(104,31000,{meter:1,value:0,text:'0'})]});
    const ah=Number(await evaluate('document.getElementById("calc-charge-value").textContent')),wh=Number(await evaluate('document.getElementById("calc-energy-value").textContent'));
    assert(Math.abs(ah-1/3600)<1e-10,'Expected 1 A-second from the 0→2→0 A pulse, got '+ah+' Ah');
    assert(Math.abs(wh-12/3600)<1e-9,'Expected 12 W-seconds from the pulse, got '+wh+' Wh');
    assert.equal(await evaluate('document.getElementById("calc-session-state").textContent'),'Running');
  });
  await regression('an intermediate overload cannot disappear behind a later valid current',async()=>{
    const at=await beginRegression(32000,1,110);
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+1000,gap:false,frames:[fixture(112,32500,{meter:1,numeric:false,value:null,text:'OL'}),fixture(113,33000,{meter:1})]});
    assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')),'Overload between valid samples must pause with an explicit gap');
    assert.equal(Number(await evaluate('document.getElementById("calc-charge-value").textContent')),0,'Integration must not bridge the overload');
  });
  await regression('an intermediate invalid voltage mode cannot disappear without a gap',async()=>{
    const at=await beginRegression(35000,1,120);
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+1000,gap:false,frames:[fixture(122,35500,{mode:'AC'}),fixture(123,36000)]});
    assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')),'A voltage mode change must pause immediately even without a new current');
  });
  await regression('unseen fresh integration frames survive a newer status snapshot',async()=>{
    const at=await beginRegression(40000,1,130);await emit('meter:status',{status:status(42000),receivedAt:at+2000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+2000,gap:false,frames:[fixture(132,41500),fixture(133,41500,{meter:1})]});
    assert.equal(await evaluate('document.getElementById("calc-session-state").textContent'),'Running','Status/feed polling order must not cause a false replay gap');
    const ah=Number(await evaluate('document.getElementById("calc-charge-value").textContent'));assert(Math.abs(ah-1.5/3600)<1e-10,'Fresh unseen current sample should add 1.5 A-seconds');
  });
  await regression('fresh status cannot keep stalled integration runtime live',async()=>{
    const at=await beginRegression(50000,1,140,true);
    await emit('meter:status',{status:status(51000),receivedAt:at+1000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:Date.now(),gap:false,frames:[fixture(142,51000),fixture(143,51000,{meter:1})]});
    for(let i=0;i<5;i++){await sleep(700);const fresh=status(51700+i*700);fresh.meters[1].readings[0].value=10000;fresh.meters[1].readings[0].text='10000';await emit('meter:status',{status:fresh,receivedAt:Date.now()});}
    assert(/paused|gap/i.test(await evaluate('document.getElementById("calc-session-state").textContent')),'Stalled current feed must pause despite fresh status');
    assert.equal(await evaluate('document.getElementById("calc-runtime-value").textContent'),'—','Stalled integration must hide its old-current runtime estimate');
  });
  await regression('current-first startup seeds its first compatible pair once',async()=>{
    const at=await beginRegression(60000,1,150,false,true);
    await emit('meter:status',{status:status(61000),receivedAt:at+1000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+1000,gap:false,frames:[fixture(152,61000),fixture(153,61000,{meter:1})]});
    assert.equal(await evaluate('document.getElementById("calc-time-value").textContent'),'1 s','Current arriving before voltage must still seed the first current endpoint');
    const ah=Number(await evaluate('document.getElementById("calc-charge-value").textContent'));assert(Math.abs(ah-1/3600)<1e-10,'First current interval must contribute 1 A-second');
  });
  await regression('resume excludes paused-range samples and establishes a fresh segment',async()=>{
    const at=await beginRegression(65000,1,160);
    await emit('meter:status',{status:status(66000),receivedAt:at+1000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+1000,gap:false,frames:[fixture(162,66000),fixture(163,66000,{meter:1})]});
    await evaluate('document.getElementById("calc-pause").click()');
    await emit('meter:status',{status:status(67000),receivedAt:at+2000});await evaluate('document.getElementById("calc-start").click()');
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+2000,gap:false,frames:[fixture(164,66500),fixture(165,66500,{meter:1}),fixture(166,67000),fixture(167,67000,{meter:1})]});
    assert.equal(await evaluate('document.getElementById("calc-session-state").textContent'),'Running','Excluding pre-resume samples must not create a false gap');
    assert.equal(await evaluate('document.getElementById("calc-time-value").textContent'),'1 s','The paused 500 ms must not be measured after resume');
    await emit('meter:status',{status:status(68000),receivedAt:at+3000});
    await emit('meter:frames',{deviceId:'calculator-fixture',bootId:'calc-boot-1',receivedAt:at+3000,gap:false,frames:[fixture(168,68000),fixture(169,68000,{meter:1})]});
    assert.equal(await evaluate('document.getElementById("calc-time-value").textContent'),'2 s','The resumed segment must establish its own baseline');
    const ah=Number(await evaluate('document.getElementById("calc-charge-value").textContent'));assert(Math.abs(ah-2/3600)<1e-10);
  });
  assert.deepEqual(regressionFailures,[],'Calculation feed regressions must all pass');
  await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(150);
  const widths=await evaluate('({body:document.body.scrollWidth,viewport:innerWidth})');assert(widths.body<=widths.viewport,'390px calculation UI overflows');
  console.log('PASS 390px responsive calculations layout');
  console.log('ALL CALCULATION BROWSER CHECKS PASSED');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();server.close();});
