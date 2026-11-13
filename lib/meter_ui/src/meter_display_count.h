#pragma once
#include <atomic>
#include <meter_limits.h>
namespace meter_ui {
inline bool validDisplayCount(unsigned n){return n>=1&&n<=ut_meter::METER_SLOTS;}
inline unsigned displaySlot(unsigned focus,unsigned index){return (focus%ut_meter::METER_SLOTS+index%ut_meter::METER_SLOTS)%ut_meter::METER_SLOTS;}
class DisplayCount {
    std::atomic<unsigned> current_{1},pending_{0};
    std::atomic<bool> saved_{true};
public:
    void load(unsigned stored,bool saved){const bool valid=validDisplayCount(stored);current_.store(valid?stored:1);pending_.store(0);saved_.store(valid&&saved);}
    unsigned count()const{return current_.load();}
    bool saved()const{return saved_.load();}
    bool request(unsigned value){if(!validDisplayCount(value))return false;pending_.store(value);return true;}
    bool consume(unsigned &out){const unsigned value=pending_.exchange(0);if(!value||(value==current_.load()&&saved_.load()))return false;saved_.store(false);current_.store(value);out=value;return true;}
    void markSaved(bool value){saved_.store(value);}
};
class OverlaySetting {
    std::atomic<unsigned> current_{1},pending_{2};
    std::atomic<bool> saved_{true};
public:
    void load(unsigned stored,bool saved){const bool valid=stored<=1;current_.store(valid?stored:1);pending_.store(2);saved_.store(valid&&saved);}
    bool overlay()const{return current_.load()!=0;}
    bool saved()const{return saved_.load();}
    void request(bool value){pending_.store(value?1:0);}
    bool consume(bool &out){const unsigned value=pending_.exchange(2);if(value>1||(value==current_.load()&&saved_.load()))return false;saved_.store(false);current_.store(value);out=value!=0;return true;}
    void markSaved(bool value){saved_.store(value);}
};
}
