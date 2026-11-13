(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.MeterCore = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const MAX_METERS=4,MAX_CHANNELS=8;
  const validMeterSlot=slot=>Number.isInteger(slot)&&slot>=0&&slot<MAX_METERS;
  const validChannel=channel=>Number.isInteger(channel)&&channel>=0&&channel<MAX_CHANNELS;
  const numericValue = r => r && r.numeric === true && typeof r.value === 'number' && Number.isFinite(r.value) ? r.value : null;
  const frameKey = (device, boot, seq) => [String(device), String(boot), Number(seq)];
  const sessionKey = (device, id) => JSON.stringify([String(device), String(id)]);
  const addressKey = address => typeof address === 'string' ? address.trim().toUpperCase() : '';
  const elapsedStatus = (statusAt,now) => Math.max(0,now-statusAt);
  function viewPreferences(input) {
    input=input && typeof input==='object'?input:{};
    return {count:[1,2,3,4].includes(input.count)?input.count:1,focus:validMeterSlot(input.focus)?input.focus:0,layout:input.layout==='separate'?'separate':'overlay',
      channels:Array.from({length:MAX_METERS},(_,slot)=>{const ch=input.channels && input.channels[slot];return validChannel(ch)?ch:0;})};
  }
  function displaySlots(focus,count) {
    const prefs=viewPreferences({focus,count});
    return Array.from({length:prefs.count},(_,index)=>(prefs.focus+index)%MAX_METERS);
  }
  function readViewPreferences(storage) {
    try {return viewPreferences(JSON.parse(storage().getItem('meter-workbench-view')));}catch(_){return viewPreferences();}
  }
  function saveViewPreferences(storage,prefs) {
    try {storage().setItem('meter-workbench-view',JSON.stringify(viewPreferences(prefs)));return true;}catch(_){return false;}
  }
  // Hold the identity selected by the person independently of a mutable scan.
  function discoverySnapshot(discovery,index){
    const device=discovery&&Array.isArray(discovery.devices)&&discovery.devices.find(d=>d&&d.index===index);
    return device&&Number.isInteger(device.index)&&device.index>=0&&addressKey(device.address)&&Number.isInteger(device.addressType)&&device.addressType>=0&&device.addressType<=3?{...device}:null;
  }
  function discoverySelectionMatches(discovery,selected){
    if(!selected)return false;const current=discoverySnapshot(discovery,selected.index);
    return !!current&&addressKey(current.address)===addressKey(selected.address)&&current.addressType===selected.addressType;
  }
  function scopePlot(points,end,windowMs) {
    const visible=points.filter(p=>Number.isFinite(p.time) && p.time>=end-windowMs && p.time<=end),segments=[];
    let segment=null,previous=null,min=null,max=null;
    visible.forEach(p=>{
      if(typeof p.value!=='number' || !Number.isFinite(p.value)){segment=null;previous=null;return;}
      min=min===null?p.value:Math.min(min,p.value);max=max===null?p.value:Math.max(max,p.value);
      if(!segment || p.breakBefore || (previous && p.time-previous.time>5000)){segment=[];segments.push(segment);}
      segment.push(p);previous=p;
    });
    if(min!==null){const padding=(max-min)*.12 || Math.max(Math.abs(max)*.02,.01);min-=padding;max+=padding;}
    return {visible,segments,min,max};
  }
  function scopeOverlay(input,end,windowMs) {
    const groups=new Map(),traces=input.map(trace=>{
      const unit=trace.unit || '',plot=scopePlot(trace.points || [],end,windowMs);
      let scale=groups.get(unit);
      if(!scale){scale={unit,min:null,max:null,slots:[]};groups.set(unit,scale);}
      scale.slots.push(trace.slot);
      plot.segments.forEach(segment=>segment.forEach(point=>{
        scale.min=scale.min===null?point.value:Math.min(scale.min,point.value);
        scale.max=scale.max===null?point.value:Math.max(scale.max,point.value);
      }));
      return {...plot,slot:trace.slot,unit,scale};
    });
    const scales=Array.from(groups.values()).filter(scale=>scale.min!==null);
    scales.forEach(scale=>{if(scale.min!==null){const padding=(scale.max-scale.min)*.12 || Math.max(Math.abs(scale.max)*.02,.01);scale.min-=padding;scale.max+=padding;}});
    traces.forEach(trace=>{trace.min=trace.scale.min;trace.max=trace.scale.max;});
    return {traces,scales,end,windowMs};
  }
  function matchesScopeReading(trace,reading) {
    return !!trace && (!reading || (trace.unit===(reading.unit || '') && trace.mode===(reading.mode || '') && trace.modeKey===reading.modeKey));
  }
  function matchesMeterFrame(status,meter,frame,boot) {
    return !!(status && status.device && String(status.device.bootId)===String(boot) && meter && validMeterSlot(Number(meter.slot)) && meter.state==='connected' && frame && frame.kind==='reading' && validMeterSlot(frame.meter) && validChannel(frame.channel) && frame.meter===Number(meter.slot) && addressKey(meter.address) && addressKey(meter.address)===addressKey(frame.address) && meter.profile && meter.profile===frame.profile);
  }
  function liveFrameEligible(status,meter,frame,boot,previous,statusAt,now) {
    if(!matchesMeterFrame(status,meter,frame,boot) || !Number.isFinite(frame.uptimeMs) || !Number.isFinite(frame.seq))return false;
    if(previous && String(previous.bootId)===String(boot) && (frame.seq<=previous.seq || frame.uptimeMs<previous.uptimeMs))return false;
    const current=(meter.readings || []).find(r=>Number(r.channel)===Number(frame.channel));
    const uptime=status.device.uptimeMs;
    if(!Number.isFinite(uptime))return false;
    if(current && Number.isFinite(current.ageMs) && frame.uptimeMs<uptime-Math.max(0,current.ageMs))return false;
    return frame.uptimeMs<=uptime+elapsedStatus(statusAt,now)+1000;
  }
  function selectLiveReading(status,meter,channel,candidate,statusAt,now) {
    if(!meter||!validMeterSlot(Number(meter.slot))||!validChannel(channel))return undefined;
    const elapsed=elapsedStatus(statusAt,now), current=(meter.readings || []).find(r=>Number(r.channel)===Number(channel));
    const age=current && Number.isFinite(current.ageMs)?Math.max(0,current.ageMs)+elapsed:Infinity;
    const statusReading=current?Object.assign({},current,{ageMs:age,fresh:meter.state==='connected' && current.fresh!==false && age<5000}):undefined;
    if(!candidate || Number(candidate.channel)!==Number(channel) || !liveFrameEligible(status,meter,candidate,candidate.bootId,null,statusAt,now))return statusReading;
    const frameAge=Math.max(0,status.device.uptimeMs+elapsed-candidate.uptimeMs);
    if(statusReading && frameAge>=age)return statusReading;
    return Object.assign({},candidate,{ageMs:frameAge,fresh:frameAge<5000});
  }
  function csvCell(value, numeric) {
    if (numeric) return typeof value === 'number' && Number.isFinite(value) ? String(value) : '';
    let text = value == null ? '' : String(value);
    if (/^[\s]*[=+\-@]/.test(text) || /^[\t\r\n]/.test(text)) text = "'" + text;
    return '"' + text.replace(/"/g, '""') + '"';
  }
  const csvFields = ['deviceId','bootId','seq','kind','event','sessionId','meter','channel','timestampMs','uptimeMs','numeric','value','text','unit','mode','modeKey','flags','address','profile','packet','ruleId','success','code','message'];
  const csvNumeric = new Set(['seq','meter','channel','timestampMs','uptimeMs','value','modeKey','flags','packet','ruleId','code']);
  function framesCsv(frames) {
    return csvFields.join(',') + '\r\n' + frames.map(f => csvFields.map(field => csvCell(field === 'value' ? numericValue(f) : f[field], csvNumeric.has(field))).join(',')).join('\r\n') + '\r\n';
  }
  class ScopeSeries {
    constructor(limit) { this.limit = limit || 600; this.series = new Map(); }
    push(boot, frame) {
      if (!frame || frame.kind !== 'reading' || !validMeterSlot(frame.meter) || !validChannel(frame.channel)) return false;
      const key = frame.meter + ':' + frame.channel;
      const signature = JSON.stringify([boot,frame.profile,frame.modeKey,frame.mode,frame.unit,frame.address]);
      let trace = this.series.get(key);
      // Check the high-water sequence before changing identities; an old frame
      // must not restore the previous physical meter's trace in the same slot.
      if (trace && trace.boot === boot && frame.seq <= trace.lastSeq) return false;
      if (!trace || trace.signature !== signature) {
        trace = {signature,boot,unit:frame.unit || '',mode:frame.mode || '',modeKey:frame.modeKey,profile:frame.profile || '',points:[],lastSeq:-1};
        this.series.set(key, trace);
      }
      if (frame.seq <= trace.lastSeq) return false;
      trace.lastSeq = frame.seq;
      trace.points.push({time:Number(frame.uptimeMs),value:numericValue(frame),seq:frame.seq,...(trace.pendingGap?{breakBefore:true}:{})});
      trace.pendingGap=false;
      if (trace.points.length > this.limit) trace.points.splice(0, trace.points.length - this.limit);
      return true;
    }
    get(meter, channel) { return this.series.get(meter + ':' + channel); }
    markGap() { this.series.forEach(trace=>{trace.pendingGap=true;}); }
    clear() { this.series.clear(); }
  }
  function openDatabase(factory) {
    return new Promise((resolve, reject) => {
      if (!factory) { reject(new Error('IndexedDB is unavailable in this browser.')); return; }
      const request = factory.open('meter-workbench', 1);
      request.onupgradeneeded = () => {
        const db = request.result;
        db.createObjectStore('sessions', {keyPath:'id'});
        const frames = db.createObjectStore('frames', {keyPath:'key'});
        frames.createIndex('bySession', 'sessionKey', {unique:false});
        const gaps = db.createObjectStore('gaps', {keyPath:'key'});
        gaps.createIndex('bySession', 'sessionKey', {unique:false});
        db.createObjectStore('meta', {keyPath:'key'});
      };
      request.onerror = () => reject(request.error || new Error('Could not open browser storage.'));
      request.onblocked = () => reject(new Error('Storage upgrade blocked. Close other dashboard tabs and retry.'));
      request.onsuccess = () => resolve(new MeterStore(request.result));
    });
  }
  function transactionResult(tx, result) {
    return new Promise((resolve,reject) => {
      tx.oncomplete = () => resolve(typeof result === 'function' ? result() : result);
      tx.onabort = () => reject(tx.error || new Error('Browser storage transaction was aborted.'));
      tx.onerror = () => {}; // onabort is the authoritative failure; requests retain default abort behavior.
    });
  }
  class MeterStore {
    constructor(db) { this.db = db; }
    cursor(device,boot) {
      const tx = this.db.transaction('meta','readonly');
      let cursor = 0;
      const req = tx.objectStore('meta').get(JSON.stringify(['cursor',device,boot]));
      req.onsuccess = () => { cursor = req.result ? req.result.seq : 0; };
      return transactionResult(tx, () => cursor);
    }
    commitBatch(batch) {
      const tx = this.db.transaction(['sessions','frames','gaps','meta'],'readwrite');
      const sessions = tx.objectStore('sessions'), frames = tx.objectStore('frames'), gaps = tx.objectStore('gaps');
      const input = Array.isArray(batch.frames) ? batch.frames : [];
      const ids = [...new Set(input.filter(f => f.sessionId).map(f => String(f.sessionId)))];
      const taggedIds = new Set(ids);
      let position = 0, added = 0;
      const defaultSession = id => ({id:sessionKey(batch.deviceId,id),sessionId:id,deviceId:batch.deviceId,label:batch.recording && batch.recording.id === id ? batch.recording.label || id : 'Recovered ' + id,createdAt:Date.now(),startedAt:null,endedAt:null,sampleCount:0,eventCount:0,gapCount:0});
      const updateSession = (id, mutate, next) => {
        const key = sessionKey(batch.deviceId,id);
        const req = sessions.get(key);
        req.onsuccess = () => {
          const session = req.result || defaultSession(id);
          if (batch.recording && batch.recording.id === id && batch.recording.label) session.label = batch.recording.label;
          mutate(session);
          sessions.put(session);
          next();
        };
      };
      const finish = () => {
        tx.objectStore('meta').put({key:JSON.stringify(['cursor',batch.deviceId,batch.bootId]),seq:batch.nextSeq,updatedAt:Date.now()});
      };
      const addGaps = index => {
        if (!batch.gap || index >= Math.max(1,ids.length)) { finish(); return; }
        const id = ids[index] || '';
        const key = [batch.deviceId,batch.bootId,batch.after,batch.oldestSeq,id];
        const req = gaps.get(key);
        req.onsuccess = () => {
          if (req.result) { addGaps(index+1); return; }
          const incompleteTail=!!id && !taggedIds.has(id);
          gaps.put({key,sessionKey:id ? sessionKey(batch.deviceId,id) : '',deviceId:batch.deviceId,bootId:batch.bootId,after:batch.after,oldestSeq:batch.oldestSeq,detectedAt:Date.now(),missingAtLeast:Math.max(0,batch.oldestSeq-batch.after-1),incompleteTail});
          if (id) updateSession(id,s => {s.gapCount++;s.incomplete=true;if(incompleteTail)s.incompleteTail=true;},() => addGaps(index+1));
          else addGaps(index+1);
        };
      };
      const next = () => {
        if (position >= input.length) { addGaps(0); return; }
        const f = input[position++];
        // Only device-tagged recording frames can become saved session data.
        if (!f.sessionId) { next(); return; }
        const key = frameKey(batch.deviceId,batch.bootId,f.seq);
        const req = frames.get(key);
        req.onsuccess = () => {
          if (req.result) { next(); return; }
          const id = String(f.sessionId);
          const record = Object.assign({}, f, {key,sessionKey:sessionKey(batch.deviceId,id),deviceId:batch.deviceId,bootId:batch.bootId});
          frames.add(record);
          added++;
          updateSession(id, s => {
            if (f.kind === 'reading') s.sampleCount++; else s.eventCount++;
            if (s.startedAt == null && typeof f.timestampMs === 'number') s.startedAt = f.timestampMs;
            s.lastTimestampMs = f.timestampMs;
            s.lastUptimeMs = f.uptimeMs;
            s.bootId = batch.bootId;
            const ranges=s.bootRanges || (s.bootRanges={}),range=ranges[batch.bootId] || (ranges[batch.bootId]={firstSeq:f.seq,lastSeq:f.seq});
            range.firstSeq=Math.min(range.firstSeq,f.seq);range.lastSeq=Math.max(range.lastSeq,f.seq);
            if (f.event === 'recording' && /stop/i.test(f.message || '')) {s.endedAt = f.timestampMs || Date.now();range.stoppedSeq=f.seq;}
          }, next);
        };
      };
      const done = transactionResult(tx, () => ({added,nextSeq:batch.nextSeq}));
      if(batch.gap){
        // A recording can have stopped entirely inside an evicted RAM region.
        // Associate the boundary with prior sessions whose stop is not known
        // before it, even when the recovered page has no tagged frames at all.
        const req=sessions.getAll();
        req.onsuccess=()=>{
          req.result.forEach(session=>{
            if(session.deviceId!==batch.deviceId)return;
            const range=session.bootRanges && session.bootRanges[batch.bootId];
            if(!range && session.bootId!==batch.bootId)return;
            const closedBeforeGap=range?Number.isFinite(range.stoppedSeq) && range.stoppedSeq<=batch.after:session.endedAt!=null;
            if(!closedBeforeGap && !ids.includes(session.sessionId))ids.push(session.sessionId);
          });next();
        };
      }else next();
      return done;
    }
    listSessions() {
      const tx = this.db.transaction('sessions','readonly');
      let rows = [];
      tx.objectStore('sessions').getAll().onsuccess = event => { rows = event.target.result; };
      return transactionResult(tx, () => rows.sort((a,b) => b.createdAt-a.createdAt));
    }
    readSession(id) {
      const tx = this.db.transaction(['sessions','frames','gaps'],'readonly');
      const result = {session:null,frames:[],gaps:[]};
      tx.objectStore('sessions').get(id).onsuccess = e => { result.session = e.target.result; };
      tx.objectStore('frames').index('bySession').getAll(id).onsuccess = e => { result.frames = e.target.result.sort((a,b) => a.bootId === b.bootId ? a.seq-b.seq : (a.timestampMs || 0)-(b.timestampMs || 0)); };
      tx.objectStore('gaps').index('bySession').getAll(id).onsuccess = e => { result.gaps = e.target.result; };
      return transactionResult(tx, result);
    }
    deleteSession(id) {
      const tx = this.db.transaction(['sessions','frames','gaps'],'readwrite');
      tx.objectStore('sessions').delete(id);
      for (const name of ['frames','gaps']) {
        const req = tx.objectStore(name).index('bySession').openCursor(id);
        req.onsuccess = () => { const cursor = req.result; if (cursor) { cursor.delete(); cursor.continue(); } };
      }
      return transactionResult(tx);
    }
  }
  return {MAX_METERS,MAX_CHANNELS,validMeterSlot,validChannel,numericValue,frameKey,sessionKey,displaySlots,readViewPreferences,saveViewPreferences,discoverySnapshot,discoverySelectionMatches,scopePlot,scopeOverlay,matchesScopeReading,matchesMeterFrame,liveFrameEligible,selectLiveReading,csvCell,framesCsv,ScopeSeries,openDatabase,MeterStore};
});
