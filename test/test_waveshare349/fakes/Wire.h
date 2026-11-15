#pragma once
#include "Arduino.h"
#include <vector>
struct FakeWire {
    uint8_t registers[128][256]={};bool missing[128]={},shortRead[128]={};unsigned writes=0;int sda=-1,scl=-1;
    uint8_t address=0,reg=0;size_t availableBytes=0;std::vector<uint8_t> tx;
    bool rejectCalendarWrite=false;
    bool begin(int a,int b,unsigned){sda=a;scl=b;return true;}
    void setTimeOut(unsigned){}
    void beginTransmission(uint8_t a){address=a;tx.clear();}
    size_t write(uint8_t b){tx.push_back(b);return 1;}
    size_t write(const uint8_t *b,size_t n){tx.insert(tx.end(),b,b+n);return n;}
    uint8_t endTransmission(bool=true){
        if(missing[address])return 2;
        if(rejectCalendarWrite&&address==0x51&&tx.size()>1&&tx[0]==4)return 2;
        if(!tx.empty())reg=tx[0];
        if(tx.size()>1){++writes;for(size_t i=1;i<tx.size();++i)registers[address][reg++]=tx[i];}
        return 0;
    }
    size_t requestFrom(uint8_t a,uint8_t n){address=a;availableBytes=missing[a]?0:shortRead[a]?n-1:n;return availableBytes;}
    size_t available(){return availableBytes;}
    uint8_t read(){--availableBytes;return registers[address][reg++];}
    size_t readBytes(uint8_t *b,size_t n){for(size_t i=0;i<n;++i)b[i]=read();return n;}
};
extern FakeWire Wire,Wire1;
