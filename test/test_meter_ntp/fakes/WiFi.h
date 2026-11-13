#pragma once
#include <cstdint>
class IPAddress {uint8_t bytes[4];public:
    IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):bytes{a,b,c,d}{}
    uint8_t operator[](unsigned index)const{return bytes[index];}
};
struct FakeWiFi {
    IPAddress softAPIP(){return {192,168,4,1};}
    IPAddress softAPSubnetMask(){return {255,255,255,0};}
};
extern FakeWiFi WiFi;
