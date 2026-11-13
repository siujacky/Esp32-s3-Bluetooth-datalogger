#pragma once
#include <cstdint>
namespace meter_ui {
class IdleDimmer {
    uint32_t lastContact_=0;
    bool enabled_=false,dimmed_=false;
public:
    static constexpr uint32_t delayMs=120000;
    static constexpr unsigned awakePercent=55,dimPercent=10;
    void configure(bool enabled,uint32_t now){enabled_=enabled;lastContact_=now;dimmed_=false;}
    // A wake contact is consumed through release by the caller.
    bool contact(uint32_t now){const bool wake=dimmed_;lastContact_=now;dimmed_=false;return wake;}
    unsigned tick(uint32_t now,bool keepAwake=false){
        if(keepAwake)contact(now);
        dimmed_=enabled_&&!keepAwake&&uint32_t(now-lastContact_)>=delayMs;
        return dimmed_?dimPercent:awakePercent;
    }
};
}
