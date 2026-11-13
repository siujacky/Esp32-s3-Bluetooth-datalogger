(function () {
  'use strict';
  const root=document.getElementById('calculator-root');
  if(!root)return;
  const M=window.MeterCalculations,C=window.MeterCore;
  if(!M || !C){root.textContent='Calculations could not load. Reload this dashboard.';return;}
  const MAX_AGE=3000,MAX_SKEW=1000;
  const defaults={voltageSource:'',currentSource:'',mode:'dc',powerFactor:'',ratedCapacityAh:'',initialSocPercent:'',direction:'positive-discharge'};
  const state={settings:{...defaults},status:null,statusAt:0,offline:true,live:new Map(),integration:new Map(),integrationCursors:new Map(),integrationStartedAt:0,integrationFloor:0,segmentHasEndpoint:false,pendingCurrents:[],lastCurrentAt:0,session:null,sessionId:'',sessionConfig:null,sessionSources:null,sessionSignature:'',createdAt:0,db:null,saved:[],storageError:'',notice:'Choose a voltage and a current channel to begin.',lastSave:0};
  const el={};
  const text=(tag,className,value)=>{const n=document.createElement(tag);if(className)n.className=className;if(value!=null)n.textContent=value;return n;};
  const number=value=>{const n=Number(value);return value!=='' && Number.isFinite(n)?n:null;};
  const format=(value,digits=5)=>Number.isFinite(value)?Number(value.toPrecision(digits)).toString():'—';
  const duration=hours=>Number.isFinite(hours)?hours<1?format(hours*60,3)+' min':format(hours,3)+' h':'—';
  const keyFor=(meter,channel)=>Number(meter)+':'+Number(channel);
  const validSource=key=>/^[0-3]:[0-7]$/.test(String(key));
  const keyParts=key=>validSource(key)?String(key).split(':').map(Number):[-1,-1];
  const meterFor=key=>state.status && (state.status.meters || []).find(m=>Number(m.slot)===keyParts(key)[0]);
  function addButton(parent,id,label,kind,handler){const n=text('button','button '+kind,label);n.type='button';if(id){n.id=id;el[id]=n;}n.addEventListener('click',handler);parent.append(n);return n;}
  function addField(parent,id,label,type,options,hint){
    const wrap=text('label','calc-field'),caption=text('span','',label);wrap.append(caption);
    const input=document.createElement(type==='select'?'select':'input');input.id=id;el[id]=input;
    if(type!=='select')input.type=type;
    if(options)for(const [value,name] of options){const option=text('option','',name);option.value=value;input.append(option);}
    if(hint)wrap.append(text('small','hint',hint));wrap.append(input);parent.append(wrap);return input;
  }
  function addMetric(parent,id,label,unit,wide){const card=text('div','calc-metric'+(wide?' calc-metric-primary':''));card.append(text('dt','',label));const value=text('dd','calc-metric-reading');const n=text('span','','—');n.id=id;el[id]=n;value.append(n);if(unit){const u=text('span','calc-metric-unit',unit);u.id=id.replace(/-value$/,'-unit');el[u.id]=u;value.append(u);}card.append(value);parent.append(card);return card;}
  function build(){
    const heading=text('div','section-heading calc-heading'),intro=text('div');intro.append(text('p','eyebrow','BROWSER CALCULATIONS'),text('h1','','Combine your measurements'),text('p','muted','Select live channels for power, resistance and a DC battery test.'));heading.append(intro);root.append(heading);
    const preview=text('section','surface calc-preview');preview.setAttribute('aria-label','Calculated live results');
    const previewHead=text('div','calc-title-row');previewHead.append(text('h2','','Live results'));const liveState=text('span','pill pending','Waiting for sources');liveState.id='calc-live-state';el[liveState.id]=liveState;previewHead.append(liveState);preview.append(previewHead);
    const metrics=text('dl','calc-metrics');addMetric(metrics,'calc-power-value','Power','W',true);addMetric(metrics,'calc-real-power-value','Real AC power','W');addMetric(metrics,'calc-resistance-value','V ÷ I','Ω');addMetric(metrics,'calc-voltage-value','Voltage','V');addMetric(metrics,'calc-current-value','Current','A');preview.append(metrics);
    const note=text('p','hint');note.id='calc-live-note';el[note.id]=note;preview.append(note);root.append(preview);
    const grid=text('div','calc-grid');
    const sources=text('section','surface');sources.append(text('h2','','Measurement sources'));
    const sourceFields=text('div','calc-form-grid');addField(sourceFields,'calc-voltage-source','Voltage meter / channel','select');addField(sourceFields,'calc-current-source','Current meter / channel','select');addField(sourceFields,'calc-mode','Electrical mode','select',[['dc','DC'],['ac','AC (RMS readings)']]);const pf=addField(sourceFields,'calc-power-factor','AC power factor (optional)','number',null,'Required to estimate real AC watts. 0 to 1.');pf.min='0';pf.max='1';pf.step='0.01';pf.placeholder='No power factor configured';sources.append(sourceFields,text('p','hint calc-source-note','Use voltage and current from the same circuit and operating point. Resistance is the V/I ratio, not an ohmmeter test. AC V×I is apparent VA.'));
    const battery=text('section','surface');battery.append(text('h2','','Battery assumptions'));
    const batteryFields=text('div','calc-form-grid');addField(batteryFields,'calc-direction','Discharge current sign','select',[['positive-discharge','Positive current means discharge'],['negative-discharge','Negative current means discharge']]);const capacity=addField(batteryFields,'calc-rated-ah','Rated battery capacity (Ah, optional)','number',null,'Enter a known rating to enable a remaining-capacity estimate.');capacity.min='0';capacity.step='any';capacity.placeholder='Measured-only test';const soc=addField(batteryFields,'calc-soc','Initial state of charge (%)','number',null,'Required together with rated capacity.');soc.min='0';soc.max='100';soc.step='any';soc.placeholder='e.g. 100';battery.append(batteryFields,text('p','hint calc-source-note','Leave both capacity and initial charge empty for measured discharge Ah/Wh only. A partial test does not establish a battery rating. Estimates assume the present current stays constant; cutoff voltage, efficiency, temperature and load changes are not modeled.'));
    grid.append(sources,battery);root.append(grid);
    const session=text('section','surface calc-session');const title=text('div','calc-title-row');title.append(text('h2','','DC battery test'));const badge=text('span','pill','Idle');badge.id='calc-session-state';el[badge.id]=badge;title.append(badge);session.append(title);
    const controls=text('div','button-row calc-controls');addButton(controls,'calc-start','Start test','primary',start);addButton(controls,'calc-pause','Pause','secondary',()=>pause('Paused by you.',false));addButton(controls,'calc-reset','Reset test','ghost',reset);addButton(controls,'calc-save','Save summary','secondary',()=>saveCurrent(true));addButton(controls,'calc-export','Export summary JSON','ghost',()=>exportSummary(currentRecord()));session.append(controls);
    const result=text('dl','calc-metrics calc-battery-metrics');addMetric(result,'calc-charge-value','Measured discharge','Ah',true);addMetric(result,'calc-energy-value','Measured discharge energy','Wh');addMetric(result,'calc-charge-in-value','Measured charge in','Ah');addMetric(result,'calc-energy-in-value','Measured charge energy in','Wh');addMetric(result,'calc-remaining-value','Estimated remaining','Ah');addMetric(result,'calc-soc-value','Estimated state of charge','%');addMetric(result,'calc-runtime-value','Estimated runtime at present current');addMetric(result,'calc-time-value','Integrated time');session.append(result);
    const message=text('p','calc-message');message.id='calc-message';message.setAttribute('role','status');message.setAttribute('aria-live','polite');el[message.id]=message;session.append(message);const assumptions=text('p','hint');assumptions.id='calc-assumptions';el[assumptions.id]=assumptions;session.append(assumptions);root.append(session);
    const storage=text('section','surface');storage.append(text('h2','','Saved calculation summaries'));const storageRow=text('div','calc-storage-row');const status=text('p','hint','Opening separate calculation storage…');status.id='calc-storage';el[status.id]=status;storageRow.append(status);addButton(storageRow,'calc-storage-retry','Retry storage','ghost small',openStorage);storage.append(storageRow,text('p','hint','Summaries and settings stay in this browser, separately from original meter recordings. Reloading ends integration; saved totals remain available below. Export a summary to keep a copy outside this browser.'));const list=text('div','calc-saved-list');list.id='calc-sessions';el[list.id]=list;storage.append(list);root.append(storage);
    const fields={'calc-voltage-source':'voltageSource','calc-current-source':'currentSource','calc-mode':'mode','calc-power-factor':'powerFactor','calc-rated-ah':'ratedCapacityAh','calc-soc':'initialSocPercent','calc-direction':'direction'};
    Object.keys(fields).forEach(id=>el[id].addEventListener('change',()=>{const prop=fields[id];state.settings[prop]=el[id].value;if(state.session && state.session.snapshot().samples>0)pause('Settings changed. Save this result, then reset before using the new assumptions or sources.',false);else if(state.session){state.session=null;state.sessionConfig=null;}persistSettings();render();}));
    applySettings();renderSaved();
  }
  function applySettings(){const map={'calc-voltage-source':'voltageSource','calc-current-source':'currentSource','calc-mode':'mode','calc-power-factor':'powerFactor','calc-rated-ah':'ratedCapacityAh','calc-soc':'initialSocPercent','calc-direction':'direction'};for(const id in map)el[id].value=state.settings[map[id]];renderSources();}
  function sourceOptions(){const result=[];for(const meter of state.status && state.status.meters || [])if(C.validMeterSlot(Number(meter.slot)))for(const reading of meter.readings || [])if(C.validChannel(Number(reading.channel)))result.push({key:keyFor(meter.slot,reading.channel),meter,reading});return result;}
  function renderSources(){
    const options=sourceOptions();
    for(const [property,id,kind] of [['voltageSource','calc-voltage-source','voltage'],['currentSource','calc-current-source','current']]){
      if(!validSource(state.settings[property]))state.settings[property]='';
      if(!state.settings[property]){const preferred=options.find(o=>M.normalizeReading(o.reading,kind,{maxAgeMs:MAX_AGE}).valid);if(preferred)state.settings[property]=preferred.key;}
      const select=el[id],value=state.settings[property];select.replaceChildren();const empty=text('option','','Choose a '+kind+' channel');empty.value='';select.append(empty);
      for(const option of options){const r=option.reading,m=option.meter,label='Meter '+(Number(m.slot)+1)+' · '+(m.name || m.address || 'Unnamed')+' · channel '+(Number(r.channel)+1)+' · '+(r.text || format(r.value))+' '+(r.unit || '')+' '+(r.mode || '')+(m.state==='connected'?'':' · '+m.state);const n=text('option','',label);n.value=option.key;select.append(n);}
      if(value && !options.some(o=>o.key===value)){const [meter,channel]=keyParts(value),missing=text('option','','Unavailable · meter '+(meter+1)+' / channel '+(channel+1));missing.value=value;select.append(missing);}
      select.value=value;
    }
  }
  function readingFor(key,forIntegration,candidateOverride){
    const meter=meterFor(key);if(!meter || !key || !state.status)return null;const channel=keyParts(key)[1],now=Date.now(),elapsed=Math.max(0,now-state.statusAt),candidate=candidateOverride || (forIntegration?state.integration:state.live).get(key);let r;
    if(forIntegration){if(!candidate || !C.matchesMeterFrame(state.status,meter,candidate,candidate.bootId))return null;r={...candidate,ageMs:Math.max(0,state.status.device.uptimeMs+elapsed-candidate.uptimeMs)};}
    else r=C.selectLiveReading(state.status,meter,channel,candidate,state.statusAt,now);
    if(!r)return null;
    const connected=!state.offline && meter.state==='connected',uptimeMs=Number.isFinite(r.uptimeMs)?r.uptimeMs:Number.isFinite(r.ageMs) && Number.isFinite(state.status.device.uptimeMs)?Math.max(0,state.status.device.uptimeMs+elapsed-r.ageMs):undefined;return {...r,meter:Number(meter.slot),channel,address:meter.address,profile:meter.profile,uptimeMs,deviceId:state.status.device.id,bootId:state.status.device.bootId,connected,fresh:connected && r.fresh!==false && Number.isFinite(r.ageMs) && r.ageMs<MAX_AGE};
  }
  function configuration(){return {mode:state.settings.mode,direction:state.settings.direction,powerFactor:number(state.settings.powerFactor),ratedCapacityAh:number(state.settings.ratedCapacityAh),initialSocPercent:number(state.settings.initialSocPercent),maxAgeMs:MAX_AGE,maxGapMs:MAX_AGE,maxSkewMs:MAX_SKEW};}
  function signature(){const identity=key=>{const meter=meterFor(key);return meter?[String(meter.address || '').trim().toUpperCase(),meter.profile || '']:null;};return JSON.stringify({...configuration(),voltageSource:state.settings.voltageSource,currentSource:state.settings.currentSource,voltageIdentity:identity(state.settings.voltageSource),currentIdentity:identity(state.settings.currentSource)});}
  function validation(){const config=configuration();if(config.mode!=='dc')return 'Battery tests require DC voltage and current.';if((config.ratedCapacityAh===null)!==(config.initialSocPercent===null))return 'Enter both rated capacity and initial charge, or leave both empty for measured-only results.';if(config.ratedCapacityAh!==null && config.ratedCapacityAh<=0)return 'Rated capacity must be greater than zero.';if(config.initialSocPercent!==null && (config.initialSocPercent<0 || config.initialSocPercent>100))return 'Initial state of charge must be from 0 to 100%.';if(state.session && state.sessionSignature!==signature())return 'Settings or selected meter identity changed. Save the existing summary, then reset to start with these settings.';return '';}
  function calculation(forIntegration){return M.calculate({voltage:readingFor(state.settings.voltageSource,forIntegration),current:readingFor(state.settings.currentSource,forIntegration),mode:state.settings.mode,powerFactor:number(state.settings.powerFactor),maxAgeMs:MAX_AGE,maxSkewMs:MAX_SKEW});}
  function start(){const reason=validation(),instant=calculation(false);if(reason || !instant.valid){state.notice=reason || 'Wait for fresh, compatible voltage and current readings.';render();return;}if(!state.session){state.sessionConfig={...configuration(),voltageSource:state.settings.voltageSource,currentSource:state.settings.currentSource};state.sessionSources={voltage:sourceSummary(state.settings.voltageSource),current:sourceSummary(state.settings.currentSource)};state.sessionSignature=signature();state.createdAt=Date.now();state.sessionId='calc-'+state.createdAt+'-'+Math.random().toString(16).slice(2,10);state.session=new M.BatterySession(configuration());}state.integration.clear();state.integrationStartedAt=Date.now();state.integrationFloor=state.status.device.uptimeMs+Math.max(0,state.integrationStartedAt-state.statusAt);state.segmentHasEndpoint=false;state.pendingCurrents=[];state.lastCurrentAt=state.integrationStartedAt;state.session.start();state.notice='Test running. Waiting for fresh received readings from both selected channels.';persistSettings();render();}
  function pause(reason,gap){if(state.session){const wasRunning=state.session.snapshot().running;state.session.pause();if(gap && wasRunning)state.session.markGap(reason);if(state.session.snapshot().samples>0)saveCurrent(false);}state.notice=reason;render();}
  function reset(){if(state.session && state.session.snapshot().samples>0)saveCurrent(false);state.session=null;state.sessionId='';state.sessionConfig=null;state.sessionSources=null;state.sessionSignature='';state.notice='Test reset. Saved summaries remain available below.';render();}
  function sourceSummary(key){const meter=meterFor(key),parts=keyParts(key);return {meter:parts[0],channel:parts[1],name:meter && meter.name || '',address:meter && meter.address || '',profile:meter && meter.profile || ''};}
  function currentRecord(){if(!state.session)return null;return {id:state.sessionId,schemaVersion:1,createdAt:state.createdAt,updatedAt:Date.now(),assumptions:{...state.sessionConfig},sources:{voltage:{...state.sessionSources.voltage},current:{...state.sessionSources.current}},summary:state.session.snapshot(),restorationPolicy:'Summary only. Integration never resumes automatically after reload.',method:'DC integration follows every received current sample timestamp (BLE reception), with browser receipt times guarding interruptions. Wh uses the latest nearby voltage within 1 second; independent meters are not synchronized. Missing, stale or interrupted intervals are excluded.'};}
  function exportSummary(record){if(!record)return;const blob=new Blob([JSON.stringify(record,null,2)+'\n'],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=record.id+'-summary.json';document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function storageTransaction(store,action){return new Promise((resolve,reject)=>{if(!state.db){reject(new Error('Calculation storage unavailable.'));return;}const tx=state.db.transaction(store,action==='read'?'readonly':'readwrite'),object=tx.objectStore(store);let result;tx.oncomplete=()=>resolve(result);tx.onabort=()=>reject(tx.error || new Error('Calculation storage transaction failed.'));tx.onerror=()=>{};if(action==='read'){const request=object.getAll();request.onsuccess=()=>{result=request.result;};}else action(object);});}
  async function openStorage(){
    if(state.db)return;
    try{state.db=await new Promise((resolve,reject)=>{if(!window.indexedDB){reject(new Error('IndexedDB unavailable.'));return;}const request=indexedDB.open('meter-calculations',1);request.onupgradeneeded=()=>{const db=request.result;db.createObjectStore('sessions',{keyPath:'id'});db.createObjectStore('settings',{keyPath:'key'});};request.onerror=()=>reject(request.error || new Error('Could not open calculation storage.'));request.onblocked=()=>reject(new Error('Close other dashboard tabs and retry calculation storage.'));request.onsuccess=()=>resolve(request.result);});state.db.onversionchange=()=>{state.db.close();state.db=null;state.storageError='Storage changed in another tab. Reload this dashboard.';renderStorage();};const settings=await storageTransaction('settings','read'),saved=settings.find(s=>s.key==='configuration');if(saved){state.settings={...defaults,...saved.value};applySettings();}state.saved=await storageTransaction('sessions','read');state.storageError='';renderSaved();render();}
    catch(error){state.storageError=error.message || String(error);renderStorage();}
  }
  async function persistSettings(){if(!state.db)return;try{await storageTransaction('settings',store=>store.put({key:'configuration',value:{...state.settings},updatedAt:Date.now()}));}catch(error){state.storageError=error.message;renderStorage();}}
  async function saveCurrent(explicit){const record=currentRecord();if(!record){if(explicit){state.notice='Start a test before saving a summary.';render();}return;}if(!state.db){if(explicit){state.notice='Browser storage is unavailable. Export this summary JSON to keep it.';render();}return;}try{await storageTransaction('sessions',store=>store.put(record));state.saved=state.saved.filter(s=>s.id!==record.id);state.saved.push(record);state.lastSave=Date.now();state.storageError='';if(explicit)state.notice='Calculation summary saved in this browser.';renderSaved();renderStorage();if(explicit)render();}catch(error){state.storageError=error.message;renderStorage();}}
  async function deleteSummary(id){try{await storageTransaction('sessions',store=>store.delete(id));state.saved=state.saved.filter(s=>s.id!==id);renderSaved();}catch(error){state.storageError=error.message;renderStorage();}}
  function renderStorage(){el['calc-storage'].textContent=state.storageError?'Calculation storage unavailable: '+state.storageError:state.db?'Separate calculation storage ready · '+state.saved.length+' saved summaries':'Opening calculation storage…';el['calc-storage'].className='hint'+(state.storageError?' bad-text':'');el['calc-storage-retry'].hidden=!!state.db;}
  function renderSaved(){const list=el['calc-sessions'];list.replaceChildren();if(!state.saved.length){list.append(text('p','hint','No calculation summaries saved yet.'));renderStorage();return;}for(const row of [...state.saved].sort((a,b)=>b.updatedAt-a.updatedAt)){const card=text('article','calc-saved-session'),s=row.summary || {};card.append(text('h3','',new Date(row.createdAt).toLocaleString()),text('p','',format(s.dischargeAh,6)+' Ah discharged · '+format(s.dischargeWh,6)+' Wh · '+(s.gapCount || 0)+' gaps'),text('p','hint',(row.assumptions && row.assumptions.ratedCapacityAh!==null?'Rated-capacity assumptions':'Measured-only test')+' · saved snapshot; no automatic resume'));const buttons=text('div','button-row');addButton(buttons,'','Export JSON','secondary small',()=>exportSummary(row));addButton(buttons,'','Delete local summary','ghost small',()=>deleteSummary(row.id));card.append(buttons);list.append(card);}renderStorage();}
  function render(){
    const result=calculation(false),reason=validation(),snap=state.session && state.session.snapshot(),running=!!(snap && snap.running);
    el['calc-power-value'].textContent=format(result.valid?(state.settings.mode==='ac'?result.voltAmps:result.watts):null);el['calc-power-unit'].textContent=state.settings.mode==='ac'?'VA':'W';el['calc-real-power-value'].textContent=format(result.valid && state.settings.mode==='ac'?result.watts:null);el['calc-resistance-value'].textContent=format(result.valid?result.ohms:null);el['calc-voltage-value'].textContent=format(result.valid?result.voltageV:null);el['calc-current-value'].textContent=format(result.valid?result.currentA:null);
    el['calc-live-state'].textContent=result.valid?'Fresh sources':state.offline?'Device offline':'Sources unavailable / stale';el['calc-live-state'].className='pill '+(result.valid?'good':'pending');
    el['calc-live-note'].textContent=result.valid?(state.settings.mode==='ac'?'AC apparent power is V×I in VA. Real power is shown only with a configured power factor.':'DC power = voltage × current. V/I needs nonzero current; signed results follow the selected meter polarities.'):'Fresh, connected, numeric voltage and current readings with compatible units/modes are required. '+(result.reason || '');
    el['calc-power-factor'].disabled=state.settings.mode!=='ac';el['calc-start'].disabled=running || !!reason || !result.valid;el['calc-start'].textContent=snap && snap.samples>0?'Resume test':'Start test';el['calc-pause'].disabled=!running;el['calc-save'].disabled=!snap;el['calc-export'].disabled=!snap;el['calc-reset'].disabled=!snap;
    el['calc-session-state'].textContent=running?'Running':snap?snap.gapCount?'Paused · gap':'Paused':'Idle';el['calc-session-state'].className='pill '+(running?'good':snap && snap.gapCount?'pending':'');
    el['calc-charge-value'].textContent=snap?format(snap.dischargeAh,7):'0';el['calc-energy-value'].textContent=snap?format(snap.dischargeWh,7):'0';el['calc-charge-in-value'].textContent=snap?format(snap.chargeAh,7):'0';el['calc-energy-in-value'].textContent=snap?format(snap.chargeWh,7):'0';
    const estimate=snap && snap.estimate;el['calc-remaining-value'].textContent=format(estimate && estimate.valid?estimate.remainingAh:null);el['calc-soc-value'].textContent=format(estimate && estimate.valid?estimate.socPercent:null);el['calc-runtime-value'].textContent=duration(estimate && estimate.valid?estimate.runtimeHours:null);el['calc-time-value'].textContent=snap?format(snap.integratedMs/1000,5)+' s':'0 s';
    el['calc-message'].textContent=reason || state.notice;el['calc-message'].className='calc-message'+(reason || snap && snap.gapCount?' calc-warning':'');const cfg=state.sessionConfig || configuration();el['calc-assumptions'].textContent=(cfg.ratedCapacityAh!==null?'Assumed rating '+format(cfg.ratedCapacityAh)+' Ah; initial charge '+format(cfg.initialSocPercent)+'%. ':'Measured-only test: no rated capacity or remaining runtime claimed. ')+(cfg.direction==='negative-discharge'?'Negative current is discharge. ':'Positive current is discharge. ')+(snap?'Integrated '+snap.samples+' accepted observations; '+snap.gapCount+' gaps. '+(estimate && !estimate.valid && estimate.reason?estimate.reason+'. ':''):'')+'Integration follows current sample timestamps. Wh uses the latest nearby voltage; independent meters are not synchronized. Freshness/gap limit: 3 s; voltage/current time skew limit: 1 s. Pauses and missing intervals are excluded.';
  }
  function integrationFreshnessReason(){
    if(!state.session || !state.session.snapshot().running)return '';
    const now=Date.now();
    if(now-state.lastCurrentAt>=MAX_AGE)return 'The received current feed stalled. Gap recorded; resume explicitly after fresh samples arrive.';
    const voltage=readingFor(state.settings.voltageSource,true),current=readingFor(state.settings.currentSource,true);
    if(!voltage || !current)return now-state.integrationStartedAt>=MAX_AGE?'Both selected feeds have not arrived. Gap recorded; resume explicitly after fresh samples arrive.':'';
    return voltage.fresh && current.fresh?'':'Received calculation samples became stale. Gap recorded; resume explicitly after fresh samples arrive.';
  }
  function receiveStatus(event){const d=event.detail || {},status=d.status;if(!status || !status.device)return;const old=state.status && state.status.device;if(old && (String(old.bootId)!==String(status.device.bootId) || String(old.id)!==String(status.device.id))){pause('Device restarted or changed. Gap recorded; resume explicitly after fresh readings arrive.',true);state.live.clear();state.integration.clear();state.integrationCursors.clear();}state.status=status;state.statusAt=Number.isFinite(d.receivedAt)?d.receivedAt:Date.now();state.offline=false;renderSources();const current=calculation(false);if(state.session && state.session.snapshot().running){if(state.sessionSignature!==signature())pause('The selected physical meter or profile changed. Save this result, then reset before using the new source.',true);else if(!current.valid)pause('Selected readings are missing, disconnected or stale. Gap recorded; resume explicitly.',true);else{const reason=integrationFreshnessReason();if(reason)pause(reason,true);}}render();}
  function integrationFrameEligible(meter,frame,boot,key){
    if(!C.matchesMeterFrame(state.status,meter,frame,boot) || !Number.isSafeInteger(frame.seq) || frame.seq<0 || !Number.isFinite(frame.uptimeMs) || frame.uptimeMs<0)return false;
    const deviceUptime=state.status.device.uptimeMs;if(!Number.isFinite(deviceUptime))return false;
    const expected=deviceUptime+Math.max(0,Date.now()-state.statusAt),previous=state.integrationCursors.get(key);
    if(expected-frame.uptimeMs>=MAX_AGE || frame.uptimeMs>expected+1000)return false;
    return !previous || frame.seq>previous.seq && frame.uptimeMs>=previous.uptimeMs;
  }
  function integrationReadingReason(reading,quantity){
    const normalized=M.normalizeReading(reading,quantity,{maxAgeMs:MAX_AGE});if(!normalized.valid)return normalized.reason;
    const mode=String(reading.mode || '').toUpperCase(),ac=!!((reading.flags || 0)&1024) || /\bAC\b/.test(mode),dc=!!((reading.flags || 0)&512) || /\bDC\b/.test(mode);
    if(ac && dc || state.settings.mode==='dc' && ac || state.settings.mode==='ac' && dc)return 'mode-mismatch';
    if(quantity==='voltage' && normalized.value<0)return 'battery-voltage-polarity';
    return '';
  }
  function ingestCurrentObservation(observation){
    const input={voltage:readingFor(state.settings.voltageSource,true),current:readingFor(state.settings.currentSource,true,observation.frame),receivedAt:observation.receivedAt,deviceId:observation.deviceId,bootId:observation.bootId,gap:false};
    if(!input.voltage || !input.current)return;
    const previous=state.session.snapshot();state.session.ingest(input);const snap=state.session.snapshot();
    if(snap.gapCount>previous.gapCount || snap.lastReason || !snap.running)pause('Integration paused: '+(snap.lastGapReason || snap.lastReason || 'fresh compatible samples required')+'. Resume explicitly after the source recovers.',true);
    else{state.segmentHasEndpoint=true;state.notice='Test running. Every current observation contributes; voltage updates do not add elapsed time.';}
  }
  function receiveFrames(event){
    const d=event.detail || {},device=state.status && state.status.device;if(!device || state.offline)return;
    if(String(d.deviceId)!==String(device.id) || String(d.bootId)!==String(device.bootId)){pause('Feed identity changed. Gap recorded; waiting for matching device status.',true);return;}
    if(d.gap)pause('Device backlog has missing readings. Gap recorded; resume explicitly.',true);
    // Display replay protection and integration have different bounds: an unseen
    // recent frame may predate the newest /status reading and still belongs in
    // the measured interval. Process observations in feed order, never collapse
    // a page to its last pair (which would erase pulses and invalid intervals).
    for(const f of d.frames || []){
      if(!f || f.kind!=='reading')continue;const key=keyFor(f.meter,f.channel);
      if(key!==state.settings.voltageSource && key!==state.settings.currentSource)continue;
      const meter=meterFor(key),frame={...f,bootId:d.bootId};
      if(C.liveFrameEligible(state.status,meter,f,d.bootId,state.live.get(key),state.statusAt,Date.now()))state.live.set(key,frame);
      // A delayed page can contain fresh readings captured while the test was
      // paused. They are outside this segment: remember their high-water marks
      // without using them as samples or turning their exclusion into a gap.
      if(state.session && C.matchesMeterFrame(state.status,meter,f,d.bootId) && Number.isSafeInteger(f.seq) && f.seq>=0 && Number.isFinite(f.uptimeMs) && f.uptimeMs>=0 && f.uptimeMs<state.integrationFloor){const previous=state.integrationCursors.get(key);if(!previous || f.seq>previous.seq && f.uptimeMs>=previous.uptimeMs)state.integrationCursors.set(key,{seq:f.seq,uptimeMs:f.uptimeMs});continue;}
      if(!integrationFrameEligible(meter,f,d.bootId,key)){pause('Old, replayed, stale or incompatible calculation samples were excluded. Gap recorded; resume explicitly.',true);continue;}
      state.integration.set(key,frame);state.integrationCursors.set(key,{seq:f.seq,uptimeMs:f.uptimeMs});
      if(!state.session || !state.session.snapshot().running)continue;
      const quantity=key===state.settings.voltageSource?'voltage':'current',reason=integrationReadingReason(readingFor(key,true),quantity);
      if(reason){pause('Integration paused: '+reason+'. Gap recorded; resume explicitly after the source recovers.',true);continue;}
      if(quantity==='voltage'){
        // Before this segment has an endpoint, current may arrive first. Its
        // first compatible voltage completes the pending baseline; subsequent
        // voltage updates never rewrite an established current endpoint.
        if(!state.segmentHasEndpoint && state.pendingCurrents.length){const pending=state.pendingCurrents;state.pendingCurrents=[];for(const observation of pending){if(!state.session.snapshot().running)break;ingestCurrentObservation(observation);}}
        continue;
      }
      state.lastCurrentAt=Number.isFinite(d.receivedAt)?d.receivedAt:Date.now();
      const observation={frame,receivedAt:state.lastCurrentAt,deviceId:d.deviceId,bootId:d.bootId};
      if(!readingFor(state.settings.voltageSource,true)){if(state.pendingCurrents.length>=64){pause('Too many current observations arrived before voltage. Gap recorded; resume after both selected feeds are available.',true);state.pendingCurrents=[];}else{state.pendingCurrents.push(observation);state.notice='Test running; waiting for a fresh received voltage to complete the first current baseline.';}continue;}
      ingestCurrentObservation(observation);
    }
    if(state.session && state.session.snapshot().running && Date.now()-state.lastSave>=1000)saveCurrent(false);
    render();
  }
  build();render();openStorage();
  window.addEventListener('meter:status',receiveStatus);window.addEventListener('meter:frames',receiveFrames);window.addEventListener('meter:offline',()=>{state.offline=true;pause('Device offline. Gap recorded; resume explicitly after reconnection.',true);});window.addEventListener('meter:panel',event=>{if(event.detail && event.detail.name==='calculations')render();});
  document.addEventListener('visibilitychange',()=>{if(document.hidden && state.session && state.session.snapshot().running)pause('Browser tab was hidden. Gap recorded; resume explicitly after returning.',true);});
  setInterval(()=>{const reason=integrationFreshnessReason();if(reason)pause(reason,true);else if(state.session && state.session.snapshot().running && !calculation(false).valid)pause('Selected readings became stale. Gap recorded; resume explicitly after fresh readings arrive.',true);else render();},500);
})();
