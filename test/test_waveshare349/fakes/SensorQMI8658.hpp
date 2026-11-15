#pragma once
#include "Wire.h"
extern bool fakeImuOk;extern float fakeAx,fakeAy,fakeAz;
struct SensorQMI8658 {
    enum {ACC_RANGE_2G,ACC_ODR_125Hz,GYR_RANGE_512DPS,GYR_ODR_112_1Hz};
    bool begin(FakeWire &,int){return fakeImuOk;}
    bool configAccelerometer(int,int){return true;}bool configGyroscope(int,int){return true;}
    bool enableAccelerometer(){return true;}bool enableGyroscope(){return true;}
    bool getAccelerometer(float &x,float &y,float &z){x=fakeAx;y=fakeAy;z=fakeAz;return fakeImuOk;}
    bool getGyroscope(float &x,float &y,float &z){x=y=z=0;return fakeImuOk;}
};
