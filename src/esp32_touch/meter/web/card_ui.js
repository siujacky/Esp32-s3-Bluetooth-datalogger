(function () {
  'use strict';
  const root=document.getElementById('card-storage-root');
  if(!root)return;
  const state={storage:null,offline:false,loading:false,remounting:false,files:[],filesLoaded:false,filesAt:0,filesEpoch:0,statusRevision:0,statusAt:0,identity:'',panel:'live',requestError:'',filesError:'',tried:false,formatConfirm:false,confirmIdentity:'',formatPosting:false,formatWatching:false,formatUnconfirmed:false,formatOperation:null,formatEpoch:0,formatError:'',formatMessage:'',formatOutcome:''};
  const el={};let filesPromise=null;
  const node=(tag,className,value)=>{const item=document.createElement(tag);if(className)item.className=className;if(value!=null)item.textContent=value;return item;};
  const message=error=>error && error.message?error.message:String(error);
  const count=value=>typeof value==='number' && Number.isFinite(value) && value>=0?Math.floor(value):null;
  function bytes(value){
    if(count(value)===null)return 'Unknown';
    const units=['B','KiB','MiB','GiB','TiB'];let unit=0;
    while(value>=1024 && unit<units.length-1){value/=1024;unit++;}
    return (unit?value.toFixed(1):String(value))+' '+units[unit];
  }
  function item(parent,id,label){const wrap=node('div','card-metric');wrap.append(node('dt','',label));const value=node('dd','','—');value.id=id;el[id]=value;wrap.append(value);parent.append(wrap);}
  function button(parent,id,label,handler){const control=node('button','button secondary',label);control.type='button';control.id=id;control.addEventListener('click',handler);el[id]=control;parent.append(control);}
  function build(){
    const section=node('section','surface card-storage');section.setAttribute('aria-labelledby','card-storage-title');
    const heading=node('div','card-heading'),intro=node('div');intro.append(node('p','eyebrow','DEVICE STORAGE'));const title=node('h2','','Device TF-card files');title.id='card-storage-title';intro.append(title);heading.append(intro);
    const badge=node('span','pill pending','Checking card');badge.id='card-storage-state';badge.setAttribute('role','status');el[badge.id]=badge;heading.append(badge);section.append(heading);
    section.append(node('p','card-help','With a mounted card, start a recording on the meter. TF logging continues with this tab closed. Browser recordings still require this page to stay open. Device files below are separate from browser-saved sessions.'));
    const controls=node('div','button-row card-controls');button(controls,'card-refresh','Refresh files',refresh);button(controls,'card-remount','Mount card',remount);button(controls,'card-format-open','Format TF card…',openFormat);el['card-format-open'].className='button danger';section.append(controls);
    const form=node('form','card-format-confirm');form.id='card-format-confirm';form.hidden=true;el[form.id]=form;form.append(node('h3','','Format TF card'));
    const warning=node('p','','Formatting deletes ALL data on the TF card, including files outside Meter Workbench. Download anything you want to keep before continuing.');warning.id='card-format-warning';form.append(warning);
    const label=node('label','','Type FORMAT TF CARD to confirm'),phrase=node('input');phrase.id='card-format-phrase';phrase.type='text';phrase.autocomplete='off';phrase.spellcheck=false;phrase.setAttribute('aria-describedby',warning.id);phrase.addEventListener('input',renderFormat);el[phrase.id]=phrase;label.append(phrase);form.append(label);
    const confirmation=node('div','button-row');button(confirmation,'card-format-submit','Erase all TF data and format',()=>{});el['card-format-submit'].type='submit';el['card-format-submit'].className='button danger';button(confirmation,'card-format-cancel','Cancel',closeFormat);form.append(confirmation);form.addEventListener('submit',event=>{event.preventDefault();formatCard();});section.append(form);
    const formatStatus=node('p','card-message');formatStatus.id='card-format-status';formatStatus.setAttribute('role','status');formatStatus.hidden=true;el[formatStatus.id]=formatStatus;section.append(formatStatus);
    const metrics=node('dl','card-metrics');item(metrics,'card-storage-used','Used / filesystem');item(metrics,'card-storage-size','Card size');item(metrics,'card-storage-written','Records written');item(metrics,'card-storage-dropped','Dropped records');section.append(metrics);
    const active=node('p','card-active-file');active.id='card-storage-active';el[active.id]=active;section.append(active);
    for(const [id,className,role] of [['card-storage-error','card-message card-error','alert'],['card-storage-gaps','card-message card-warning','status']]){const alert=node('p',className);alert.id=id;alert.hidden=true;alert.setAttribute('role',role);el[id]=alert;section.append(alert);}
    const files=node('div','card-files');files.id='card-files';files.setAttribute('aria-label','Files on device TF card');el[files.id]=files;section.append(files);root.append(section);
  }
  function formatPending(s){return !!(s && (s.formatting===true || s.formatResult==='queued' || s.formatResult==='running'));}
  function cardBusy(s){return !!(s && (s.recording===true || Number(s.queueDepth)>0 || Number(s.bufferedRecords)>0 || Number(s.downloads)>0 || s.remounting===true || formatPending(s)));}
  function canFormat(){const s=state.storage;return !!(s && s.supported===true && !state.offline && !state.loading && !state.remounting && !cardBusy(s) && !state.formatPosting && !state.formatWatching && !state.formatUnconfirmed);}
  function available(){const s=state.storage;return !!(s && s.supported===true && s.mounted===true && s.remounting!==true && !formatPending(s) && !state.offline);}
  function clearFiles(){state.filesEpoch++;state.files=[];state.filesLoaded=false;state.filesError='';state.filesAt=0;}
  function render(){
    const s=state.storage,full=s && count(s.totalBytes)>0 && count(s.usedBytes)!==null && s.usedBytes>=s.totalBytes;
    let label='Checking card',kind='pending';
    if(state.offline){label='Device offline';kind='bad';}
    else if(state.formatPosting || state.formatWatching || formatPending(s))label='Formatting TF card…';
    else if(state.remounting || s && s.remounting===true)label='Mounting card…';
    else if(s && s.supported!==true){label='TF card not supported';kind='';}
    else if(s && s.mounted!==true){label='Card not mounted';kind='pending';}
    else if(full){label='Card full';kind='bad';}
    else if(s && s.error){label='Card error';kind='bad';}
    else if(s){label=s.recording===true?'Recording to TF':'Mounted';kind='good';}
    el['card-storage-state'].textContent=label;el['card-storage-state'].className='pill '+kind;
    el['card-refresh'].disabled=state.loading || state.remounting || state.formatPosting;
    el['card-refresh'].textContent=state.loading?'Refreshing…':'Refresh files';
    el['card-remount'].disabled=state.loading || state.remounting || state.offline || !s || s.supported!==true || cardBusy(s) || state.formatPosting || state.formatWatching || state.formatUnconfirmed;
    el['card-remount'].textContent=state.remounting?'Mounting…':s && s.mounted===true?'Remount card':'Retry mount';
    const supported=s && s.supported===true;
    el['card-storage-used'].textContent=supported?bytes(s.usedBytes)+' / '+bytes(s.totalBytes):'—';
    el['card-storage-size'].textContent=supported?bytes(s.cardBytes):'—';
    for(const [id,key] of [['card-storage-written','writtenRecords'],['card-storage-dropped','droppedRecords']])el[id].textContent=supported && count(s[key])!==null?String(count(s[key])):'—';
    el['card-storage-active'].hidden=!(supported && s.activeFile);
    el['card-storage-active'].textContent=s && s.activeFile?(state.offline?'Last reported file: ':'Current session file: ')+String(s.activeFile):'';
    const errors=[state.requestError,state.filesError,state.formatError,supported && typeof s.error==='string'?s.error:''].filter(Boolean);
    if(full && !errors.some(error=>/full/i.test(error)))errors.push('Card is full. TF recording may be incomplete. Download files and check the card.');
    el['card-storage-error'].hidden=errors.length===0;el['card-storage-error'].textContent=[...new Set(errors)].join(' ');
    const dropped=supported?count(s.droppedRecords):0;
    el['card-storage-gaps'].hidden=!dropped;el['card-storage-gaps'].textContent=dropped?dropped+' records dropped. The TF log has gaps and may be incomplete.':'';
    renderFormat();renderFiles();
  }
  function renderFiles(){
    const list=el['card-files'];list.replaceChildren();const s=state.storage;
    let empty='Checking for a TF card…';
    if(state.offline)empty='Device offline. Refresh after reconnecting to list and download card files.';
    else if(s && s.supported!==true)empty='This device target does not support TF-card logging. Browser-saved sessions remain available below.';
    else if(state.formatPosting || state.formatWatching || formatPending(s))empty='TF-card formatting is in progress. Files will refresh after its result is confirmed.';
    else if(state.formatUnconfirmed)empty='The TF-card format result is unconfirmed. Choose Refresh files to check status.';
    else if(state.remounting || s && s.remounting===true)empty='Waiting for the card mount result…';
    else if(s && s.mounted!==true)empty='Insert a supported card, then choose Retry mount. A mount retry never formats the card.';
    else if(state.filesError)empty='The device file list is unavailable. Choose Refresh files to retry.';
    else if(state.filesLoaded)empty='No TF recordings yet. Use Start recording in Live to create a device log.';
    if(!state.files.length){list.append(node('p','card-empty',empty));return;}
    if(state.filesError)list.append(node('p','card-file-meta','Showing last known files. Choose Refresh files to retry.'));
    for(const file of state.files){
      const name=typeof file.name==='string'?file.name:'Unnamed file';
      const active=!!(s && s.recording===true && (file.active===true || name===s.activeFile));
      const row=node('article','card-file'+(active?' card-file-active':'')),detail=node('div','card-file-detail');
      detail.append(node('h3','card-file-name',name),node('p','card-file-meta',bytes(file.bytes)+(active?' · Active recording · download is a snapshot':'')));
      const safe=/^[A-Za-z0-9_-]+\.csv$/.test(name);
      if(!safe)detail.append(node('p','card-file-warning','Filename unavailable for download.'));
      row.append(detail);
      if(available() && !state.remounting && !state.formatPosting && !state.formatWatching && !state.formatUnconfirmed && !state.filesError && safe){const link=node('a','button ghost card-download','Download CSV');link.href='/api/storage/download?name='+encodeURIComponent(name);link.download=name;link.setAttribute('aria-label','Download '+name+(active?' (snapshot)':''));row.append(link);}
      else if(state.offline)row.append(node('span','card-file-meta','Offline'));
      list.append(row);
    }
  }
  async function request(path,method,body,timeout=8000){
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),Math.max(1,timeout));
    const options={method:method || 'GET',cache:'no-store',signal:controller.signal,headers:{Accept:'application/json'}};
    if(body!==undefined){options.headers['Content-Type']='application/json';options.body=JSON.stringify(body);}
    try{
      const response=await fetch(path,options);let data;
      try{data=await response.json();}catch(_){throw new Error('Card request returned invalid JSON ('+response.status+').');}
      if(!response.ok || !data || data.ok===false){const error=new Error(data && (data.error || data.message) || 'Card request failed ('+response.status+').');error.status=response.status;throw error;}
      return data;
    }catch(error){if(error.name==='AbortError')throw new Error('Card request timed out. Refresh after checking the device connection.');throw error;}finally{clearTimeout(timer);}
  }
  function receive(storage){
    if(!storage || typeof storage!=='object' || typeof storage.supported!=='boolean' || typeof storage.mounted!=='boolean')throw new Error('Device returned an invalid card status.');
    const previous=state.storage,wasOffline=state.offline;state.statusRevision++;state.storage=storage;state.offline=false;state.requestError='';
    if(!available())clearFiles();
    receiveFormat(storage);
    render();
    if(available() && (wasOffline || !previous || !previous.mounted || previous.remounting===true || previous.activeFile!==storage.activeFile || previous.recording!==storage.recording || state.panel==='sessions' && Date.now()-state.filesAt>5000))loadFiles();
  }
  function loadFiles(){
    if(!available())return Promise.resolve();
    if(filesPromise)return filesPromise;
    const epoch=state.filesEpoch;
    filesPromise=(async()=>{
      try{
        const data=await request('/api/storage/files'),problem=data.error || data.message;
        if(problem)throw new Error(typeof problem==='string'?problem:'Device could not refresh the card file list.');
        if(!Array.isArray(data.files))throw new Error('Device returned an invalid card file list.');
        if(epoch===state.filesEpoch && available()){state.files=data.files.filter(file=>file && typeof file==='object');state.filesLoaded=true;state.filesAt=Date.now();state.filesError='';}
      }
      catch(error){if(epoch===state.filesEpoch){const reason=message(error);if(!/\bbusy\b|\bin use\b/i.test(reason)){state.files=[];state.filesLoaded=false;}state.filesError=reason;}}
      finally{filesPromise=null;render();if(epoch!==state.filesEpoch && available())loadFiles();}
    })();return filesPromise;
  }
  async function refresh(){
    if(state.loading || state.remounting || state.formatPosting)return;
    state.loading=true;state.tried=true;state.requestError='';render();const revision=state.statusRevision;
    try{const storage=await request('/api/storage/status');if(revision===state.statusRevision)receive(storage);await loadFiles();}
    catch(error){if(revision===state.statusRevision){state.offline=true;state.filesEpoch++;state.requestError=message(error);}}
    finally{state.loading=false;render();}
  }
  async function remount(){
    const s=state.storage;if(state.loading || state.remounting || state.offline || !s || s.supported!==true || cardBusy(s) || state.formatPosting || state.formatWatching || state.formatUnconfirmed)return;
    state.remounting=true;state.requestError='';render();
    try{
      await request('/api/storage/remount','POST',{});const deadline=Date.now()+15000;
      do{
        const revision=state.statusRevision,storage=await request('/api/storage/status','GET',undefined,Math.min(8000,deadline-Date.now()));
        if(revision===state.statusRevision)receive(storage);
        if(available())break;
        const remaining=deadline-Date.now();if(remaining<=0)break;
        await new Promise(resolve=>setTimeout(resolve,Math.min(1000,remaining)));
      }while(Date.now()<deadline);
      if(!available())state.requestError='Card is still not mounted. Check the card and choose Retry mount.';
      else await loadFiles();
    }catch(error){state.requestError=message(error);}
    finally{state.remounting=false;render();}
  }
  function renderFormat(){
    el['card-format-open'].disabled=!canFormat();
    el['card-format-confirm'].hidden=!state.formatConfirm;
    el['card-format-submit'].disabled=!state.formatConfirm || !canFormat() || state.confirmIdentity!==state.identity || el['card-format-phrase'].value!=='FORMAT TF CARD';
    const remote=formatPending(state.storage)?state.storage.formatMessage || 'TF-card formatting is in progress.':'';
    const text=state.formatError || state.formatMessage || remote;
    el['card-format-status'].hidden=!text;el['card-format-status'].textContent=text;
    el['card-format-status'].className='card-message '+(state.formatError?'card-error':state.formatOutcome==='success'?'card-success':'card-warning');
    el['card-format-status'].setAttribute('role',state.formatError?'alert':'status');
  }
  function openFormat(){if(!canFormat())return;state.formatConfirm=true;state.confirmIdentity=state.identity;el['card-format-phrase'].value='';renderFormat();el['card-format-phrase'].focus();}
  function closeFormat(){state.formatConfirm=false;el['card-format-phrase'].value='';renderFormat();}
  function operationId(){const data=new Uint8Array(16);if(window.crypto && typeof window.crypto.getRandomValues==='function')window.crypto.getRandomValues(data);else for(let index=0;index<data.length;index++)data[index]=Math.floor(Math.random()*256);return Array.from(data,value=>value.toString(16).padStart(2,'0')).join('');}
  function receiveFormat(storage){
    const operation=state.formatOperation;
    if(!operation || operation.identity!==state.identity || storage.formatOperationId!==operation.id || !(state.formatPosting || state.formatWatching || state.formatUnconfirmed))return;
    if(formatPending(storage)){state.formatMessage=storage.formatMessage || 'TF format is queued or running. Waiting for its result.';state.formatError='';return;}
    if(storage.formatResult!=='success' && storage.formatResult!=='failed')return;
    state.formatWatching=false;state.formatUnconfirmed=false;state.formatOutcome=storage.formatResult;
    state.formatMessage=typeof storage.formatMessage==='string' && storage.formatMessage || (storage.formatResult==='success'?'TF card format completed.':'TF card format failed.');
    state.formatError=storage.formatResult==='failed'?state.formatMessage:'';clearFiles();
  }
  function interruptFormat(reason,changedDevice){
    closeFormat();
    if(!state.formatPosting && !state.formatWatching && !state.formatUnconfirmed)return;
    state.formatEpoch++;state.formatPosting=false;state.formatWatching=false;state.formatUnconfirmed=!changedDevice;state.formatError=reason;state.formatMessage='';state.formatOutcome='unknown';
    if(changedDevice)state.formatOperation=null;
  }
  async function formatCard(){
    if(!state.formatConfirm || !canFormat() || state.confirmIdentity!==state.identity || el['card-format-phrase'].value!=='FORMAT TF CARD')return;
    const operation={id:operationId(),identity:state.identity},epoch=++state.formatEpoch;
    const current=()=>state.formatEpoch===epoch && state.formatOperation===operation && state.identity===operation.identity && !state.offline;
    state.formatOperation=operation;state.formatPosting=true;state.formatWatching=true;state.formatUnconfirmed=false;state.formatError='';state.formatMessage='Sending TF-card format request. Waiting for device confirmation…';state.formatOutcome='pending';closeFormat();clearFiles();render();
    try{
      const acknowledgement=await request('/api/storage/format','POST',{confirm:'FORMAT TF CARD',operationId:operation.id});
      if(!current())return;
      if(acknowledgement.error)throw new Error(String(acknowledgement.error));
      state.formatPosting=false;if(state.formatWatching)state.formatMessage='TF format requested. Waiting for the device to report completion…';render();
      const deadline=Date.now()+60000;
      while(current() && state.formatWatching && Date.now()<deadline){
        const revision=state.statusRevision,storage=await request('/api/storage/status','GET',undefined,Math.min(8000,deadline-Date.now()));
        if(!current())return;
        if(revision===state.statusRevision)receive(storage);
        if(!state.formatWatching)break;
        const remaining=deadline-Date.now();if(remaining>0)await new Promise(resolve=>setTimeout(resolve,Math.min(1000,remaining)));
      }
      if(!current())return;
      if(state.formatWatching){state.formatWatching=false;state.formatUnconfirmed=true;state.formatError='TF-card format is still unconfirmed. Choose Refresh files to check status; do not submit another format request.';state.formatOutcome='unknown';}
      else if(available())await loadFiles();
    }catch(error){if(!current())return;if(!state.formatWatching&&!state.formatUnconfirmed&&(state.formatOutcome==='success'||state.formatOutcome==='failed'))return;state.formatWatching=false;state.formatUnconfirmed=![400,403,409].includes(error.status);state.formatError=message(error)+(state.formatUnconfirmed?' TF-card format result is unconfirmed. Choose Refresh files to check status; do not submit another format request.':'');state.formatOutcome='failed';}
    finally{if(state.formatEpoch===epoch && state.formatOperation===operation){state.formatPosting=false;render();}}
  }
  build();render();
  window.addEventListener('meter:status',event=>{
    const detail=event.detail || {},status=detail.status;
    if(typeof detail.receivedAt==='number' && Number.isFinite(detail.receivedAt)){if(detail.receivedAt<state.statusAt)return;state.statusAt=detail.receivedAt;}
    const device=status && status.device;
    if(device && device.id!=null && device.bootId!=null){const identity=JSON.stringify([String(device.id),String(device.bootId)]);if(identity!==state.identity){interruptFormat('Previous device format result is unknown. Check that device before taking another action.',true);state.identity=identity;state.statusRevision++;state.storage=null;clearFiles();}}
    if(status && status.storage){try{receive(status.storage);}catch(error){state.offline=true;state.statusRevision++;state.filesEpoch++;state.requestError=message(error);render();}}
    else if(!state.tried)refresh();
  });
  window.addEventListener('meter:panel',event=>{state.panel=event.detail && event.detail.name || '';if(state.panel==='sessions')refresh();});
  window.addEventListener('meter:offline',()=>{state.offline=true;state.statusRevision++;state.filesEpoch++;interruptFormat('TF-card format result is unconfirmed because the device went offline. Choose Refresh files after reconnecting; do not submit another format request.',false);render();});
  refresh();
})();
