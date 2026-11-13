const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const F=require('../../src/esp32_touch/meter/web/value_format.js');
const cases=JSON.parse(fs.readFileSync(path.join(__dirname,'../fixtures/meter_value_format.json')));
test('shared native/browser decimal cases',()=>{for(const c of cases){const value=c.kind==='missing'?null:c.kind==='nan'?NaN:c.kind==='infinity'?Infinity:c.value;const result=F.formatNumber(value,c.format);assert.deepEqual(result,{state:c.state,cells:c.cells,decimalIndex:c.decimalIndex},c.name);}});
test('fixed width and decimal anchor through magnitude/sign changes',()=>{for(const value of [-9999.99,-123.45,-1.2,-.001,0,1.2,9.99,12.34,1234.56,9999.99]){const out=F.formatNumber(value,{digits:6,decimals:2,padding:'blank'});assert.equal(out.cells.length,8);assert.equal(out.decimalIndex,5);}});
