#pragma once
#include "meter_ui.h"
#include <ut_trend.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace meter_ui {
struct OverlayRange {
    bool valid=false;
    double low=0,high=0;
    void include(double value){
        if(!std::isfinite(value))return;
        if(!valid){low=high=value;valid=true;}
        else {low=std::min(low,value);high=std::max(high,value);}
    }
};
struct OverlaySeries {const char *unit=nullptr;OverlayRange observed;};
inline OverlayRange overlayObserved(const ut_meter::Trend *trend,uint32_t now,uint32_t window){
    OverlayRange out;
    if(trend&&window)for(size_t i=0;i<trend->size();++i){
        const auto &p=trend->point(i);
        if(uint32_t(now-p.at)<=window)out.include(p.value);
    }
    return out;
}
inline OverlayRange overlayScale(const OverlaySeries *series,unsigned count,unsigned index){
    OverlayRange out;
    if(!series||index>=count)return out;
    for(unsigned i=0;i<count;++i){
        if(i!=index&&(!series[index].unit||!series[i].unit||std::strcmp(series[index].unit,series[i].unit)))continue;
        if(series[i].observed.valid){out.include(series[i].observed.low);out.include(series[i].observed.high);}
    }
    if(!out.valid)return out;
    const double span=out.high-out.low;
    double margin=std::isfinite(span)?span*.1:std::max(std::fabs(out.low),std::fabs(out.high))*.1;
    if(out.high==out.low){margin=std::fabs(out.low)*.05;if(!(margin>0))margin=1;}
    const double maximum=std::numeric_limits<double>::max();
    out.low=std::max(-maximum,out.low-margin);out.high=std::min(maximum,out.high+margin);
    return out;
}
struct OverlayPoint {bool visible=false,join=false;int x=0,y=0;};
class OverlayCursor {
    bool pen_=false;
    ut_meter::TrendPoint previous_{};
public:
    OverlayPoint next(const ut_meter::TrendPoint &p,Rect plot,const OverlayRange &range,uint32_t now,uint32_t window){
        OverlayPoint out;
        if(!range.valid||plot.w<2||plot.h<2||!window||uint32_t(now-p.at)>window||!std::isfinite(p.value)||range.high<=range.low){pen_=false;return out;}
        const double span=range.high-range.low;
        const double fraction=std::isfinite(span)?(p.value-range.low)/span:(p.value/2-range.low/2)/(range.high/2-range.low/2);
        out.x=plot.x+plot.w-1-int(uint64_t(now-p.at)*unsigned(plot.w-1)/window);
        out.y=plot.y+plot.h-1-int(std::max(0.0,std::min(1.0,fraction))*double(plot.h-1));
        out.visible=true;out.join=pen_&&ut_meter::Trend::join(previous_,p);
        previous_=p;pen_=true;return out;
    }
};
struct OverlayLayout {bool valid=false;Rect plot{},time{},legend[ut_meter::METER_SLOTS]{};};
inline uint16_t overlayDim565(uint16_t pixel){
    // 5/8 black over the existing chart, preserving each 5/6/5 channel.
    return uint16_t((((pixel>>11)*3/8)<<11)|((((pixel>>5)&63)*3/8)<<5)|((pixel&31)*3/8));
}
struct OverlayHud {Rect source,value,metadata,range;};
inline OverlayHud overlayHud(Rect row){
    if(row.h>=56)return {{row.x+3,row.y+2,row.w-6,8},{row.x+3,row.y+10,row.w-6,18},
        {row.x+3,row.y+29,row.w-6,16},{row.x+3,row.y+45,row.w-6,8}};
    return {{row.x+4,row.y+3,row.w-8,8},{row.x+4,row.y+11,row.w-8,row.h-40},
        {row.x+4,row.y+row.h-27,row.w-8,16},{row.x+4,row.y+row.h-11,row.w-8,8}};
}
inline bool overlayOverlap(Rect a,Rect b){return a.w>0&&a.h>0&&b.w>0&&b.h>0&&a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
inline Rect overlayAvoidControl(Rect row,Rect control){
    if(!overlayOverlap(row,control))return row;
    const int left=control.x-row.x-4,right=row.x+row.w-control.x-control.w-4;
    if(left>=right)row.w=std::max(0,left);
    else {row.x=control.x+control.w+4;row.w=std::max(0,right);}
    return row;
}
inline OverlayLayout overlayLayout(Rect canvas,unsigned count,Rect hud={},Rect control={}){
    OverlayLayout out;
    if(!validDisplayCount(count)||canvas.w<=0||canvas.h<=0)return out;
    // The plot deliberately includes every HUD/control pixel. A round panel
    // clips the backing canvas physically; only its text needs the mapped safe
    // envelope. Rectangular panels float compact cards near the canvas edges.
    out.plot=canvas;
    if(hud.w<=0||hud.h<=0)hud={canvas.x+4,canvas.y+4,canvas.w-8,canvas.h-8};
    hud=intersect(hud,canvas);
    const int rowHeight=hud.h>=240?56:48,gap=hud.h>=240?4:2;
    if(count==4){
        const int columnGap=8,width=(hud.w-columnGap)/2;
        if(width<132||hud.h<rowHeight*2+gap+16)return out;
        out.time=overlayAvoidControl({hud.x,hud.y+hud.h-12,std::min(hud.w,232),12},control);
        if(out.time.w<132)return out;
        for(unsigned i=0;i<4;++i){out.legend[i]=overlayAvoidControl({hud.x+int(i%2)*(width+columnGap),hud.y+int(i/2)*(rowHeight+gap),width,rowHeight},control);if(out.legend[i].w<132||overlayOverlap(out.legend[i],out.time))return OverlayLayout{};}
        out.valid=true;return out;
    }
    if(hud.w<224||hud.h<int(count)*rowHeight+int(count-1)*gap+16)return out;
    const int width=std::min(hud.w,232);
    out.time=overlayAvoidControl({hud.x,hud.y+hud.h-12,width,12},control);
    if(out.time.w<180)return out;
    for(unsigned i=0;i<count;++i){
        out.legend[i]=overlayAvoidControl({hud.x,hud.y+int(i)*(rowHeight+gap),width,rowHeight},control);
        if(out.legend[i].w<224||overlayOverlap(out.legend[i],out.time))return out;
    }
    out.valid=true;return out;
}
}
