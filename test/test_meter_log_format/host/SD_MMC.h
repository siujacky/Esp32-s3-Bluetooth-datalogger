#pragma once
#include "fake_runtime.h"
struct FakeSDMMC:FakeSD {
    static int clk,cmd,d0,frequency;static bool oneBit;
    bool setPins(int a,int b,int c){clk=a;cmd=b;d0=c;return a==41&&b==39&&c==40;}
    bool begin(const char *path,bool mode,bool format,int khz,unsigned files){
        oneBit=mode;frequency=khz;SPIClass unused(HSPI);
        return mode&&khz==20000&&FakeSD::begin(0,unused,0,path,files,format);
    }
};
extern FakeSDMMC SD_MMC;
