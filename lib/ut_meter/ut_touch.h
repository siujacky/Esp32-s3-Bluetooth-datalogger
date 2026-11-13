#pragma once
#include <stdint.h>
namespace ut_meter {
enum class TouchEdge : uint8_t {None,Top,Left,Right};
struct TouchAction {bool tap=false,swipeUp=false;TouchEdge edge=TouchEdge::None;int deltaY=0;uint16_t x=0,y=0;};
class TouchGesture {
public:
    TouchAction update(uint16_t x,uint16_t y,bool down,bool scrollable,uint32_t now,unsigned width=0,unsigned height=0);
private:
    bool held_=false,moved_=false,scroll_=false,tapped_=false;
    uint16_t startX_=0,startY_=0,lastX_=0,lastY_=0;
    uint32_t lastTap_=0;
};
}
