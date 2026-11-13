#include "meter_calc.h"
#include <cmath>
#include <cstring>
#include <cctype>
#include <algorithm>
namespace meter_calc {
static bool finite(double n){return std::isfinite(n);}
static bool token(const char *s,const char *word){
    if(!s)return false;
    for(size_t n=0;s[n];++n)if((n==0||!std::isalnum(static_cast<unsigned char>(s[n-1])))&&
       std::toupper(static_cast<unsigned char>(s[n]))==word[0]&&s[n+1]&&
       std::toupper(static_cast<unsigned char>(s[n+1]))==word[1]&&
       !std::isalnum(static_cast<unsigned char>(s[n+2])))return true;
    return false;
}
static bool normalized(const Reading &r,bool voltage,double &value,const char *&reason){
    if(!r.numeric||!finite(r.value)){reason="invalid-reading";return false;}
    if(!r.connected){reason="disconnected";return false;}
    if(!r.fresh||r.ageMs>=3000){reason="stale-reading";return false;}
    constexpr uint32_t derived=1|2|4|8|128|2048|16384|32768|65536|131072|524288|1048576;
    if(r.flags&derived){reason="non-instantaneous-reading";return false;}
    const char *u=r.unit?r.unit:"";double scale=0;
    if(voltage){if(!strcmp(u,"V"))scale=1;else if(!strcmp(u,"mV"))scale=.001;}
    else {if(!strcmp(u,"A"))scale=1;else if(!strcmp(u,"mA"))scale=.001;else if(!strcmp(u,"uA")||!strcmp(u,"µA")||!strcmp(u,"μA"))scale=.000001;}
    if(!scale){reason="unsupported-unit";return false;}
    value=r.value*scale;if(!finite(value)){reason="numeric-overflow";return false;}return true;
}
Result calculate(const Reading &v,const Reading &i,const Config &c){
    Result out;
    if(!normalized(v,true,out.voltageV,out.reason)||!normalized(i,false,out.currentA,out.reason))return out;
    for(const Reading *r:{&v,&i}){
        const bool ac=(r->flags&1024)||token(r->mode,"AC"),dc=(r->flags&512)||token(r->mode,"DC");
        if((ac&&dc)||(ac&&!c.ac)||(dc&&c.ac)){out.reason="mode-mismatch";return out;}
    }
    const uint64_t skew=v.timeMs>i.timeMs?v.timeMs-i.timeMs:i.timeMs-v.timeMs;
    if(skew>1000){out.reason="sample-skew";return out;}
    if(c.ac&&c.powerFactorSet&&(!finite(c.powerFactor)||c.powerFactor<0||c.powerFactor>1)){out.reason="invalid-power-factor";return out;}
    const double product=out.voltageV*out.currentA;
    if(!finite(product)){out.reason="numeric-overflow";return out;}
    if(out.currentA!=0){out.ohms=out.voltageV/out.currentA;if(!finite(out.ohms)){out.reason="numeric-overflow";return out;}if(c.ac)out.ohms=std::fabs(out.ohms);out.ohmsValid=true;}
    out.voltAmps=std::fabs(product);out.wattsValid=!c.ac||c.powerFactorSet;
    if(out.wattsValid)out.watts=c.ac?out.voltAmps*c.powerFactor:product;
    out.valid=true;out.reason="";return out;
}
void Battery::start(){totals_.running=true;totals_.reason="waiting-for-readings";}
void Battery::pause(){if(totals_.running&&totals_.samples)markGap("paused");totals_.running=false;}
void Battery::reset(){*this=Battery();}
void Battery::markGap(const char *reason){
    if(!gapOpen_)++totals_.gaps;
    gapOpen_=true;if(totals_.samples)continuityLost_=true;
    baseline_=false;totals_.running=false;totals_.reason=reason;
}
static bool sameConfig(const Config &a,const Config &b){return a.ac==b.ac&&a.negativeDischarge==b.negativeDischarge&&a.capacityAh==b.capacityAh&&a.initialSocPercent==b.initialSocPercent;}
static void areas(double a,double b,double hours,double &positive,double &negative){
    positive=negative=0;
    if(a>=0&&b>=0){positive=(a/2+b/2)*hours;return;}
    if(a<=0&&b<=0){negative=-(a/2+b/2)*hours;return;}
    const double scale=std::max(std::fabs(a),std::fabs(b));
    const double fraction=(std::fabs(a)/scale)/(std::fabs(a)/scale+std::fabs(b)/scale);
    const double first=std::fabs(a)/2*hours*fraction,last=std::fabs(b)/2*hours*(1-fraction);
    positive=a>0?first:last;negative=a>0?last:first;
}
void Battery::update(const Reading &v,const Reading &i,const Config &c,uint64_t received,bool gap){
    if(!totals_.running)return;
    if(gap){markGap("feed-gap");return;}
    if(c.ac){markGap("battery-requires-dc");return;}
    if(configSet_&&!sameConfig(config_,c)){markGap("configuration-changed-reset-test");return;}
    config_=c;configSet_=true;
    if(!v.source||!i.source){markGap("missing-source-metadata");return;}
    // A replacement meter can restart its packet counter. Identity changes
    // must invalidate continuity before comparing counters from two sources.
    if(cursors_&&(v.source!=vSource_||i.source!=iSource_)){
        vSeq_=v.seq;iSeq_=i.seq;vTime_=v.timeMs;iTime_=i.timeMs;vSource_=v.source;iSource_=i.source;
        markGap("source-changed");return;
    }
    if(cursors_&&(v.seq<vSeq_||i.seq<iSeq_)){totals_.reason="replayed-frame";return;}
    if(cursors_&&(v.timeMs<vTime_||i.timeMs<iTime_)){markGap("time-regression");return;}
    const bool currentAdvanced=!cursors_||i.seq>iSeq_;
    const bool duplicate=cursors_&&v.seq==vSeq_&&i.seq==iSeq_;
    const bool changed=cursors_&&(v.source!=vSource_||i.source!=iSource_);
    vSeq_=v.seq;iSeq_=i.seq;vTime_=v.timeMs;iTime_=i.timeMs;vSource_=v.source;iSource_=i.source;cursors_=true;
    if(changed){markGap("source-changed");return;}
    const Result result=calculate(v,i,c);
    if(!result.valid){markGap(result.reason);return;}
    if(result.voltageV<0){markGap("battery-voltage-polarity");return;}
    if(duplicate){totals_.reason="duplicate-frame";return;}
    const double sign=c.negativeDischarge?-1:1,current=result.currentA*sign,power=result.watts*sign;
    if(baseline_&&currentAdvanced){
        if(i.timeMs<lastTime_||received<lastReceived_){markGap("time-regression");return;}
        const uint64_t dt=i.timeMs-lastTime_;
        if(dt>3000||received-lastReceived_>3000){markGap("sample-gap");return;}
        double discharge,charge,dischargeWh,chargeWh;
        areas(lastCurrent_,current,double(dt)/3600000,discharge,charge);
        areas(lastPower_,power,double(dt)/3600000,dischargeWh,chargeWh);
        if(!finite(totals_.dischargeAh+discharge)||!finite(totals_.chargeAh+charge)||!finite(totals_.dischargeWh+dischargeWh)||!finite(totals_.chargeWh+chargeWh)){markGap("numeric-overflow");return;}
        totals_.dischargeAh+=discharge;totals_.chargeAh+=charge;totals_.dischargeWh+=dischargeWh;totals_.chargeWh+=chargeWh;totals_.integratedMs+=dt;
    }
    if(currentAdvanced){lastTime_=i.timeMs;lastReceived_=received;lastCurrent_=current;lastPower_=power;baseline_=true;}
    lastDischarge_=current;gapOpen_=false;++totals_.samples;totals_.reason="";
}
Summary Battery::summary()const{
    Summary out=totals_;out.netAh=out.dischargeAh-out.chargeAh;out.netWh=out.dischargeWh-out.chargeWh;
    if(config_.capacityAh>0&&finite(config_.capacityAh)&&config_.initialSocPercent>=0&&config_.initialSocPercent<=100&&!continuityLost_&&baseline_&&out.running){
        out.estimateValid=true;out.remainingAh=std::max(0.,std::min(config_.capacityAh,config_.capacityAh*(config_.initialSocPercent/100)-out.netAh));
        out.socPercent=out.remainingAh/config_.capacityAh*100;
        if(lastDischarge_>0){out.runtimeHours=out.remainingAh/lastDischarge_;out.runtimeValid=finite(out.runtimeHours);}
    }
    return out;
}
}
