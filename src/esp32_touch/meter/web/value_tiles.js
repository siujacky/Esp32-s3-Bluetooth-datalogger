(function(root,factory){const api=factory(typeof module==='object'&&module.exports?require('./value_format.js'):root.MeterValueFormat);if(typeof module==='object'&&module.exports)module.exports=api;else root.MeterValueTiles=api;})(typeof globalThis!=='undefined'?globalThis:this,function(F){
  'use strict';
  const key='meter-workbench-values-v1';
  function defaults(){return {version:1,view:'values',presets:{1:'solo',2:'large-small',3:'large-small-small',4:'grid'},formats:Array.from({length:4},()=>Array.from({length:8},()=>({digits:6,decimals:2,padding:'blank'})))};}
  function compatible(p,count){return p==='grid'?count===4:p==='solo'?count===1:p==='equal'?count===2||count===3:p==='large-small'?count===2:(p==='large-small-small'||p==='large-large-small')&&count===3;}
  function presetCount(p,count){return p==='grid'?4:p==='solo'?1:p==='equal'?(count===3?3:2):p==='large-small'?2:p==='large-small-small'||p==='large-large-small'?3:0;}
  function readPreferences(getStorage){const out=defaults();Object.defineProperty(out,"storageAvailable",{value:true,writable:true});try{let stored;try{stored=getStorage().getItem(key);}catch(_){out.storageAvailable=false;return out;}const raw=JSON.parse(stored);if(!raw||typeof raw!=='object'||raw.version!==1)return out;if(raw.view==='values'||raw.view==='scope')out.view=raw.view;for(let c=1;c<=4;c++)if(raw.presets&&compatible(raw.presets[c],c))out.presets[c]=raw.presets[c];for(let s=0;s<4;s++)for(let c=0;c<8;c++)if(raw.formats&&raw.formats[s]&&F.validNumberFormat(raw.formats[s][c]))out.formats[s][c]={...raw.formats[s][c]};}catch(_){}return out;}
  function savePreferences(getStorage,prefs){try{getStorage().setItem(key,JSON.stringify(prefs));return true;}catch(_){return false;}}
  function readingNumber(r,format){if(!r)return F.formatNumber(null,format);if(r.numeric===true&&typeof r.value==='number')return F.formatNumber(r.value,format);return {state:'invalid',cells:r.text==='OL'?'OL':'ERR',decimalIndex:255};}
  const hosts=new WeakMap();let observer,measureContext;
  function metrics(width,height,capacity,role,maxEm=Infinity){
    if(!measureContext){const canvas=document.createElement('canvas');measureContext=canvas.getContext('2d');}
    const cap={main:280,large:220,small:78,equal:180,chart:64,mini:32,preview:80}[role]||140;
    let size=Math.floor(Math.min(cap,maxEm,width/(capacity*.6001),height/.74));
    for(;size>=4;size--){measureContext.font='500 '+size+'px MeterDigits';const sample=measureContext.measureText('0123456789.+-OLVREe'),cell=Math.ceil(measureContext.measureText('0').width),top=Math.ceil(sample.actualBoundingBoxAscent||size*.73),bottom=Math.ceil(sample.actualBoundingBoxDescent||size*.02);if(cell*capacity<=width&&top+bottom<=height)return {size,cell,x:width-cell*capacity,baseline:(height-top-bottom)/2+top};}
    return null;
  }
  function updateNumber(host){
    const data=hosts.get(host);if(!data)return;const box=host.getBoundingClientRect(),width=Math.floor(box.width),height=Math.floor(box.height);if(width<=0||height<=0)return;
    const count=data.format.digits+1+(data.format.decimals?1:0),m=metrics(width,height,count,data.role,data.maxEm);if(!m){host.textContent=data.formatted.cells.trim();host.dataset.fontError='Number area too small';return;}
    const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 '+width+' '+height);svg.setAttribute('role','img');svg.setAttribute('aria-label',data.label);svg.setAttribute('focusable','false');
    const content=data.formatted.cells.padStart(count,' ');let x=m.x;
    for(const c of content){if(c!==' '){const t=document.createElementNS(ns,'text');t.setAttribute('x',String(x));t.setAttribute('y',String(m.baseline));t.setAttribute('font-family','MeterDigits,monospace');t.setAttribute('font-weight','500');t.setAttribute('font-size',String(m.size));t.setAttribute('fill','currentColor');t.textContent=c;svg.append(t);}x+=m.cell;}
    host.replaceChildren(svg);host.dataset.fontSize=String(m.size);host.dataset.decimalX=String(data.formatted.decimalIndex===255?-1:m.x+data.formatted.decimalIndex*m.cell);host.dataset.formatted=data.formatted.cells;host.dataset.numberState=data.formatted.state;host.dataset.fontError='';
  }
  function setNumber(host,r,format,role='chart',label,maxEm=Infinity){
    if(typeof document==='undefined'||!host)return;
    const formatted=readingNumber(r,format);host.classList.add('meter-number');const old=hosts.get(host);hosts.set(host,{formatted,format:{...format},role,maxEm,label:label||(formatted.state==='overflow'?'Display overflow; original '+(r&&r.text||''):formatted.state==='unavailable'?'No reading':formatted.cells==='OL'?'Overload':formatted.cells.trim())+(r&&r.unit?' '+r.unit:'')});
    if(!observer&&typeof ResizeObserver==='function')observer=new ResizeObserver(items=>{for(const item of items)updateNumber(item.target);});if(!old&&observer)observer.observe(host);
    updateNumber(host);
  }
  function refreshNumbers(rootNode){if(typeof document==='undefined')return;for(const host of (rootNode||document).querySelectorAll('.meter-number'))updateNumber(host);}
  if(typeof document!=='undefined'&&document.fonts)document.fonts.load('500 40px MeterDigits').then(()=>{document.documentElement.dataset.numeralFont='ready';refreshNumbers();}).catch(()=>{document.documentElement.dataset.numeralFont='failed';});
  return {defaults,readPreferences,savePreferences,presetCount,compatible,readingNumber,setNumber,refreshNumbers};
});
