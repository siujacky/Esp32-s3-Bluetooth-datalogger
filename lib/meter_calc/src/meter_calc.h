#pragma once
#include <stdint.h>
namespace meter_calc {
struct Reading {
    double value=0;
    const char *unit="",*mode="";
    bool numeric=false,fresh=false,connected=false;
    uint32_t flags=0,ageMs=0;
    uint64_t timeMs=0,seq=0,source=0;
};
struct Config {
    bool ac=false,powerFactorSet=false,negativeDischarge=false;
    double powerFactor=1,capacityAh=0,initialSocPercent=-1;
};
struct Result {
    bool valid=false,wattsValid=false,ohmsValid=false;
    double voltageV=0,currentA=0,watts=0,voltAmps=0,ohms=0;
    const char *reason="waiting-for-readings";
};
Result calculate(const Reading &voltage,const Reading &current,const Config &config);
struct Summary {
    bool running=false,estimateValid=false,runtimeValid=false;
    uint32_t samples=0,gaps=0;
    uint64_t integratedMs=0;
    double chargeAh=0,dischargeAh=0,chargeWh=0,dischargeWh=0;
    double netAh=0,netWh=0,remainingAh=0,socPercent=0,runtimeHours=0;
    const char *reason="not-started";
};
class Battery {
public:
    void start();
    void pause();
    void reset();
    void update(const Reading &voltage,const Reading &current,const Config &config,
                uint64_t receivedMs,bool gap=false);
    Summary summary() const;
private:
    void markGap(const char *reason);
    Summary totals_;
    bool baseline_=false,cursors_=false,continuityLost_=false,gapOpen_=false,configSet_=false;
    uint64_t lastTime_=0,lastReceived_=0,vSeq_=0,iSeq_=0,vTime_=0,iTime_=0,vSource_=0,iSource_=0;
    double lastCurrent_=0,lastPower_=0,lastDischarge_=0;
    Config config_;
};
}
