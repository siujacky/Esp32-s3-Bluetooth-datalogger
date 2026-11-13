(function () {
  'use strict';
  const C = window.MeterCore, V=window.MeterValueTiles, F=window.MeterValueFormat, $ = id => document.getElementById(id);
  const prefs=C.readViewPreferences(()=>window.localStorage),slotColors=['#62e2bf','#7bbcff','#c3a0ff','#ffbf69'];
  const state = {status:null,profiles:[],focus:prefs.focus,count:prefs.count,layout:prefs.layout,channels:prefs.channels,channel:prefs.channels[prefs.focus],db:null,dbError:'',feedBoot:'',feedDevice:'',feedGeneration:0,storageGeneration:0,after:0,deviceId:'',saved:[],live:new Map(),events:[],eventKeys:new Set(),gaps:new Set(),rules:[],cards:[],panel:'live',deleteSession:'',deleteRule:null,lastSavedRefresh:0,statusAt:0,anchoredBoot:'',expanded:false,multiKey:'',views:[]};
  const valuePrefs=V.readPreferences(()=>window.localStorage);let valueSaved=valuePrefs.storageAvailable,valueKey='',valueViews=[],applyingValues=false;
  const scope = new C.ScopeSeries(2400);
  let setupSlot=state.focus,rotationBusy=false;
  let wifiScanTimer=null,wifiPollBusy=false,selectedWifi=null,selectedDevice=null,connectingDevice=false;
  const ntpUi={dirty:false,busy:false,browserBusy:false,offline:false,loaded:'',message:'',bad:false,awaiting:null,syncAfter:null};
  function emit(name,detail){window.dispatchEvent(new CustomEvent('meter:'+name,{detail}));}
  function node(tag, className, text) {
    const item = document.createElement(tag);
    if (className) item.className = className;
    if (text != null) item.textContent = text;
    return item;
  }
  function button(text, className, click) { const b = node('button','button '+(className || ''),text); b.type='button'; b.addEventListener('click',click); return b; }
  function setText(id,text) { $(id).textContent = text == null ? '' : String(text); }
  function notice(text,good) { const n=$('notice'); n.textContent=text; n.className=good?'good':''; n.hidden=false; }
  function errorText(error) { return error && error.message ? error.message : String(error); }
  async function api(path,method,body) {
    const abort = new AbortController(), timer=setTimeout(() => abort.abort(),12000);
    try {
      const options={method:method || 'GET',cache:'no-store',signal:abort.signal,headers:{Accept:'application/json'}};
      if (body !== undefined) { options.headers['Content-Type']='application/json'; options.body=JSON.stringify(body); }
      const response=await fetch(path,options);
      let data;
      try { data=await response.json(); } catch (_) { throw new Error('Device returned an invalid JSON response ('+response.status+').'); }
      if (!response.ok || data.ok === false) throw new Error(data.error || data.message || 'Device request failed ('+response.status+').');
      return data;
    } catch (error) {
      if (error.name === 'AbortError') throw new Error('Device request timed out. Check your connection.');
      throw error;
    } finally { clearTimeout(timer); }
  }
  async function action(body) {
    try { await api('/api/control','POST',body); await refreshStatus(); }
    catch (error) { notice(errorText(error)); }
  }
  const meters = () => state.status && Array.isArray(state.status.meters) ? state.status.meters : [];
  const meterAt = slot => meters().find(m => Number(m.slot) === Number(slot)) || {slot,id:'m'+(slot+1),name:'Meter '+(slot+1),state:'disconnected',readings:[]};
  const profileName = id => { const p=state.profiles.find(p => p.id===id); return p ? p.name+(p.experimental?' · experimental':'') : id || 'Select profile'; };
  function readingText(r) {
    if (!r) return '—';
    const number=C.numericValue(r);
    if (number === null) return r.text || '—';
    return r.text || String(number);
  }
  const displayedSlots=()=>C.displaySlots(state.focus,state.count);
  const selectedReading=slot=>C.selectLiveReading(state.status,meterAt(slot),state.channels[slot],state.live.get(slot+':'+state.channels[slot]),state.statusAt,Date.now());
  const focusedReading=()=>selectedReading(state.focus);
  function saveView(){C.saveViewPreferences(()=>window.localStorage,state);}
  function changeChannel(slot,channel){if(!C.validMeterSlot(slot)||!C.validChannel(channel))return;state.channels[slot]=channel;if(slot===state.focus)state.channel=channel;saveView();renderFocus();drawScope();}
  function selectMeterSetup(slot){if(!Number.isInteger(slot)||slot<0||slot>=C.MAX_METERS)return;setupSlot=slot;$('connect-slot').value=String(slot);renderCards();renderDiscovery();}
  function installMeterTabs(){const tabs=$('meter-setup-tabs');tabs.replaceChildren();for(let slot=0;slot<C.MAX_METERS;slot++){const tab=document.createElement('button');tab.type='button';tab.id='meter-setup-tab-'+slot;tab.className='meter-setup-tab';tab.setAttribute('role','tab');tab.setAttribute('aria-controls','meter-setup-panel-'+slot);tab.style.setProperty('--meter-color',slotColors[slot]);tab.innerHTML='<strong>M'+(slot+1)+'</strong><span></span>';tab.addEventListener('click',()=>selectMeterSetup(slot));tab.addEventListener('keydown',event=>{let next=slot;if(event.key==='ArrowRight')next=(slot+1)%4;else if(event.key==='ArrowLeft')next=(slot+3)%4;else if(event.key==='Home')next=0;else if(event.key==='End')next=3;else return;event.preventDefault();selectMeterSetup(next);$('meter-setup-tab-'+next).focus();});tabs.appendChild(tab);}}
  function renderMeterTabs(){for(let slot=0;slot<C.MAX_METERS;slot++){const tab=$('meter-setup-tab-'+slot);if(!tab)continue;tab.setAttribute('aria-selected',String(slot===setupSlot));tab.tabIndex=slot===setupSlot?0:-1;const meter=meterAt(slot);tab.querySelector('span').textContent=meter.state==='connected'?(meter.fresh?'LIVE':'LINKED'):['connecting','reconnecting'].includes(meter.state)?'LINKING':'OFF';}}
  function buildCards() {
    $('meter-cards').replaceChildren(); state.cards=[];
    for (let slot=0;slot<C.MAX_METERS;slot++) {
      const card=node('article','meter-card'), title=node('div','meter-title'), name=node('h2','', 'Meter '+(slot+1)), status=node('span','pill','Disconnected');
      card.id='meter-setup-panel-'+slot;card.setAttribute('role','tabpanel');card.setAttribute('aria-labelledby','meter-setup-tab-'+slot);card.style.setProperty('--meter-color',slotColors[slot]);
      title.append(name,status); const identity=node('p','identity'), values=node('div','meter-readings');
      const label=node('label','','Bluetooth model / profile'), select=node('select'); select.setAttribute('aria-label','Meter '+(slot+1)+' Bluetooth profile');
      select.append(new Option('Select a profile…',''));
      state.profiles.forEach(p => select.append(new Option(p.name+(p.experimental?' · experimental':''),p.id)));
      label.append(select);
      select.addEventListener('change',() => { if(select.value) action({action:'profile',slot,profile:select.value}); });
      const controls=node('div','button-row');
      const focus=button('View','ghost',() => chooseFocus(slot));
      const connect=button('Choose device','secondary',() => { $('connect-slot').value=String(slot); $('discovery-panel').hidden=false; $('discovery-panel').scrollIntoView({behavior:'smooth',block:'nearest'}); renderDiscovery(); });
      const disconnect=button('Disconnect','ghost',() => action({action:'disconnect',slot}));
      controls.append(focus,connect,disconnect);card.append(title,identity,values,label,controls);$('meter-cards').append(card);
      state.cards.push({card,name,status,identity,values,select,focus,disconnect});
    }
    installMeterTabs();selectMeterSetup(setupSlot);
  }
  function renderCards() {
    renderMeterTabs();
    state.cards.forEach((ui,slot) => {
      const meter=meterAt(slot), connected=meter.state==='connected';
      ui.card.hidden=slot!==setupSlot;ui.card.classList.toggle('selected',slot===setupSlot); ui.focus.setAttribute('aria-pressed',String(slot===state.focus));
      ui.name.textContent=meter.name || 'Meter '+(slot+1); ui.status.textContent=meter.state || 'disconnected'; ui.status.className='pill '+(connected?'good':meter.state==='connecting'?'pending':'');
      ui.identity.textContent=meter.address || 'No device selected'; ui.disconnect.disabled=!connected && meter.state!=='connecting' && meter.state!=='reconnecting';
      ui.select.disabled=connected||meter.state==='connecting'||meter.state==='reconnecting';
      if (document.activeElement !== ui.select && [...ui.select.options].some(o => o.value===meter.profile)) ui.select.value=meter.profile;
      ui.values.replaceChildren();
      (Array.isArray(meter.readings)?meter.readings:[]).slice(0,2).forEach(r => {
        const value=node('span','mini-reading'),digits=node('span','mini-number');V.setNumber(digits,r,valuePrefs.formats[slot][Number(r.channel)||0],'mini');value.append(digits,node('small','',r.unit || ''));ui.values.append(value);
      });
      if (!ui.values.childElementCount) ui.values.append(node('span','mini-reading','—'));
    });
  }
  function chooseFocus(slot) {
    if(!C.validMeterSlot(slot))return;
    state.focus=slot; state.channel=state.channels[slot]; saveView(); renderCards(); renderFocus(); drawScope();
    action({action:'focus',slot});
  }
  function renderDiscovery() {
    const discovery=state.status && state.status.discovery || {},select=$('discovered-device');
    const devices=(discovery.devices||[]).filter(d=>C.discoverySnapshot(discovery,d.index));
    const changed=selectedDevice&&!C.discoverySelectionMatches(discovery,selectedDevice);
    if(changed)selectedDevice=null;
    setText('scan-state',changed?'Discovery changed. Select the exact device again before connecting.':discovery.scanning?'Scanning… discovered identities appear below.':'Choose a device, then press Connect. Selection alone does not connect.');
    $('scan-button').disabled=!!discovery.scanning;
    // Avoid replacing an unchanged select while its native popup has focus.
    const signature=JSON.stringify(devices.map(d=>[d.index,d.name,d.address,d.addressType]));
    if(select.dataset.devices!==signature){select.replaceChildren(new Option(devices.length?'Select a discovered device…':'No devices found yet',''),...devices.map(d=>new Option((d.name||'Unnamed meter')+' · '+d.address,String(d.index))));select.dataset.devices=signature;}
    select.value=selectedDevice?String(selectedDevice.index):'';select.disabled=connectingDevice||!devices.length;
    const slot=Number($('connect-slot').value),meter=meterAt(slot),busy=['connected','connecting','reconnecting'].includes(meter.state);
    $('connect-device').disabled=connectingDevice||!selectedDevice||busy||!C.validMeterSlot(slot);
    $('connect-slot').disabled=connectingDevice;
    setText('discovery-list',selectedDevice?(selectedDevice.address+' · '+(selectedDevice.rssi==null?'RSSI unavailable':selectedDevice.rssi+' dBm')+' · '+(selectedDevice.profile?profileName(selectedDevice.profile):'Select profile manually')):devices.length?'Device names may repeat. Check the Bluetooth address.':discovery.scanning?'Listening for nearby meters…':'Turn on meter Bluetooth, then scan.');
    setText('connect-device-state',connectingDevice?'Sending connection request…':busy?'Disconnect this slot before choosing another meter.':selectedDevice?'Selected '+(selectedDevice.name||'Unnamed meter')+' · '+selectedDevice.address:'Select a device to enable Connect.');
  }
  async function connectSelectedDevice(){
    if(connectingDevice)return;
    const slot=Number($('connect-slot').value),device=selectedDevice,discovery=state.status&&state.status.discovery;
    if(!C.validMeterSlot(slot)||!C.discoverySelectionMatches(discovery,device)){selectedDevice=null;renderDiscovery();notice('Discovery changed. Select the exact device again.');return;}
    if(['connected','connecting','reconnecting'].includes(meterAt(slot).state)){notice('Disconnect this slot before changing its meter.');return;}
    const profile=state.cards[slot].select.value;
    if(!profile){notice('Choose an explicit Bluetooth model / profile in Meter '+(slot+1)+' first.');return;}
    connectingDevice=true;renderDiscovery();
    try{await action({action:'connect',slot,deviceIndex:device.index,address:device.address,addressType:device.addressType,profile});}
    finally{connectingDevice=false;renderDiscovery();}
  }
  function channelOptions(select,slot){
    const readings=meterAt(slot).readings || [],channels=[...new Set([0,state.channels[slot],...readings.map(r=>Number(r.channel)),...Array.from(state.live.values()).filter(f=>Number(f.meter)===slot).map(f=>Number(f.channel))])].filter(ch=>Number.isInteger(ch) && ch>=0 && ch<8).sort((a,b)=>a-b);
    if(select.dataset.channels!==channels.join(',')){select.replaceChildren(...channels.map(ch=>new Option(String(ch+1),String(ch))));select.dataset.channels=channels.join(',');}
    select.value=String(state.channels[slot]);
  }
  function ageText(r){return r?((r.fresh===false || r.ageMs>=5000?'STALE · ':'')+(Number.isFinite(r.ageMs)?(r.ageMs/1000).toFixed(1)+'s since sample':'Sample received')):'No samples received';}
  function selectedTrace(slot){
    const r=selectedReading(slot),trace=scope.get(slot,state.channels[slot]);
    return C.matchesScopeReading(trace,r)?trace:null;
  }
  function layoutViews(){
    const multi=state.count>1,overlay=multi && state.layout==='overlay',slots=displayedSlots(),key=state.layout+':'+(multi?slots.join(','):'');
    $('display-count').value=String(state.count);$('chart-layout').value=state.layout;$('instrument').dataset.count=String(state.count);$('instrument').dataset.layout=state.layout;$('instrument').style.setProperty('--meter-color',slotColors[state.focus]);
    $('instrument').classList.toggle('chart-surface',valuePrefs.view==='scope'&&(!multi || overlay));$('instrument').classList.toggle('value-surface',valuePrefs.view==='values');
    $('single-reading').hidden=multi;$('single-scope').hidden=multi&&!overlay;$('focus-channel-control').hidden=multi;$('multi-values').hidden=!multi;$('scope-lanes').hidden=!multi||overlay;$('scope-overlay-legend').hidden=!overlay;
    setText('clear-scope',multi?'Clear shown traces':'Clear plot');
    const isValues=valuePrefs.view==='values';$('instrument-view').value=valuePrefs.view;$('value-layout').value=valuePrefs.presets[state.count];updateLayoutPictures();
    $('value-layout-control').hidden=!isValues;$('value-format-button').hidden=!isValues;$('chart-layout-control').hidden=isValues;
    $('value-tiles').hidden=!isValues;$('values-font-status').hidden=!isValues;
    if(isValues){for(const id of ['single-reading','single-scope','multi-values','scope-lanes','scope-overlay-legend','focus-channel-control'])$(id).hidden=true;}
    document.querySelector('#instrument .scope-toolbar').hidden=isValues;
    if(state.multiKey===key)return;state.multiKey=key;state.views=[];$('multi-values').replaceChildren();$('scope-lanes').replaceChildren();$('scope-overlay-legend').replaceChildren();
    slots.forEach(slot=>{
      if(!multi)return;
      const card=node('article','display-value-card'),heading=node('div','display-value-heading'),name=node('h2'),status=node('span','pill');card.dataset.slot=String(slot);card.style.setProperty('--meter-color',slotColors[slot]);heading.append(name,status);
      const reading=node('div','display-value-reading'),value=node('strong','display-value'),unit=node('span','display-unit');reading.append(value,unit);
      const label=node('label','display-channel','Channel'),select=node('select');select.id='meter-channel-'+slot;select.setAttribute('aria-label','Meter '+(slot+1)+' channel');label.append(select);select.addEventListener('change',()=>changeChannel(slot,Number(select.value)));
      const mode=node('p','hint display-mode'),age=node('p','hint display-age');card.append(heading,reading,mode,age,label);$('multi-values').append(card);
      const lane=node('section',overlay?'overlay-trace':'scope-lane'),header=node('div','lane-heading'),title=node('h3'),current=node('div','lane-current'),laneValue=node('strong','lane-current-value'),laneUnit=node('span','lane-unit'),laneStatus=node('span','lane-status');lane.id=(overlay?'overlay-trace-':'scope-lane-')+slot;lane.dataset.slot=String(slot);lane.style.setProperty('--meter-color',slotColors[slot]);current.append(laneValue,laneUnit);header.append(title,current,laneStatus);lane.append(header);
      let canvas=null,empty=null;
      if(!overlay){const wrap=node('div','scope-wrap');canvas=node('canvas');empty=node('div','scope-empty','No numeric samples in this window.');canvas.setAttribute('role','img');canvas.setAttribute('aria-label','Meter '+(slot+1)+' measurement scope, independent scale');empty.append(node('small','','Only received samples are plotted'));wrap.append(canvas,empty);lane.append(wrap);}
      const laneMode=node('p','lane-mode hint'),range=node('p','lane-range hint');lane.append(laneMode,range);if(overlay)lane.append(label);$(overlay?'scope-overlay-legend':'scope-lanes').append(lane);state.views.push({slot,card,name,status,value,unit,mode,age,select,title,laneValue,laneUnit,laneStatus,laneMode,canvas,empty,range});
    });
  }
  function saveValues(){valueSaved=V.savePreferences(()=>window.localStorage,valuePrefs);return valueSaved;}
  function renderValueTiles(){
    const preset=valuePrefs.presets[state.count],slots=displayedSlots(),key=preset+':'+slots.join(',');
    $('value-tiles').dataset.preset=preset;$('value-tiles').dataset.count=String(state.count);
    if(key!==valueKey){valueKey=key;valueViews=[];$('value-tiles').replaceChildren();
      slots.forEach((slot,index)=>{const tile=node('article','value-tile'),heading=node('div','value-tile-heading'),name=node('h3'),status=node('span','value-tile-status'),value=node('div','value-number'),unit=node('div','value-unit'),footer=node('div','value-tile-meta'),detail=node('div','value-tile-detail'),raw=node('p','value-raw'),mode=node('p'),age=node('p'),label=node('label','value-channel','Channel'),select=node('select');
        const role=preset==='solo'?'main':preset==='equal'||preset==='grid'?'equal':index===0||preset==='large-large-small'&&index===1?'large':'small';tile.dataset.slot=String(slot);tile.dataset.role=role;tile.style.setProperty('--meter-color',slotColors[slot]);
        select.id='value-channel-'+slot;select.setAttribute('aria-label','Meter '+(slot+1)+' value channel');select.addEventListener('change',()=>changeChannel(slot,Number(select.value)));label.append(select);heading.append(name,status);detail.append(raw,mode,age);footer.append(detail,label);tile.append(heading,value,unit,footer);$('value-tiles').append(tile);valueViews.push({slot,tile,name,status,value,unit,raw,mode,age,select,role});
      });
    }
    valueViews.forEach(ui=>{const r=selectedReading(ui.slot),meter=meterAt(ui.slot),f=valuePrefs.formats[ui.slot][state.channels[ui.slot]],out=V.readingNumber(r,f),stale=!!r&&(r.fresh===false||r.ageMs>=5000),fresh=meter.state==='connected'&&r&&!stale;
      ui.name.textContent='M'+(ui.slot+1)+' / CH'+(state.channels[ui.slot]+1);ui.name.title='Meter '+(ui.slot+1)+' · '+(meter.name||'')+' · '+(meter.address||'No source');ui.status.textContent=meter.state==='connected'?(fresh?'LIVE':r?'STALE':'WAITING'):r?'OFFLINE':(meter.state||'disconnected').toUpperCase();ui.tile.classList.toggle('stale',!fresh);
      V.setNumber(ui.value,r,f,ui.role,'Meter '+(ui.slot+1)+', channel '+(state.channels[ui.slot]+1)+', '+out.cells.trim()+' '+(r&&r.unit||'')+', '+ui.status.textContent);
      ui.unit.textContent=r&&r.unit||'—';ui.raw.textContent=r&&(out.state==='overflow'||out.state==='invalid'&&out.cells!=='OL')?'Original: '+readingText(r)+' '+(r.unit||''):'';ui.raw.hidden=!ui.raw.textContent;
      ui.mode.textContent=(r?r.mode||profileName(meter.profile):'Waiting for reading')+(r&&r.flags?' · Flags 0x'+Number(r.flags).toString(16):'');ui.age.textContent=ageText(r);channelOptions(ui.select,ui.slot);
    });
    const mainSizes=valueViews.filter(ui=>ui.role==='large').map(ui=>Number(ui.value.dataset.fontSize)).filter(size=>size>0);
    if(mainSizes.length){const smallLimit=Math.max(4,Math.floor(Math.min(...mainSizes)*.75));for(const ui of valueViews)if(ui.role==='small'){const r=selectedReading(ui.slot),f=valuePrefs.formats[ui.slot][state.channels[ui.slot]];const out=V.readingNumber(r,f);V.setNumber(ui.value,r,f,ui.role,'Meter '+(ui.slot+1)+', channel '+(state.channels[ui.slot]+1)+', '+out.cells.trim()+' '+(r&&r.unit||'')+', '+ui.status.textContent,smallLimit);}}
    const failed=document.documentElement.dataset.numeralFont==='failed';setText('values-font-status',(failed?'Outline font failed to load; readable fallback active. ':'')+(valueSaved?'Display settings saved in this browser.':'Display settings are in this session only; browser storage unavailable.'));
    $('value-format-device').disabled=applyingValues||!(state.status&&state.status.display&&state.status.display.values);
  }
  function formatSelection(){return {slot:Number($('value-format-slot').value),channel:Number($('value-format-channel').value),format:{digits:$('value-format-digits').valueAsNumber,decimals:$('value-format-decimals').valueAsNumber,padding:$('value-format-padding').value}};}
  function loadValueFormat(){const {slot,channel}=formatSelection(),f=valuePrefs.formats[slot][channel];$('value-format-digits').value=String(f.digits);$('value-format-decimals').value=String(f.decimals);$('value-format-padding').value=f.padding;previewValueFormat();}
  function previewValueFormat(){const {slot,channel,format}=formatSelection();if(!F.validNumberFormat(format)){setText('value-format-preview-state','Use 3..8 digits, with 0..6 decimals below the digit count.');return;}
    const source=channel===state.channels[slot]?selectedReading(slot):(meterAt(slot).readings||[]).find(r=>Number(r.channel)===channel),example=!source||source.numeric!==true,reading=example?{numeric:true,value:12.34,text:'12.34',unit:''}:source;
    V.setNumber($('value-format-preview'),reading,format,'preview');setText('value-format-preview-state',example?'Example preview only. Saving changes the display format; no samples are generated.':'Last received reading; preview only until saved.');
  }
  function openValueFormat(){const panel=$('value-format-panel');panel.hidden=false;$('value-format-slot').value=String(state.focus);$('value-format-channel').value=String(state.channels[state.focus]);setText('value-format-result','Save in browser keeps native settings independent. Apply to device explicitly changes its count, value layout and this selected channel format.');loadValueFormat();$('value-format-slot').focus();}
  function closeValueFormat(restore=true){const wasOpen=!$('value-format-panel').hidden;$('value-format-panel').hidden=true;if(wasOpen&&restore&&!$('value-format-button').hidden)$('value-format-button').focus({preventScroll:true});}
  async function waitNativeValues(predicate){let status;for(let i=0;i<24;i++){status=await refreshStatus();if(predicate(status))return status;await new Promise(r=>setTimeout(r,200));}throw Error('Device did not acknowledge the requested settings; re-read its status before retrying.');}
  async function applyValuesToDevice(){if(applyingValues)return;const selected=formatSelection();if(!F.validNumberFormat(selected.format)){setText('value-format-result','Invalid digit/decimal format.');return;}
    applyingValues=true;for(const e of $('value-format-panel').querySelectorAll('input,select,button'))if(e.id!=='value-format-close')e.disabled=true;
    const preset=valuePrefs.presets[state.count],count=state.count;let completed='No settings acknowledged.';
    try{await api('/api/control','POST',{action:'display-count',count});await waitNativeValues(s=>s.display&&s.display.count===count&&s.display.saved);completed='Meter count applied.';
      await api('/api/control','POST',{action:'value-layout',preset});await waitNativeValues(s=>s.display&&s.display.values&&s.display.values.layout===preset&&s.display.values.saved);completed+=' Layout applied.';
      await api('/api/control','POST',{action:'value-format',slot:selected.slot,channel:selected.channel,...selected.format});
      await waitNativeValues(s=>s.display&&s.display.values&&s.display.values.saved&&(s.display.values.formats||[]).some(f=>f.slot===selected.slot&&f.channel===selected.channel&&f.digits===selected.format.digits&&f.decimals===selected.format.decimals&&f.padding===selected.format.padding));
      setText('value-format-result','Device screen layout and selected number format are saved. Browser settings remain separate.');
    }catch(error){setText('value-format-result',completed+' '+errorText(error)+' Completed requests are not automatically retried.');}
    finally{applyingValues=false;for(const e of $('value-format-panel').querySelectorAll('input,select,button'))e.disabled=false;renderValueTiles();}
  }
  function renderFocus() {
    layoutViews();
    const meter=meterAt(state.focus);channelOptions($('focus-channel'),state.focus);
    const r=focusedReading(), trace=selectedTrace(state.focus);
    setText('focus-name',(meter.name || 'Meter '+(state.focus+1)).toUpperCase());setText('focus-state',meter.state || 'disconnected');
    $('focus-state').className='pill '+(meter.state==='connected'?'good':'');
    V.setNumber($('big-value'),r,valuePrefs.formats[state.focus][state.channels[state.focus]],'chart');setText('big-unit',r && r.unit || '');setText('big-mode',r ? r.mode || profileName(meter.profile) : 'Waiting for a reading');
    const stale=r && (r.fresh===false || r.ageMs>=5000);
    setText('sample-age',ageText(r));
    $('sample-age').className=stale?'stale':'';
    setText('reading-flags',r && r.flags ? 'Flags 0x'+Number(r.flags).toString(16) : '');setText('scope-unit',state.count===1?(r && r.unit || trace && trace.unit || ''):'· INDEPENDENT SCALES');
    let rate='Actual meter samples only';
    if(trace && trace.points.length>1) { const p=trace.points.slice(-12), delta=p[p.length-1].time-p[0].time; if(delta>0) rate=((p.length-1)*1000/delta).toFixed(2)+' samples / second · '+trace.points.length+' points'; }
    setText('sample-rate',rate);
    state.views.forEach(ui=>{
      const m=meterAt(ui.slot),reading=selectedReading(ui.slot),stale=reading && (reading.fresh===false || reading.ageMs>=5000);
      ui.name.textContent='Meter '+(ui.slot+1);ui.name.title=m.name || ui.name.textContent;ui.status.textContent=m.state || 'disconnected';ui.status.className='pill '+(m.state==='connected'&&!stale?'good':stale?'pending':'');
      V.setNumber(ui.value,reading,valuePrefs.formats[ui.slot][state.channels[ui.slot]],'chart');V.setNumber(ui.laneValue,reading,valuePrefs.formats[ui.slot][state.channels[ui.slot]],'chart');ui.unit.textContent=ui.laneUnit.textContent=reading && reading.unit || '';
      ui.mode.textContent=reading?reading.mode || profileName(m.profile):'Waiting for a reading';ui.age.textContent=ageText(reading);ui.age.classList.toggle('stale',!!stale);channelOptions(ui.select,ui.slot);
      const slotName='Meter '+(ui.slot+1);ui.title.textContent=slotName+(m.name && m.name!==slotName?' · '+m.name:'')+' · Channel '+(state.channels[ui.slot]+1);ui.laneStatus.textContent=(m.state || 'disconnected')+' · '+ageText(reading);
      ui.laneMode.textContent=ui.mode.textContent+(reading && reading.flags?' · Flags 0x'+Number(reading.flags).toString(16):'')+(V.readingNumber(reading,valuePrefs.formats[ui.slot][state.channels[ui.slot]]).state==='overflow'?' · Original: '+readingText(reading)+' '+(reading.unit||''):'');
      ui.laneStatus.classList.toggle('stale',!!stale);
    });
    renderValueTiles();
    if(V.readingNumber(r,valuePrefs.formats[state.focus][state.channels[state.focus]]).state==='overflow')setText('big-mode','OVR · Original: '+readingText(r)+' '+(r&&r.unit||''));
  }
  function drawScope() {
    const slots=displayedSlots(),traces=slots.map(selectedTrace),windowMs=Number($('scope-window').value);
    const end=Math.max(Number(state.status && state.status.device && state.status.device.uptimeMs)||0,...traces.map(t=>t && t.points.length?t.points[t.points.length-1].time:0));
    if(state.count===1){$('scope').setAttribute('aria-label','Meter '+(state.focus+1)+' measurement scope, plotted from received samples');paintScope($('scope'),$('scope-empty'),$('scope-range'),traces[0],end,windowMs,slotColors[state.focus]);}
    else if(state.layout==='overlay')paintOverlay(traces,end,windowMs);
    else state.views.forEach((ui,index)=>paintScope(ui.canvas,ui.empty,ui.range,traces[index],end,windowMs,slotColors[ui.slot]));
  }
  const formatRange=n=>Math.abs(n)>=1e6 || (Math.abs(n)>0 && Math.abs(n)<.001)?n.toExponential(2):Number(n.toPrecision(6)).toString();
  function paintOverlay(traces,end,windowMs) {
    const canvas=$('scope'),rect=canvas.getBoundingClientRect();if(rect.width<1 || rect.height<1)return;
    const model=C.scopeOverlay(state.views.map((ui,index)=>({slot:ui.slot,unit:(selectedReading(ui.slot) || {}).unit || (traces[index] || {}).unit || '',points:traces[index]?traces[index].points:[]})),end,windowMs);
    const mixed=model.scales.length>1,ratio=Math.min(window.devicePixelRatio || 1,2);canvas.width=Math.round(rect.width*ratio);canvas.height=Math.round(rect.height*ratio);canvas.dataset.end=String(end);
    const ctx=canvas.getContext('2d');if(!ctx)return;ctx.scale(ratio,ratio);
    const width=rect.width,height=rect.height,column=Math.min(76,Math.floor(width*.43/Math.max(1,model.scales.length))),left=0,top=0,w=width,h=height;
    ctx.clearRect(0,0,width,height);ctx.font='500 10px MeterDigits,ui-monospace,Consolas,monospace';ctx.lineWidth=1;ctx.strokeStyle='#263b45';
    for(let i=0;i<=5;i++){const x=Math.min(width-.5,Math.round(w*i/5)+.5);ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,height);ctx.stroke();}
    for(let i=0;i<=4;i++){const y=Math.min(height-.5,Math.round(h*i/4)+.5);ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(width,y);ctx.stroke();}
    model.scales.forEach((scale,index)=>{
      const x=column*(index+1);ctx.fillStyle=mixed?slotColors[scale.slots[0]]:'#afc3cd';ctx.textAlign='right';floatLabel(ctx,scale.unit || 'unitless',x,15,column-3);
      if(scale.min!==null)for(let i=0;i<=4;i++)floatLabel(ctx,Number((scale.max-(scale.max-scale.min)*i/4).toPrecision(4)).toString(),x,Math.max(29,Math.min(height-22,h*i/4+3)),column-3);
    });
    ctx.fillStyle='#809da9';ctx.textAlign='center';for(let i=0;i<=5;i++)floatLabel(ctx,i===5?'now':('-'+((windowMs/1000)*(1-i/5)).toFixed(0)+'s'),Math.max(19,Math.min(width-19,w*i/5)),height-9);
    ctx.save();ctx.beginPath();ctx.rect(left-3,top-3,w+6,h+6);ctx.clip();
    model.traces.forEach((trace,index)=>{
      const {segments,min,max}=trace,color=slotColors[trace.slot];if(min===null || !segments.length)return;
      const x=p=>left+w*(1-(end-p.time)/windowMs),y=p=>top+h*(max-p.value)/(max-min);
      ctx.strokeStyle=color;ctx.lineWidth=2;ctx.lineJoin='round';ctx.beginPath();segments.forEach(segment=>segment.forEach((p,i)=>{if(i)ctx.lineTo(x(p),y(p));else ctx.moveTo(x(p),y(p));}));ctx.stroke();
      ctx.fillStyle=color;segments.forEach(segment=>segment.forEach(p=>{ctx.beginPath();ctx.arc(x(p),y(p),trace.visible.length<100?2.8:1.5,0,Math.PI*2);ctx.fill();}));
    });ctx.restore();
    model.traces.forEach((trace,index)=>{
      const numeric=trace.segments.some(segment=>segment.length),label=trace.min===null?'No numeric samples':formatRange(trace.min)+' to '+formatRange(trace.max)+' '+(trace.unit || 'unitless');
      state.views[index].range.textContent=(numeric?'':'No numeric trace · ')+label+' · '+trace.visible.length+' received samples in view';
    });
    $('scope-empty').hidden=model.traces.some(trace=>trace.segments.length);
    setText('scope-unit',!model.scales.length?'· WAITING FOR NUMERIC SAMPLES':mixed?'· INDEPENDENT UNIT SCALES':'· SHARED '+(model.scales[0].unit || 'UNITLESS')+' SCALE');
    setText('scope-range',!model.scales.length?'No numeric samples in this window':mixed?'Same time window · each unit uses its labeled color scale':'Same time window · equal units share one combined numeric scale');
    canvas.setAttribute('aria-label','Overlaid measurement traces. '+state.views.map((ui,index)=>'Meter '+(ui.slot+1)+', channel '+(state.channels[ui.slot]+1)+', '+ui.laneValue.textContent+' '+ui.laneUnit.textContent+', '+ui.laneStatus.textContent+', '+ui.range.textContent).join('. '));
  }
  function floatLabel(ctx,text,x,y,maxWidth){
    const width=Math.min(ctx.measureText(text).width,maxWidth || Infinity),left=ctx.textAlign==='right'?x-width:ctx.textAlign==='center'?x-width/2:x,color=ctx.fillStyle;
    ctx.fillStyle='rgba(10,19,25,.72)';ctx.fillRect(left-3,y-10,width+6,14);ctx.fillStyle=color;ctx.fillText(text,x,y,maxWidth);
  }
  function paintScope(canvas,empty,range,trace,end,windowMs,color) {
    const rect=canvas.getBoundingClientRect();if(rect.width<1 || rect.height<1)return;
    const ratio=Math.min(window.devicePixelRatio || 1,2);canvas.width=Math.round(rect.width*ratio);canvas.height=Math.round(rect.height*ratio);
    const ctx=canvas.getContext('2d');if(!ctx)return;ctx.scale(ratio,ratio);
    const full=$('instrument').classList.contains('chart-surface'),width=rect.width,height=rect.height,left=full?0:61,right=full?0:17,top=full?0:23,bottom=full?0:30,w=width-left-right,h=height-top-bottom;
    ctx.clearRect(0,0,width,height);ctx.font='500 10px MeterDigits,ui-monospace,Consolas,monospace';ctx.lineWidth=1;ctx.strokeStyle='#263b45';ctx.fillStyle='#809da9';
    for(let i=0;i<=5;i++){const x=full?Math.min(width-.5,Math.round(w*i/5)+.5):left+w*i/5;ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,top+h);ctx.stroke();}
    for(let i=0;i<=4;i++){const y=full?Math.min(height-.5,Math.round(h*i/4)+.5):top+h*i/4;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(left+w,y);ctx.stroke();}
    const plot=C.scopePlot(trace?trace.points:[],end,windowMs),{visible,segments,min,max}=plot;canvas.dataset.end=String(end);
    empty.hidden=min!==null;
    if(min===null) {range.textContent='Auto scale · no numeric samples in this window';return;}
    const format=n => Math.abs(n)>=1e6 || (Math.abs(n)>0 && Math.abs(n)<.001)?n.toExponential(2):Number(n.toPrecision(5)).toString();
    ctx.textAlign='right';for(let i=0;i<=4;i++)floatLabel(ctx,format(max-(max-min)*i/4),full?57:left-8,full?Math.max(15,Math.min(height-22,h*i/4+3)):top+h*i/4+3);
    ctx.textAlign='center';for(let i=0;i<=5;i++)floatLabel(ctx,i===5?'now':('-'+((windowMs/1000)*(1-i/5)).toFixed(0)+'s'),full?Math.max(19,Math.min(width-19,w*i/5)):left+w*i/5,height-9);
    ctx.strokeStyle=color;ctx.lineWidth=2;ctx.lineJoin='round';ctx.beginPath();
    segments.forEach(segment=>segment.forEach((p,index)=>{const x=left+w*(1-(end-p.time)/windowMs),y=top+h*(max-p.value)/(max-min);if(index)ctx.lineTo(x,y);else ctx.moveTo(x,y);}));ctx.stroke();
    ctx.fillStyle=color;segments.forEach(segment=>segment.forEach(p=>{const x=left+w*(1-(end-p.time)/windowMs),y=top+h*(max-p.value)/(max-min);ctx.beginPath();ctx.arc(x,y,visible.length<100?2.4:1.5,0,Math.PI*2);ctx.fill();}));
    range.textContent=format(min)+' to '+format(max)+' '+(trace.unit || '')+' · '+visible.length+' received samples in view';
  }
  function renderRecording() {
    const r=state.status && state.status.recording || {}, active=!!r.active;
    setText('recording-pill',active?'Recording':'Idle');$('recording-pill').className='pill '+(active?'recording':'');
    setText('recording-title',active?'Recording: '+(r.label || r.id):'Keep a measurement session');
    const card=state.status && state.status.storage || {},cardReady=tfStorageReady();
    const cardBusy=card.supported&&(card.recording||Number(card.queueDepth)>0||Number(card.bufferedRecords)>0||card.formatting||card.formatResult==='queued'||card.formatResult==='running');
    $('record-start').disabled=active || (!state.db&&!cardReady) || !!card.remounting || !!cardBusy || !state.status;$('record-stop').disabled=!active;$('record-mark').disabled=!active;
  }
  function tfStorageReady() {const card=state.status && state.status.storage;return !!(card && card.supported===true && card.mounted===true && !card.remounting && !card.formatting && card.formatResult!=='queued' && card.formatResult!=='running' && !card.error);}
  function renderStorage() {
    const samples=state.saved.reduce((sum,s)=>sum+(s.sampleCount || 0),0);
    const status=$('storage-state');status.className='storage-status '+(state.dbError?'bad':state.db?'':'pending');
    status.textContent=state.dbError?'Browser storage error: '+state.dbError+' Browser recording data is not confirmed saved. '+(tfStorageReady()?'TF-card storage is independent; check Device TF-card files for its write status. The live feed continues. ':'Keep this page open and retry before the device RAM backlog expires.'):state.db?'IndexedDB ready · '+samples.toLocaleString()+' samples committed · '+state.saved.length+' local sessions':'Opening browser IndexedDB…';
  }
  async function openStorage() {
    const generation=++state.storageGeneration;state.feedGeneration++;
    if(state.db){state.db.db.close();state.db=null;}
    let store;
    try {
      store=await C.openDatabase(window.indexedDB);
      // Publish the new store together with its real durable cursor. A TF-only
      // live cursor may have advanced while opening/reading IndexedDB.
      for(;;){
        const device=state.deviceId,boot=state.feedBoot,after=device && boot?await store.cursor(device,boot):0;
        if(generation!==state.storageGeneration){store.db.close();return;}
        if(device!==state.deviceId || boot!==state.feedBoot)continue;
        state.feedGeneration++;state.after=after;state.db=store;break;
      }
      store.db.onversionchange=() => {store.db.close();if(state.db!==store)return;state.storageGeneration++;state.feedGeneration++;state.db=null;state.dbError='Another tab changed storage. Reload this dashboard.';renderStorage();renderRecording();};
      state.dbError='';await refreshSessions();renderRecording();
    }
    catch(error){if(store)store.db.close();if(generation!==state.storageGeneration)return;state.db=null;state.dbError=errorText(error);renderStorage();renderRecording();}
  }
  function dateText(timestamp) {return typeof timestamp==='number' && timestamp>0?new Date(timestamp).toLocaleString():'Clock not anchored';}
  async function refreshSessions() {
    if(!state.db){renderStorage();$('sessions-list').replaceChildren(node('div','empty-state','Browser storage is unavailable. Retry storage to inspect local sessions.'));return;}
    try {state.saved=await state.db.listSessions();state.lastSavedRefresh=Date.now();renderStorage();renderSessions();}
    catch(error){state.dbError=errorText(error);renderStorage();}
  }
  function download(name,type,text) {
    const url=URL.createObjectURL(new Blob([text],{type})), link=node('a');link.href=url;link.download=name;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  async function exportSession(session,format) {
    try {const data=await state.db.readSession(session.id),name=(session.label || session.sessionId).replace(/[^a-zA-Z0-9_.-]+/g,'_').slice(0,80) || 'meter-session';
      if(format==='csv')download(name+'.csv','text/csv;charset=utf-8','\ufeff'+C.framesCsv(data.frames));
      else download(name+'.json','application/json;charset=utf-8',JSON.stringify({format:'meter-workbench-session',version:1,exportedAt:new Date().toISOString(),...data},null,2));
    }catch(error){notice('Export failed: '+errorText(error));}
  }
  function renderSessions() {
    const list=$('sessions-list');list.replaceChildren();
    state.saved.forEach(session => {
      const row=node('article','session-card');row.append(node('h3','',session.label || session.sessionId),node('p','hint',dateText(session.startedAt)+' · '+session.deviceId));
      if(session.incompleteTail)row.append(node('p','hint bad-text','Incomplete recording: its stop or final samples may have been evicted from the device backlog. See JSON gap metadata.'));
      const stats=node('div','stats');[['sampleCount','samples'],['eventCount','events'],['gapCount','gaps']].forEach(([field,label]) => {const item=node('div');item.append(node('strong','',session[field] || 0),node('span','',label));stats.append(item);});row.append(stats);
      row.append(node('p','hint','Session ID: '+session.sessionId));
      const controls=node('div','button-row');controls.append(button('Export CSV','secondary',()=>exportSession(session,'csv')),button('Export JSON','secondary',()=>exportSession(session,'json')),button('Delete local','ghost',()=>{state.deleteSession=session.id;renderSessions();}));row.append(controls);
      if(state.deleteSession===session.id){const confirm=node('div','delete-warning');confirm.append(node('p','hint','Permanently delete this session from this browser?'));const actions=node('div','button-row');actions.append(button('Delete session','danger',async()=>{try{await state.db.deleteSession(session.id);state.deleteSession='';await refreshSessions();}catch(error){notice('Delete failed: '+errorText(error));}}),button('Cancel','ghost',()=>{state.deleteSession='';renderSessions();}));confirm.append(actions);row.append(confirm);}
      list.append(row);
    });
    if(!state.saved.length)list.append(node('div','empty-state','No saved sessions yet. Start a recording from Live; committed samples will appear here.'));
  }
  function addEvent(boot,frame) {
    const key=boot+':'+frame.seq;if(state.eventKeys.has(key))return;state.eventKeys.add(key);state.events.unshift(frame);if(state.events.length>40)state.events.pop();
    if(state.eventKeys.size>400)state.eventKeys=new Set(state.events.map(e=>boot+':'+e.seq));
    const list=$('event-list');list.replaceChildren();state.events.forEach(e=>list.append(node('li',e.success===false?'error':'',dateText(e.timestampMs)+' · '+(e.event || e.kind)+' · '+(e.message || '')+(e.code?' · HTTP '+e.code:''))));setText('event-count',state.events.length);
  }
  async function anchorClock(boot) {
    if(validNtpClock(state.status && state.status.clock))return;
    if(state.anchoredBoot===boot)return;
    try{await api('/api/time','POST',{unixMs:Date.now()});state.anchoredBoot=boot;}catch(error){notice('Timestamp sync failed: '+errorText(error));}
  }
  async function refreshStatus() {
    const status=await api('/api/status'),previous=state.status;
    if(previous){
      for(let slot=0;slot<C.MAX_METERS;slot++){
        const before=(previous.meters || []).find(m=>Number(m.slot)===slot),after=(status.meters || []).find(m=>Number(m.slot)===slot);
        if((before && before.address)!==(after && after.address) || (before && before.profile)!==(after && after.profile) || (previous.device && previous.device.bootId)!==(status.device && status.device.bootId)){
          for(const key of scope.series.keys())if(key.startsWith(slot+':'))scope.series.delete(key);
          for(const key of state.live.keys())if(key.startsWith(slot+':'))state.live.delete(key);
        }
      }
    }
    state.status=status;state.statusAt=Date.now();state.deviceId=String(status.device && status.device.id || 'unknown-device');ntpUi.offline=false;
    emit('status',{status:JSON.parse(JSON.stringify(status)),receivedAt:state.statusAt});
    setText('api-state','Device online');$('api-state').className='pill good';
    renderCards();renderDiscovery();renderFocus();renderRecording();renderStorage();renderNetwork();renderRuleHealth();drawScope();
    const boot=String(status.device && status.device.bootId || '');if(boot)anchorClock(boot);
    return status;
  }
  async function statusLoop() {try{await refreshStatus();}catch(error){emit('offline',{});setText('api-state','Connection lost');$('api-state').className='pill bad';setText('feed-state','Device unreachable: '+errorText(error)+' The browser can recover only frames still held in the device RAM backlog.');ntpUi.offline=true;renderDateTime();}finally{setTimeout(statusLoop,2000);}}
  async function frameLoop() {
    let delay=500,writingStorage=false,context=null,next=0,frames=[];
    const current=()=>context && context.generation===state.feedGeneration && context.deviceId===state.deviceId && context.db===state.db;
    try {
      if(!state.status || !state.deviceId)return;
      context={generation:state.feedGeneration,deviceId:state.deviceId,db:state.db,after:state.after};
      const reply=await api('/api/frames?after='+context.after+'&limit=64'), boot=String(reply.bootId || '');
      if(!current()){delay=0;return;}
      if(boot!==state.feedBoot || context.deviceId!==state.feedDevice){const hadBoot=!!state.feedBoot,after=context.db?await context.db.cursor(context.deviceId,boot):0;if(!current()){delay=0;return;}state.feedBoot=boot;state.feedDevice=context.deviceId;state.after=after;scope.clear();state.live.clear();emit('frames',{deviceId:context.deviceId,bootId:boot,frames:[],gap:hadBoot,receivedAt:Date.now()});if(hadBoot)notice('The device restarted. A new sequence has begun; previous committed sessions remain in this browser.');delay=0;return;}
      frames=Array.isArray(reply.frames)?reply.frames:[];
      emit('frames',{deviceId:state.deviceId,bootId:boot,frames:frames.map(f=>({...f})),gap:!!reply.gap,receivedAt:Date.now()});
      if(reply.gap)scope.markGap();
      frames.forEach(f=>{if(f.kind==='reading'){
        const key=f.meter+':'+f.channel,meter=meterAt(Number(f.meter));
        if(C.matchesMeterFrame(state.status,meter,f,boot))scope.push(boot,f);
        if(C.liveFrameEligible(state.status,meter,f,boot,state.live.get(key),state.statusAt,Date.now()))state.live.set(key,{...f,bootId:boot});
      }else addEvent(boot,f);});
      renderFocus();drawScope();
      if(reply.gap){const key=boot+':'+state.after+':'+reply.oldestSeq;if(!state.gaps.has(key)){state.gaps.add(key);notice('Recording gap: frames before sequence '+reply.oldestSeq+' are no longer in device RAM. Missing samples cannot be recovered.');}setText('feed-state','Gap detected · requested after '+state.after+' · oldest available '+reply.oldestSeq+' · newest '+reply.latestSeq);}
      else setText('feed-state','Feed '+boot+' · through sequence '+reply.nextSeq+' · RAM window '+reply.oldestSeq+'–'+reply.latestSeq+' · '+state.gaps.size+' gap boundaries detected in this visit.');
      next=Number.isFinite(reply.nextSeq)?reply.nextSeq:context.after;
      if(context.db){writingStorage=true;await context.db.commitBatch({deviceId:context.deviceId,bootId:boot,after:context.after,nextSeq:next,oldestSeq:reply.oldestSeq,gap:!!reply.gap,frames,recording:state.status.recording});writingStorage=false;if(!current()){delay=0;return;}state.after=next;state.dbError='';if(Date.now()-state.lastSavedRefresh>1000)await refreshSessions();}
      else if(tfStorageReady() || !frames.some(f=>f.sessionId))state.after=next;
      else {state.dbError=state.dbError || 'Recording frames are waiting for browser storage.';renderStorage();}
      if(frames.length>=64 && (state.db || tfStorageReady()))delay=25;
    }catch(error){
      if(!current()){delay=0;return;}
      if(writingStorage){
        state.dbError=errorText(error);
        if(tfStorageReady()){
          // Degrade to live-only until Retry storage restores the durable
          // cursor. Never commit a later page past this failed transaction.
          context.db.db.close();state.db=null;state.feedGeneration++;state.after=next;
          renderStorage();renderRecording();setText('feed-state','Live TF-backed feed continues. Browser recovery is paused: '+errorText(error)+' Retry browser storage to replay its uncommitted RAM backlog.');
          if(frames.length>=64)delay=25;return;
        }
        renderStorage();
      }
      setText('feed-state','Frame recovery paused: '+errorText(error)+' The sequence cursor has not advanced beyond the last completed storage transaction.');
    }
    finally{setTimeout(frameLoop,delay);}
  }
  async function recording(actionName) {
    const label=$('session-label').value.trim();
    try {await api('/api/recording','POST',{action:actionName,label});await refreshStatus();notice(actionName==='start'?(tfStorageReady()?'Device recording started. TF-card logging continues with this tab closed; check card write status. '+(state.db && !state.dbError?'Browser saved counts update only after browser storage commits.':'Browser storage is unavailable; its recording copy is not confirmed saved. Retry browser storage before the RAM backlog expires.'):'Device recording started. Browser saved counts update only after browser storage commits.'):actionName==='stop'?'Device recording stopped. TF-card files and browser sessions are independent; check card write status and wait for remaining browser frames to commit.':'Marker sent to the device.',true);}
    catch(error){notice('Recording action failed: '+errorText(error));}
  }
  function renderScreenRotation(){const display=state.status&&state.status.display||{},supported=Number.isInteger(display.rotation)&&display.rotation>=0&&display.rotation<4;const select=$('screen-rotation');select.disabled=!supported||rotationBusy;if(supported&&!rotationBusy)select.value=String(display.rotation);$('screen-rotation-state').textContent=!supported?'Rotation controls unavailable on this firmware.':display.rotationSaved?'Orientation saved.':'Orientation is active but not yet saved.';}
  function renderNetwork() {
    renderScreenRotation();
    const status=state.status || {}, n=status.network || {},d=status.device || {};
    const display=status.display || {},supported=typeof display.autoDim==='boolean';
    $('auto-dim').disabled=!supported;$('auto-dim').setAttribute('aria-pressed',String(display.autoDim===true));
    setText('auto-dim',supported?'Auto dim: '+(display.autoDim?'On':'Off'):'Auto dim: unavailable');
    if(supported)setText('auto-dim-state',(display.autoDim?'After 2 minutes idle: 10%. Touch once to wake; again to use controls.':'Screen stays at normal brightness.')+' '+(display.dimmed?'Screen is dimmed. ':'')+(display.autoDimSaved===false?'Setting is in RAM; saving failed.':'Setting saved on device.')+' Meter connections and logging continue.');
    $('touch-calibrate').hidden=!(status.capabilities && status.capabilities.touchCalibration===true);
    setText('ap-ssid',n.apSsid || 'Access point');setText('ap-address',n.apIp || '—');setText('sta-state',n.connected?'Connected'+(n.staSsid?' to '+n.staSsid:''):n.state==='joining'?'Joining '+(n.staSsid || ''):n.state==='failed'?'Join failed':n.state==='reconnecting'?'Reconnecting':'Not connected');setText('sta-address',n.staIp || '—');setText('wifi-connection-state',n.message || '');setText('device-id',d.id || '—');setText('boot-id',d.bootId || '—');
    const s=Math.floor((d.uptimeMs || 0)/1000);setText('device-uptime',Math.floor(s/3600)+'h '+Math.floor(s/60)%60+'m '+s%60+'s');
    renderDateTime();
  }
  function validClockTime(value){return typeof value==='number'&&Number.isFinite(value)&&value>=1577836800000&&value<=4102444800000;}
  function validNtpClock(clock){return !!clock&&clock.source==='ntp'&&clock.synced===true&&validClockTime(clock.unixMs);}
  function ntpServerValid(server){
    if(typeof server!=='string'||!server.length||server.length>253)return false;
    if(/^[0-9.]+$/.test(server)){
      const octets=server.split('.');return octets.length===4&&octets.every(o=>/^(0|[1-9][0-9]{0,2})$/.test(o)&&Number(o)<=255)&&Number(octets[0])>0&&Number(octets[0])!==127&&Number(octets[0])<224;
    }
    return server.split('.').every(label=>label.length<=63&&/^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/i.test(label));
  }
  function ntpDraftServer(){return $('ntp-preset').value==='custom'?$('ntp-server').value:$('ntp-preset').value;}
  function editNtp(){ntpUi.dirty=true;ntpUi.message='Changes are not saved. Press Save settings to apply.';ntpUi.bad=false;renderDateTime();}
  function renderDateTime(){
    const status=state.status || {},ntp=status.ntp,clock=status.clock;
    const supported=!!ntp&&typeof ntp.enabled==='boolean'&&typeof ntp.server==='string'&&typeof ntp.ready==='boolean',online=!!state.status&&!ntpUi.offline;
    const usable=supported&&ntp.ready===true&&online,blocked=!usable||ntpUi.busy||ntp.pending===true;
    if(supported&&ntpUi.awaiting&&!ntp.pending&&ntp.server===ntpUi.awaiting.server&&ntp.enabled===ntpUi.awaiting.enabled){
      if(ntp.saved===true){ntpUi.message='NTP settings saved on device. Time sync is reported separately.';ntpUi.bad=false;ntpUi.awaiting=null;}
      else if(ntp.error){ntpUi.message='NTP settings applied in RAM; saving was not confirmed. '+ntp.error;ntpUi.bad=true;ntpUi.awaiting=null;}
    }
    if(supported&&ntpUi.syncAfter!==null&&!ntp.pending){
      const signature=String(ntp.lastSyncUnixMs)+':'+String(ntp.lastSyncUptimeMs);
      if(ntp.synced===true&&validClockTime(ntp.lastSyncUnixMs)&&signature!==ntpUi.syncAfter){ntpUi.message='NTP response accepted by device.';ntpUi.bad=false;ntpUi.syncAfter=null;}
      else if(ntp.error){ntpUi.message='NTP sync failed: '+ntp.error;ntpUi.bad=true;ntpUi.syncAfter=null;}
    }
    if(usable&&!ntpUi.dirty&&!ntpUi.busy&&!ntpUi.awaiting){
      const signature=JSON.stringify([ntp.enabled,ntp.server]);
      if(signature!==ntpUi.loaded){$('ntp-enabled').value=String(ntp.enabled);$('ntp-preset').value=['pool.ntp.org','time.cloudflare.com','time.google.com'].includes(ntp.server)?ntp.server:'custom';$('ntp-server').value=ntp.server;ntpUi.loaded=signature;}
    }
    const custom=$('ntp-preset').value==='custom';$('ntp-custom-label').hidden=!custom;$('ntp-server').required=custom;
    for(const id of ['ntp-enabled','ntp-preset','ntp-save'])$(id).disabled=blocked;
    $('ntp-server').disabled=blocked||!custom;
    $('ntp-sync').disabled=blocked||!ntp.enabled||ntpUi.dirty||!!ntpUi.awaiting;
    $('sync-clock').disabled=!online||ntpUi.browserBusy;
    let source='Unavailable on this firmware',time='—';
    if(clock&&['none','browser','ntp'].includes(clock.source)){
      source=clock.synced===true&&validClockTime(clock.unixMs)?clock.source==='ntp'?'NTP':clock.source==='browser'?'Browser':'Not synchronized':'Not synchronized';
      if(source==='NTP'||source==='Browser')time=new Date(clock.unixMs).toISOString();
    }
    setText('ntp-clock-source',(ntpUi.offline?'Last reported: ':'')+source);setText('ntp-clock-time',time);
    let description;
    if(!online)description=state.status?'Device unreachable. NTP status and clock above are from the last successful status response.':'Connecting to device…';
    else if(!supported)description='NTP settings are unavailable on this firmware. Browser clock synchronization remains available.';
    else if(!ntp.ready)description='Loading saved NTP settings…';
    else{
      description=ntp.pending?'NTP request pending on device.':!ntp.enabled?'NTP is disabled.':ntp.waitingForNetwork?'Waiting for network. Join station Wi-Fi, or use a numeric NTP server on this access point\'s subnet.':ntp.syncing?'Waiting for NTP response…':ntp.active?'NTP is active.':'NTP service is inactive.';
      description+=' '+(ntp.saved===true?'Current settings saved on device.':'Current settings are not saved on device.');
      if(ntp.error)description+=' '+ntp.error;
    }
    setText('ntp-status',description);$('ntp-status').classList.toggle('bad-text',!online||!!(supported&&ntp.error));
    setText('ntp-command',ntpUi.message);$('ntp-command').classList.toggle('bad-text',ntpUi.bad);
    setText('ntp-last-sync',supported&&ntp.synced===true&&validClockTime(ntp.lastSyncUnixMs)?'Last successful NTP: '+new Date(ntp.lastSyncUnixMs).toISOString()+' · '+(ntp.lastSyncServer || 'Server not reported')+'.':'No successful NTP sync reported.');
  }
  async function ntpRequest(sync){
    if(ntpUi.busy)return;
    const server=ntpDraftServer(),enabled=$('ntp-enabled').value==='true';
    if(!sync&&!ntpServerValid(server)){ntpUi.message='Enter a DNS hostname or unicast IPv4 address without a URL, port or spaces.';ntpUi.bad=true;renderDateTime();return;}
    ntpUi.busy=true;ntpUi.bad=false;ntpUi.message=sync?'Sending NTP sync request…':'Sending NTP settings…';renderDateTime();
    try{
      const reply=await api('/api/ntp','POST',sync?{action:'sync'}:{enabled,server});
      if(reply.accepted!==true)throw new Error('Device did not confirm NTP request acceptance.');
      if(sync){const ntp=state.status.ntp;ntpUi.syncAfter=String(ntp.lastSyncUnixMs)+':'+String(ntp.lastSyncUptimeMs);ntpUi.message='Sync request accepted. Waiting for an NTP response.';}
      else {ntpUi.dirty=false;ntpUi.awaiting={enabled,server};ntpUi.message='Settings request accepted. Waiting for device persistence confirmation.';}
      try{await refreshStatus();}catch(error){ntpUi.offline=true;ntpUi.message+=' Status unavailable: '+errorText(error);}
    }catch(error){ntpUi.message='NTP request failed: '+errorText(error);ntpUi.bad=true;}
    finally{ntpUi.busy=false;renderDateTime();}
  }
  function renderWifiScan(data) {
    const busy=data.state==='queued'||data.state==='scanning';$('wifi-scan').disabled=busy;setText('wifi-scan',busy?'Scanning…':'Scan Wi-Fi');
    setText('wifi-scan-state',data.message || (busy?'Scanning nearby networks…':data.state==='complete'?'Select a network below.':'Scan nearby 2.4 GHz networks, or enter a name manually.'));
    const list=$('wifi-networks');list.replaceChildren();
    (Array.isArray(data.networks)?data.networks:[]).forEach(network=>{
      if(typeof network.ssid!=='string'||!network.ssid)return;
      const row=button('','secondary wifi-network',()=>{
        selectedWifi={ssid:network.ssid,security:network.security};$('wifi-ssid').value=network.ssid;$('wifi-password').value='';
        const open=network.security==='Open';$('wifi-password').required=!open;$('wifi-password').minLength=open?0:8;$('wifi-password').disabled=open;
        list.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b===row)));
        setText('wifi-state',open?'Open network selected. Press Join network to connect.':'Network selected. Enter its password, then press Join network.');
        (open?$('wifi-join'):$('wifi-password')).focus();
      });
      row.disabled=network.joinable!==true;row.setAttribute('aria-pressed',String(!!selectedWifi&&selectedWifi.ssid===network.ssid&&selectedWifi.security===network.security));
      row.append(node('span','wifi-network-name',network.ssid),node('span','wifi-network-detail',network.security+' · '+network.rssi+' dBm · channel '+network.channel+(network.joinable===true?'':' · not supported')));list.append(row);
    });
    if(data.state==='complete'&&!list.childElementCount)list.append(node('p','hint','No named networks found. Retry or enter a hidden network name manually.'));
    return busy;
  }
  async function pollWifiScan() {
    if(wifiPollBusy)return;wifiPollBusy=true;clearTimeout(wifiScanTimer);
    try{const scanning=renderWifiScan(await api('/api/wifi/scan'));if(scanning)wifiScanTimer=setTimeout(pollWifiScan,700);}
    catch(error){$('wifi-scan').disabled=false;setText('wifi-scan','Scan Wi-Fi');setText('wifi-scan-state','Scan unavailable: '+errorText(error));}
    finally{wifiPollBusy=false;}
  }
  async function scanWifi() {
    clearTimeout(wifiScanTimer);$('wifi-scan').disabled=true;setText('wifi-scan','Scanning…');setText('wifi-scan-state','Starting Wi-Fi scan…');
    try{await api('/api/wifi/scan','POST',{});await pollWifiScan();}
    catch(error){$('wifi-scan').disabled=false;setText('wifi-scan','Scan Wi-Fi');setText('wifi-scan-state','Scan failed: '+errorText(error));}
  }
  function renderRuleHealth() {
    const wh=state.status && state.status.webhook || {};setText('webhook-health','Outbound queue: '+(wh.queued || 0)+' waiting · '+(wh.dropped || 0)+' dropped. No automatic duplicate POST retries.');
    if(state.panel==='triggers' && state.rules.length){const summaries=state.status && state.status.rules || [];state.rules=state.rules.map(rule=>({...rule,...summaries.find(s=>Number(s.id)===Number(rule.id))}));renderRules();}
  }
  async function loadRules() {try{const data=await api('/api/rules');state.rules=Array.isArray(data)?data:data.rules || [];renderRules();}catch(error){notice('Could not load triggers: '+errorText(error));}}
  function renderRules() {
    const list=$('rules-list');list.replaceChildren();
    state.rules.forEach(rule=>{
      const row=node('article','rule-card'),heading=node('div','meter-title');heading.append(node('h3','','Meter '+(Number(rule.meter)+1)+' · Channel '+(Number(rule.channel)+1)),node('span','pill '+(rule.enabled?'good':''),rule.enabled?'Enabled':'Disabled'));row.append(heading);
      row.append(node('p','',rule.operator+' '+rule.threshold+' '+(rule.unit || '')+' · hysteresis '+rule.hysteresis+' · '+(Number(rule.cooldownMs)/1000)+'s cooldown'),node('p','rule-destination',(rule.method || 'POST')+' '+(rule.url || '')));
      const outcome=String(rule.lastResult || '').toLowerCase(), benign=['','idle','ok','success','never','queued','delivered'].includes(outcome), successfulCode=Number(rule.lastCode)>=200 && Number(rule.lastCode)<300;
      const delivery=node('p','delivery'+(!benign && !successfulCode?' error':''));delivery.textContent=(rule.lastResult || 'No delivery yet')+(rule.lastCode?' · HTTP '+rule.lastCode:'')+' · fired '+(rule.fireCount || 0)+' · deliveries attempted '+(rule.deliveryCount || 0)+(rule.armed===false?' · waiting to re-arm':'');row.append(delivery);
      const controls=node('div','button-row');controls.append(button('Edit','secondary',()=>editRule(rule)),button('Send test','ghost',async()=>{try{await api('/api/rules/test','POST',{id:rule.id});notice('Test request queued for trigger '+rule.id+'. Check delivery status.',true);await loadRules();}catch(error){notice('Test failed: '+errorText(error));}}),button(state.deleteRule===rule.id?'Confirm delete':'Delete','danger',async()=>{if(state.deleteRule!==rule.id){state.deleteRule=rule.id;renderRules();return;}try{await api('/api/rules?id='+encodeURIComponent(rule.id),'DELETE');state.deleteRule=null;await loadRules();}catch(error){notice('Delete failed: '+errorText(error));}}));row.append(controls);list.append(row);
    });
    if(!list.childElementCount)list.append(node('div','empty-state','No triggers configured. Create one for an exact meter, channel and unit.'));
  }
  function editRule(rule) {
    const values={id:rule.id,meter:rule.meter,channel:Number(rule.channel)+1,unit:rule.unit,operator:rule.operator,threshold:rule.threshold,hysteresis:rule.hysteresis,cooldown:Number(rule.cooldownMs)/1000,method:rule.method || 'POST',url:rule.url,headers:JSON.stringify(rule.headers || {},null,2),body:rule.body || '',ca:rule.caPem || ''};
    Object.keys(values).forEach(key=>{$('rule-'+key).value=values[key] == null?'':values[key];});$('rule-enabled').checked=!!rule.enabled;setText('rule-form-title','Edit trigger '+rule.id);setText('rule-form-status','');$('rule-form').scrollIntoView({behavior:'smooth',block:'start'});
  }
  function resetRule() {$('rule-form').reset();$('rule-id').value='';setText('rule-form-title','New trigger');setText('rule-form-status','');}
  async function saveRule(event) {
    event.preventDefault();
    try {
      const headers=JSON.parse($('rule-headers').value || '{}');if(!headers || typeof headers!=='object' || Array.isArray(headers) || Object.values(headers).some(v=>typeof v!=='string'))throw new Error('Headers must be a JSON object with string values.');
      const url=new URL($('rule-url').value);if(!['http:','https:'].includes(url.protocol))throw new Error('Target URL must use http:// or https://.');
      const rule={enabled:$('rule-enabled').checked,meter:Number($('rule-meter').value),channel:Number($('rule-channel').value)-1,unit:$('rule-unit').value,operator:$('rule-operator').value,threshold:Number($('rule-threshold').value),hysteresis:Number($('rule-hysteresis').value),cooldownMs:Math.round(Number($('rule-cooldown').value)*1000),method:$('rule-method').value,url:url.href,headers,body:$('rule-body').value,caPem:$('rule-ca').value};
      if(!C.validMeterSlot(rule.meter)||!C.validChannel(rule.channel))throw new Error('Choose meter 1 to 4 and channel 1 to 8.');
      if($('rule-id').value!=='')rule.id=Number($('rule-id').value);
      const result=await api('/api/rules','POST',rule);setText('rule-form-status','Trigger '+result.id+' saved.');$('rule-id').value=result.id;setText('rule-form-title','Edit trigger '+result.id);await loadRules();
    }catch(error){setText('rule-form-status',errorText(error));notice('Could not save trigger: '+errorText(error));}
  }
  async function storageInfo() {
    const storage=navigator.storage;
    $('persist-storage').hidden=!storage || typeof storage.persist!=='function';
    if(!storage)return;
    if(typeof storage.estimate==='function')try{const e=await storage.estimate();setText('session-storage-info','Local to this browser and dashboard address · '+((e.usage || 0)/1048576).toFixed(1)+' MB used'+(e.quota?' of approximately '+(e.quota/1048576).toFixed(0)+' MB':'')+'. Export sessions to keep a separate copy. Clearing site data removes recordings.');}catch(_){}
  }
  function showPanel(name) {
    if(!['live','calculations','sessions','triggers','network'].includes(name))name='live';state.panel=name;if(name!=='live'&&layoutPictures.dialog&&layoutPictures.dialog.open)layoutPictures.dialog.close();if(name!=='live')closeValueFormat(false);
    if(name!=='live' && (state.expanded || document.fullscreenElement===$('instrument'))){state.expanded=false;if(document.fullscreenElement===$('instrument'))document.exitFullscreen().catch(()=>{});fullscreenState();}
    document.querySelectorAll('.panel').forEach(p=>{p.hidden=p.id!=='panel-'+name;});document.querySelectorAll('.tab').forEach(t=>{t.classList.toggle('active',t.dataset.panel===name);t.setAttribute('aria-selected',String(t.dataset.panel===name));});
    if(name==='sessions'){refreshSessions();storageInfo();}if(name==='triggers')loadRules();if(name==='network')pollWifiScan();if(name==='live'){renderFocus();requestAnimationFrame(drawScope);}emit('panel',{name});
  }
  document.querySelectorAll('.tab').forEach(t=>t.addEventListener('click',()=>{location.hash=t.dataset.panel;}));
  window.addEventListener('hashchange',()=>showPanel(location.hash.slice(1)));window.addEventListener('resize',()=>{drawScope();renderValueTiles();});
  $('scan-button').addEventListener('click',()=>{$('discovery-panel').hidden=false;action({action:'scan'});});$('close-discovery').addEventListener('click',()=>{$('discovery-panel').hidden=true;});
  $('connect-slot').addEventListener('change',()=>selectMeterSetup(Number($('connect-slot').value)));
  $('discovered-device').addEventListener('change',()=>{selectedDevice=$('discovered-device').value===''?null:C.discoverySnapshot(state.status&&state.status.discovery,Number($('discovered-device').value));renderDiscovery();});
  $('connect-slot').addEventListener('change',renderDiscovery);$('connect-device').addEventListener('click',connectSelectedDevice);
  $('focus-channel').addEventListener('change',()=>changeChannel(state.focus,Number($('focus-channel').value)));
  $('instrument-view').addEventListener('change',()=>{valuePrefs.view=$('instrument-view').value==='scope'?'scope':'values';saveValues();renderFocus();drawScope();V.refreshNumbers($('instrument'));});
  $('value-layout').addEventListener('change',()=>{const preset=$('value-layout').value,count=V.presetCount(preset,state.count);if(!count)return;state.count=count;valuePrefs.presets[count]=preset;saveValues();saveView();renderFocus();drawScope();});
  $('value-format-button').addEventListener('click',openValueFormat);$('value-format-close').addEventListener('click',()=>closeValueFormat());
  for(const id of ['value-format-slot','value-format-channel'])$(id).addEventListener('change',loadValueFormat);
  for(const id of ['value-format-digits','value-format-decimals','value-format-padding'])$(id).addEventListener('input',previewValueFormat);
  $('value-format-save').addEventListener('click',()=>{const {slot,channel,format}=formatSelection();if(!F.validNumberFormat(format)){setText('value-format-result','Invalid format: choose 3..8 digits and fewer decimals.');return;}valuePrefs.formats[slot][channel]={...format};const ok=saveValues();setText('value-format-result',ok?'Saved in this browser. Original data precision is unchanged.':'Applied for this session; browser storage could not save it.');renderCards();renderFocus();});
  $('value-format-device').addEventListener('click',applyValuesToDevice);
  document.addEventListener('keydown',e=>{if($('value-format-panel').hidden)return;if(e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();closeValueFormat();return;}if(e.key!=='Tab')return;e.preventDefault();e.stopImmediatePropagation();const controls=Array.from($('value-format-panel').querySelectorAll('button,input,select')).filter(el=>!el.disabled&&el.getClientRects().length);const i=controls.indexOf(document.activeElement),next=i<0?0:(i+(e.shiftKey?-1:1)+controls.length)%controls.length;if(controls[next])controls[next].focus();},true);
  $('display-count').addEventListener('change',()=>{const count=Number($('display-count').value);state.count=[1,2,3,4].includes(count)?count:1;saveView();renderFocus();drawScope();});
  $('chart-layout').addEventListener('change',()=>{state.layout=$('chart-layout').value==='separate'?'separate':'overlay';saveView();renderFocus();drawScope();});
  $('scope-window').addEventListener('change',drawScope);$('clear-scope').addEventListener('click',()=>{displayedSlots().forEach(slot=>scope.series.delete(slot+':'+state.channels[slot]));renderFocus();drawScope();});
  function fullscreenState(){
    const expanded=state.panel==='live' && (document.fullscreenElement===$('instrument') || state.expanded),changed=$('fullscreen-button').getAttribute('aria-pressed')!==String(expanded);
    $('instrument').classList.toggle('expanded',state.expanded);document.documentElement.classList.toggle('instrument-expanded',expanded);setText('fullscreen-button',expanded?'Exit expanded':'Expand');$('fullscreen-button').setAttribute('aria-pressed',String(expanded));
    if(changed){const target=expanded || state.panel==='live'?$('fullscreen-button'):document.querySelector('.tab[data-panel="'+state.panel+'"]');if(target)target.focus({preventScroll:true});}
    requestAnimationFrame(()=>{drawScope();renderValueTiles();});
  }
  $('fullscreen-button').addEventListener('click',async()=>{if(document.fullscreenElement){await document.exitFullscreen();return;}if(state.expanded){state.expanded=false;fullscreenState();return;}try{if($('instrument').requestFullscreen)await $('instrument').requestFullscreen();else state.expanded=true;}catch(_){state.expanded=true;}fullscreenState();});document.addEventListener('fullscreenchange',fullscreenState);document.addEventListener('keydown',e=>{
    if(layoutPictures.dialog && layoutPictures.dialog.open){if(e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();layoutPictures.dialog.close();}return;}
    if(e.key==='Escape' && state.expanded){e.preventDefault();state.expanded=false;fullscreenState();return;}
    if(e.key!=='Tab' || $('fullscreen-button').getAttribute('aria-pressed')!=='true')return;
    const controls=Array.from($('instrument').querySelectorAll('button,select,input,textarea,a[href],[tabindex]')).filter(el=>{const r=el.getBoundingClientRect();return !el.matches(':disabled') && el.tabIndex>=0 && getComputedStyle(el).visibility!=='hidden' && r.width>0 && r.height>0 && r.right>0 && r.bottom>0 && r.x<innerWidth && r.y<innerHeight;});
    if(!controls.length)return;e.preventDefault();const index=controls.indexOf(document.activeElement),next=index<0?(e.shiftKey?controls.length-1:0):(index+(e.shiftKey?-1:1)+controls.length)%controls.length;controls[next].focus({preventScroll:true});
  });
  $('recording-form').addEventListener('submit',e=>{e.preventDefault();recording('start');});$('record-stop').addEventListener('click',()=>recording('stop'));$('record-mark').addEventListener('click',()=>recording('mark'));
  $('refresh-sessions').addEventListener('click',refreshSessions);$('retry-storage').addEventListener('click',openStorage);
  $('persist-storage').addEventListener('click',async()=>{const storage=navigator.storage;if(!storage || typeof storage.persist!=='function'){notice('Persistent storage requests are unavailable at this address. IndexedDB recording still works where the browser permits it.');return;}try{notice(await storage.persist()?'Persistent storage granted. Keep exported copies for important sessions.':'The browser did not grant persistent storage. Export sessions to keep a separate copy.',true);}catch(error){notice(errorText(error));}});
  $('rule-form').addEventListener('submit',saveRule);$('new-rule').addEventListener('click',()=>{resetRule();$('rule-form').scrollIntoView({behavior:'smooth'});});$('cancel-rule').addEventListener('click',resetRule);$('rule-from-focus').addEventListener('click',()=>{$('rule-meter').value=state.focus;$('rule-channel').value=state.channel+1;const r=focusedReading();$('rule-unit').value=r && r.unit || '';});
  $('wifi-scan').addEventListener('click',scanWifi);
  $('wifi-ssid').addEventListener('input',()=>{selectedWifi=null;$('wifi-password').required=false;$('wifi-password').minLength=0;$('wifi-password').disabled=false;$('wifi-networks').querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed','false'));});
  $('wifi-form').addEventListener('submit',async e=>{e.preventDefault();$('wifi-join').disabled=true;setText('wifi-state','Sending credentials…');try{await api('/api/wifi','POST',{ssid:$('wifi-ssid').value,password:$('wifi-password').value});$('wifi-password').value='';setText('wifi-state','Credentials saved. Joining Wi-Fi; check station status above.');await refreshStatus();}catch(error){setText('wifi-state','Join request failed: '+errorText(error));}finally{$('wifi-join').disabled=false;}});
  for(const id of ['ntp-enabled','ntp-preset'])$(id).addEventListener('change',editNtp);
  $('ntp-server').addEventListener('input',editNtp);
  $('ntp-form').addEventListener('submit',e=>{e.preventDefault();ntpRequest(false);});$('ntp-sync').addEventListener('click',()=>ntpRequest(true));
  $('sync-clock').addEventListener('click',async()=>{ntpUi.browserBusy=true;renderDateTime();try{const reply=await api('/api/time','POST',{unixMs:Date.now()});notice(reply.source==='ntp'?'Device already uses NTP; browser time was not applied.':'Device timestamps synchronized to this browser clock.',true);await refreshStatus();}catch(error){notice('Clock sync failed: '+errorText(error));}finally{ntpUi.browserBusy=false;renderDateTime();}});
  $('screen-rotation').addEventListener('change',async()=>{const rotation=Number($('screen-rotation').value);rotationBusy=true;renderScreenRotation();try{await api('/api/control','POST',{action:'screen-rotation',rotation});await refreshStatus();}catch(error){notice('Screen rotation failed: '+errorText(error));}finally{rotationBusy=false;renderScreenRotation();}});
  $('auto-dim').addEventListener('click',async()=>{const enabled=!(state.status && state.status.display && state.status.display.autoDim);$('auto-dim').disabled=true;try{await api('/api/control','POST',{action:'auto-dim',enabled});await refreshStatus();notice('Device auto dim '+(enabled?'enabled.':'disabled.'),true);}catch(error){notice('Auto dim failed: '+errorText(error));}finally{renderNetwork();}});
  $('touch-calibrate').addEventListener('click',async()=>{try{await api('/api/control','POST',{action:'touch-calibrate'});notice('Touch calibration started. Follow the four corner targets and centre check on the device.',true);}catch(error){notice('Touch calibration request failed: '+errorText(error));}});

  const layoutPictures={buttons:{},dialog:null,mode:'values',opener:null};
  function layoutPicture(preset,count,mini=false){
    const icon=document.createElement('span');icon.className='layout-preview value-tiles';icon.dataset.preset=preset;icon.dataset.count=String(count);icon.setAttribute('aria-hidden','true');
    for(let i=0;i<count;i++){const pane=document.createElement('span');pane.className='value-tile layout-pane';pane.style.setProperty('--pane-color',slotColors[(state.focus+i)%4]);pane.textContent=mini?'':String(i+1);icon.appendChild(pane);}return icon;
  }
  function chartPicture(overlay,count){
    const icon=document.createElementNS('http://www.w3.org/2000/svg','svg');icon.setAttribute('viewBox','0 0 96 64');icon.setAttribute('aria-hidden','true');icon.classList.add('layout-chart-preview');
    for(let i=0;i<count;i++){const cols=count===4?2:1,rows=count===4?2:count,x=overlay?1:(i%cols)*96/cols+1,y=overlay?1:Math.floor(i/cols)*64/rows+1,w=overlay?94:96/cols-2,h=overlay?62:64/rows-2;
      const box=document.createElementNS(icon.namespaceURI,'rect');for(const [k,v] of Object.entries({x,y,width:w,height:h,rx:2,fill:'#10272f',stroke:'#67808a'}))box.setAttribute(k,String(v));if(!overlay||i===0)icon.appendChild(box);
      const line=document.createElementNS(icon.namespaceURI,'polyline');line.setAttribute('points',Array.from({length:7},(_,p)=>[x+3+p*(w-6)/6,y+3+((p*7+i*3)%11)*(h-6)/11].join(',')).join(' '));line.setAttribute('fill','none');line.setAttribute('stroke',slotColors[(state.focus+i)%4]);line.setAttribute('stroke-width','2');icon.appendChild(line);
    }return icon;
  }
  function pictureChoices(mode){if(mode==='chart')return [{value:'overlay',label:'Together',count:state.count},{value:'separate',label:'Separate',count:state.count}];return ['solo','equal','large-small','large-small-small','large-large-small','grid'].map(value=>{const count=V.presetCount(value,state.count),names={solo:'Solo',equal:'Equal '+count,'large-small':'Main + 1','large-small-small':'Main + 2','large-large-small':'2 big + 1',grid:'Grid 4'};return {value,count,label:names[value]};});}
  function updateLayoutPictures(){for(const [mode,button] of Object.entries(layoutPictures.buttons)){const value=mode==='values'?valuePrefs.presets[state.count]:state.layout,count=mode==='values'?V.presetCount(value,state.count):state.count;button.replaceChildren(mode==='values'?layoutPicture(value,count,true):chartPicture(value==='overlay',count));button.title='Choose '+(mode==='values'?'value':'chart')+' layout';}}
  function openLayoutPictures(mode,opener){
    const d=layoutPictures.dialog;layoutPictures.mode=mode;layoutPictures.opener=opener;d.querySelector('h2').textContent=mode==='values'?'Choose a value layout':'Choose a chart layout';const grid=d.querySelector('.layout-picture-grid');grid.replaceChildren();
    for(const choice of pictureChoices(mode)){const button=document.createElement('button');button.type='button';button.className='layout-picture-choice';button.dataset.layout=choice.value;button.id='layout-choice-'+mode+'-'+choice.value;button.setAttribute('aria-pressed',String(choice.value===(mode==='values'?valuePrefs.presets[state.count]:state.layout)));button.setAttribute('aria-label',choice.label+', '+choice.count+(choice.count===1?' meter':' meters'));button.appendChild(mode==='values'?layoutPicture(choice.value,choice.count):chartPicture(choice.value==='overlay',choice.count));const title=document.createElement('strong');title.textContent=choice.label;const detail=document.createElement('span');detail.textContent=choice.count+(choice.count===1?' meter':' meters');button.append(title,detail);button.addEventListener('click',()=>{const select=$(mode==='values'?'value-layout':'chart-layout');select.value=choice.value;select.dispatchEvent(new Event('change',{bubbles:true}));d.close();});grid.appendChild(button);}
    d.showModal();const selected=grid.querySelector('[aria-pressed="true"]');if(selected){selected.focus();selected.scrollIntoView({block:'nearest'});}
  }
  function installLayoutPictures(){
    const d=document.createElement('dialog');d.id='layout-picture-dialog';d.className='layout-picture-dialog';d.setAttribute('aria-labelledby','layout-picture-title');d.innerHTML='<div class="layout-picture-heading"><h2 id="layout-picture-title">Choose layout</h2><button type="button" id="layout-picture-cancel" class="button secondary">Cancel</button></div><div class="layout-picture-grid"></div>';$('instrument').appendChild(d);layoutPictures.dialog=d;d.querySelector('button').addEventListener('click',()=>d.close());d.addEventListener('close',()=>{if(state.panel==='live'&&layoutPictures.opener&&layoutPictures.opener.getClientRects().length)layoutPictures.opener.focus();});d.addEventListener('click',event=>{const r=d.getBoundingClientRect();if(event.target===d&&(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom))d.close();});
    for(const [mode,id] of [['values','value-layout'],['chart','chart-layout']]){const label=$(id).parentElement,container=document.createElement('div');container.className='layout-picture-control';container.id=label.id;label.removeAttribute('id');if(mode==='values')$(id).hidden=true;label.replaceWith(container);const button=document.createElement('button');button.id=id+'-pictures';button.type='button';button.className='layout-picture-open';button.setAttribute('aria-label','Choose '+(mode==='values'?'value':'chart')+' layout visually');button.setAttribute('aria-haspopup','dialog');button.setAttribute('aria-controls',d.id);button.addEventListener('click',()=>openLayoutPictures(mode,button));container.append(label,button);layoutPictures.buttons[mode]=button;}
    updateLayoutPictures();const toolbar=$('instrument').querySelector('.instrument-toolbar');const place=()=>{const instrument=$('instrument');if(!instrument.getClientRects().length)return;const top=toolbar.getBoundingClientRect().bottom-instrument.getBoundingClientRect().top+8;instrument.style.setProperty('--layout-toolbar-bottom',Math.ceil(top)+'px');};new ResizeObserver(place).observe(toolbar);window.addEventListener('resize',place);place();
  }
  async function init() {
    showPanel(location.hash.slice(1));await openStorage();
    try{const data=await api('/api/profiles');state.profiles=Array.isArray(data)?data:data.profiles || [];}catch(error){notice('Could not load model profiles: '+errorText(error));}
    buildCards();statusLoop();frameLoop();
  }
  installLayoutPictures();init();
  // Age labels continue to advance while network requests fail or are pending.
  // This timer never creates samples or advances the scope.
  setInterval(()=>{if(state.status){renderFocus();drawScope();}},500);
})();
