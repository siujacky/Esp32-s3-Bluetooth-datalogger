const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = path.resolve(__dirname, '../../src/esp32_touch/meter/web/calculations.js');
const calc = fs.existsSync(source) ? require(source) : {};

const reading = (unit, value, extra = {}) => ({numeric:true, value, unit, mode:'DC', modeKey:1,
  fresh:true, connected:true, ageMs:0, flags:512, meter:unit.includes('V') ? 0 : 1,
  channel:0, address:unit.includes('V') ? 'AA:01' : 'AA:02', profile:'ut60bt', ...extra});
const pair = (time, seq, amps = 1, extra = {}) => ({deviceId:'esp', bootId:'boot', receivedAt:100000 + time,
  voltage:reading('V', 12, {uptimeMs:time, seq}), current:reading('A', amps, {uptimeMs:time, seq:seq + 1}), ...extra});
const session = (config = {}) => {
  assert.equal(typeof calc.BatterySession, 'function', 'timestamped battery calculation is missing');
  const result = new calc.BatterySession(config); result.start(); return result;
};
const close = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-10, `${actual} != ${expected}`);

test('explicit supported SI prefixes normalize without interpreting display text', () => {
  assert.equal(typeof calc.normalizeReading, 'function', 'unit conversion is missing');
  for (const [unit, value, quantity, expected] of [
    ['mV',1234,'voltage',1.234], ['V',-12,'voltage',-12], ['uA',400,'current',0.0004],
    ['µA',400,'current',0.0004], ['μA',400,'current',0.0004], ['mA',250,'current',0.25], ['A',0,'current',0]
  ]) close(calc.normalizeReading(reading(unit,value),quantity).value, expected);
  for (const input of [reading('V', null),reading('V','12'),reading('V',Infinity),
    reading('V',0,{numeric:false,text:'OL'}),reading('MV',1),reading('A',1)]) {
    assert.equal(calc.normalizeReading(input,'voltage').valid,false);
  }
});

test('invalid and stale values cannot silently become zero or fresh', () => {
  assert.equal(typeof calc.calculate, 'function', 'instantaneous calculation is missing');
  for (const extra of [{fresh:false},{connected:false},{ageMs:3000},{ageMs:-1},{flags:513},{flags:514}]) {
    const result=calc.calculate({voltage:reading('V',12,extra),current:reading('A',1),mode:'dc'});
    assert.equal(result.valid,false);
    assert.equal(result.watts,null);
  }
});

test('DC computes signed power and effective resistance from normalized units', () => {
  const result=calc.calculate({voltage:reading('mV',12000),current:reading('mA',-250),mode:'dc'});
  assert.equal(result.valid,true); close(result.voltageV,12); close(result.currentA,-0.25);
  close(result.watts,-3); close(result.ohms,-48); assert.equal(result.voltAmps,null);
  const zero=calc.calculate({voltage:reading('V',12),current:reading('A',0),mode:'dc'});
  assert.equal(zero.valid,true); assert.equal(zero.watts,0); assert.equal(zero.ohms,null);
  assert.equal(zero.resistanceReason,'zero-current');
});

test('AC reports apparent VA, with real W only when PF is explicitly configured', () => {
  const args={voltage:reading('V',120,{mode:'AC V',flags:1024}),current:reading('A',2,{mode:'AC A',flags:1024}),mode:'ac'};
  const apparent=calc.calculate(args); assert.equal(apparent.valid,true);
  assert.equal(apparent.voltAmps,240); assert.equal(apparent.watts,null); assert.equal(apparent.ohms,60);
  assert.equal(calc.calculate({...args,powerFactor:0.75}).watts,180);
  assert.equal(calc.calculate({...args,powerFactor:0}).watts,0);
  for (const pf of [-0.1,1.1,'0.5',NaN]) assert.equal(calc.calculate({...args,powerFactor:pf}).valid,false);
});

test('AC DC mismatches and mixed coupling cannot produce power', () => {
  for (const voltage of [reading('V',12,{mode:'AC V',flags:1024}),reading('V',12,{mode:'AC+DC V',flags:1536})]) {
    assert.equal(calc.calculate({voltage,current:reading('A',1),mode:'dc'}).valid,false);
  }
  assert.equal(calc.calculate({voltage:reading('V',12),current:reading('A',1),mode:'ac'}).valid,false);
  assert.equal(calc.calculate({voltage:reading('V',12),current:reading('A',1)}).valid,false);
});

test('session integrates timestamps by trapezoid instead of sample counts', () => {
  const s=session(); s.ingest(pair(1000,1,1)); s.ingest(pair(3000,3,3));
  const out=s.snapshot(); close(out.dischargeAh,4/3600); close(out.dischargeWh,48/3600);
  close(out.netAh,4/3600); assert.equal(out.integratedMs,2000); assert.equal(out.samples,2);
  assert.equal(out.estimate.mode,'measured-only'); assert.equal(out.estimate.remainingAh,null);
  assert.equal(out.estimate.runtimeHours,null);
});

test('signed current crossing zero separates real charging and discharging areas', () => {
  const s=session(); s.ingest(pair(1000,1,2)); s.ingest(pair(3000,3,-2));
  const out=s.snapshot(); close(out.dischargeAh,1/3600); close(out.chargeAh,1/3600); close(out.netAh,0);
  close(out.dischargeWh,12/3600); close(out.chargeWh,12/3600);
  const reverse=session({direction:'negative-discharge'}); reverse.ingest(pair(1000,1,-1)); reverse.ingest(pair(3000,3,-1));
  close(reverse.snapshot().dischargeAh,2/3600); close(reverse.snapshot().chargeAh,0);
});

test('replayed or regressing frame sequences do not add integration or replace newer baselines', () => {
  const s=session(); s.ingest(pair(1000,1)); s.ingest(pair(2000,3));
  s.ingest(pair(2000,3)); s.ingest(pair(1500,1)); s.ingest(pair(3000,5));
  const out=s.snapshot(); close(out.netAh,2/3600); assert.equal(out.samples,3);
  const regression=pair(3500,7); regression.current.uptimeMs=1000; s.ingest(regression);
  assert.equal(s.snapshot().lastReason,'time-regression'); close(s.snapshot().netAh,2/3600);
});

test('current can advance while the paired fresh voltage stays unchanged', () => {
  const s=session(); const first=pair(1000,1); s.ingest(first);
  s.ingest({...pair(1800,3),voltage:first.voltage});
  close(s.snapshot().netAh,0.8/3600); assert.equal(s.snapshot().samples,2);
  const skew=pair(3000,5); skew.voltage=first.voltage; s.ingest(skew);
  assert.equal(s.snapshot().lastReason,'sample-skew'); close(s.snapshot().netAh,0.8/3600);
});

test('same-millisecond asynchronous readings update the endpoint without charge or a false gap', () => {
  const s=session({ratedCapacityAh:2,initialSocPercent:50});
  const first=pair(1000,1); s.ingest(first);
  const voltageUpdate={...first,receivedAt:101001,voltage:{...first.voltage,seq:3,value:18}};
  s.ingest(voltageUpdate);
  const refreshed=s.snapshot();
  assert.equal(refreshed.samples,2); assert.equal(refreshed.gapCount,0);
  assert.equal(refreshed.integratedMs,0); assert.equal(refreshed.netAh,0);
  assert.equal(refreshed.last.watts,18); assert.equal(refreshed.estimate.valid,true);
  s.ingest({...pair(2000,4,2),voltage:voltageUpdate.voltage});
  const integrated=s.snapshot();
  close(integrated.netAh,1.5/3600); close(integrated.netWh,24/3600);
  assert.equal(integrated.integratedMs,1000); assert.equal(integrated.gapCount,0);
  const backwards={...pair(2500,6,2),voltage:{...voltageUpdate.voltage,seq:8,uptimeMs:900}};
  s.ingest(backwards);
  assert.equal(s.snapshot().lastReason,'time-regression'); close(s.snapshot().netAh,1.5/3600);
});

test('voltage-only timestamps cannot stretch a current pulse before its falling edge', () => {
  const s=session(); const baseline=pair(1000,1,0); s.ingest(baseline);
  const rising={...pair(1500,3,2),voltage:baseline.voltage}; s.ingest(rising);
  const voltageOnly={...rising,receivedAt:102000,voltage:{...baseline.voltage,seq:5,uptimeMs:2000}};
  s.ingest(voltageOnly);
  close(s.snapshot().netAh,0.5/3600); assert.equal(s.snapshot().integratedMs,500);
  s.ingest({...pair(2000,6,0),voltage:voltageOnly.voltage});
  const out=s.snapshot();
  close(out.netAh,1/3600); close(out.netWh,12/3600);
  assert.equal(out.integratedMs,1000); assert.equal(out.gapCount,0);
});

test('later voltage previews cannot rewrite the power of a previous current endpoint', () => {
  const s=session(); const baseline=pair(1000,1,1); s.ingest(baseline);
  const voltageOnly={...baseline,receivedAt:101500,voltage:{...baseline.voltage,value:24,seq:3,uptimeMs:1500}};
  s.ingest(voltageOnly);
  assert.equal(s.snapshot().last.watts,24); assert.equal(s.snapshot().netAh,0);
  s.ingest({...pair(2000,4,1),voltage:voltageOnly.voltage});
  close(s.snapshot().netAh,1/3600); close(s.snapshot().netWh,18/3600);
});

test('source change never bridges charge and old-source replay cannot restore a baseline', () => {
  const s=session(); s.ingest(pair(1000,1)); s.ingest(pair(2000,3));
  const replacement=pair(3000,5); replacement.current.address='BB:02'; s.ingest(replacement);
  s.ingest(pair(2500,3)); const next=pair(4000,7); next.current.address='BB:02'; s.ingest(next);
  close(s.snapshot().netAh,2/3600); assert.equal(s.snapshot().gapCount,1);
});

test('unit prefix changes normalize continuously while measurement mode changes break integration', () => {
  const s=session(); s.ingest(pair(1000,1));
  const milli=pair(2000,3); milli.current.unit='mA'; milli.current.value=1000; s.ingest(milli);
  close(s.snapshot().netAh,1/3600); assert.equal(s.snapshot().gapCount,0);
  const changed=pair(3000,5); changed.current.modeKey=2; s.ingest(changed);
  close(s.snapshot().netAh,1/3600); assert.equal(s.snapshot().gapCount,1);
  const ac=pair(4000,7); ac.current.mode='AC A'; ac.current.flags=1024; s.ingest(ac);
  assert.equal(s.snapshot().last,null); assert.equal(s.snapshot().lastReason,'mode-mismatch');
});

test('missing browser time and explicit feed gaps never bridge even closely spaced device timestamps', () => {
  for (const mutate of [p=>{p.receivedAt+=5000;},p=>{p.gap=true;},p=>{p.voltage.ageMs=3000;},p=>{p.current.connected=false;}]) {
    const s=session(); s.ingest(pair(1000,1)); const second=pair(2000,3); mutate(second); s.ingest(second);
    close(s.snapshot().netAh,0); assert.ok(s.snapshot().gapCount>=1);
  }
  const s=session(); s.ingest(pair(1000,1)); s.ingest(pair(5001,3,1,{receivedAt:102000}));
  close(s.snapshot().netAh,0); assert.equal(s.snapshot().lastGapReason,'timestamp-gap');
});

test('reboots and device changes rebase without accepting an old stream again', () => {
  const s=session(); s.ingest(pair(1000,1)); s.ingest(pair(2000,3));
  s.ingest(pair(100,1,1,{bootId:'new',receivedAt:103000}));
  s.ingest(pair(2100,5,1,{receivedAt:103500}));
  s.ingest(pair(1100,3,1,{bootId:'new',receivedAt:104000}));
  close(s.snapshot().netAh,2/3600); assert.equal(s.snapshot().gapCount,1);
  s.ingest(pair(100,1,1,{deviceId:'other',bootId:'new',receivedAt:105000}));
  close(s.snapshot().netAh,2/3600); assert.equal(s.snapshot().gapCount,2);
});

test('battery session requires source and freshness metadata; invalid input cannot integrate', () => {
  for (const field of ['seq','uptimeMs','address','profile','meter','channel','fresh','connected','ageMs']) {
    const s=session(); s.ingest(pair(1000,1)); const second=pair(2000,3); delete second.current[field];
    s.ingest(second); close(s.snapshot().netAh,0); assert.equal(s.snapshot().last,null);
  }
  const s=session(); s.ingest(pair(1000,1)); const negative=pair(2000,3); negative.voltage.value=-12;
  s.ingest(negative); assert.equal(s.snapshot().lastReason,'battery-voltage-polarity');
  const ac=session({mode:'ac'}); ac.ingest(pair(1000,1)); assert.equal(ac.snapshot().lastReason,'battery-requires-dc');
});

test('rated runtime is based on entered capacity and initial SOC, clamped without inferring capacity', () => {
  const s=session({ratedCapacityAh:2,initialSocPercent:50}); s.ingest(pair(1000,1)); s.ingest(pair(2000,3));
  const e=s.snapshot().estimate; assert.equal(e.mode,'rated'); assert.equal(e.valid,true);
  close(e.remainingAh,1-1/3600); close(e.runtimeHours,1-1/3600); close(e.socPercent,(1-1/3600)*50);
  const empty=session({ratedCapacityAh:0.0001,initialSocPercent:10}); empty.ingest(pair(1000,1)); empty.ingest(pair(2000,3));
  assert.equal(empty.snapshot().estimate.remainingAh,0); assert.equal(empty.snapshot().estimate.socPercent,0);
  const charging=session({ratedCapacityAh:1,initialSocPercent:100}); charging.ingest(pair(1000,1,-1)); charging.ingest(pair(2000,3,-1));
  assert.equal(charging.snapshot().estimate.remainingAh,1); assert.equal(charging.snapshot().estimate.runtimeHours,null);
  for (const config of [{ratedCapacityAh:2},{initialSocPercent:50},{ratedCapacityAh:0,initialSocPercent:50},{ratedCapacityAh:2,initialSocPercent:101}]) {
    const result=session(config); result.ingest(pair(1000,1)); assert.equal(result.snapshot().estimate.valid,false);
    assert.equal(result.snapshot().estimate.remainingAh,null);
  }
});

test('gaps and pause preserve measured totals but invalidate remaining-capacity assumptions until reset', () => {
  const s=session({ratedCapacityAh:2,initialSocPercent:50}); s.ingest(pair(1000,1)); s.ingest(pair(2000,3));
  s.pause(); s.start(); s.ingest(pair(3000,5)); s.ingest(pair(4000,7));
  close(s.snapshot().netAh,2/3600); assert.equal(s.snapshot().estimate.valid,false);
  assert.equal(s.snapshot().estimate.remainingAh,null); assert.equal(s.snapshot().estimate.runtimeHours,null);
  s.reset(); assert.equal(s.snapshot().running,false); assert.equal(s.snapshot().netAh,0);
  s.start(); s.ingest(pair(5000,9)); assert.equal(s.snapshot().estimate.valid,true);
  s.markGap('offline'); assert.equal(s.snapshot().last,null); assert.equal(s.snapshot().estimate.valid,false);
});

test('sessions keep immutable inputs and export finite serializable snapshots', () => {
  const s=session(); const input=pair(1000,1); const before=JSON.stringify(input); s.ingest(input);
  assert.equal(JSON.stringify(input),before); const out=s.snapshot(); out.last.currentA=999;
  assert.equal(s.snapshot().last.currentA,1); assert.doesNotThrow(()=>JSON.stringify(s.snapshot()));
});

test('duplicate polling cannot keep a stale sample or an explicit feed gap live', () => {
  for (const mutate of [p=>{p.voltage.ageMs=3000;},p=>{p.gap=true;},p=>{p.current.flags=513;}]) {
    const s=session({ratedCapacityAh:1,initialSocPercent:100}); const first=pair(1000,1); s.ingest(first);
    const repeated=structuredClone(first); mutate(repeated); s.ingest(repeated);
    assert.equal(s.snapshot().last,null); assert.equal(s.snapshot().estimate.valid,false);
    s.ingest(pair(2000,3)); close(s.snapshot().netAh,0);
  }
});

test('session provenance must match its enclosing device and boot context', () => {
  for (const extra of [{deviceId:'other'},{bootId:'earlier'}]) {
    const s=session(); s.ingest(pair(1000,1)); const invalid=pair(2000,3);
    Object.assign(invalid.voltage,extra); Object.assign(invalid.current,extra); s.ingest(invalid);
    assert.equal(s.snapshot().last,null); close(s.snapshot().netAh,0);
  }
});

test('extreme finite values remain bounded instead of corrupting arithmetic or SOC', () => {
  const product=calc.calculate({voltage:reading('V',1e308),current:reading('A',2),mode:'dc'});
  assert.equal(product.valid,false); assert.equal(product.watts,null);
  const division=calc.calculate({voltage:reading('V',1),current:reading('A',Number.MIN_VALUE),mode:'dc'});
  assert.equal(division.valid,false); assert.equal(division.ohms,null);
  const huge=session({ratedCapacityAh:1e308,initialSocPercent:50}); huge.ingest(pair(1000,1));
  close(huge.snapshot().estimate.remainingAh/1e308,0.5);
  const crossing=session(); const first=pair(1000,1,1e308); first.voltage.value=1e-308;
  const second=pair(3000,3,-1e308); second.voltage.value=1e-308;
  crossing.ingest(first); crossing.ingest(second);
  close(crossing.snapshot().dischargeAh/1e308,1/7200);
  close(crossing.snapshot().chargeAh/1e308,1/7200);
});

test('UMD exposes the same calculation API in a plain browser without CommonJS', () => {
  assert.equal(fs.existsSync(source),true,'browser calculation module is missing');
  const context={}; vm.runInNewContext(fs.readFileSync(source,'utf8'),context);
  assert.equal(typeof context.MeterCalculations.BatterySession,'function');
  assert.equal(context.MeterCalculations.calculate({voltage:reading('V',12),current:reading('A',1),mode:'dc'}).watts,12);
});
