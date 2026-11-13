#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace meter_web {
struct Sample {
    uint8_t meter=0,channel=0;
    uint32_t packet=0,modeKey=0,flags=0;
    uint64_t uptimeMs=0;
    double value=0;
    bool numeric=false;
    char address[18]={},profile[32]={},text[24]={},unit[12]={},mode[40]={};
};
class Provider {
public:
    virtual ~Provider()=default;
    virtual String stateJson()=0;
    virtual String profilesJson()=0;
    virtual bool control(JsonVariantConst body,String &error)=0;
};
struct NetworkInfo {String apSsid,apPassword,apIp,staIp,staSsid,staState="disconnected",message;bool connected=false;};
// Starts HTTP/network and outbound work separately from the display loop.
bool begin(Provider &provider);
void tick();
void publish(const Sample &sample);
NetworkInfo network();
// Read-only radio/startup diagnostics; never prints network credentials.
void printDiagnostics();
}
