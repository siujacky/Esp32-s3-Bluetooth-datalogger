// Isolated source-asset Chromium fixture. No requests are sent to a physical meter.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),web=path.join(root,'src/esp32_touch/meter/web'),sleep=ms=>new Promise(r=>setTimeout(r,ms));
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(f=>fs.existsSync(f));
assert(chrome,'Set METER_TEST_CHROME to a Chromium executable.');
const addresses=['AA','BB','CC'],names=['Meter 1','Meter 2','Meter 3'],units=[['V','Hz'],['V','mV'],['°C','°F']],values=[[12.34,50],[24.68,250],[23.5,74.3]];
let autoDim=true,dimFailure=false,dimRequests=[];
let seq=0,frames=[],gap=false,blocked=false,states=['connected','connected','connected'],stale=[false,false,false],available=[false,false,false],modeKeys=[1,1,1];
function add(slot,ch,extra={}){frames.push({kind:'reading',seq:++seq,meter:slot,channel:ch,address:addresses[slot],profile:'fixture',numeric:true,value:values[slot][ch],text:String(values[slot][ch]),unit:units[slot][ch],mode:'DC',modeKey:modeKeys[slot],uptimeMs:99900,...extra});}
function triplet(slot,ch){const changes=[[-.05,.02,0],[.02,-.06,0],[-.03,.03,0]];for(let t=0;t<3;t++)add(slot,ch,{uptimeMs:90000+t*4500,value:values[slot][ch]*(1+changes[slot][t])});}
for(let i=0;i<3;i++)for(let ch=0;ch<2;ch++)triplet(i,ch);
function status(){return {display:{autoDim,autoDimSaved:true,dimmed:false,brightnessPercent:55,dimAfterMs:120000},device:{id:'multi-fixture',bootId:'multi-boot',uptimeMs:100000},network:{},recording:{active:false},meters:[0,1,2].map(slot=>({slot,name:names[slot],address:addresses[slot],profile:'fixture',state:states[slot],readings:available[slot]?[0,1].map(channel=>({channel,value:values[slot][channel],numeric:true,text:String(values[slot][channel]),unit:units[slot][channel],mode:'DC',modeKey:modeKeys[slot],ageMs:stale[slot]?9000:50,fresh:!stale[slot]})):[]})),discovery:{devices:[],scanning:false},webhook:{queued:0,dropped:0},rules:[]};}
function fixtureHtml(){let html=fs.readFileSync(path.join(web,'index.html'),'utf8');for(const [marker,file] of Object.entries({METER_CSS:'style.css',METER_CALC_CSS:'calculations.css',METER_CARD_CSS:'card.css',METER_CORE:'core.js',METER_CALCULATIONS:'calculations.js',METER_CALC_UI:'calculations_ui.js',METER_CARD_UI:'card_ui.js',METER_VALUE_FORMAT:'value_format.js',METER_VALUE_TILES:'value_tiles.js',METER_APP:'app.js'}))html=html.replace('/* '+marker+' */',()=>fs.readFileSync(path.join(web,file),'utf8'));return html.replace('</head>','<link rel="icon" href="data:,"></head><script>window.fixtureBlocked='+blocked+';if(window.fixtureBlocked)Object.defineProperty(window,"localStorage",{get(){throw new DOMException("Fixture denied","SecurityError");}});</script>');}
const server=http.createServer((req,res)=>{const url=new URL(req.url,'http://localhost'),json=data=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(data));};
  if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fixtureHtml());return;}
  if(url.pathname==='/api/status'){json(status());return;}
  if(url.pathname==='/api/profiles'){json([{id:'fixture',name:'Fixture'}]);return;}
  if(url.pathname==='/api/control'&&req.method==='POST'){let body='';req.on('data',part=>body+=part);req.on('end',()=>{const data=JSON.parse(body);if(data.action==='auto-dim'){dimRequests.push(data);if(dimFailure){res.statusCode=503;json({error:'Fixture refused dim request'});return;}assert.equal(typeof data.enabled,'boolean');autoDim=data.enabled;}json({ok:true});});return;}
  if(url.pathname==='/api/frames'){const after=Number(url.searchParams.get('after')||0),page=frames.filter(f=>available[f.meter]&&f.seq>after);json({bootId:'multi-boot',frames:page,gap:gap&&page.length>0,oldestSeq:1,latestSeq:seq,nextSeq:page.length?page.at(-1).seq:after});return;}
  if(url.pathname.startsWith('/api/')){json({ok:true,files:[]});return;}
  res.statusCode=204;res.end();
});
let browser,ws,messageId=0;const pending=new Map(),errors=[],surfaceProof=[];
async function cdp(method,params={}){const id=++messageId;return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const r=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw new Error(JSON.stringify(r.exceptionDetails));return r.result.value;}
async function waitFor(expression){for(let i=0;i<100;i++){try{if(await evaluate('Boolean('+expression+')'))return;}catch(e){if(!/Execution context was destroyed|Cannot find context/.test(e.message))throw e;}await sleep(100);}assert.fail('Timed out: '+expression);}
async function field(id,value){await evaluate('(()=>{const e=document.getElementById('+JSON.stringify(id)+');e.value='+JSON.stringify(String(value))+';e.dispatchEvent(new Event("change",{bubbles:true}));})()');}
async function focus(slot){await evaluate('document.querySelectorAll(".meter-card .button-row button")[ '+slot+'*3].click()');}
async function shot(name){if(!process.env.METER_TEST_SCREENSHOT_DIR)return;fs.mkdirSync(process.env.METER_TEST_SCREENSHOT_DIR,{recursive:true});const expanded=await evaluate('!!document.fullscreenElement || document.getElementById("instrument").classList.contains("expanded")');const r=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:!expanded});fs.writeFileSync(path.join(process.env.METER_TEST_SCREENSHOT_DIR,name+'.png'),Buffer.from(r.data,'base64'));}
async function counts(){return evaluate('({cards:document.querySelectorAll("#multi-values .display-value-card").length,lanes:document.querySelectorAll("#scope-lanes .scope-lane").length,slots:Array.from(document.querySelectorAll("#scope-lanes .scope-lane")).map(e=>Number(e.dataset.slot))})');}
async function lane(slot){return evaluate('(()=>{const e=Array.from(document.querySelectorAll("#scope-lanes .scope-lane")).find(e=>e.dataset.slot==='+JSON.stringify(String(slot))+');return {value:e.querySelector(".lane-current-value").textContent,unit:e.querySelector(".lane-unit").textContent,status:e.querySelector(".lane-status").textContent,range:e.querySelector(".lane-range").textContent,empty:!e.querySelector(".scope-empty").hidden,end:e.querySelector("canvas").dataset.end};})()');}
// A reserved toolbar, reading card, legend or axis gutter must fail these checks.
async function fullSurface(expanded=false){
  if(!expanded)await evaluate('document.getElementById("instrument").scrollIntoView({block:"start"})');
  await sleep(100);
  const geometry=await evaluate(`(()=>{
    const box=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom};};
    const visible=e=>e.getClientRects().length>0;
    const instrument=document.getElementById('instrument'),canvas=document.getElementById('scope'),c=box(canvas),count=Number(instrument.dataset.count);
    const controls=['display-count','chart-layout','scope-window','clear-scope','fullscreen-button',...(count===1?['focus-channel']:Array.from(document.querySelectorAll('#scope-overlay-legend select')).map(e=>e.id))].map(id=>{
      const e=document.getElementById(id),r=box(e),hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
      return {id,...r,visible:visible(e),hit:hit===e||e.contains(hit)};
    });
    const huds=Array.from(document.querySelectorAll('.instrument-toolbar,#single-reading,.scope-toolbar,#scope-overlay-legend,.scope-footer')).filter(visible).map(e=>({id:e.id||e.className,...box(e)}));
    return {canvas:c,instrument:box(instrument),viewport:{x:0,y:0,width:innerWidth,height:innerHeight},controls,huds,duplicateCards:Array.from(document.querySelectorAll('#multi-values .display-value-card')).filter(visible).length};
  })()`);
  const target=expanded?geometry.viewport:geometry.instrument;
  for(const key of ['x','y','width','height'])assert(Math.abs(geometry.canvas[key]-target[key])<=1,'Canvas must fill '+(expanded?'viewport':'instrument')+' '+key+': '+JSON.stringify(geometry));
  assert.equal(geometry.duplicateCards,0,'Overlay must show each current value once');
  if(geometry.viewport.width>=1000)assert(geometry.huds.find(h=>h.id==='instrument-toolbar').height<=80,'Desktop controls must remain a compact floating HUD');
  for(const hud of [...geometry.huds,...geometry.controls]){
    assert(hud.width>0 && hud.height>0 && hud.x>=geometry.canvas.x-1 && hud.y>=geometry.canvas.y-1 && hud.right<=geometry.canvas.right+1 && hud.bottom<=geometry.canvas.bottom+1,'HUD/control outside chart: '+JSON.stringify(hud));
  }
  // Normal stages may extend below the browser viewport, so hit testing is done
  // when expanded, where all controls must be visible and operable together.
  if(expanded){for(const control of geometry.controls)assert(control.visible&&control.hit,'HUD obscures '+JSON.stringify(control));assert.equal(await evaluate('getComputedStyle(document.documentElement).overflowY'),'hidden','Expanded chart must lock page scrolling and remove its scrollbar');}
  const edges=await evaluate(`(()=>{const c=document.getElementById('scope'),d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;let left=0,top=0,right=0,bottom=0;for(let y=0;y<c.height;y++)for(let x=0;x<c.width;x++){const p=(y*c.width+x)*4;if(d[p+3]&&d[p]===38&&d[p+1]===59&&d[p+2]===69){if(x<8)left++;if(y<8)top++;if(x>=c.width-8)right++;if(y>=c.height-8)bottom++;}}return {left,top,right,bottom};})()`);
  for(const [edge,pixels] of Object.entries(edges))assert(pixels>10,'Grid must reach '+edge+' edge, with axis labels floating over it: '+JSON.stringify(edges));
  surfaceProof.push({count:await evaluate('document.getElementById("display-count").value'),expanded,...geometry,gridPixels:edges});return geometry;
}
async function dataBeneathHuds(){
  const pixels=await evaluate(`(()=>{
    const c=document.getElementById('scope'),r=c.getBoundingClientRect(),d=c.getContext('2d').getImageData(0,0,c.width,c.height).data,colors=[[98,226,191],[123,188,255],[195,160,255]];
    const huds=Array.from(document.querySelectorAll('.instrument-toolbar,#single-reading,.scope-toolbar,#scope-overlay-legend,.scope-footer')).filter(e=>e.getClientRects().length).map(e=>e.getBoundingClientRect());
    let under=0,outside=0;for(let y=0;y<c.height;y++)for(let x=0;x<c.width;x++){const p=(y*c.width+x)*4;if(d[p+3]&&colors.some(rgb=>rgb.every((v,k)=>Math.abs(d[p+k]-v)<8))){const px=r.x+x*r.width/c.width,py=r.y+y*r.height/c.height;if(huds.some(h=>px>=h.x&&px<h.right&&py>=h.y&&py<h.bottom))under++;else outside++;}}
    return {under,outside};
  })()`);
  assert(pixels.under>15,'Data must extend beneath floating metadata: '+JSON.stringify(pixels));assert(pixels.outside>100,'Data must also remain visible outside metadata: '+JSON.stringify(pixels));
  return pixels;
}
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));const origin='http://127.0.0.1:'+server.address().port,profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-multi-fixture-'));
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port;for(let i=0;i<80;i++){try{port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(port))break;}catch(_){}await sleep(100);}assert(port,'Chromium did not start');
  const tab=await(await fetch('http://127.0.0.1:'+port+'/json/new?about:blank',{method:'PUT'})).json();ws=new WebSocket(tab.webSocketDebuggerUrl);await new Promise((r,j)=>{ws.addEventListener('open',r,{once:true});ws.addEventListener('error',j,{once:true});});
  ws.addEventListener('message',event=>{const m=JSON.parse(event.data);if(m.id&&pending.has(m.id)){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);}if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails.text);if(m.method==='Runtime.consoleAPICalled'&&m.params.type==='error')errors.push(JSON.stringify(m.params.args));if(m.method==='Log.entryAdded'&&m.params.entry.level==='error')errors.push(m.params.entry.text);});
  await cdp('Page.enable');await cdp('Runtime.enable');await cdp('Log.enable');await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1100,deviceScaleFactor:1,mobile:false});await cdp('Page.navigate',{url:origin});
  await waitFor('document.getElementById("api-state")?.textContent==="Device online"');
  await field('instrument-view','scope');
  assert.equal(await evaluate('document.getElementById("chart-layout")?.value'),'overlay','Overlay must be the default');
  await evaluate('location.hash="network"');await waitFor('!document.getElementById("auto-dim").disabled');
  assert.equal(await evaluate('document.getElementById("auto-dim").getAttribute("aria-pressed")'),'true');
  await evaluate('document.getElementById("auto-dim").click()');await waitFor('document.getElementById("auto-dim").textContent==="Auto dim: Off"');
  assert.deepEqual(dimRequests.at(-1),{action:'auto-dim',enabled:false});
  dimFailure=true;await evaluate('document.getElementById("auto-dim").click()');await waitFor('document.getElementById("notice").textContent.includes("Auto dim failed")');
  assert.equal(autoDim,false);assert.equal(await evaluate('document.getElementById("auto-dim").getAttribute("aria-pressed")'),'false');
  await sleep(100);assert.deepEqual(errors.splice(0),['Failed to load resource: the server responded with a status of 503 (Service Unavailable)'],'Only the intentionally refused request may report a network error');
  dimFailure=false;await evaluate('document.getElementById("auto-dim").click()');await waitFor('document.getElementById("auto-dim").textContent==="Auto dim: On"');
  await evaluate('location.hash="live"');await waitFor('!document.getElementById("panel-live").hidden');
  assert.equal(await evaluate('document.getElementById("display-count").value'),'1');
  await field('display-count',3);let initial=await overlay();assert.equal(initial.canvases,1);assert(initial.ranges.every(r=>r.includes('0 received')));assert(initial.heading.includes('WAITING'),'Missing slots must not invent numeric axes');await shot('overlay-all-empty');
  available[0]=true;await focus(0);await waitFor('document.querySelector("#overlay-trace-0 .lane-range")?.textContent.includes("3 received")');initial=await overlay();assert(initial.heading.includes('SHARED V'));assert(initial.ranges[1].includes('No numeric'));assert(initial.ranges[2].includes('No numeric'));await shot('overlay-one-valid-two-empty');
  available=[true,true,true];await focus(0);await waitFor('document.querySelector("#overlay-trace-2 .lane-range")?.textContent.includes("3 received")');await field('display-count',1);
  assert.equal(await evaluate('document.getElementById("big-value").textContent'),'12.34');
  await waitFor('document.getElementById("scope-range").textContent.includes("3 received")');await shot('overlay-single-desktop');await fullSurface();
  async function overlay(){return evaluate('({canvases:document.querySelectorAll("#instrument canvas").length,cards:document.querySelectorAll("#multi-values .display-value-card").length,slots:Array.from(document.querySelectorAll("#scope-overlay-legend .overlay-trace")).map(e=>Number(e.dataset.slot)),ranges:Array.from(document.querySelectorAll("#scope-overlay-legend .lane-range")).map(e=>e.textContent),labels:Array.from(document.querySelectorAll("#scope-overlay-legend .overlay-trace")).map(e=>e.textContent),heading:document.getElementById("scope-unit").textContent})');}
  async function tracePixels(slots){const found=await evaluate('(()=>{const c=document.getElementById("scope"),d=c.getContext("2d").getImageData(0,0,c.width,c.height).data,colors=[[98,226,191],[123,188,255],[195,160,255]],n=[0,0,0];for(let y=35;y<c.height-35;y++)for(let x=Math.ceil(c.width*.5);x<c.width-18;x++){const p=(y*c.width+x)*4;colors.forEach((rgb,i)=>{if(rgb.every((v,k)=>Math.abs(d[p+k]-v)<8))n[i]++;});}return n;})()');slots.forEach(slot=>assert(found[slot]>15,'Plot area missing trace color for slot '+slot+': '+found));}
  await field('display-count',2);let model=await overlay();assert.equal(model.canvases,1);assert.equal(model.cards,2);assert.deepEqual(model.slots,[0,1]);assert(model.heading.includes('SHARED'));
  assert.equal(model.ranges[0],model.ranges[1],'Same-unit traces need one numeric range');assert(model.ranges[0].includes(' V · 3 received'));await tracePixels([0,1]);await fullSurface();await shot('overlay-two-same-unit-desktop');
  await focus(1);assert.deepEqual((await overlay()).slots,[1,2]);await focus(2);assert.deepEqual((await overlay()).slots,[2,3]);await focus(0);
  await field('display-count',3);model=await overlay();assert.equal(model.canvases,1);assert.deepEqual(model.slots,[0,1,2]);assert(model.heading.includes('INDEPENDENT'));assert(model.ranges[2].includes('°C'));await tracePixels([0,1,2]);await fullSurface();await shot('overlay-three-mixed-desktop');
  names.fill('UT60BT');await waitFor('Array.from(document.querySelectorAll("#scope-overlay-legend h3")).every(e=>e.textContent.includes("UT60BT"))');
  const titles=await evaluate('Array.from(document.querySelectorAll("#scope-overlay-legend h3")).filter(e=>e.getClientRects().length).map(e=>e.textContent)');
  assert.equal(titles.length,3);titles.forEach((title,slot)=>assert(title.includes('Meter '+(slot+1))&&title.includes('UT60BT')&&title.includes('Channel 1'),'Identical advertised names must retain visible slot and channel identity: '+JSON.stringify(titles)));
  units[2][0]='V';values[2][0]=37.02;triplet(2,0);
  await waitFor('document.getElementById("scope-unit").textContent.includes("SHARED V") && document.querySelector("#overlay-trace-2 .lane-range").textContent.includes(" V · 3 received") && document.getElementById("feed-state").textContent.includes("sequence '+seq+'")');model=await overlay();assert.equal(model.canvases,1);assert(model.ranges.every(r=>r===model.ranges[0]));await tracePixels([0,1,2]);await shot('overlay-three-same-unit-desktop');
  units[2][0]='°C';values[2][0]=23.5;triplet(2,0);
  await waitFor('document.querySelector("#overlay-trace-2 .lane-unit").textContent==="°C" && document.querySelector("#overlay-trace-2 .lane-range").textContent.includes("3 received")');
  await field('meter-channel-1',1);model=await overlay();assert(model.labels[1].includes('Channel 2'));assert(model.labels[1].includes('250'));assert(model.ranges[1].includes('mV'));assert(model.ranges.every(r=>r.includes('3 received')),'Changing a channel destroyed existing history');await shot('overlay-three-mixed-units');
  // Decoder modes can change identity while retaining the same display mode label/unit.
  await evaluate('window.addEventListener("meter:status",e=>{window.fixtureModeSeen=e.detail.status.meters[0].readings[0].modeKey;})');
  modeKeys[0]=2;await focus(0);await waitFor('window.fixtureModeSeen===2');model=await overlay();
  assert(model.ranges[0].includes('0 received'),'Old modeKey history remained visible beneath a new same-unit/same-label reading');assert(model.ranges[1].includes('3 received'));assert(model.ranges[2].includes('3 received'));await shot('overlay-modekey-awaiting-frame');
  triplet(0,0);await waitFor('document.querySelector("#overlay-trace-0 .lane-range").textContent.includes("3 received") && document.getElementById("feed-state").textContent.includes("sequence '+seq+'")');model=await overlay();assert(model.ranges.every(r=>r.includes('3 received')),'Matching mode frames must restore only the affected trace');
  // Stale and disconnected history stays visible and labeled, never looks live.
  stale[1]=true;states[2]='disconnected';await waitFor('document.querySelector("#overlay-trace-1 .lane-status")?.textContent.includes("STALE")');
  model=await overlay();assert(model.labels[2].includes('disconnected'));assert(model.labels[2].includes('23.5'));assert(model.labels[1].includes('250'));await shot('overlay-three-stale-disconnected');
  // A source replacement removes only that slot, while numeric invalid frames make gaps.
  addresses[0]='REPLACED';await waitFor('document.querySelector("#overlay-trace-0 .lane-range")?.textContent.includes("0 received")');
  model=await overlay();assert(model.ranges[1].includes('3 received'));assert(model.ranges[2].includes('3 received'));
  add(0,0,{uptimeMs:100000,numeric:false,value:null,text:'OL'});await waitFor('document.getElementById("feed-state").textContent.includes("sequence '+seq+'")');
  assert((await overlay()).ranges[0].includes('No numeric'));add(0,0,{uptimeMs:100100});await waitFor('document.querySelector("#overlay-trace-0 .lane-range")?.textContent.includes("2 received")');
  gap=true;add(0,0,{uptimeMs:100200});await waitFor('document.getElementById("feed-state").textContent.includes("Gap detected")');gap=false;
  assert((await overlay()).ranges[1].includes('3 received'));await evaluate('document.getElementById("clear-scope").click()');assert((await overlay()).ranges.every(r=>r.includes('0 received')));
  await field('meter-channel-1',0);assert((await overlay()).ranges[1].includes('3 received'),'Clear removed a hidden channel history');
  // Persist Separate and Overlay independently of count/focus/channel, then reload RAM history.
  await field('chart-layout','separate');assert.equal((await counts()).lanes,3);assert.equal((await lane(1)).unit,'V');
  let before=await evaluate('performance.timeOrigin');await cdp('Page.reload');await waitFor('performance.timeOrigin!=='+before+' && document.getElementById("api-state")?.textContent==="Device online"');
  assert.equal(await evaluate('document.getElementById("chart-layout").value'),'separate');assert.equal((await counts()).lanes,3);assert.equal(await evaluate('document.getElementById("display-count").value'),'3');
  await field('chart-layout','overlay');await field('meter-channel-1',1);before=await evaluate('performance.timeOrigin');await cdp('Page.reload');await waitFor('performance.timeOrigin!=='+before+' && document.getElementById("api-state")?.textContent==="Device online"');
  assert.equal(await evaluate('document.getElementById("chart-layout").value'),'overlay');assert.equal(await evaluate('document.getElementById("meter-channel-1").value'),'1');
  stale=[false,false,false];states=['connected','connected','connected'];
  await focus(0);await waitFor('Array.from(document.querySelectorAll("#scope-overlay-legend .lane-status")).every(e=>!e.textContent.includes("disconnected") && !e.textContent.includes("STALE"))');
  for(let i=0;i<3;i++)for(let ch=0;ch<2;ch++)triplet(i,ch);
  await waitFor('Array.from(document.querySelectorAll("#scope-overlay-legend .lane-range")).every(e=>e.textContent.includes("3 received"))');
  for(const count of [1,2,3]){
    await field('display-count',count);await evaluate('document.getElementById("instrument").requestFullscreen=undefined;document.getElementById("fullscreen-button").click()');await sleep(100);
    assert(await evaluate('document.getElementById("instrument").classList.contains("expanded")'));await fullSurface(true);
    await shot('overlay-expanded-'+count);await evaluate('document.getElementById("fullscreen-button").click()');
  }
  async function key(key,modifiers=0){await cdp('Input.dispatchKeyEvent',{type:'keyDown',key,code:key,modifiers});await cdp('Input.dispatchKeyEvent',{type:'keyUp',key,code:key,modifiers});}
  // Expanded is a keyboard-contained instrument, including its CSS fallback.
  await field('display-count',3);await evaluate('document.getElementById("fullscreen-button").click();document.getElementById("instrument-view").focus()');
  await key('Tab',8);const wrappedBack=await evaluate('document.activeElement.id');
  await evaluate('document.getElementById("meter-channel-2").focus()');await key('Tab');const wrappedForward=await evaluate('document.activeElement.id');
  await evaluate('location.hash="network"');await waitFor('!document.getElementById("panel-network").hidden');await sleep(100);
  const left=await evaluate('({expanded:document.getElementById("instrument").classList.contains("expanded"),locked:document.documentElement.classList.contains("instrument-expanded"),pressed:document.getElementById("fullscreen-button").getAttribute("aria-pressed"),activePanel:document.activeElement.dataset.panel})');
  await evaluate('location.hash="live"');await waitFor('!document.getElementById("panel-live").hidden');
  const returned=await evaluate('({expanded:document.getElementById("instrument").classList.contains("expanded"),locked:document.documentElement.classList.contains("instrument-expanded")})');
  console.log('Expanded keyboard/panel navigation: '+JSON.stringify({wrappedBack,wrappedForward,left,returned}));
  assert.equal(wrappedBack,'meter-channel-2','Shift+Tab must wrap inside visible expanded controls');assert.equal(wrappedForward,'instrument-view','Tab must wrap inside visible expanded controls');
  assert.deepEqual(left,{expanded:false,locked:false,pressed:'false',activePanel:'network'},'Leaving Live must close expanded mode, release scrolling and focus the destination tab');assert.deepEqual(returned,{expanded:false,locked:false},'Returning Live must retain the normal chart surface');await fullSurface();
  await evaluate('document.getElementById("fullscreen-button").click();document.getElementById("meter-channel-2").disabled=true;document.getElementById("instrument-view").focus()');await key('Tab',8);assert.equal(await evaluate('document.activeElement.id'),'meter-channel-1','Disabled HUD controls must be skipped');await evaluate('document.getElementById("meter-channel-2").disabled=false');await key('Escape');
  assert.equal(await evaluate('document.activeElement.id'),'fullscreen-button','Escape must restore focus to Expand');assert.equal(await evaluate('document.documentElement.classList.contains("instrument-expanded")'),false);
  await evaluate('delete document.getElementById("instrument").requestFullscreen');await cdp('Page.bringToFront');await cdp('Emulation.setFocusEmulationEnabled',{enabled:true});
  for(const count of [1,2,3]){
    await field('display-count',count);await cdp('Runtime.evaluate',{expression:'document.getElementById("fullscreen-button").click()',userGesture:true});await waitFor('document.fullscreenElement?.id==="instrument"');await fullSurface(true);assert.equal(await evaluate('document.querySelectorAll("#instrument canvas").length'),1);
    await evaluate('document.getElementById("instrument-view").focus()');await key('Tab',8);assert.equal(await evaluate('document.activeElement.id'),count===1?'clear-scope':'meter-channel-'+(count-1),'Native fullscreen must contain keyboard focus');await key('Tab');assert.equal(await evaluate('document.activeElement.id'),'instrument-view');await shot('overlay-fullscreen-'+count);
    const transition=await evaluate('(async()=>{await document.exitFullscreen();const snapshot=()=>({fullscreen:!!document.fullscreenElement,pressed:document.getElementById("fullscreen-button").getAttribute("aria-pressed"),focus:document.activeElement.id});const immediate=snapshot();await new Promise(requestAnimationFrame);return {immediate,nextFrame:snapshot()};})()');
    console.log('Native fullscreen exit count '+count+': '+JSON.stringify(transition));
    // The browser exit promise can resolve before fullscreenchange updates the
    // application. Wait for that completed UI transition, then assert focus.
    await waitFor('!document.fullscreenElement && document.getElementById("fullscreen-button").getAttribute("aria-pressed")==="false"');
    assert.equal(await evaluate('document.activeElement.id'),'fullscreen-button','Native fullscreen exit must restore Expand focus');
  }
  await cdp('Runtime.evaluate',{expression:'document.getElementById("fullscreen-button").click()',userGesture:true});await waitFor('document.fullscreenElement?.id==="instrument"');await evaluate('location.hash="network"');await waitFor('!document.fullscreenElement && !document.getElementById("panel-network").hidden');
  assert.equal(await evaluate('document.documentElement.classList.contains("instrument-expanded")'),false,'Panel navigation must also release actual browser fullscreen');await evaluate('location.hash="live"');await waitFor('!document.getElementById("panel-live").hidden');await fullSurface();
  await field('display-count',3);await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(150);assert(await evaluate('document.body.scrollWidth<=innerWidth'),'390px mobile overflow');await tracePixels([0,1,2]);await shot('overlay-three-mobile');
  for(const [width,height] of [[390,844],[480,320]]){
    await cdp('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:true});
    for(const count of [1,2,3]){
      await field('display-count',count);await fullSurface();
      await evaluate('document.getElementById("instrument").requestFullscreen=undefined;document.getElementById("fullscreen-button").click()');await fullSurface(true);
      assert(await evaluate('document.body.scrollWidth<=innerWidth'),'Mobile expanded overflow');await shot('overlay-'+width+'x'+height+'-expanded-'+count);
      // A real pointer must reach Exit through the floating layers.
      const exit=await evaluate('(()=>{const r=document.getElementById("fullscreen-button").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()');
      await cdp('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...exit});await cdp('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...exit});
      assert.equal(await evaluate('document.getElementById("instrument").classList.contains("expanded")'),false,'Pointer could not exit expanded chart');
    }
  }
  blocked=true;await cdp('Page.reload');await waitFor('window.fixtureBlocked && document.getElementById("api-state")?.textContent==="Device online"');await field('instrument-view','scope');
  assert.equal(await evaluate('document.getElementById("chart-layout").value'),'overlay');assert.equal(await evaluate('document.getElementById("display-count").value'),'1');await field('display-count',3);assert.equal((await overlay()).canvases,1);await field('chart-layout','separate');assert.equal((await counts()).lanes,3);await field('chart-layout','overlay');assert.equal((await overlay()).canvases,1);await fullSurface();
  // Deliberately span the entire time window and place peaks beneath the HUD.
  // These remain test-only received frames, never a generated product waveform.
  await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1100,deviceScaleFactor:1,mobile:false});
  await field('meter-channel-1',0);await evaluate('document.getElementById("clear-scope").click()');
  const levels=[0,100,40,80,10,70,20,90,30,60];
  for(let slot=0;slot<3;slot++)for(let step=0;step<=20;step++)add(slot,0,{uptimeMs:70000+step*1500,value:levels[(step+slot)%levels.length]});
  await waitFor('document.getElementById("feed-state").textContent.includes("sequence '+seq+'")');
  const dataPixels=[];
  for(const count of [1,2,3]){await field('display-count',count);await evaluate('document.getElementById("instrument").requestFullscreen=undefined;document.getElementById("fullscreen-button").click()');await fullSurface(true);dataPixels.push({count,...await dataBeneathHuds()});await shot('full-surface-data-beneath-huds-'+count);await evaluate('document.getElementById("fullscreen-button").click()');}
  if(process.env.METER_TEST_SCREENSHOT_DIR)fs.writeFileSync(path.join(process.env.METER_TEST_SCREENSHOT_DIR,'surface-geometry.json'),JSON.stringify({surfaces:surfaceProof,dataPixels},null,2));
  assert.deepEqual(errors,[],'Browser errors');
  console.log('PASS Overlay 1/2/3, actual single canvas, equal-unit combined ranges, mixed independent unit ranges and all colored trace pixels');
  console.log('PASS slot rotation, channel/history retention, missing/invalid/source reset isolation, stale/disconnected truth and gaps');
  console.log('PASS Separate regression, layout/count/channel reload, expanded/native fullscreen, mobile nooverflow and denied localStorage; zero console errors');
  console.log('PASS exact full-surface/viewport geometry, all four grid edges, overlaid visible/hittable controls and pointer Exit at 390x844 and 480x320 for counts 1/2/3');
  console.log('PASS actual colored data pixels beneath and outside floating metadata: '+JSON.stringify(dataPixels));
  console.log('PASS unique visible slot/channel labels for identical Bluetooth names; keyboard focus containment and exit restoration; hash navigation releases fallback/native fullscreen and scrolling');
  console.log('ALL OVERLAY BROWSER CHECKS PASSED');
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();server.close();});
