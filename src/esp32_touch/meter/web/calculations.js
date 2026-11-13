(function (root, factory) {
  'use strict';
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.MeterCalculations = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const finite = value => typeof value === 'number' && Number.isFinite(value);
  const positiveOption = (value, fallback) => finite(value) && value > 0 ? value : fallback;
  const limits = options => ({maxAgeMs:positiveOption(options && options.maxAgeMs,3000),
    maxGapMs:positiveOption(options && options.maxGapMs,3000),maxSkewMs:positiveOption(options && options.maxSkewMs,1000)});
  const failedReading = reason => ({valid:false,value:null,unit:null,reason});
  const address = value => typeof value === 'string' ? value.trim().toUpperCase() : '';
  // HOLD/REL/MIN/MAX/PEAK/AVG/lead error/comparison/peak extrema/inrush/unknown
  // are not instantaneous absolute measurements. Battery-low and range flags are allowed.
  const derivedFlags = 1 | 2 | 4 | 8 | 128 | 2048 | 16384 | 32768 | 65536 | 131072 | 524288 | 1048576;

  function normalizeReading(reading, quantity, options) {
    if (!reading || reading.numeric !== true || !finite(reading.value)) return failedReading('invalid-reading');
    if (reading.connected === false) return failedReading('disconnected');
    if (reading.fresh === false) return failedReading('stale-reading');
    if (reading.ageMs !== undefined && (!finite(reading.ageMs) || reading.ageMs < 0 || reading.ageMs >= limits(options).maxAgeMs)) return failedReading('stale-reading');
    if (reading.flags !== undefined && (!Number.isInteger(reading.flags) || reading.flags < 0 || reading.flags > 0xffffffff)) return failedReading('invalid-flags');
    if ((reading.flags || 0) & derivedFlags) return failedReading('non-instantaneous-reading');
    const units = quantity === 'voltage' ? {V:1,mV:0.001} : quantity === 'current' ? {A:1,mA:0.001,uA:0.000001,'µA':0.000001,'μA':0.000001} : null;
    if (!units || !Object.prototype.hasOwnProperty.call(units,reading.unit)) return failedReading('unsupported-unit');
    const value = reading.value * units[reading.unit];
    if (!finite(value)) return failedReading('numeric-overflow');
    return {valid:true,value,unit:quantity === 'voltage' ? 'V' : 'A',reason:null};
  }

  function coupling(reading) {
    const mode = String(reading.mode || '').toUpperCase();
    const ac = !!((reading.flags || 0) & 1024) || /\bAC\b/.test(mode);
    const dc = !!((reading.flags || 0) & 512) || /\bDC\b/.test(mode);
    return ac && dc ? 'mixed' : ac ? 'ac' : dc ? 'dc' : null;
  }

  function calculate(options) {
    options = options || {};
    const result = {valid:false,reason:null,voltageV:null,currentA:null,watts:null,voltAmps:null,
      ohms:null,resistanceReason:null,mode:options.mode || null,powerFactor:null};
    const fail = reason => Object.assign(result,{reason});
    if (options.mode !== 'dc' && options.mode !== 'ac') return fail('select-power-mode');
    const voltage = normalizeReading(options.voltage,'voltage',options);
    const current = normalizeReading(options.current,'current',options);
    if (!voltage.valid || !current.valid) return fail(!voltage.valid ? voltage.reason : current.reason);
    for (const reading of [options.voltage,options.current]) {
      const measuredMode = coupling(reading);
      if (measuredMode && measuredMode !== options.mode) return fail('mode-mismatch');
    }
    if (finite(options.voltage.uptimeMs) && finite(options.current.uptimeMs) &&
        Math.abs(options.voltage.uptimeMs-options.current.uptimeMs) > limits(options).maxSkewMs) return fail('sample-skew');
    if (options.voltage.deviceId !== undefined && options.current.deviceId !== undefined && String(options.voltage.deviceId) !== String(options.current.deviceId)) return fail('device-mismatch');
    if (options.voltage.bootId !== undefined && options.current.bootId !== undefined && String(options.voltage.bootId) !== String(options.current.bootId)) return fail('boot-mismatch');
    if (options.mode === 'ac' && options.powerFactor != null && (!finite(options.powerFactor) || options.powerFactor < 0 || options.powerFactor > 1)) return fail('invalid-power-factor');
    const product = voltage.value * current.value;
    const resistance = current.value === 0 ? null : voltage.value / current.value;
    if (!finite(product) || (resistance !== null && !finite(resistance))) return fail('numeric-overflow');
    result.valid = true;
    result.voltageV = voltage.value; result.currentA = current.value;
    result.ohms = options.mode === 'ac' && resistance !== null ? Math.abs(resistance) : resistance;
    result.resistanceReason = resistance === null ? 'zero-current' : null;
    if (options.mode === 'dc') result.watts = product;
    else {
      result.voltAmps = Math.abs(product);
      if (options.powerFactor != null) { result.powerFactor = options.powerFactor; result.watts = result.voltAmps * options.powerFactor; }
    }
    return result;
  }

  function sourceMetadata(reading) {
    return !!(reading && Number.isSafeInteger(reading.seq) && reading.seq >= 0 && finite(reading.uptimeMs) && reading.uptimeMs >= 0 &&
      Number.isInteger(reading.meter) && reading.meter >= 0 && reading.meter <= 3 &&
      Number.isInteger(reading.channel) && reading.channel >= 0 && reading.channel <= 7 &&
      address(reading.address) && typeof reading.profile === 'string' && reading.profile.trim() &&
      reading.fresh === true && reading.connected === true && finite(reading.ageMs));
  }
  // Prefix changes alone are safe: conversion occurs before integration. A mode,
  // selected channel, physical meter, or decoder profile change starts a new segment.
  const sourceKey = reading => JSON.stringify([reading.meter,reading.channel,address(reading.address),reading.profile,reading.modeKey == null ? null : reading.modeKey,coupling(reading)]);
  const cursor = reading => ({seq:reading.seq,time:reading.uptimeMs,source:sourceKey(reading),value:reading.value,unit:reading.unit});

  // Integrate a straight segment, splitting at zero so opposing current never
  // cancels the separately reported charge and discharge totals.
  function signedAreas(first, second, hours) {
    if (first >= 0 && second >= 0) return {positive:(first/2+second/2)*hours,negative:0};
    if (first <= 0 && second <= 0) return {positive:0,negative:-(first/2+second/2)*hours};
    const scale = Math.max(Math.abs(first),Math.abs(second));
    const fraction = (Math.abs(first)/scale) / (Math.abs(first)/scale+Math.abs(second)/scale);
    const initial = Math.abs(first)/2*hours*fraction;
    const final = Math.abs(second)/2*hours*(1-fraction);
    return first > 0 ? {positive:initial,negative:final} : {positive:final,negative:initial};
  }

  class BatterySession {
    constructor(config) {
      config = config || {};
      this.config = Object.assign(limits(config),{mode:config.mode == null ? 'dc' : config.mode,
        direction:config.direction == null ? 'positive-discharge' : config.direction,
        ratedCapacityAh:finite(config.ratedCapacityAh) && config.ratedCapacityAh > 0 ? config.ratedCapacityAh : null,
        initialSocPercent:finite(config.initialSocPercent) && config.initialSocPercent >= 0 && config.initialSocPercent <= 100 ? config.initialSocPercent : null});
      this.reset();
    }
    reset() {
      this.running = false; this.samples = 0; this.integratedMs = 0; this.gapCount = 0;
      this.chargeAh = 0; this.dischargeAh = 0; this.chargeWh = 0; this.dischargeWh = 0;
      this.lastGapReason = null; this.lastReason = null;
      this._last = null; this._previous = null; this._cursors = null;
      this._stream = null; this._seenStreams = new Set(); this._gapOpen = false; this._continuityLost = false;
      return this.snapshot();
    }
    start() { this.running = true; this.lastReason = null; return this.snapshot(); }
    pause() {
      if (this.running && this.samples > 0) this.markGap('paused');
      this.running = false;
      return this.snapshot();
    }
    markGap(reason) {
      if (!this._gapOpen) this.gapCount++;
      this._gapOpen = true;
      if (this.samples > 0) this._continuityLost = true;
      this.lastGapReason = reason || 'gap'; this.lastReason = this.lastGapReason;
      this._last = null; this._previous = null;
      return this.snapshot();
    }
    ingest(input) {
      if (!this.running) { this.lastReason = 'paused'; return this.snapshot(); }
      input = input || {};
      if (this.config.mode !== 'dc') return this.markGap('battery-requires-dc');
      if (!['positive-discharge','negative-discharge'].includes(this.config.direction)) return this.markGap('invalid-direction');
      const voltage = input.voltage, current = input.current;
      if (!sourceMetadata(voltage) || !sourceMetadata(current) || !finite(input.receivedAt) || input.receivedAt < 0 ||
          input.deviceId == null || String(input.deviceId).trim() === '' || input.bootId == null || String(input.bootId).trim() === '') return this.markGap('missing-source-metadata');
      for (const reading of [voltage,current]) {
        if (reading.deviceId !== undefined && String(reading.deviceId) !== String(input.deviceId)) return this.markGap('device-mismatch');
        if (reading.bootId !== undefined && String(reading.bootId) !== String(input.bootId)) return this.markGap('boot-mismatch');
      }
      // Gaps also apply to a batch containing only previously seen frames.
      if (input.gap) this.markGap('feed-gap');
      const stream = JSON.stringify([String(input.deviceId),String(input.bootId)]);
      if (stream !== this._stream) {
        if (this._seenStreams.has(stream)) { this.lastReason = 'replayed-stream'; return this.snapshot(); }
        if (this._stream !== null) this.markGap('stream-change');
        this._stream = stream; this._seenStreams.add(stream); this._cursors = null;
      }
      const nextCursors = [cursor(voltage),cursor(current)];
      const currentAdvanced = !this._cursors || nextCursors[1].seq > this._cursors[1].seq;
      let duplicated = false;
      if (this._cursors) {
        for (let i=0;i<2;i++) {
          const next = nextCursors[i], old = this._cursors[i];
          if (next.seq < old.seq) {
            this.lastReason = 'replayed-frame'; return this.snapshot();
          }
          if (next.seq === old.seq && (next.time !== old.time || next.source !== old.source || next.value !== old.value || next.unit !== old.unit)) return this.markGap('replayed-frame');
          if (next.time < old.time) return this.markGap('time-regression');
        }
        duplicated = nextCursors.every((next,i)=>next.seq === this._cursors[i].seq);
        if (nextCursors.some((next,i)=>next.source !== this._cursors[i].source)) this.markGap('source-change');
      }
      // High-water cursors include invalid newer samples so an old valid reading
      // cannot restore a prior source/mode after an overload or stale packet.
      this._cursors = nextCursors;
      const value = calculate(Object.assign({},this.config,{voltage,current}));
      if (!value.valid) return this.markGap(value.reason);
      if (value.voltageV < 0) return this.markGap('battery-voltage-polarity');
      // Recheck freshness before deduplication: repeated status polls must not
      // preserve a once-live baseline after its age crosses the stale limit.
      if (duplicated) { this.lastReason = 'duplicate-frame'; return this.snapshot(); }
      // The selected current meter owns the integration clock. A later voltage
      // notification cannot extend the duration of a held current reading.
      const time = current.uptimeMs;
      const sign = this.config.direction === 'negative-discharge' ? -1 : 1;
      const point = {time,receivedAt:input.receivedAt,current:value.currentA*sign,power:value.watts*sign};
      if (this._previous) {
        const dt = point.time-this._previous.time, receivedDt = point.receivedAt-this._previous.receivedAt;
        if (dt < 0) return this.markGap('time-regression');
        if (receivedDt < 0) this.markGap('browser-time-regression');
        else if (receivedDt > this.config.maxGapMs) this.markGap('browser-gap');
        else if (dt > this.config.maxGapMs) this.markGap('timestamp-gap');
        else if (dt > 0) {
          const charge = signedAreas(this._previous.current,point.current,dt/3600000);
          const energy = signedAreas(this._previous.power,point.power,dt/3600000);
          const totals = [this.dischargeAh+charge.positive,this.chargeAh+charge.negative,this.dischargeWh+energy.positive,this.chargeWh+energy.negative];
          if (!totals.every(finite)) return this.markGap('numeric-overflow');
          [this.dischargeAh,this.chargeAh,this.dischargeWh,this.chargeWh] = totals;
          this.integratedMs += dt;
        }
      }
      // A current update in the same millisecond replaces its endpoint without
      // elapsed time. Voltage-only previews must not retrofit an earlier current
      // endpoint's power or restart integration across an invalid-voltage gap.
      if (currentAdvanced) this._previous = point;
      this._last = value; this._gapOpen = false;
      this.samples++; this.lastReason = null;
      return this.snapshot();
    }
    snapshot() {
      const capacity = this.config.ratedCapacityAh, initial = this.config.initialSocPercent;
      const rated = capacity !== null && initial !== null;
      const estimate = {mode:rated ? 'rated' : 'measured-only',ratedCapacityAh:capacity,initialSocPercent:initial,
        remainingAh:null,socPercent:null,runtimeHours:null,valid:false,reason:null,
        assumptions:['Capacity and initial SOC are entered assumptions, never inferred from partial discharge.',
          'Runtime assumes the latest measured discharge current stays constant; battery chemistry, cutoff and efficiency are not modeled.',
          'The current meter receive timestamps set elapsed time. Wh pairs independent current and nearby voltage readings, not synchronized measurements.',
          'Only received, contiguous intervals are measured. Gaps make unobserved charge unknown.']};
      if (!rated) estimate.reason = 'capacity-and-initial-soc-required';
      else if (this._continuityLost) estimate.reason = 'unobserved-charge-unknown';
      else if (!this._last) estimate.reason = 'no-fresh-samples';
      else {
        estimate.valid = true;
        estimate.remainingAh = Math.max(0,Math.min(capacity,capacity*(initial/100)-(this.dischargeAh-this.chargeAh)));
        estimate.socPercent = Math.max(0,Math.min(100,estimate.remainingAh/capacity*100));
        const dischargeA = this._last.currentA*(this.config.direction === 'negative-discharge' ? -1 : 1);
        if (this.running && dischargeA > 0) {
          const hours = estimate.remainingAh/dischargeA;
          if (finite(hours)) estimate.runtimeHours = hours;
        }
      }
      return {running:this.running,samples:this.samples,integratedMs:this.integratedMs,gapCount:this.gapCount,
        lastGapReason:this.lastGapReason,lastReason:this.lastReason,chargeAh:this.chargeAh,dischargeAh:this.dischargeAh,
        netAh:this.dischargeAh-this.chargeAh,chargeWh:this.chargeWh,dischargeWh:this.dischargeWh,netWh:this.dischargeWh-this.chargeWh,
        last:this._last ? Object.assign({},this._last) : null,estimate,config:Object.assign({},this.config)};
    }
  }

  return {normalizeReading,calculate,BatterySession};
});
