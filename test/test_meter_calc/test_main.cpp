#include <meter_calc.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifndef METER_CALC_STANDALONE
#include <unity.h>
#endif
using namespace meter_calc;
#ifdef METER_CALC_STANDALONE
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#else
#define CHECK(x) TEST_ASSERT_TRUE(x)
#endif
static bool near(double a,double b){return std::fabs(a-b)<1e-9;}
static Reading reading(double n,const char *unit,uint64_t at=1000,uint64_t seq=1,uint64_t source=1){Reading r;r.value=n;r.unit=unit;r.mode="DC";r.numeric=r.fresh=r.connected=true;r.timeMs=at;r.seq=seq;r.source=source;return r;}
void test_native_power_and_battery_contract(){
    Config c;auto v=reading(12000,"mV"),i=reading(2000,"mA",1000,1,2);auto r=calculate(v,i,c);
    CHECK(r.valid&&r.wattsValid&&r.ohmsValid&&near(r.watts,24)&&near(r.ohms,6));
    i.unit="uA";CHECK(near(calculate(v,i,c).currentA,.002));i=reading(2,"A",1000,1,2);
    i.numeric=false;CHECK(!calculate(v,i,c).valid);i.numeric=true;i.ageMs=3000;CHECK(!calculate(v,i,c).valid);i.ageMs=0;i.flags=1;CHECK(!calculate(v,i,c).valid);i.flags=0;
    i.timeMs=2001;CHECK(!calculate(v,i,c).valid);i.timeMs=1000;
    c.ac=true;CHECK(!calculate(v,i,c).valid);v.mode=i.mode="AC";r=calculate(v,i,c);CHECK(r.valid&&!r.wattsValid&&near(r.voltAmps,24));c.powerFactorSet=true;c.powerFactor=.8;CHECK(near(calculate(v,i,c).watts,19.2));c.powerFactor=2;CHECK(!calculate(v,i,c).valid);
    c=Config();v=reading(12,"V");i=reading(1,"A",1000,1,2);i.value=0;CHECK(calculate(v,i,c).valid&&!calculate(v,i,c).ohmsValid);i.value=1;
    Battery b;c.capacityAh=10;c.initialSocPercent=50;b.start();b.update(v,i,c,1000);
    v.timeMs=i.timeMs=2000;v.seq=i.seq=2;b.update(v,i,c,2000);auto s=b.summary();CHECK(s.running&&s.integratedMs==1000&&near(s.dischargeAh,1.0/3600)&&near(s.dischargeWh,12.0/3600)&&s.estimateValid&&s.runtimeValid);
    b.update(v,i,c,2050);CHECK(b.summary().integratedMs==1000);
    // Voltage-only previews cannot rewrite the previous current endpoint.
    v.value=24;v.seq=3;v.timeMs=2100;b.update(v,i,c,2100);v.value=12;v.seq=4;v.timeMs=i.timeMs=3000;i.seq=3;b.update(v,i,c,3000);CHECK(near(b.summary().dischargeWh,24.0/3600));
    i.value=-1;i.seq=4;i.timeMs=v.timeMs=4000;v.seq=5;b.update(v,i,c,4000);s=b.summary();CHECK(near(s.dischargeAh,2.25/3600)&&near(s.chargeAh,.25/3600));
    i.fresh=false;b.update(v,i,c,5000);s=b.summary();CHECK(!s.running&&!s.estimateValid&&s.gaps==1);b.start();i.fresh=true;i.seq=5;i.timeMs=v.timeMs=5000;v.seq=6;b.update(v,i,c,5000);CHECK(!b.summary().estimateValid);
    b.reset();b.start();i.value=1;v=reading(12,"V");i=reading(1,"A",1000,1,2);b.update(v,i,c,1000);i.seq=v.seq=2;i.timeMs=v.timeMs=5000;b.update(v,i,c,5000);CHECK(b.summary().integratedMs==0&&!b.summary().running);
    b.reset();b.start();v=reading(12,"V");i=reading(1,"A",1000,1,2);b.update(v,i,c,1000);i.seq=v.seq=2;i.timeMs=v.timeMs=2000;i.source=3;b.update(v,i,c,2000);CHECK(!b.summary().running&&b.summary().integratedMs==0);
    b.reset();b.start();v=reading(12,"V");i=reading(1,"A",1000,1,2);b.update(v,i,c,1000);v.seq=i.seq=2;v.timeMs=i.timeMs=2000;b.update(v,i,c,2000);b.pause();CHECK(!b.summary().runtimeValid&&!b.summary().estimateValid);
    b.reset();b.start();v=reading(12,"V",1000,100);i=reading(1,"A",1000,101,2);b.update(v,i,c,1000);
    i.source=3;i.seq=1;i.timeMs=2000;v.timeMs=2000;v.seq=101;b.update(v,i,c,2000);CHECK(!b.summary().running&&!b.summary().estimateValid);
    b.reset();b.start();c.capacityAh=1e308;c.initialSocPercent=50;v=reading(12,"V");i=reading(1,"A",1000,1,2);b.update(v,i,c,1000);CHECK(b.summary().estimateValid&&near(b.summary().socPercent,50));
    std::puts("PASS native calculation conversion, validity, AC/PF, trapezoids, signed crossing, replay, voltage preview, gap/source/pause estimates");
}
#ifdef METER_CALC_STANDALONE
int main(){test_native_power_and_battery_contract();}
#else
void setUp(){}
void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(test_native_power_and_battery_contract);return UNITY_END();}
#endif
