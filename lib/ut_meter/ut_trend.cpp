#include "ut_trend.h"
#include <math.h>
#include <string.h>
namespace ut_meter {
void Trend::push(const Reading &r, Model model, uint32_t at) {
    uint32_t key=r.modeKey?r.modeKey:r.mode;
    if(!profile_ || mode_!=key || channel_!=r.channel || model_!=model || strcmp(unit_,unit(r))) {
        head_=count_=0;mode_=key;channel_=r.channel;model_=model;profile_=true;
        strncpy(unit_,unit(r),sizeof(unit_)-1);unit_[sizeof(unit_)-1]=0;
    }
    TrendPoint p{at,r.numeric && isfinite(r.value)?r.value:NAN};
    if(count_<CAPACITY)points_[(head_+count_++)%CAPACITY]=p;
    else {points_[head_]=p;head_=(head_+1)%CAPACITY;}
}
bool Trend::matchesReading(const Reading &reading) const {
    const uint32_t key=reading.modeKey?reading.modeKey:reading.mode;
    return profile_&&mode_==key&&channel_==reading.channel&&!strcmp(unit_,unit(reading));
}
bool Trend::range(uint32_t now, TrendRange &out) const {
    bool have=false;double low=0,high=0;
    for(size_t i=0;i<count_;++i) {
        const auto &p=point(i);
        if(!visible(p,now) || !isfinite(p.value))continue;
        if(!have){low=high=p.value;have=true;}
        else {low=fmin(low,p.value);high=fmax(high,p.value);}
    }
    if(!have)return false;
    double margin=(high-low)*.1;
    if(high==low) {margin=fabs(low)*.05;if(margin==0)margin=1;}
    out.low=low-margin;out.high=high+margin;
    return true;
}
bool Trend::join(const TrendPoint &a,const TrendPoint &b) {
    return isfinite(a.value) && isfinite(b.value) && uint32_t(b.at-a.at)<=STALE_MS;
}
}
