const test=require('node:test');
const assert=require('node:assert/strict');
const C=require('../../src/esp32_touch/meter/web/core.js');

test('display count rotates all selectable pairs around the focused slot',()=>{
  assert.equal(typeof C.displaySlots,'function','multi-meter slot selection is missing');
  for(const [focus,count,want] of [[0,1,[0]],[1,1,[1]],[0,2,[0,1]],[1,2,[1,2]],[2,2,[2,3]],[2,3,[2,3,0]],[3,2,[3,0]]])assert.deepEqual(C.displaySlots(focus,count),want);
  assert.deepEqual(C.displaySlots(-1,0),[0]);
  assert.deepEqual(C.displaySlots(9,5),[0]);
});

test('browser view preferences retain independent channels and reject malformed saved values',()=>{
  assert.equal(typeof C.readViewPreferences,'function','safe browser view preferences are missing');
  const saved={count:3,focus:2,channels:[1,4,0],layout:'separate'};let written;
  const storage={getItem:()=>JSON.stringify(saved),setItem:(key,value)=>{written=JSON.parse(value);}};
  assert.deepEqual(C.readViewPreferences(()=>storage),{...saved,channels:[1,4,0,0]});
  assert.equal(C.saveViewPreferences(()=>storage,{count:2,focus:1,channels:[2,0,1]}),true);
  assert.deepEqual(written,{count:2,focus:1,channels:[2,0,1,0],layout:'overlay'});
  for(const input of ['{',JSON.stringify({count:99,focus:-1,channels:[-2,'1',100],layout:'invalid'}),'null'])assert.deepEqual(C.readViewPreferences(()=>({getItem:()=>input})),{count:1,focus:0,channels:[0,0,0,0],layout:'overlay'});
});

test('denied localStorage getter and failed writes leave usable in-memory defaults',()=>{
  const denied=()=>{throw new DOMException('blocked','SecurityError');};
  assert.deepEqual(C.readViewPreferences(denied),{count:1,focus:0,channels:[0,0,0,0],layout:'overlay'});
  assert.equal(C.saveViewPreferences(denied,{count:3,channels:[0,1,0]}),false);
  assert.equal(C.saveViewPreferences(()=>({setItem(){throw new Error('quota');}}),{count:2}),false);
});

test('overlay keeps equal-unit magnitudes on one combined scale and common time window',()=>{
  assert.equal(typeof C.scopeOverlay,'function','shared overlay scale model is missing');
  const model=C.scopeOverlay([{slot:0,unit:'V',points:[{time:1000,value:0},{time:2000,value:2}]},{slot:1,unit:'V',points:[{time:2000,value:20},{time:4000,value:999}]}],3000,2500);
  assert.deepEqual(model.scales,[{unit:'V',min:-2.4,max:22.4,slots:[0,1]}]);
  assert.equal(model.traces[0].min,model.traces[1].min);assert.equal(model.traces[0].max,model.traces[1].max);
  assert.deepEqual(model.traces[1].visible,[{time:2000,value:20}]);
  assert.equal(model.end,3000);assert.equal(model.windowMs,2500);
});

test('mixed-unit overlay has independent labeled ranges and missing data cannot force zero',()=>{
  assert.equal(typeof C.scopeOverlay,'function','mixed overlay scale model is missing');
  const model=C.scopeOverlay([{slot:2,unit:'V',points:[{time:1000,value:12}]},{slot:0,unit:'mA',points:[{time:1000,value:1000}]},{slot:1,unit:'V',points:[{time:1000,value:null}]}],2000,5000);
  assert.deepEqual(model.scales,[{unit:'V',min:11.76,max:12.24,slots:[2,1]},{unit:'mA',min:980,max:1020,slots:[0]}]);
  assert.equal(model.traces[2].segments.length,0);assert.equal(model.traces[2].min,11.76);
  assert.deepEqual(C.scopeOverlay([{slot:0,unit:'Hz',points:[]}],2000,5000).scales,[]);
});

test('missing overlay slots never invent unitless axes or a mixed-unit chart',()=>{
  const model=C.scopeOverlay([{slot:0,unit:'V',points:[{time:1000,value:12}]},{slot:1,points:[]},{slot:2,points:[]}],2000,5000);
  assert.deepEqual(model.scales,[{unit:'V',min:11.76,max:12.24,slots:[0]}]);
  assert.equal(model.traces[1].min,null);assert.equal(model.traces[2].max,null);
  const empty=C.scopeOverlay([{slot:0,points:[]},{slot:1,points:[]},{slot:2,points:[]}],2000,5000);
  assert.deepEqual(empty.scales,[]);assert(empty.traces.every(t=>t.min===null && t.max===null));
});

test('overlay preserves each trace gap boundary and exact unit-prefix separation',()=>{
  assert.equal(typeof C.scopeOverlay,'function','overlay gap model is missing');
  const model=C.scopeOverlay([{slot:0,unit:'V',points:[{time:1000,value:1},{time:2000,value:null},{time:3000,value:2},{time:4000,value:3,breakBefore:true}]},{slot:1,unit:'mV',points:[{time:1000,value:10},{time:3000,value:11}]}],4000,5000);
  assert.equal(model.scales.length,2);assert.deepEqual(model.traces[0].segments.map(s=>s.map(p=>p.value)),[[1],[2],[3]]);assert.deepEqual(model.traces[1].segments.map(s=>s.map(p=>p.value)),[[10,11]]);
});

test('common chart window keeps independent unit ranges and breaks at invalid or missing samples',()=>{
  assert.equal(typeof C.scopePlot,'function','independent chart window model is missing');
  const voltage=[{time:1000,value:12},{time:2000,value:null},{time:3000,value:13},{time:4000,value:14},{time:10000,value:15},{time:11000,value:16,breakBefore:true}];
  const current=[{time:9000,value:1000},{time:10000,value:2000}];
  const v=C.scopePlot(voltage,11000,15000),a=C.scopePlot(current,11000,15000);
  assert.deepEqual(v.segments.map(s=>s.map(p=>p.value)),[[12],[13,14],[15],[16]]);
  assert(v.min<12 && v.max>16);assert(a.min<1000 && a.max>2000);
  assert.equal(C.scopePlot([{time:1,value:null}],100,1000).min,null);
  assert.deepEqual(C.scopePlot(voltage,11000,2000).visible.map(p=>p.value),[15,16]);
});

test('source resets and feed gaps never clear another meter or channel history',()=>{
  const series=new C.ScopeSeries(10),f={kind:'reading',seq:1,meter:0,channel:0,address:'AA',profile:'x',mode:'DC',modeKey:1,unit:'V',numeric:true,value:12,uptimeMs:1000};
  series.push('boot',f);series.push('boot',{...f,seq:2,channel:1,unit:'Hz',value:50});series.push('boot',{...f,seq:3,meter:1,address:'BB',unit:'mA',value:1000});
  assert.equal(typeof series.markGap,'function','feed gap boundaries are missing');series.markGap();
  series.push('boot',{...f,seq:4,value:13,uptimeMs:2000});
  assert.deepEqual(C.scopePlot(series.get(0,0).points,2000,5000).segments.map(s=>s.map(p=>p.value)),[[12],[13]]);
  series.push('boot',{...f,seq:5,address:'CC',value:99,uptimeMs:3000});
  assert.deepEqual(series.get(0,0).points.map(p=>p.value),[99]);
  assert.deepEqual(series.get(0,1).points.map(p=>p.value),[50]);assert.deepEqual(series.get(1,0).points.map(p=>p.value),[1000]);
});

test('a changed modeKey hides old history even when unit and mode label stay identical',()=>{
  const series=new C.ScopeSeries(10),frame={kind:'reading',seq:1,meter:0,channel:0,address:'AA',profile:'fixture',mode:'Voltage',modeKey:1,unit:'V',numeric:true,value:12,uptimeMs:1000};
  series.push('boot',frame);series.push('boot',{...frame,seq:2,meter:1,address:'BB',value:24});
  assert.equal(series.get(0,0).modeKey,1,'History must retain the numeric decoder mode identity');
  assert.equal(typeof C.matchesScopeReading,'function','Scope reading identity guard is missing');
  assert.equal(C.matchesScopeReading(series.get(0,0),frame),true);
  assert.equal(C.matchesScopeReading(series.get(0,0),{...frame,modeKey:2}),false);
  assert.equal(C.matchesScopeReading(series.get(1,0),{...frame,meter:1,address:'BB'}),true);
  series.push('boot',{...frame,seq:3,modeKey:2,value:13,uptimeMs:2000});
  assert.deepEqual(series.get(0,0).points.map(p=>p.value),[13]);assert.equal(series.get(0,0).modeKey,2);
  assert.equal(C.matchesScopeReading(series.get(0,0),{...frame,modeKey:2}),true);
  assert.deepEqual(series.get(1,0).points.map(p=>p.value),[24]);
});
