#pragma once
#include <stdint.h>
#include <meter_ui.h>
#include <meter_selection.h>
#include <meter_orientation.h>
class DeskPanel;
class String;
namespace meter_ble {struct Event;}
namespace ut_meter {enum class TouchEdge : uint8_t;}
namespace meter_touch {
// Browsing a menu is safe to reflow; active input needs stable touch targets.
inline bool blocksAutoRotation(bool uiOpen,bool editingText,bool confirmingAction,bool selectionOpen){
    return uiOpen&&(editingText||confirmingAction||selectionOpen);
}
// These bounds are shared by rendering and hit testing, including partially
// visible rows. Use wide arithmetic so serial gestures cannot overflow them.
inline int scrollOffset(int current,int delta,int content,int viewport){
    return meter_ui::scrollOffset(current,delta,content,viewport);
}
inline int rowAt(int y,int top,int bottom,int scroll,int step,int count){
    if(y<top||y>=bottom||step<=0)return -1;
    const int64_t row=(int64_t(y)-top+scroll)/step;
    return row>=0&&row<count?int(row):-1;
}
inline unsigned cursorOffset(unsigned current,int delta,unsigned length){
    const int64_t next=int64_t(current)+delta;
    return next<0?0:next>length?length:unsigned(next);
}
inline bool canInsert(unsigned length,unsigned maximum){return length<maximum;}
inline meter_ui::Map nativeMenuMap(const meter_ui::Map &configured,unsigned physicalWidth,unsigned physicalHeight,unsigned rotation){
    meter_ui::Map map=meter_ui::orientationMap(configured,physicalWidth,physicalHeight,rotation);
    auto &a=map.advanced;
    if(!a.safe_circle){
        a.viewport.h=std::max(a.viewport.h,a.reference_height-24-a.viewport.y);a.message.y=a.reference_height-16;
        if(std::min(a.reference_width,a.reference_height)>=300)a.row.info_size=2;
        if(a.viewport.h<144){a.row.step=56;a.row.gap=4;a.row.title_y=4;a.row.detail_y=32;}
    }
    return map;
}
// Tabs stay fixed while the form below scrolls. Narrow boards use two rows so
// all four slot targets retain their 44px minimum instead of shrinking labels.
struct MeterTabs {
    meter_ui::Rect area{};int columns=0,height=0;
    explicit MeterTabs(meter_ui::Rect viewport){
        if(viewport.w<96)return;
        const int cols=viewport.w>=192?4:2,extent=(4/cols)*48;
        if(viewport.h<extent+44)return;
        columns=cols;height=extent;
        area={viewport.x,viewport.y,viewport.w,height};
    }
    meter_ui::Rect rect(unsigned slot)const{
        if(!columns||slot>=4)return {};
        const int column=slot%unsigned(columns),row=slot/unsigned(columns);
        const int left=area.x+area.w*column/columns,right=area.x+area.w*(column+1)/columns;
        return {left,area.y+row*48,right-left-4,44};
    }
    int hit(int x,int y)const{for(unsigned i=0;i<4;++i)if(rect(i).contains(x,y))return int(i);return -1;}
};
// Dropdowns use their entire available height for choices. The swipe list has
// no footer controls, so a small display can still reach every choice.
inline meter_ui::SelectionLayout swipeSelectionLayout(meter_ui::Rect area,unsigned count,bool visual){
    meter_ui::SelectionLayout out;if(!count||count>64||area.w<132||area.h<88)return out;
    out.columns=visual&&area.w>=360?2:1;out.rowHeight=visual?(out.columns==2?80:64):44;out.gap=visual?4:0;
    const int capacity=(area.h-44)/out.rowHeight;if(capacity<1)return out;
    out.visible=std::min(count,unsigned(capacity)*out.columns);out.scrollable=count>out.visible;
    const int height=44+int((out.visible+out.columns-1)/out.columns)*out.rowHeight;
    out.box={area.x,area.y+(area.h-height)/2,area.w,height};out.header={out.box.x,out.box.y,out.box.w,44};
    const int cancel=area.w>=280?104:76;out.cancel={out.box.x+out.box.w-cancel,out.box.y,cancel,44};
    out.list={out.box.x,out.box.y+44,out.box.w,height-44};out.valid=true;return out;
}
inline int keyboardCode(unsigned page,unsigned index){
    if(page>=6||index>=16)return -1;
    const unsigned code=32+page*16+index;
    return code<=126?int(code):-1;
}
struct NativeKeyboard:meter_ui::Keyboard {
    unsigned keyCount=16;int captionHeight=0,textHeight=0;
    NativeKeyboard(meter_ui::Rect area,meter_ui::KeyboardSpec settings):meter_ui::Keyboard(area,settings),captionHeight(settings.caption_height),textHeight(settings.text_height){
        if(valid||area.w<132||area.h<88)return;
        // A 172px-high rotated LCD cannot show four rows of printable keys.
        // Page eight characters at a time, keeping editing and Done visible.
        keyCount=8;columns=8;rows=1;captionHeight=0;textHeight=14;spec.font_size=1;spec.gap=2;
        const int stride=(area.h-textHeight)/3;
        controlsY=area.y+textHeight;controlHeight=stride-spec.gap;
        keyTop=controlsY+stride;keyHeight=stride;footerY=keyTop+stride;footerHeight=stride-spec.gap;
        valid=controlHeight>=22&&area.w/8-spec.gap>=14;
    }
    unsigned pages()const{return (95+keyCount-1)/keyCount;}
    int code(unsigned page,unsigned index)const{const unsigned value=32+(page%pages())*keyCount+index;return index<keyCount&&value<=126?int(value):-1;}
    int hit(int x,int y)const{
        if(!valid)return -1;
        for(int i=0;i<4;++i)if(control(i).contains(x,y))return i;
        for(unsigned i=0;i<keyCount;++i)if(key(int(i)).contains(x,y))return 4+int(i);
        for(int i=0;i<4;++i)if(footer(i).contains(x,y))return 20+i;
        return -1;
    }
};
struct KeyboardGeometry {
    int columns,rows,controlsY,controlHeight,keyTop,keyHeight,footerY,footerHeight;
    KeyboardGeometry(int width,int top,int bottom){
        const bool wide=width>=320;
        const meter_ui::Keyboard keys({0,top,width,bottom-top},{wide?8:4,28,24,2,10,wide?12:24,wide?44:30,wide?44:30,1});
        columns=keys.columns;rows=keys.rows;controlsY=keys.controlsY;controlHeight=keys.controlHeight;
        keyTop=keys.keyTop;keyHeight=keys.keyHeight;footerY=keys.footerY;footerHeight=keys.footerHeight;
    }
};
inline int keyboardIndexAt(int x,int y,int left,int width,const KeyboardGeometry &keys){
    if(keys.columns<=0||keys.keyHeight<=0||width<keys.columns||x<left||x>=left+width||y<keys.keyTop||y>=keys.keyTop+keys.rows*keys.keyHeight)return -1;
    int column=(x-left)/(width/keys.columns);if(column>=keys.columns)column=keys.columns-1;
    return (y-keys.keyTop)/keys.keyHeight*keys.columns+column;
}
bool begin();
void open();
// Swipe entry from live values. Back returns to live values.
void openValueMenu();
void openEdgeMenu(ut_meter::TouchEdge edge);
// Reflow after the panel rotation changes; preserve unsaved fields and jobs.
void rotationChanged();
void openValues();
void openConnections();
enum class Selector {Meter,Count,Channel,Layout,Device,Profile};
// returnView: -1 stays in touch setup,0 returns original setup,1 values,2 scope.
void openSelector(Selector type,int returnView=-1);
void close();
bool isOpen();
bool autoRotationBlocked();
void draw(DeskPanel &panel);
void contact(uint16_t x,uint16_t y,bool down);
void resetContact();
void tick();
void sample(const meter_ble::Event &event);
unsigned scopeWindowSeconds();
int scopeChannel(unsigned slot);
bool takeScopeRequest();
bool takeValuesRequest();
bool takeClearScopeRequest();
bool takeDrawRequest();
bool command(const String &line);
const char *pageName();
}
