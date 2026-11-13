(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.MeterValueFormat=api;})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  function validNumberFormat(f){return !!f&&Number.isInteger(f.digits)&&f.digits>=3&&f.digits<=8&&Number.isInteger(f.decimals)&&f.decimals>=0&&f.decimals<=6&&f.decimals<f.digits&&(f.padding==='blank'||f.padding==='zero');}
  function formatNumber(value,f={digits:6,decimals:2,padding:'blank'}){
    const error=(state,cells)=>({state,cells,decimalIndex:255});
    if(!validNumberFormat(f))return error('invalid','ERR');
    if(value===null||value===undefined)return error('unavailable','--');
    if(typeof value!=='number'||!Number.isFinite(value))return error('invalid','ERR');
    const scaled=Math.abs(value)*10**f.decimals,limit=10**f.digits;
    if(!Number.isFinite(scaled)||scaled>limit)return error('overflow','OVR');
    const rounded=Math.floor(scaled+.5+4*Number.EPSILON*scaled);
    if(rounded>=limit)return error('overflow','OVR');
    const integer=f.digits-f.decimals,digits=String(rounded).padStart(f.digits,'0');
    let cells=(' '+digits.slice(0,integer)+(f.decimals?'.'+digits.slice(integer):'')).split('');
    if(f.padding==='blank')for(let i=1;i<integer&&cells[i]==='0';i++)cells[i]=' ';
    if(value<0&&rounded>0){let sign=0;if(f.padding==='blank'){let first=1;while(cells[first]===' ')first++;sign=first-1;}cells[sign]='-';}
    return {state:'numeric',cells:cells.join(''),decimalIndex:f.decimals?integer+1:255};
  }
  return {formatNumber,validNumberFormat};
});
