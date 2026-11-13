#include "ut_touch.h"
#include <stdlib.h>
namespace ut_meter {
TouchAction TouchGesture::update(uint16_t x,uint16_t y,bool down,bool scrollable,uint32_t now,unsigned width,unsigned height) {
    TouchAction out;
    if(down) {
        if(!held_) {
            startX_=lastX_=x;startY_=lastY_=y;moved_=false;scroll_=scrollable;
        } else {
            if(abs(int(x)-startX_)>9 || abs(int(y)-startY_)>9)moved_=true;
            if(moved_ && scroll_)out.deltaY=int(lastY_)-y;
            lastX_=x;lastY_=y;
        }
    } else if(held_) {
        if(!moved_ && (!tapped_ || uint32_t(now-lastTap_)>=250)) {
            out.tap=true;out.x=lastX_;out.y=lastY_;lastTap_=now;tapped_=true;
        } else if(moved_&&!scroll_) {
            const int up=int(startY_)-int(lastY_),side=abs(int(lastX_)-int(startX_));
            out.swipeUp=up>=48&&up>=2*side;
            if(width&&height&&startX_<width&&startY_<height){
                // Fingertip centres and saved calibration can place a physical
                // edge 25-35 pixels inward. Keep that start reachable without
                // relaxing the deliberate 48px, 2:1 inward movement check.
                unsigned band=(width<height?width:height)/6;
                if(band<24)band=24;
                if(band>48)band=48;
                const int dx=int(lastX_)-startX_,dy=int(lastY_)-startY_;
                if(startY_<band&&dy>=48&&dy>=2*abs(dx))out.edge=TouchEdge::Top;
                else if(startX_<band&&dx>=48&&dx>=2*abs(dy))out.edge=TouchEdge::Left;
                else if(startX_>=width-band&&-dx>=48&&-dx>=2*abs(dy))out.edge=TouchEdge::Right;
                if(out.edge!=TouchEdge::None)out.swipeUp=false;
            }
        }
    }
    held_=down;
    return out;
}
}
