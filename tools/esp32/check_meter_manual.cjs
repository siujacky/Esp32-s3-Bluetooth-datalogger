// Visual/structural check in an isolated Chromium profile; no meter controls.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawn}=require('node:child_process'),{pathToFileURL}=require('node:url'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),url=process.argv[2] || pathToFileURL(path.join(root,'src/esp32_touch/meter/README.html')).href;
const output=path.resolve(process.argv[3] || path.join(root,'.build','manual-check'));fs.mkdirSync(output,{recursive:true});
const chrome=process.env.METER_TEST_CHROME || ['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
assert(chrome,'Chromium not found');
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),profile=fs.mkdtempSync(path.join(os.tmpdir(),'meter-manual-check-'));
let browser,ws,id=0;const pending=new Map();
async function cdp(method,params={}){return new Promise((resolve,reject)=>{const n=++id;pending.set(n,{resolve,reject});ws.send(JSON.stringify({id:n,method,params}));});}
async function evaluate(expression){const result=await cdp('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;}
async function wait(expression){for(let i=0;i<100;i++){try{if(await evaluate(expression))return;}catch(_){}await sleep(100);}throw new Error('Timed out: '+expression);}
async function screenshot(name){await evaluate('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');const shot=await cdp('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(output,name),Buffer.from(shot.data,'base64'));}
(async()=>{
  browser=spawn(chrome,['--headless=new','--disable-gpu','--no-first-run','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore',windowsHide:true});
  let port;for(let i=0;i<100;i++){try{port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];if(/^\d+$/.test(port))break;}catch(_){}await sleep(100);}assert(port,'Chromium did not start');
  const tab=await(await fetch('http://127.0.0.1:'+port+'/json/new?about:blank',{method:'PUT'})).json();ws=new WebSocket(tab.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.id && pending.has(message.id)){const entry=pending.get(message.id);pending.delete(message.id);message.error?entry.reject(new Error(JSON.stringify(message.error))):entry.resolve(message.result);}});
  await cdp('Page.enable');await cdp('Runtime.enable');await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1050,deviceScaleFactor:1,mobile:false});
  await cdp('Page.navigate',{url});await wait('document.readyState==="complete" && !!document.getElementById("meters")');
  assert.equal(await evaluate('document.querySelectorAll("#meters tbody tr").length'),35,'Missing profiles');
  assert(await evaluate('Array.from(document.links).filter(a=>a.getAttribute("href").startsWith("#")).every(a=>!!document.getElementById(decodeURIComponent(a.hash.slice(1))))'),'Broken local anchor');
  assert(await evaluate('new Set(Array.from(document.querySelectorAll("[id]")).map(e=>e.id)).size===document.querySelectorAll("[id]").length'),'Duplicate IDs');
  assert(await evaluate('!document.querySelector("script[src],link[rel=stylesheet],img[src^=http]")'),'Manual needs external rendering assets');
  const total=await evaluate('document.querySelectorAll(".function-card").length');assert(total>=39,'Incomplete function finder');
  await evaluate('document.documentElement.style.scrollBehavior="auto"');
  async function query(value){await evaluate('(()=>{const input=document.getElementById("function-search");input.value='+JSON.stringify(value)+';input.dispatchEvent(new Event("input",{bubbles:true}))})()');}
  async function visible(){return evaluate('Array.from(document.querySelectorAll(".function-card:not([hidden])")).map(e=>({group:e.dataset.group,text:e.textContent}))');}
  await query('CSV');let matches=await visible();assert(matches.length>0&&matches.length<total,'Search did not filter');assert(matches.every(e=>e.text.toLowerCase().includes('csv')),'Search returned unrelated cards');
  await query('TF CSV');matches=await visible();assert(matches.length>0&&matches.every(e=>/tf/i.test(e.text)&&/csv/i.test(e.text)),'Multiple search terms must all match');
  await evaluate('document.querySelector("[data-filter=storage]").click()');matches=await visible();assert(matches.length>0&&matches.every(e=>e.group==='storage'),'Category and text filter did not intersect');
  await query('no-such-function-12345');assert.equal((await visible()).length,0,'Impossible search has results');assert(await evaluate('!document.getElementById("finder-empty").hidden'),'Missing empty state');
  await evaluate('document.getElementById("search-clear").click()');assert.equal((await visible()).length,total,'Clear must restore every function and category');assert(await evaluate('document.activeElement.id==="function-search"'),'Clear must focus search');
  await evaluate('document.querySelector("[data-filter=calculate]").click()');matches=await visible();assert(matches.length>0&&matches.every(e=>e.group==='calculate'),'Category filter failed');
  await cdp('Emulation.setEmulatedMedia',{media:'print'});assert(await evaluate('Array.from(document.querySelectorAll(".function-card")).every(e=>getComputedStyle(e).display!=="none")'),'Printing must include filtered-out functions');await cdp('Emulation.setEmulatedMedia',{media:''});
  await evaluate('document.getElementById("search-clear").click();window.scrollTo(0,0)');
  assert(await evaluate('document.body.scrollWidth<=innerWidth'),'Desktop manual overflow');await screenshot('readme-desktop.png');
  await evaluate('document.getElementById("start").scrollIntoView()');await screenshot('readme-calibration-desktop.png');
  await evaluate('document.getElementById("finder").scrollIntoView()');await screenshot('readme-finder-desktop.png');
  await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await sleep(200);
  await evaluate('window.scrollTo(0,0)');
  const widths=await evaluate('({viewport:innerWidth,body:document.body.scrollWidth,overflow:Array.from(document.querySelectorAll("body *")).filter(e=>e.getBoundingClientRect().right>innerWidth+1).slice(0,8).map(e=>({tag:e.tagName,id:e.id,width:e.getBoundingClientRect().width,right:e.getBoundingClientRect().right}))})');
  await screenshot('readme-mobile.png');assert(widths.body<=widths.viewport,'Mobile manual overflow: '+JSON.stringify(widths));
  await evaluate('document.getElementById("finder").scrollIntoView()');await query('calibration');assert((await visible()).length>0,'Mobile search failed');await screenshot('readme-finder-mobile.png');
  await evaluate('document.getElementById("search-clear").click()');
  // Inspect every chapter, including the long API/profile tables, at each narrow viewport.
  async function chapterWidths(){return evaluate('Array.from(document.querySelectorAll("main>section")).map(e=>({id:e.id,width:e.scrollWidth,available:e.clientWidth})).filter(e=>e.width>e.available+1)');}
  assert.deepEqual(await chapterWidths(),[],'Phone chapter overflow');
  await cdp('Emulation.setDeviceMetricsOverride',{width:480,height:320,deviceScaleFactor:1,mobile:true});await sleep(150);await evaluate('window.scrollTo(0,0)');
  assert(await evaluate('document.body.scrollWidth<=innerWidth'),'480px landscape overflow');assert.deepEqual(await chapterWidths(),[],'480px chapter overflow');await screenshot('readme-landscape-480.png');
  await evaluate('document.getElementById("finder").scrollIntoView()');await screenshot('readme-finder-landscape-480.png');
  const results={passed:true,url,profiles:35,functions:total,sections:await evaluate('document.querySelectorAll("main > section").length'),checks:['all local anchors','unique IDs','offline assets','text and multiple-term search','category and text intersection','empty state','clear and focus','print all functions','desktop / 390px phone / 480px landscape overflow'],mobile:widths,landscape:{width:480,height:320}};
  if(process.argv[4]){
    await cdp('Emulation.setDeviceMetricsOverride',{width:512,height:512,deviceScaleFactor:1,mobile:false});
    await cdp('Page.navigate',{url:process.argv[4]});await wait('document.readyState==="complete" && document.documentElement.tagName.toLowerCase()==="svg"');await screenshot('wifi-qr-web-private.png');results.webQrRendered=true;
  }
  fs.writeFileSync(path.join(output,'readme-verification.json'),JSON.stringify(results,null,2));console.log('PASS complete manual, 35 profiles, '+total+' searchable functions, local anchors, search/filter/clear/empty/print, desktop + 390px phone + 480px landscape layout'+(results.webQrRendered?', actual Wi-Fi QR SVG':''));
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(async()=>{if(ws){try{await cdp('Browser.close');}catch(_){}ws.close();}if(browser)browser.kill();});
