const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = path.resolve(__dirname, '../../src/esp32_touch/meter/web/core.js');
const core = fs.existsSync(source) ? require(source) : {};

test('null, overload and nonfinite samples never become numeric zero', () => {
  assert.equal(typeof core.numericValue, 'function', 'numeric reading validation is missing');
  for (const reading of [{numeric:false,value:0}, {numeric:true,value:null}, {numeric:true,value:''}, {numeric:true,value:Infinity}, {numeric:true,value:'3'}]) {
    assert.equal(core.numericValue(reading), null);
  }
  assert.equal(core.numericValue({numeric:true,value:0}), 0);
  assert.equal(core.numericValue({numeric:true,value:-1.25}), -1.25);
});

test('CSV protects hostile metadata while retaining negative numeric values', () => {
  assert.equal(typeof core.csvCell, 'function', 'CSV encoding is missing');
  assert.equal(core.csvCell('=HYPERLINK("x")'), '"\'=HYPERLINK(""x"")"');
  assert.equal(core.csvCell('+meter'), '"\'+meter"');
  assert.equal(core.csvCell('-meter'), '"\'-meter"');
  assert.equal(core.csvCell('@meter'), '"\'@meter"');
  assert.equal(core.csvCell('\t=1'), '"\'\t=1"');
  assert.equal(core.csvCell('a,b\n"c"'), '"a,b\n""c"""');
  assert.equal(core.csvCell(-1.25, true), '-1.25');
  assert.equal(core.csvCell(null, true), '');
});

test('dedupe keys distinguish device, reboot and sequence', () => {
  assert.equal(typeof core.frameKey, 'function', 'frame identity is missing');
  assert.deepEqual(core.frameKey('esp1', 'boot1', 7), ['esp1','boot1',7]);
  assert.notDeepEqual(core.frameKey('esp1','boot1',7), core.frameKey('esp2','boot1',7));
  assert.notDeepEqual(core.frameKey('esp1','boot1',7), core.frameKey('esp1','boot2',7));
});

test('scope stores only received samples, separates channels and suppresses repeated frames', () => {
  assert.equal(typeof core.ScopeSeries, 'function', 'scope model is missing');
  const scope = new core.ScopeSeries(4);
  const frame = {kind:'reading',seq:1,meter:0,channel:0,profile:'ut60bt',modeKey:1,unit:'V',numeric:true,value:2,uptimeMs:1000};
  scope.push('boot1', frame);
  scope.push('boot1', frame);
  scope.push('boot1', {...frame,seq:2,channel:1,value:5,uptimeMs:1100});
  assert.deepEqual(scope.get(0,0).points.map(p => p.value), [2]);
  assert.deepEqual(scope.get(0,1).points.map(p => p.value), [5]);
  assert.equal(scope.get(0,0).points[0].time, 1000);
});

test('scope resets a trace on unit, mode, profile or boot changes', () => {
  assert.equal(typeof core.ScopeSeries, 'function', 'scope model is missing');
  const scope = new core.ScopeSeries(4);
  let frame = {kind:'reading',seq:1,meter:0,channel:0,profile:'a',modeKey:1,unit:'V',numeric:true,value:2,uptimeMs:1000};
  scope.push('boot1', frame);
  for (const change of [{unit:'mV'},{modeKey:2},{profile:'b'}]) {
    frame = {...frame,...change,seq:frame.seq+1};
    scope.push('boot1', frame);
    assert.equal(scope.get(0,0).points.length, 1);
  }
  scope.push('boot2', {...frame,seq:1,value:9});
  assert.deepEqual(scope.get(0,0).points.map(p => p.value), [9]);
});

test('replacement physical meter resets scope and late previous frames cannot restore its trace', () => {
  const scope=new core.ScopeSeries(4);
  const frame={kind:'reading',seq:10,meter:0,channel:0,address:'AA',profile:'ut60bt',modeKey:1,unit:'V',numeric:true,value:1,uptimeMs:1000};
  scope.push('boot1',frame);
  scope.push('boot1',{...frame,seq:11,address:'BB',value:7,uptimeMs:2000});
  assert.deepEqual(scope.get(0,0).points.map(p=>p.value),[7]);
  assert.equal(scope.push('boot1',frame),false,'A late frame from the previous physical meter must not reset the replacement trace');
  assert.deepEqual(scope.get(0,0).points.map(p=>p.value),[7]);
});

test('live reading freshness uses device measurement uptime and rejects older backlog', () => {
  assert.equal(typeof core.selectLiveReading,'function','Live measurement selection is missing');
  const meter={slot:0,state:'connected',address:'AA',profile:'ut60bt',readings:[{channel:0,numeric:true,value:9,unit:'V',ageMs:100,fresh:true}]};
  const status={device:{bootId:'boot1',uptimeMs:60000}}, frame={bootId:'boot1',kind:'reading',seq:10,meter:0,channel:0,address:'AA',profile:'ut60bt',uptimeMs:30000,numeric:true,value:1,unit:'V'};
  assert.equal(core.selectLiveReading(status,meter,0,frame,1000,1500).value,9,'Receiving a 30 second old replay must not override the current reading');
  const later={...frame,seq:11,uptimeMs:60500,value:12};
  const selected=core.selectLiveReading(status,meter,0,later,1000,2500);
  assert.equal(selected.value,12);
  assert.equal(selected.ageMs,1000,'Age is device uptime delta plus elapsed status age, never receipt time');
  assert.equal(core.selectLiveReading(status,{...meter,readings:[]},0,frame,1000,1500).fresh,false,'Old backlog remains stale without a newer status reading');
});

test('live cache requires connected matching device, boot, profile and increasing sequence', () => {
  assert.equal(typeof core.liveFrameEligible,'function','Live frame identity checking is missing');
  const meter={slot:0,state:'connected',address:'AA',profile:'ut60bt',readings:[]},status={device:{bootId:'boot1',uptimeMs:10000}};
  const frame={kind:'reading',seq:10,meter:0,channel:0,address:'AA',profile:'ut60bt',uptimeMs:9900};
  assert.equal(core.liveFrameEligible(status,meter,frame,'boot1',null,0,100),true);
  for(const [change,boot,previous] of [[{address:'BB'},'boot1',null],[{profile:'other'},'boot1',null],[{},'boot2',null],[{seq:9},'boot1',{...frame,bootId:'boot1'}],[{seq:11,uptimeMs:9000},'boot1',{...frame,bootId:'boot1'}]])assert.equal(core.liveFrameEligible(status,meter,{...frame,...change},boot,previous,0,100),false);
  assert.equal(core.liveFrameEligible(status,{...meter,state:'disconnected'},frame,'boot1',null,0,100),false);
  const disconnected=core.selectLiveReading(status,{...meter,state:'disconnected',readings:[{channel:0,value:8,numeric:true,unit:'V',fresh:true,ageMs:100}]},0,{...frame,value:99,numeric:true,bootId:'boot1'},0,100);
  assert.equal(disconnected.value,8);assert.equal(disconnected.fresh,false);
});

test('scope leaves breaks for invalid samples and bounds memory', () => {
  assert.equal(typeof core.ScopeSeries, 'function', 'scope model is missing');
  const scope = new core.ScopeSeries(3);
  for (let seq=1;seq<=5;seq++) scope.push('b',{kind:'reading',seq,meter:0,channel:0,profile:'a',unit:'V',modeKey:1,numeric:seq!==4,value:seq===4?null:seq,uptimeMs:seq*100});
  assert.deepEqual(scope.get(0,0).points.map(p => p.value), [3,null,5]);
});

test('exports retain backend tags, distinguish channel and quote event metadata', () => {
  assert.equal(typeof core.framesCsv, 'function', 'session CSV export is missing');
  const csv = core.framesCsv([{deviceId:'esp1',bootId:'b',seq:1,kind:'reading',sessionId:'s',meter:0,channel:1,numeric:true,value:-2.5,unit:'V',timestampMs:null,uptimeMs:200,text:'-2.5',profile:'ut60bt',address:'A',mode:'DC',message:'=unsafe'}]);
  const rows = csv.split('\r\n');
  assert.match(rows[0], /channel/);
  assert.match(rows[1], /,-2.5,/);
  assert.match(rows[1], /"'=unsafe"/);
  assert.match(rows[1], /"esp1"/);
});
