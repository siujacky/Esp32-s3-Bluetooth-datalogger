const {test}=require('node:test'),assert=require('node:assert/strict');
const C=require('../../src/esp32_touch/meter/web/core.js');
const V=require('../../src/esp32_touch/meter/web/value_tiles.js');
const M=require('../../src/esp32_touch/meter/web/calculations.js');

test('four slots wrap and extend legacy browser preferences without losing channels',()=>{
  for(let focus=0;focus<4;focus++)assert.deepEqual(C.displaySlots(focus,4),Array.from({length:4},(_,i)=>(focus+i)%4));
  assert.deepEqual(C.displaySlots(3,2),[3,0]);
  const legacy={count:3,focus:2,layout:'separate',channels:[1,4,7]};
  const storage={getItem:()=>JSON.stringify(legacy)};
  assert.deepEqual(C.readViewPreferences(()=>storage),{...legacy,channels:[1,4,7,0]});
  let written;assert(C.saveViewPreferences(()=>({setItem:(key,value)=>written=JSON.parse(value)}),{count:4,focus:3,channels:[1,4,7,6]}));
  assert.deepEqual(written,{count:4,focus:3,layout:'overlay',channels:[1,4,7,6]});
});

test('grid belongs to count four and legacy value formats migrate in place',()=>{
  assert.equal(V.compatible('grid',4),true);assert.equal(V.presetCount('grid',1),4);
  assert.equal(V.compatible('equal',4),false);assert.equal(V.compatible('grid',3),false);
  const legacy=V.defaults();legacy.formats=legacy.formats.slice(0,3);delete legacy.presets[4];legacy.formats[2][7]={digits:8,decimals:6,padding:'zero'};
  const data=new Map([['meter-workbench-values-v1',JSON.stringify(legacy)]]),storage={getItem:key=>data.get(key),setItem:(key,value)=>data.set(key,value)};
  let prefs=V.readPreferences(()=>storage);assert.equal(prefs.presets[4],'grid');assert.equal(prefs.formats.length,4);assert.deepEqual(prefs.formats[2][7],legacy.formats[2][7]);
  prefs.formats[3][7]={digits:7,decimals:4,padding:'zero'};assert(V.savePreferences(()=>storage,prefs));prefs=V.readPreferences(()=>storage);assert.deepEqual(prefs.formats[3][7],{digits:7,decimals:4,padding:'zero'});
  assert.equal(V.readPreferences(()=>{throw Error('denied')}).formats.length,4);
});

test('fourth-slot frames retain raw values and slot four cannot enter live/history models',()=>{
  const status={device:{bootId:'b',uptimeMs:1000}},meter={slot:3,state:'connected',address:'AA:04',profile:'fixture',readings:[]};
  const frame={kind:'reading',seq:1,meter:3,channel:7,address:'AA:04',profile:'fixture',numeric:true,value:-1.23456789,unit:'mA',uptimeMs:1000};
  assert(C.matchesMeterFrame(status,meter,frame,'b'));assert(C.liveFrameEligible(status,meter,frame,'b',null,0,0));
  const scope=new C.ScopeSeries();assert(scope.push('b',frame));assert.equal(scope.get(3,7).points[0].value,frame.value);assert(C.framesCsv([frame]).includes('-1.23456789'));
  for(const invalid of [4,-1,1.5]){assert.equal(scope.push('b',{...frame,meter:invalid,seq:2}),false);assert.equal(C.matchesMeterFrame(status,{...meter,slot:invalid},{...frame,meter:invalid},'b'),false);}
  assert.equal(scope.push('b',{...frame,channel:8,seq:3}),false);
});

test('discovery snapshots distinguish identical names and reject a replaced scan identity',()=>{
  const first={index:0,name:'Same meter',address:'AA:01',addressType:0},second={index:1,name:'Same meter',address:'AA:02',addressType:1};
  const discovery={devices:[first,second]},chosen=C.discoverySnapshot(discovery,1);assert.deepEqual(chosen,second);
  second.address='AA:03';assert.equal(chosen.address,'AA:02');assert.equal(C.discoverySelectionMatches(discovery,chosen),false);
  second.address='aa:02';assert.equal(C.discoverySelectionMatches(discovery,chosen),true);
  second.addressType=0;assert.equal(C.discoverySelectionMatches(discovery,chosen),false);
  assert.equal(C.discoverySnapshot(discovery,99),null);
});

test('battery integration accepts fourth-slot source and rejects slot four',()=>{
  const pair=(at,seq,meter)=>({deviceId:'d',bootId:'b',receivedAt:10000+at,voltage:{numeric:true,value:12,unit:'V',mode:'DC',flags:512,fresh:true,connected:true,ageMs:0,meter:0,channel:0,address:'AA:01',profile:'fixture',uptimeMs:at,seq},current:{numeric:true,value:2,unit:'A',mode:'DC',flags:512,fresh:true,connected:true,ageMs:0,meter,channel:7,address:'AA:04',profile:'fixture',uptimeMs:at,seq:seq+1}});
  const good=new M.BatterySession();good.start();good.ingest(pair(1000,1,3));good.ingest(pair(2000,3,3));assert.equal(good.snapshot().integratedMs,1000);assert(Math.abs(good.snapshot().dischargeWh-24/3600)<1e-12);
  const bad=new M.BatterySession();bad.start();bad.ingest(pair(1000,1,4));assert.equal(bad.snapshot().samples,0);
});
