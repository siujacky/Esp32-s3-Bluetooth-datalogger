#pragma once
#include "ut_meter.h"

namespace ut_meter {
struct TrendPoint { uint32_t at; double value; };
struct TrendRange { double low=0, high=0; };
class Trend {
public:
    static constexpr size_t CAPACITY=128;
    static constexpr uint32_t WINDOW_MS=60000;
    void push(const Reading &reading, Model model, uint32_t at);
    void reset(){head_=count_=0;profile_=false;unit_[0]=0;}
    size_t size() const {return count_;}
    const TrendPoint &point(size_t index) const {return points_[(head_+index)%CAPACITY];}
    bool range(uint32_t now, TrendRange &out) const;
    bool matchesReading(const Reading &reading) const;
    static bool visible(const TrendPoint &p,uint32_t now) {return uint32_t(now-p.at)<=WINDOW_MS;}
    static bool join(const TrendPoint &a,const TrendPoint &b);
private:
    TrendPoint points_[CAPACITY]={};
    size_t head_=0,count_=0;
    bool profile_=false;
    uint32_t mode_=0;
    uint8_t channel_=0;
    Model model_=Model::UT60BT;
    char unit_[12]={};
};
}
