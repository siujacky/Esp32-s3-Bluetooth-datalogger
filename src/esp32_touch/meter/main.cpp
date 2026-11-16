#include <Arduino.h>
#include "../panel.h"
#include "meter_ble.h"
#include <math.h>
#include <Wire.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <ut_trend.h>
#include <ut_touch.h>
#include "meter_web.h"
#include "meter_api.h"
#include "meter_calibration.h"
#include "meter_touch_app.h"
#include <esp_timer.h>
#include <new>
#include <wifi_qr.h>
#include <meter_ui_config.h>
#include <meter_overlay.h>
#include "meter_design.h"
#include "meter_display.h"
#include <meter_orientation.h>
#include "meter_value_settings.h"
#include "meter_value_view.h"
#include "meter_board_devices.h"
#include <meter_numerals.h>

static DeskPanel panel;
static unsigned selected=0;
static bool panelOK=false;
static uint32_t drawAt=0,lastRenderUs=0;
static String feedback;
static uint32_t feedbackUntil=0;
// The native forms/QR/scope renderer nests with the BLE snapshot in loop().
// The first device run exhausted Arduino's default 8 KiB loop stack during
// redraw; use the supported weak-hook override and expose its free watermark.
size_t getArduinoLoopTaskStackSize(){return 16384;}
static bool fullScreen=true, scopeView=false, autoReader=true;
static bool suppressTouchUntilUp=false;
static ut_meter::Trend *trends[meter_ble::MAX_CONNECTIONS][meter_ble::MAX_CHANNELS]={};
static char trendSources[meter_ble::MAX_CONNECTIONS][18]={};
static char trendProfiles[meter_ble::MAX_CONNECTIONS][32]={};
static bool networkPage=false;
static int setupScroll=0;
static ut_meter::TouchGesture touchGesture;
static constexpr uint16_t BG=meter_design::modern?meter_design::BG:0x0841,
    FG=meter_design::modern?meter_design::FG:0xffff,
    MUTED=meter_design::modern?meter_design::MUTED:0xa554,
    GREEN=meter_design::modern?meter_design::ACCENT:0x5f2c,
    WARN=meter_design::modern?meter_design::WARN:0xfd20,
    BUTTON=meter_design::modern?meter_design::SURFACE:0x2127;
static const char *stateText(meter_ble::State state) {
    switch(state) {
    case meter_ble::State::Idle:return "DISCONNECTED";
    case meter_ble::State::Scanning:return "SCANNING";
    case meter_ble::State::Connecting:return "CONNECTING";
    case meter_ble::State::Connected:return "CONNECTED";
    case meter_ble::State::Retry:return "RECONNECTING";
    default:return "ERROR";
    }
}
template<class T> static const char *profileText(const T &v){const auto *p=dmm::findProfile(v.profile);return p?p->name:v.profile;}
template<class T> static bool fresh(const T &v,uint32_t now) {
    return ut_meter::isFresh(v.state==meter_ble::State::Connected,v.hasReading,now,v.readingAt);
}
template<class T> static bool fresh(const T &v){return fresh(v,millis());}
static void text(int x,int y,const String &s,uint8_t size=1,uint16_t color=FG) {
    panel.setFont(nullptr);panel.setTextSize(size);panel.setTextColor(color);panel.setCursor(x,y);panel.print(s);
}
static void centered(int y,const String &s,uint8_t size=1,uint16_t color=FG) {
    text((panel.width()-int(s.length())*6*size)/2,y,s,size,color);
}
using Rect=meter_ui::Rect;
static meter_ui::Map ui=meter_ui::configuredMap();
static unsigned appliedRotation=4;
static meter_ui::Transform transform(meter_ui::View v){
    int width=panel.width(),height=panel.height();
    if((panel.getRotation()^v.rotation)&1)std::swap(width,height);
    return {v,width,height};
}
static meter_ui::Transform readingTransform(){return transform(meter_ui::view(ui.reading));}
static meter_ui::Transform setupTransform(){return transform(meter_ui::view(ui.setup));}
static Rect countButton(){return readingTransform().rect(ui.overview.count_button);}
static int footerRight(int right){return scopeView&&ui.overview.show_count_button?std::min(right,countButton().x-readingTransform().xLength(12)):right;}
static bool contains(const Rect &r,uint16_t x,uint16_t y){return r.contains(x,y);}
static void label(const meter_ui::Transform &tx,meter_ui::Label p,const String &s,uint16_t color=FG,int offset=0){
    p=tx.label(p);if(!p.size)return;
    const int width=int(s.length())*6*p.size;
    text(p.x-(p.align==0?width/2:p.align>0?width:0),p.y+offset,s,p.size,color);
}
static String fitText(const String &s,int width,int size=1){
    const int columns=std::max(0,width/(6*std::max(1,size)));
    if(int(s.length())<=columns)return s;
    if(columns<4)return s.substring(0,columns);
    return s.substring(0,columns-3)+"...";
}
static void fittedLabel(const meter_ui::Transform &tx,meter_ui::Label p,const String &s,int width,uint16_t color=FG,int offset=0){
    label(tx,p,fitText(s,width,tx.font(p.size)),color,offset);
}
static void credentialLabel(const meter_ui::Transform &tx,meter_ui::Label p,const String &s,int width,uint16_t color=FG){
    p=tx.label(p);if(!p.size)return;
    while(p.size>1&&int(s.length())*6*p.size>width)--p.size;
    const int columns=std::max(1,width/(6*p.size));
    for(unsigned start=0,line=0;start<s.length();start+=columns,++line){
        const String part=s.substring(start,start+columns);const int length=int(part.length())*6*p.size;
        text(p.x-(p.align==0?length/2:p.align>0?length:0),p.y+line*(8*p.size+tx.yLength(3)),part,p.size,color);
    }
}
static Rect statusBadge(const meter_ui::Transform &tx,meter_ui::Label anchor,const String &s,uint16_t color,int offset=0){
    const auto p=tx.label(anchor);
    const int size=std::max(1,p.size),w=int(s.length())*6*size+tx.xLength(20),h=8*size+tx.yLength(12);
    Rect r={p.x-(p.align==0?w/2:p.align>0?w:0),p.y-tx.yLength(4)+offset,w,h};
    meter_design::badge(panel,r,s.c_str(),color,size);
    return r;
}
static Rect readingButton(){return setupTransform().rect(ui.setup.header);}
static Rect setupViewport(){return setupTransform().rect(ui.setup.viewport);}
static int setupTop(){return setupViewport().y;}
static int setupBottom(){const Rect r=setupViewport();return r.y+r.h;}
static int setupStep(){return setupTransform().yLength(ui.setup.row.step);}
static int setupInfoHeight(){return setupTransform().yLength(ui.setup.info_height);}
static constexpr unsigned SETUP_BUTTONS=8;
static meter_ui::GridLayout setupGrid(){
    const auto tx=setupTransform();const auto viewport=setupViewport();
    return {{tx.x(ui.setup.row.x),viewport.y,tx.xLength(ui.setup.row.width),viewport.h},
        ui.setup.row.columns,tx.xLength(ui.setup.row.column_gap),setupStep(),tx.yLength(ui.setup.row.height),setupInfoHeight()};
}
static int maxSetupScroll(){return std::max(0,setupGrid().contentHeight(SETUP_BUTTONS)-setupViewport().h);}
static Rect buttonRect(unsigned i){
    return setupGrid().rect(i,setupScroll);
}
static void button(unsigned i,const String &s) {
    Rect r=buttonRect(i);panel.fillRoundRect(r.x,r.y,r.w,r.h,8,BUTTON);
    uint8_t size=setupTransform().font(ui.setup.row.font_size);
    unsigned columns=(r.w-12)/(6*size);
    if(s.length()<=columns)text(r.x+(r.w-int(s.length())*6*size)/2,r.y+(r.h-8*size)/2,s,size);
    else {
        String first=s.substring(0,columns),second=s.substring(columns,2*columns);
        text(r.x+(r.w-int(first.length())*6*size)/2,r.y+(r.h-18*size)/2,first,size);
        text(r.x+(r.w-int(second.length())*6*size)/2,r.y+(r.h+2*size)/2,second,size);
    }
}
static void actionCard(unsigned i,const meter_ble::View &v,bool active){
    const Rect r=buttonRect(i),visible=setupGrid().visible(i,setupScroll);
    if(visible.w<=0||visible.h<=0)return;
    static const meter_design::Icon icons[]={meter_design::Icon::Scan,meter_design::Icon::Next,meter_design::Icon::Link,
        meter_design::Icon::Model,meter_design::Icon::Scope,meter_design::Icon::Meter,meter_design::Icon::Wifi,meter_design::Icon::Settings};
    static const char *titles[]={"Scan meters","Next meter","Connect","Meter model","Live scope","Meter slot","Web & Wi-Fi","Functions"};
    String title=i==2&&active?"Disconnect":titles[i],caption;
    switch(i){
    case 0:caption=v.scanning?"Searching nearby...":"Find nearby devices";break;
    case 1:caption=active?"Disconnect to select":v.count?String("Selected ")+String(selected+1)+" of "+String(v.count):"Scan to find a meter";break;
    case 2:caption=active?"End this connection":"Start live readings";break;
    case 3:caption=profileText(v);break;
    case 4:caption="View signal over time";break;
    case 5:caption=String("Active slot ")+String(v.activeSlot+1)+" of "+String(meter_ble::MAX_CONNECTIONS);break;
    case 6:caption="QR code and browser access";break;
    default:caption="Logging, math and rules";break;
    }
    const auto tx=setupTransform();
    meter_design::frame(panel,r,meter_design::SURFACE,meter_design::BORDER);
    meter_design::icon(panel,icons[i],r.x+tx.xLength(25),r.y+tx.yLength(26),GREEN,tx.xLength(18));
    meter_design::heading(panel,{r.x+tx.xLength(49),r.y+tx.yLength(10),r.w-tx.xLength(61),tx.yLength(28)},title.c_str(),FG);
    const int detailSize=std::max(1,tx.font(ui.setup.row.font_size)/2);
    text(r.x+tx.xLength(16),r.y+r.h-tx.yLength(24),fitText(caption,r.w-tx.xLength(32),detailSize),detailSize,MUTED);
}
static String flagsText(const ut_meter::Reading &r) {
    String s;
    if(r.flags[0]&2)s+="HOLD ";if(r.flags[0]&1)s+="REL ";
    if(r.flags[0]&4)s+="MIN ";if(r.flags[0]&8)s+="MAX ";
    if(r.flags[1]&1)s+="CAUTION ";if(r.flags[1]&2)s+="LOW BAT ";
    if(r.flags[1]&4)s+="MANUAL ";
    if(r.flags[2]&2)s+="P-MIN ";if(r.flags[2]&4)s+="P-MAX ";
    return s;
}
static String valueText(const meter_ble::DisplaySnapshot &v,unsigned slot){
    const auto prefs=meter_values::snapshot();return String(meter_value_view::formatted(v,prefs.formats[slot][v.channel]).cells);
}
static void drawReading(const meter_ble::DisplaySnapshot &,unsigned activeSlot,uint32_t now){
    const auto tx=readingTransform();meter_value_view::draw(panel,tx.rect(ui.overview.values),meter_ui::ValuePreset::Solo,activeSlot,1,now,tx.yLength(ui.overview.gap));
}
static const ut_meter::Trend *slotTrend(unsigned slot,const meter_ble::DisplaySnapshot &v){
    return !strcmp(trendSources[slot],v.peer)&&!strcmp(trendProfiles[slot],v.profile)?trends[slot][v.channel]:nullptr;
}
static uint16_t slotColor(unsigned slot){
    const uint16_t colors[]={ui.overview.slot0_color,ui.overview.slot1_color,ui.overview.slot2_color,ui.overview.slot3_color};
    return colors[slot%meter_ble::MAX_CONNECTIONS];
}
static const char *sampleState(const meter_ble::DisplaySnapshot &v,uint32_t now){
    if(fresh(v,now))return "LIVE";
    if(v.hasReading)return v.state==meter_ble::State::Connected?"STALE":"OFFLINE";
    switch(v.state){
    case meter_ble::State::Connecting:return "LINKING";
    case meter_ble::State::Retry:return "RETRY";
    case meter_ble::State::Connected:return "NO DATA";
    case meter_ble::State::Error:return "ERROR";
    default:return "NO LINK";
    }
}
// Fit complete numbers using progressively smaller fonts. Unlike headings,
// current values are never shortened with an ellipsis.
static void numberInRect(Rect area,const String &value,uint16_t color,bool=false,unsigned capacity=10){
    if(!meter_value_view::textNumber(panel,area,value.c_str(),capacity,color,110))text(area.x,area.y,"FONT ERROR",1,WARN);
}

static void drawCountButton(){
    if(!ui.overview.show_count_button)return;
    const Rect r=countButton();
    const String title=String("Meters ")+String(meter_display::count());
    const unsigned size=r.w>=int(title.length())*12+12&&r.h>=28?2:1;
    meter_design::badge(panel,r,title,GREEN,size);
}
static void overviewHeading(Rect area,bool scope){
    const auto tx=readingTransform();
    const int h=std::min(tx.yLength(26),std::max(8,area.y-tx.yLength(8)));
    meter_design::heading(panel,{area.x,std::max(0,area.y-h-tx.yLength(7)),area.w,h},scope?"Live scope":"Live values",FG);
    const int y=area.y+area.h+tx.yLength(10);
    const int width=std::max(0,footerRight(area.x+area.w)-area.x);
    if(!scope&&y+8<=panel.height())text(area.x,y,fitText("Swipe up for menu",width),1,MUTED);
}
static void drawValueOverview(unsigned focus,unsigned count,uint32_t now){
    const auto tx=readingTransform();const auto prefs=meter_values::snapshot();meter_value_view::draw(panel,tx.rect(ui.overview.values),prefs.presets[count-1],focus,count,now,tx.yLength(ui.overview.gap));
}
static void drawOverviewTrace(Rect plot,const ut_meter::Trend *trend,uint32_t now,uint32_t window,uint16_t color){
    if(plot.w<2||plot.h<2)return;
    const uint16_t grid=meter_design::modern?meter_design::BORDER:0x1945;
    for(int i=0;i<=6;++i)panel.drawFastVLine(plot.x+(plot.w-1)*i/6,plot.y,plot.h,grid);
    for(int i=0;i<=2;++i)panel.drawFastHLine(plot.x,plot.y+(plot.h-1)*i/2,plot.w,grid);
    bool have=false;ut_meter::TrendRange range;
    if(trend)for(size_t i=0;i<trend->size();++i){
        const auto &p=trend->point(i);
        if(uint32_t(now-p.at)>window||!isfinite(p.value))continue;
        if(!have){range.low=range.high=p.value;have=true;}
        else {range.low=fmin(range.low,p.value);range.high=fmax(range.high,p.value);}
    }
    if(!have){
        const String waiting="No samples";
        if(plot.w>=int(waiting.length())*6&&plot.h>=8)text(plot.x+(plot.w-int(waiting.length())*6)/2,plot.y+(plot.h-8)/2,waiting,1,MUTED);
        return;
    }
    double margin=(range.high-range.low)*.1;
    if(range.high==range.low){margin=fabs(range.low)*.05;if(margin==0)margin=1;}
    range.low-=margin;range.high+=margin;
    bool pen=false;int oldX=0,oldY=0;ut_meter::TrendPoint previous{};
    for(size_t i=0;i<trend->size();++i){
        const auto &p=trend->point(i);
        if(uint32_t(now-p.at)>window||!isfinite(p.value)){pen=false;continue;}
        const uint32_t age=now-p.at;
        const int x=plot.x+plot.w-1-uint64_t(age)*(plot.w-1)/window;
        int y=plot.y+plot.h-1-int((p.value-range.low)*(plot.h-1)/(range.high-range.low));
        y=constrain(y,plot.y,plot.y+plot.h-1);
        if(pen&&ut_meter::Trend::join(previous,p))panel.drawLine(oldX,oldY,x,y,color);
        panel.fillCircle(x,y,1,color);pen=true;oldX=x;oldY=y;previous=p;
    }
    char high[18],low[18];snprintf(high,sizeof(high),"%.4g",range.high);snprintf(low,sizeof(low),"%.4g",range.low);
    if(plot.h>=18){for(char *p=high;*p;++p)if(*p=='e')*p='E';for(char *p=low;*p;++p)if(*p=='e')*p='E';meter_value_view::textNumber(panel,{plot.x+2,plot.y,std::min(84,plot.w-4),8},high,10,MUTED,12);meter_value_view::textNumber(panel,{plot.x+2,plot.y+plot.h-8,std::min(84,plot.w-4),8},low,10,MUTED,12);}
}
static void drawScopeOverview(unsigned focus,unsigned count,uint32_t now,bool examples=false){
    const auto tx=readingTransform();
    const Rect area=tx.rect(ui.overview.scope);
    overviewHeading(area,true);
    const int gap=tx.yLength(ui.overview.gap);
    const unsigned seconds=meter_touch::scopeWindowSeconds();
    const uint32_t window=seconds*1000UL;
    for(unsigned index=0;index<count;++index){
        const unsigned slot=meter_ui::displaySlot(focus,index);
        const auto v=examples?meter_value_view::exampleReading(slot,now):meter_ble::displaySnapshot(slot,meter_touch::scopeChannel(slot),now);
        const Rect lane=meter_ui::scopeLane(area,count,index,gap);
        const int pad=std::min(7,std::max(2,lane.h/12));
        const int header=std::max(18,std::min(32,lane.h/3));
        const uint16_t color=slotColor(slot);
        meter_design::frame(panel,lane,meter_design::modern?meter_design::SURFACE:BUTTON,meter_design::modern?meter_design::BORDER:BUTTON);
        panel.fillRoundRect(lane.x,lane.y,std::max(2,tx.xLength(3)),lane.h,1,color);
        const String unit=v.hasReading?String(ut_meter::unit(v.reading)):String("--");
        const String source=String("Meter ")+String(slot+1)+" / CH "+String(v.channel+1);
        const auto prefs=meter_values::snapshot();const auto &format=prefs.formats[slot][v.channel];
        const auto numeric=meter_value_view::formatted(v,format);
        const bool overflow=numeric.state==meter_ui::NumberState::Overflow;
        const char *status=examples?"EXAMPLE":sampleState(v,now);
        if(lane.h<50){
            // The short landscape LCD needs horizontal space for its trace:
            // three text rows on the left, a full-height plot on the right.
            // At the mapped 29px minimum this preserves a 25px Y axis, with
            // room for both scale labels and a complete bitmap-font number.
            const int sourceWidth=(lane.w-pad*3)/2;
            text(lane.x+pad,lane.y+pad,fitText(source,sourceWidth),1,color);
            text(lane.x+pad,lane.y+pad+8,fitText(String(status)+" / "+unit,sourceWidth),1,fresh(v,now)?color:WARN);
            numberInRect({lane.x+pad,lane.y+pad+16,sourceWidth,std::max(8,lane.h-pad*2-16)},String(numeric.cells),fresh(v,now)?FG:MUTED,false,meter_ui::numberCellCount(format));
            const Rect plot={lane.x+sourceWidth+pad*2,lane.y+pad,lane.w-sourceWidth-pad*3,lane.h-pad*2};
            drawOverviewTrace(plot,examples?nullptr:slotTrend(slot,v),now,window,color);
            if(overflow){panel.fillRect(plot.x+1,plot.y+1,plot.w-2,8,BG);meter_value_view::textNumber(panel,{plot.x+2,plot.y+1,plot.w-4,8},v.reading.text,24,WARN,12);}
            continue;
        }
        const int sourceWidth=(lane.w-pad*3)*45/100;
        text(lane.x+pad,lane.y+pad,fitText(source,sourceWidth),1,color);
        text(lane.x+pad,lane.y+pad+9,fitText(String(status)+" / "+unit,sourceWidth),1,fresh(v,now)?color:WARN);
        const Rect numberArea={lane.x+sourceWidth+pad*2,lane.y+pad,lane.w-sourceWidth-pad*3,header-10};
        numberInRect(numberArea,String(numeric.cells),fresh(v,now)?FG:MUTED,true,meter_ui::numberCellCount(format));
        if(overflow)meter_value_view::textNumber(panel,{numberArea.x,lane.y+pad+header-8,numberArea.w,8},v.reading.text,24,WARN,12);
        const Rect plot={lane.x+pad,lane.y+pad+header+2,lane.w-pad*2,std::max(1,lane.h-pad*2-header-2)};
        drawOverviewTrace(plot,examples?nullptr:slotTrend(slot,v),now,window,color);
    }
    // One shared time window and one draw timestamp keep every lane aligned.
    const int y=area.y+area.h+tx.yLength(10);
    if(y+8<=panel.height()){
        const String caption=String("-")+String(seconds)+"s to now / tap for value";
        text(area.x,y,fitText(caption,std::max(0,footerRight(area.x+area.w)-area.x)),1,MUTED);
    }
}
static void drawScopeOverlay(unsigned focus,unsigned count,uint32_t now){
    const auto tx=readingTransform();
    const Rect canvas={0,0,panel.width(),panel.height()};
    const Rect hud=tx.reference.safe_circle?tx.rect(ui.overview.scope):Rect{};
    const Rect control=ui.overview.show_count_button?countButton():Rect{};
    const auto layout=meter_ui::overlayLayout(canvas,count,hud,control);
    if(!layout.valid){drawScopeOverview(focus,count,now);return;}
    const unsigned seconds=meter_touch::scopeWindowSeconds();
    const uint32_t window=seconds*1000UL;
    // Capture one selected-channel snapshot at a time. Keep only the tiny
    // legend fields and history pointers across traces, never Connection/View.
    static_assert(sizeof(meter_ble::DisplaySnapshot)<=256,"display snapshot exceeds loop stack budget");
    struct Trace {
        const ut_meter::Trend *history=nullptr;
        char value[24]={},unit[12]={},peer[18]={},metadata[112]={},original[24]={};
        const char *status="NO LINK";
        unsigned slot=0,channel=0;
        bool live=false;
    } traces[meter_ble::MAX_CONNECTIONS];
    meter_ui::OverlaySeries series[meter_ble::MAX_CONNECTIONS];
    for(unsigned index=0;index<count;++index){
        auto &trace=traces[index];
        trace.slot=meter_ui::displaySlot(focus,index);
        const auto v=meter_ble::displaySnapshot(trace.slot,meter_touch::scopeChannel(trace.slot),now);
        trace.channel=v.channel;trace.status=sampleState(v,now);trace.live=fresh(v,now);
        valueText(v,trace.slot).toCharArray(trace.value,sizeof(trace.value));
        snprintf(trace.unit,sizeof(trace.unit),"%s",v.hasReading?ut_meter::unit(v.reading):"--");
        snprintf(trace.peer,sizeof(trace.peer),"%s",v.peer);
        // Every active annunciator precedes the mode. All flags fit in the two
        // compact HUD lines; only an unusually long mode may need an ellipsis.
        String metadata=v.hasReading?String(ut_meter::modeName(v.reading)):String("Waiting for reading");
        const auto numberFormat=meter_values::snapshot().formats[trace.slot][trace.channel];
        const auto formatted=meter_value_view::formatted(v,numberFormat);
        if(v.hasReading&&formatted.state==meter_ui::NumberState::Overflow)snprintf(trace.original,sizeof(trace.original),"%s",v.reading.text);
        if(v.hasReading){
            const auto &r=v.reading;String flags;
            if(r.flags[0]&2)flags+="HOLD ";if(r.flags[0]&1)flags+="REL ";
            if(r.flags[0]&4)flags+="MIN ";if(r.flags[0]&8)flags+="MAX ";
            if(r.flags[1]&2)flags+="LOW BAT ";if(r.flags[1]&1)flags+="CAUTION ";
            if(r.flags[1]&4)flags+="MANUAL ";
            if(r.flags[2]&2)flags+="P-MIN ";if(r.flags[2]&4)flags+="P-MAX ";
            if(flags.length())metadata=flags+"/ "+metadata;
        }
        metadata.toCharArray(trace.metadata,sizeof(trace.metadata));
        // Reconnect clears channel metadata even for the same peer/profile.
        // Keep old history hidden until its unit is known again; stale/offline
        // snapshots that retain a reading still have a truthful labeled trace.
        const auto *history=v.hasReading?slotTrend(trace.slot,v):nullptr;
        trace.history=history&&history->matchesReading(v.reading)?history:nullptr;
        series[index].unit=v.hasReading?trace.unit:nullptr;
        series[index].observed=meter_ui::overlayObserved(trace.history,now,window);
    }
    const Rect plot=layout.plot;
    const uint16_t grid=meter_design::modern?meter_design::BORDER:0x1945;
    if(meter_design::modern)panel.fillRect(plot.x,plot.y,plot.w,plot.h,meter_design::SURFACE);
    for(int i=0;i<=6;++i)panel.drawFastVLine(plot.x+(plot.w-1)*i/6,plot.y,plot.h,grid);
    for(int i=0;i<=4;++i)panel.drawFastHLine(plot.x,plot.y+(plot.h-1)*i/4,plot.w,grid);
    bool anySamples=false;
    for(unsigned index=0;index<count;++index){
        const auto &trace=traces[index];
        const auto scale=meter_ui::overlayScale(series,count,index);
        const uint16_t color=slotColor(trace.slot);
        // A fresh cursor for every trace prevents joins between meters. NaNs,
        // expired samples and stale-duration gaps lift the pen inside a trace.
        meter_ui::OverlayCursor cursor;
        int oldX=0,oldY=0;
        if(trace.history)for(size_t i=0;i<trace.history->size();++i){
            const auto p=cursor.next(trace.history->point(i),plot,scale,now,window);
            if(!p.visible)continue;
            if(p.join)panel.drawLine(oldX,oldY,p.x,p.y,color);
            panel.fillCircle(p.x,p.y,1,color);oldX=p.x;oldY=p.y;anySamples=true;
        }
    }
    if(!anySamples){
        const String waiting="No samples";
        const int x=plot.x+(plot.w-int(waiting.length())*6)/2,y=plot.y+(plot.h-8)/2;
        text(x,y,waiting,1,MUTED);
    }
    // Draw every trace before shading its HUD. Later traces must remain under
    // earlier cards too; these are floating overlays, never plot subdivisions.
    auto shade=[](Rect r){
        for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)
            panel.drawPixel(x,y,meter_ui::overlayDim565(panel.getPixel(x,y)));
    };
    for(unsigned index=0;index<count;++index){
        const auto &trace=traces[index];
        const uint16_t color=slotColor(trace.slot);
        const Rect row=layout.legend[index];const auto hud=meter_ui::overlayHud(row);
        shade(row);panel.drawFastVLine(row.x,row.y,row.h,color);
        const String source=String("M")+String(trace.slot+1)+" CH"+String(trace.channel+1)+" "+(trace.peer[0]?trace.peer:"No source");
        text(hud.source.x,hud.source.y,fitText(source,hud.source.w-int(strlen(trace.status))*6-4),1,color);
        text(hud.source.x+hud.source.w-int(strlen(trace.status))*6,hud.source.y,trace.status,1,trace.live?color:WARN);
        const int unitWidth=std::min(74,hud.value.w/3);
        const auto prefs=meter_values::snapshot();
        numberInRect({hud.value.x,hud.value.y,hud.value.w-unitWidth,hud.value.h},String(trace.value),trace.live?FG:MUTED,false,meter_ui::numberCellCount(prefs.formats[trace.slot][trace.channel]));
        text(hud.value.x+hud.value.w-unitWidth+3,hud.value.y+(hud.value.h-8)/2,trace.unit,1,color);
        const String metadata=trace.metadata;const unsigned columns=unsigned(hud.metadata.w/6);
        unsigned split=std::min(columns,unsigned(metadata.length()));
        if(metadata.length()>columns){
            const int space=metadata.lastIndexOf(' ',columns);
            if(space>0)split=unsigned(space);
        }
        text(hud.metadata.x,hud.metadata.y,metadata.substring(0,split),1,MUTED);
        while(split<metadata.length()&&metadata[split]==' ')++split;
        if(metadata.length()>split)text(hud.metadata.x,hud.metadata.y+8,fitText(metadata.substring(split),hud.metadata.w),1,WARN);
        // Identical units repeat one combined Y range; prefixes stay apart.
        const auto scale=meter_ui::overlayScale(series,count,index);char range[48];
        if(scale.valid)snprintf(range,sizeof(range),"Y %.3g..%.3g %s",scale.low,scale.high,trace.unit);
        else snprintf(range,sizeof(range),"Y no samples / %s",trace.unit);
        const int rangeUnitWidth=std::min(74,hud.range.w/3),rangeWidth=hud.range.w-rangeUnitWidth;
        if(trace.original[0]){
            meter_value_view::textNumber(panel,{hud.range.x,hud.range.y,rangeWidth,hud.range.h},trace.original,24,color,12);
            text(hud.range.x+rangeWidth+3,hud.range.y,trace.unit,1,color);
        }else if(scale.valid){
            char low[18],high[18];snprintf(low,sizeof(low),"%.3g",scale.low);snprintf(high,sizeof(high),"%.3g",scale.high);
            for(char *p=low;*p;++p)if(*p=='e')*p='E';for(char *p=high;*p;++p)if(*p=='e')*p='E';
            const int part=(rangeWidth-12)/2;
            meter_value_view::textNumber(panel,{hud.range.x,hud.range.y,part,hud.range.h},low,10,color,12);
            text(hud.range.x+part,hud.range.y,"..",1,color);
            meter_value_view::textNumber(panel,{hud.range.x+part+12,hud.range.y,rangeWidth-part-12,hud.range.h},high,10,color,12);
            text(hud.range.x+rangeWidth+3,hud.range.y,trace.unit,1,color);
        }else text(hud.range.x,hud.range.y,range,1,color);
    }
    unsigned unitGroups=0;
    for(unsigned index=0;index<count;++index){
        if(!series[index].observed.valid||!series[index].unit)continue;
        bool seen=false;
        for(unsigned earlier=0;earlier<index;++earlier){
            if(series[earlier].observed.valid&&series[earlier].unit&&!strcmp(series[earlier].unit,series[index].unit)){seen=true;break;}
        }
        if(!seen)++unitGroups;
    }
    const String scaleMode=unitGroups>1?String("per-unit Y"):unitGroups==1?String("shared Y"):String("no Y samples");
    const String caption=scaleMode+" / -"+String(seconds)+"s..now / tap:value";
    shade(layout.time);
    text(layout.time.x+4,layout.time.y+2,fitText(caption,layout.time.w-8),1,MUTED);
}
static void drawModernControls(const meter_ble::View &v){
    const auto tx=setupTransform();const Rect viewport=setupViewport(),header=readingButton();
    const int origin=viewport.y-setupScroll,pad=tx.xLength(16);
    const bool active=v.state==meter_ble::State::Connected||v.state==meter_ble::State::Connecting||v.state==meter_ble::State::Retry;
    const Rect source={viewport.x,origin,viewport.w,std::max(1,setupInfoHeight()-tx.yLength(10))};
    if(meter_ui::intersect(source,viewport).h>0){
        meter_design::frame(panel,source,meter_design::SURFACE,meter_design::BORDER);
        const auto title=tx.label(ui.setup.info.title),state=tx.label(ui.setup.info.state);
        const bool portrait=panel.height()>panel.width();
        if(portrait){
            text(source.x+pad,origin+12,String("SOURCE / SLOT ")+String(v.activeSlot+1),1,MUTED);
            const String caption=stateText(v.state);meter_design::badge(panel,{source.x+source.w-pad-108,origin+8,108,24},caption,fresh(v)?GREEN:WARN);
        }else{
            text(title.x,state.y+origin,String("SOURCE / SLOT ")+String(v.activeSlot+1),1,MUTED);
            statusBadge(tx,ui.setup.info.state,stateText(v.state),fresh(v)?GREEN:WARN,origin);
        }
        const auto &candidate=v.meters[selected];
        String name=v.count?String(selected+1)+" / "+String(v.count)+"  "+candidate.name:"Choose a Bluetooth meter";
        String address=v.count?String(candidate.address):"Tap Scan meters to begin";
        if(active&&v.peer[0]){
            address=v.peer;name=profileText(v);
            for(unsigned i=0;i<v.count;++i)if(!strcmp(v.meters[i].address,v.peer)){name=v.meters[i].name;break;}
        }
        if(portrait){
            meter_design::heading(panel,{source.x+pad,origin+44,source.w-2*pad,30},name.c_str(),FG);
            text(source.x+pad,origin+84,fitText(address,source.w-2*pad),1,MUTED);
            text(source.x+pad,origin+106,fitText(v.profile,source.w-2*pad),1,MUTED);
        }else{
            meter_design::heading(panel,{title.x,title.y+origin-tx.yLength(3),source.x+source.w-pad-title.x,tx.yLength(24)},name.c_str(),FG);
            fittedLabel(tx,ui.setup.info.address,address,source.w/2-pad,MUTED,origin);
            fittedLabel(tx,ui.setup.info.model,v.profile,source.w/2-pad,MUTED,origin);
        }
    }
    for(unsigned i=0;i<SETUP_BUTTONS;++i)actionCard(i,v,active);
    // The source card scrolls with the actions; the header and footer stay fixed.
    panel.fillRect(0,0,panel.width(),viewport.y,BG);
    panel.fillRect(0,viewport.y+viewport.h,panel.width(),panel.height()-viewport.y-viewport.h,BG);
    meter_design::heading(panel,{viewport.x+tx.xLength(4),tx.y(8),std::max(1,header.x-viewport.x-tx.xLength(16)),tx.yLength(29)},"Meter setup",FG);
    text(viewport.x+tx.xLength(4),tx.y(43),fitText("Connect meters and choose a view",header.x-viewport.x-tx.xLength(16)),1,MUTED);
    meter_design::frame(panel,header,meter_design::RAISED,meter_design::BORDER);
    meter_design::heading(panel,{header.x+tx.xLength(12),header.y+tx.yLength(6),header.w-tx.xLength(24),header.h-tx.yLength(12)},"Value",GREEN);
    String message=(int32_t(feedbackUntil-millis())>0)?feedback:String(v.message);
    text(viewport.x+tx.xLength(4),viewport.y+viewport.h+tx.yLength(8),fitText(message,viewport.w-tx.xLength(8)),1,MUTED);
    label(tx,ui.setup.footer,"SWIPE TO EXPLORE  /  TAP TO OPEN",MUTED);
    const int content=setupGrid().contentHeight(SETUP_BUTTONS),maximum=maxSetupScroll();
    if(maximum){
        const int width=tx.xLength(3),height=viewport.h,thumb=std::max(tx.yLength(24),height*height/content);
        const int x=std::min(panel.width()-width,viewport.x+viewport.w+tx.xLength(3)),y=viewport.y+setupScroll*(height-thumb)/maximum;
        panel.fillRoundRect(x,viewport.y,width,height,width/2,meter_design::SURFACE);
        panel.fillRoundRect(x,y,width,thumb,width/2,GREEN);
    }
}
static void drawControls(const meter_ble::View &v) {
    if(meter_design::modern){drawModernControls(v);return;}
    const auto tx=setupTransform();const int headerScale=tx.font(ui.setup.header_font_size);
    const int origin=setupTop()-setupScroll;
    label(tx,ui.setup.info.state,stateText(v.state),fresh(v)?GREEN:WARN,origin);
    const auto &c=v.meters[selected];
    String title=v.count?String(selected+1)+"/"+v.count+" "+c.name:"Enable meter BLE";
    const int titleColumns=std::max(1,setupViewport().w/(6*std::max(1,tx.font(ui.setup.info.title.size))));
    label(tx,ui.setup.info.title,title.substring(0,titleColumns),FG,origin);
    label(tx,ui.setup.info.address,v.count?String(c.address):"Tap Scan to find a meter",MUTED,origin);
    label(tx,ui.setup.info.model,String("Model ")+v.profile,MUTED,origin);
    if(v.count)label(tx,ui.setup.info.rssi,String(c.rssi)+" dBm",MUTED,origin);
    String message=(int32_t(feedbackUntil-millis())>0)?feedback:String(v.message);
    const int columns=std::max(1,setupViewport().w/(6*std::max(1,tx.font(ui.setup.info.message.size))));
    label(tx,ui.setup.info.message,message.substring(0,columns),MUTED,origin);
    label(tx,ui.setup.info.message_second,message.substring(columns,2*columns),MUTED,origin);
    button(0,"SCAN");button(1,"DEVICE v");
    bool active=v.state==meter_ble::State::Connected || v.state==meter_ble::State::Connecting || v.state==meter_ble::State::Retry;
    button(2,active?"DISCONNECT":"CONNECT");button(3,v.profile);button(4,"SCOPE");
    button(5,String("METER ")+String(v.activeSlot+1)+"/4 v");button(6,"WEB / WIFI");button(7,"ADVANCED / FUNCTIONS");
    // Mask scrolled content behind the fixed header/footer.
    panel.fillRect(0,0,panel.width(),setupTop(),BG);
    panel.fillRect(0,setupBottom(),panel.width(),panel.height()-setupBottom(),BG);
    Rect r=readingButton();panel.fillRoundRect(r.x,r.y,r.w,r.h,8,BUTTON);
    text(r.x+(r.w-7*6*headerScale)/2,r.y+8,"READING",headerScale);
    centered(r.y+r.h-12,v.count?String(c.name):"Meter setup",1,MUTED);
    label(tx,ui.setup.footer,"SWIPE UP / DOWN",MUTED);
    int barHeight=setupBottom()-setupTop();
    int thumb=barHeight*barHeight/(setupInfoHeight()+SETUP_BUTTONS*setupStep());
    int barY=setupTop()+(maxSetupScroll()?setupScroll*(barHeight-thumb)/maxSetupScroll():0);
    const Rect viewport=setupViewport();const int barWidth=tx.xLength(3),barX=std::min(panel.width()-barWidth,viewport.x+viewport.w+tx.xLength(2));
    panel.fillRect(barX,setupTop(),barWidth,barHeight,BUTTON);
    panel.fillRect(barX,barY,barWidth,thumb,MUTED);
}
static void drawNetwork() {
    const auto tx=setupTransform();const auto &network=ui.setup.network;
    auto n=meter_web::network();Rect r=readingButton();
    const Rect qrRect=tx.rect(network.qr);
    int credentialsWidth=panel.width()-qrRect.x-qrRect.w-tx.xLength(28);
    if(meter_design::modern){
        meter_design::heading(panel,{tx.x(16),tx.y(8),std::max(1,r.x-tx.xLength(28)),tx.yLength(29)},"Web & Wi-Fi",FG);
        text(tx.x(16),tx.y(43),"Use your phone or computer",1,MUTED);
        meter_design::frame(panel,r,meter_design::RAISED,meter_design::BORDER);
        meter_design::heading(panel,{r.x+tx.xLength(12),r.y+tx.yLength(6),r.w-tx.xLength(24),r.h-tx.yLength(12)},"Setup",GREEN);
        meter_design::frame(panel,qrRect,meter_design::SURFACE,meter_design::BORDER);
        const bool portrait=panel.height()>panel.width();
        const Rect details=portrait?Rect{tx.x(12),qrRect.y+qrRect.h+tx.yLength(8),panel.width()-tx.xLength(24),std::max(1,panel.height()-qrRect.y-qrRect.h-tx.yLength(20))}:Rect{qrRect.x+qrRect.w+tx.xLength(10),qrRect.y,panel.width()-qrRect.x-qrRect.w-tx.xLength(26),qrRect.h};
        meter_design::frame(panel,details,meter_design::SURFACE,meter_design::BORDER);
        credentialsWidth=details.w-tx.xLength(16);
    }else{
        panel.fillRoundRect(r.x,r.y,r.w,r.h,8,BUTTON);
        const int font=tx.font(2);text(r.x+(r.w-5*6*font)/2,r.y+(r.h-8*font)/2,"SETUP",font);
    }
    static String qrSsid,qrPassword;static meter_wifi::Qr qr;static bool ready=false;
    if(qrSsid!=n.apSsid || qrPassword!=n.apPassword || !ready){qrSsid=n.apSsid;qrPassword=n.apPassword;ready=meter_wifi::encode(qrSsid.c_str(),qrPassword.c_str(),qr);}
    if(ready){
        const int modules=qr.size(),pixel=std::min(qrRect.w,qrRect.h)/(modules+8),extent=(modules+8)*pixel;
        if(pixel<1)label(tx,{network.qr.x+network.qr.w/2,network.qr.y+40,1,0},"QR too small - join manually",WARN);
        else {
            const int left=qrRect.x+(qrRect.w-extent)/2,top=qrRect.y+(qrRect.h-extent)/2;
            panel.fillRect(left,top,extent,extent,0xffff);
            for(int y=0;y<modules;++y)for(int x=0;x<modules;++x)if(qr.dark(x,y))panel.fillRect(left+(x+4)*pixel,top+(y+4)*pixel,pixel,pixel,0);
        }
    }else label(tx,{network.qr.x+network.qr.w/2,network.qr.y+40,1,0},"Wi-Fi starting...",MUTED);
    if(meter_design::modern){
        const auto heading=tx.label(network.heading);
        meter_design::heading(panel,{heading.x-credentialsWidth/2,heading.y,credentialsWidth,tx.yLength(25)},"1. Join Wi-Fi",FG);
        const auto ssid=tx.label(network.ssid);
        text(ssid.x-int(strlen("Network name (SSID)"))*3,ssid.y-tx.yLength(16),"Network name (SSID)",1,MUTED);
        credentialLabel(tx,network.ssid,n.apSsid,credentialsWidth);
        label(tx,network.password_caption,"Wi-Fi password",MUTED);
        credentialLabel(tx,network.password,n.apPassword,credentialsWidth);
        label(tx,network.ip_caption,"2. Open in your browser",MUTED);
        credentialLabel(tx,network.ip,n.apIp,credentialsWidth,GREEN);
        const auto ip=tx.label(network.ip);
        const String url="http://"+n.apIp;
        text(ip.x-int(url.length())*3,ip.y+tx.yLength(24),url,1,MUTED);
        if(n.connected)fittedLabel(tx,network.lan,"LAN "+n.staIp,credentialsWidth,MUTED);
        else label(tx,network.lan,"QR joins this Wi-Fi network",MUTED);
        return;
    }
    label(tx,network.heading,"SCAN TO JOIN");label(tx,network.ssid,n.apSsid);
    label(tx,network.password_caption,"Wi-Fi password",MUTED);
    const auto password=tx.label(network.password);
    // Split long passwords within the available horizontal safe area.
    const int available=std::min(password.x,panel.width()-password.x)*2;
    if(password.size&&int(n.apPassword.length())*6*password.size>available){
        const unsigned half=(n.apPassword.length()+1)/2;
        label(tx,network.password,n.apPassword.substring(0,half));
        label(tx,network.password,n.apPassword.substring(half),FG,8*password.size+tx.yLength(2));
    }else label(tx,network.password,n.apPassword);
    label(tx,network.ip_caption,"Open in your browser",MUTED);label(tx,network.ip,"http://"+n.apIp,GREEN);
    if(n.connected)label(tx,network.lan,"LAN "+n.staIp,MUTED);
}
static void drawSetup(){
    const auto v=meter_ble::view();
    if(selected>=v.count)selected=0;
    drawControls(v);
}
static void drawFullScreen(){
    const uint32_t now=millis();
    const unsigned focus=meter_ble::activeSlot(),count=meter_display::count();
    if(scopeView){
        if(count==1||meter_display::overlay())drawScopeOverlay(focus,count,now);
        else drawScopeOverview(focus,count,now);
    }else if(count>1)drawValueOverview(focus,count,now);
    else{
        const auto v=meter_ble::displaySnapshot(focus,meter_touch::scopeChannel(focus),now);
        drawReading(v,focus,now);
    }
    if(scopeView)drawCountButton();
}
static void draw() {
    const uint32_t renderStarted=micros();
    if(!panel.getBuffer())return;
    if(meter_calibration::active()){meter_calibration::draw(panel);panel.flush();return;}
    panel.setRotation(meter_touch::isOpen()?ui.advanced.rotation:fullScreen?ui.reading.rotation:ui.setup.rotation);
    panel.fillScreen(BG);panel.setTextWrap(false);
    if(meter_touch::isOpen())meter_touch::draw(panel);
    else if(fullScreen)drawFullScreen();else if(networkPage)drawNetwork();else drawSetup();
    panel.flush();lastRenderUs=micros()-renderStarted;
}
static void message(const char *s){feedback=s;feedbackUntil=millis()+3000;drawAt=0;}
static void action(unsigned i) {
    const auto v=meter_ble::view();bool ok=true;
    bool active=v.state==meter_ble::State::Connected || v.state==meter_ble::State::Connecting || v.state==meter_ble::State::Retry;
    if(i==0){fullScreen=false;setupScroll=0;autoReader=true;ok=meter_ble::scan();}
    else if(i==1){meter_touch::openSelector(meter_touch::Selector::Device);autoReader=false;}
    else if(i==2){if(active)ok=meter_ble::disconnect();else{meter_touch::openConnections();autoReader=false;}}
    else if(i==3){meter_touch::openSelector(meter_touch::Selector::Profile);autoReader=false;}
    else if(i==4){fullScreen=true;scopeView=true;autoReader=false;}
    else if(i==5){meter_touch::openSelector(meter_touch::Selector::Meter,0);autoReader=false;}
    else if(i==6){networkPage=true;autoReader=false;}
    else if(i==7){fullScreen=false;autoReader=false;meter_touch::open();}
    if(!ok)message("Busy; try again");
    drawAt=0;
}
static void tap(uint16_t x,uint16_t y) {
    if(meter_display::touch(millis()))return;
    if(meter_touch::isOpen()){meter_touch::command(String("ui tap ")+String(x)+" "+String(y));drawAt=0;return;}
    if(fullScreen){
        if(!scopeView)return;
        if(ui.overview.show_count_button&&contains(countButton(),x,y)){
            meter_touch::openSelector(meter_touch::Selector::Count,scopeView?2:1);fullScreen=false;autoReader=false;
            drawAt=0;return;
        }
        if(scopeView)scopeView=false;
        else fullScreen=false;
        autoReader=false;drawAt=0;return;
    }
    if(networkPage){networkPage=false;drawAt=0;return;}
    Rect r=readingButton();
    if(contains(r,x,y)){fullScreen=true;scopeView=false;drawAt=0;return;}
    if(y<setupTop() || y>=setupBottom())return;
    const int index=setupGrid().indexAt(x,y,setupScroll,SETUP_BUTTONS);
    if(index>=0)action(unsigned(index));
}
static void scrollSetup(int delta) {
    if(meter_touch::isOpen()){meter_touch::command(String("ui scroll ")+String(delta));drawAt=0;return;}
    if(fullScreen || networkPage)return;
    setupScroll=meter_ui::scrollOffset(setupScroll,delta,maxSetupScroll()+setupViewport().h,setupViewport().h);autoReader=false;drawAt=0;
}
static void contact(uint16_t x,uint16_t y,bool down) {
    if(meter_touch::isOpen()){meter_touch::contact(x,y,down);return;}
    bool scrollable=!fullScreen && !networkPage && y>=setupTop() && y<setupBottom();
    auto a=touchGesture.update(x,y,down,scrollable,millis(),panel.width(),panel.height());
    if(a.deltaY)scrollSetup(a.deltaY);
    if(a.edge!=ut_meter::TouchEdge::None&&fullScreen){
        if(meter_display::touch(millis()))return;
        meter_touch::openEdgeMenu(a.edge);autoReader=false;drawAt=0;return;
    }
    if(a.swipeUp&&fullScreen&&!scopeView){
        if(meter_display::touch(millis()))return;
        meter_touch::openValueMenu();fullScreen=false;autoReader=false;drawAt=0;return;
    }
    if(a.tap)tap(a.x,a.y);
}
static void applyOrientation(){
    if(meter_calibration::busy()||appliedRotation==meter_display::rotation())return;
    appliedRotation=meter_display::rotation();ui=meter_ui::orientationMap(meter_ui::configuredMap(),METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,appliedRotation);
    panel.setRotation(appliedRotation);setupScroll=0;touchGesture=ut_meter::TouchGesture();meter_touch::rotationChanged();suppressTouchUntilUp=true;drawAt=0;
}
static void status() {
    auto v=meter_ble::view();
    Serial.printf("meter=1 board=%s panel=%u canvas=%dx%d psram=%u state=%s model=%s count=%u peer=%s packets=%lu bytes=%lu dropped=%lu heap=%u\n",
        METER_ID,
        panelOK,panel.width(),panel.height(),ESP.getPsramSize(),stateText(v.state),v.profile,v.count,v.peer,(unsigned long)v.packets,(unsigned long)v.bytes,(unsigned long)v.dropped,ESP.getFreeHeap());
    Serial.printf("mapping_sha256=%s physical=%dx%d rotations=reading:%d/setup:%d/advanced:%d\n",METER_MAPPING_SHA256,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,ui.reading.rotation,ui.setup.rotation,ui.advanced.rotation);
    for(unsigned i=0;i<v.count;i++)Serial.printf("candidate=%u name=%s address=%s type=%u rssi=%d\n",i,v.meters[i].name,v.meters[i].address,v.meters[i].addressType,v.meters[i].rssi);
    Serial.printf("message=%s\n",v.message);
    Serial.printf("panel_message=%s\n",panel.diagnostic);
    Serial.printf("touch_errors=%lu\n",(unsigned long)panel.touchErrors);
#ifdef UT_METER_JC3248
    panel.printTouchStatus();
    meter_calibration::printStatus();
#endif
#ifdef UT_METER_LCD349
    panel.printTouchStatus();
#endif
    Serial.printf("loop_stack_free=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    const auto digits=meter_numerals::stats();Serial.printf("numerals=outline render_us=%lu cache_bytes=%u peak_bytes=%u misses=%u failures=%u font=%s\n",(unsigned long)lastRenderUs,unsigned(digits.bytes),unsigned(digits.peakBytes),digits.misses,digits.failures,meter_numerals::fontSha());
    auto *history=trends[v.activeSlot][v.reading.channel];
    Serial.printf("view=%s trend_samples=%u scroll=%d scroll_max=%d slot=%u display_count=%u display_saved=%u\n",meter_touch::isOpen()?"functions":fullScreen?(scopeView?"scope":"reading"):networkPage?"network":"setup",history?unsigned(history->size()):0,setupScroll,maxSetupScroll(),v.activeSlot,meter_display::count(),meter_display::saved());
    Serial.printf("screen_rotation=%u rotation_saved=%u\n",meter_display::rotation(),meter_display::rotationSaved());
    auto net=meter_web::network();Serial.printf("web_ssid=%s web_password=%s web_ap=%s web_sta=%s\n",net.apSsid.c_str(),net.apPassword.c_str(),net.apIp.c_str(),net.staIp.c_str());
    if(v.hasReading)Serial.printf("reading=%s value=%.9g unit=%s mode=%u range=%u channel=%u numeric=%u fresh=%u age_ms=%lu flags=%02x,%02x,%02x\n",
        v.reading.text,v.reading.value,ut_meter::unit(v.reading),v.reading.mode,v.reading.range,v.reading.channel,v.reading.numeric,fresh(v),
        (unsigned long)(millis()-v.readingAt),v.reading.flags[0],v.reading.flags[1],v.reading.flags[2]);
}
static void emitScreen(bool redraw){
    if(!panel.getBuffer())return;
    if(redraw)draw();

        size_t bytes=size_t(panel.width())*panel.height()*2;
        // Emit logical orientation: a rotated canvas's raw storage still
        // has portrait row order, so capture it through getPixel().
        uint16_t *row=new(std::nothrow) uint16_t[panel.width()];
        if(!row){Serial.println("ERROR screen row allocation failed");return;}
        Serial.printf("RGB565 %d %d %u\n",panel.width(),panel.height(),unsigned(bytes));
        for(int y=0;y<panel.height();++y) {
            for(int x=0;x<panel.width();++x)row[x]=panel.getPixel(x,y);
            Serial.write(reinterpret_cast<uint8_t *>(row),panel.width()*2);
        }
        delete[] row;Serial.println();

}
static void command(const String &line) {
    if(meter_calibration::command(line,panel)){drawAt=0;return;}
    if(meter_calibration::active()&&(line.startsWith("tap ")||line.startsWith("swipe ")||line.startsWith("view ")||line.startsWith("ui action ")||line.startsWith("ui tap ")||line.startsWith("ui text ")||line=="ui done"||line=="ui back")){Serial.println("Calibration active: finish the targets or use touch cancel");return;}
    const bool menuWasOpen=meter_touch::isOpen();
    if(meter_touch::command(line)){if(meter_touch::isOpen()&&(!menuWasOpen||line.startsWith("view ")||line=="ui open")){fullScreen=false;autoReader=false;}drawAt=0;return;}
    unsigned index=0,tapY=0,endX=0,endY=0;int delta=0;
    if(line=="status")status();
    else if(line=="web status")meter_web::printDiagnostics();
#ifdef UT_METER_LCD349
    else if(line=="touch status")panel.printTouchStatus();
    else if(line=="board status"){JsonDocument devices;meter_board::writeStatus(devices.to<JsonObject>());serializeJson(devices,Serial);Serial.println();}
#endif
#ifdef UT_METER_JC3248
    else if(line=="touch status")panel.printTouchStatus();
#endif
    else if(line=="i2c") {
        Serial.print("i2c_addresses=");
        for(uint8_t address=1;address<127;++address) {
            Wire.beginTransmission(address);
            if(Wire.endTransmission()==0)Serial.printf("%02x ",address);
        }
        Serial.println();
    }
#ifdef UT_METER_AMOLED
    else if(line=="identify") {
        if(panelOK)Serial.println("board_probe=round AMOLED initialized");
        else {
            // Only probe alternate wiring while no panel/touch driver is
            // active. Restore the round board bus before returning.
            Wire.end();Wire.begin(42,41,400000);Wire.setTimeOut(20);
            Wire.beginTransmission(0x63);
            bool lcdTouch=Wire.endTransmission()==0;
            Wire.end();Wire.begin(15,14,400000);Wire.setTimeOut(20);
            Wire.beginTransmission(0x34);
            bool roundPower=Wire.endTransmission()==0;
            Wire.beginTransmission(0x5a);
            bool roundTouch=Wire.endTransmission()==0;
            Serial.printf("board_probe=lcd147_touch63:%u amoled175_power34:%u amoled175_touch5a:%u\n",lcdTouch,roundPower,roundTouch);
        }
    }
#endif
    else if(line=="screen")emitScreen(true);
    else if(line=="scope screen separate"){
        panel.setRotation(ui.reading.rotation);panel.fillScreen(BG);
        drawScopeOverview(0,meter_display::count(),millis(),true);
        text(8,panel.height()-18,"EXAMPLE DATA / UI SELFTEST",1,WARN);panel.flush();emitScreen(false);drawAt=0;
    }
    else if(line.startsWith("values screen ")){
        const String name=line.substring(14);meter_ui::ValuePreset preset;
        if(!meter_ui::parseValuePreset(name.c_str(),name.length(),preset)){Serial.println("ERROR value preset");return;}
        const unsigned count=meter_ui::valuePresetCount(preset,meter_display::count());const uint32_t began=micros();
        panel.setRotation(ui.reading.rotation);panel.fillScreen(BG);
        meter_value_view::draw(panel,readingTransform().rect(ui.overview.values),preset,0,count,millis(),readingTransform().yLength(ui.overview.gap),true);
        text(8,panel.height()-18,"EXAMPLE DATA / UI SELFTEST",1,WARN);panel.flush();
        const uint32_t render=micros()-began;emitScreen(false);Serial.printf("values_selftest=1 example=1 render_us=%lu\n",(unsigned long)render);drawAt=0;
    }
    else if(line=="view reading"){meter_touch::close();touchGesture=ut_meter::TouchGesture();fullScreen=true;scopeView=false;autoReader=false;}
    else if(line=="view scope"){meter_touch::close();touchGesture=ut_meter::TouchGesture();fullScreen=true;scopeView=true;autoReader=false;}
    else if(line=="view setup"){meter_touch::close();touchGesture=ut_meter::TouchGesture();fullScreen=false;networkPage=false;autoReader=false;}
    else if(line=="view network"){meter_touch::close();touchGesture=ut_meter::TouchGesture();fullScreen=false;networkPage=true;autoReader=false;}
    else if(sscanf(line.c_str(),"slot %u",&index)==1)meter_ble::focus(index);
    else if(line.startsWith("profile "))meter_ble::setProfile(meter_ble::activeSlot(),line.substring(8).c_str());
    else if(sscanf(line.c_str(),"tap %u %u",&index,&tapY)==2){
        tap(index,tapY);
        Serial.printf("tap_view=%s\n",fullScreen?(scopeView?"scope":"reading"):"setup");
    }
    else if(sscanf(line.c_str(),"swipe %u %u %u %u",&index,&tapY,&endX,&endY)==4) {
        contact(index,tapY,true);
        for(int i=1;i<=8;++i)contact(int(index)+(int(endX)-int(index))*i/8,int(tapY)+(int(endY)-int(tapY))*i/8,true);
        contact(endX,endY,false);Serial.printf("scroll=%d\n",setupScroll);
    }
    else if(sscanf(line.c_str(),"scroll %d",&delta)==1)scrollSetup(delta);
    else if(line=="scan")action(0);
    else if(line=="next")action(1);
    else if(line=="disconnect")meter_ble::disconnect();
    else if(line=="model ut60bt")meter_ble::setModel(ut_meter::Model::UT60BT);
    else if(line=="model ut61plus")meter_ble::setModel(ut_meter::Model::UT61Plus);
    else if(sscanf(line.c_str(),"connect %u",&index)==1) {
        if(index<meter_ble::view().count){selected=index;autoReader=true;meter_ble::connect(index);}
        else Serial.println("ERROR candidate index");
    } else Serial.println("Commands: status, scan, next, connect <index>, disconnect, model ut60bt|ut61plus, view reading|scope|setup, tap x y, swipe x y x y, scroll pixels, screen");
    drawAt=0;
}
void setup() {
    Serial.begin(115200);panelOK=panel.begin();panel.brightness(55);
    meter_calibration::begin(panel);meter_display::begin();meter_values::begin();meter_ble::begin();meter_web::begin(meter_api::provider());meter_touch::begin();applyOrientation();draw();status();
}
void loop() {
    static bool physicalContact=false;
    static String line;
    while(Serial.available()) {
        char c=char(Serial.read());
        if(c=='\n'){command(line);line="";}
        else if(c!='\r') {if(line.length()<8192)line+=c;else line="";}
    }
    applyOrientation();
    uint16_t x=0,y=0;bool down=false;
    if(meter_calibration::tick(panel))drawAt=0;
    if(meter_calibration::takeContactReset()){touchGesture=ut_meter::TouchGesture();meter_touch::resetContact();suppressTouchUntilUp=true;}
    if(panel.touch(x,y,down)){
        physicalContact=down;
        if(down&&meter_display::touch(millis())){
            touchGesture=ut_meter::TouchGesture();meter_touch::resetContact();suppressTouchUntilUp=true;
        }
        if(meter_calibration::active())meter_calibration::contact(panel,down);
        else if(suppressTouchUntilUp){if(!down)suppressTouchUntilUp=false;}
        else contact(x,y,down);
    }
    meter_ble::Event event;
    for(unsigned i=0;i<64 && meter_ble::poll(event);++i) {
        meter_touch::sample(event);
        if(event.slot<meter_ble::MAX_CONNECTIONS && event.reading.channel<meter_ble::MAX_CHANNELS) {
            if(strcmp(trendSources[event.slot],event.address)||strcmp(trendProfiles[event.slot],event.profile)) {
                for(auto *history:trends[event.slot])if(history)history->reset();
                memcpy(trendSources[event.slot],event.address,sizeof(event.address));
                memcpy(trendProfiles[event.slot],event.profile,sizeof(event.profile));
            }
            auto r=meter_ble::displayReading(event.reading);
            unsigned profile=0;for(;profile<dmm::profileCount();++profile)if(!strcmp(dmm::profileAt(profile)->id,event.profile))break;
            auto *&history=trends[event.slot][event.reading.channel];
            if(!history){void *memory=psramFound()?ps_malloc(sizeof(ut_meter::Trend)):malloc(sizeof(ut_meter::Trend));if(memory)history=new(memory)ut_meter::Trend();}
            if(history)history->push(r,static_cast<ut_meter::Model>(profile+2),event.at);
        }
        meter_web::Sample sample;sample.meter=event.slot;sample.channel=event.reading.channel;sample.packet=event.packet;
        sample.modeKey=event.reading.modeKey;sample.flags=event.reading.flags;sample.value=event.reading.value;sample.numeric=event.reading.numeric;
        sample.uptimeMs=uint64_t(esp_timer_get_time()/1000)-uint32_t(millis()-event.at);
        memcpy(sample.address,event.address,sizeof(sample.address));memcpy(sample.profile,event.profile,sizeof(sample.profile));
        memcpy(sample.text,event.reading.text,sizeof(sample.text));memcpy(sample.unit,event.reading.unit,sizeof(sample.unit));memcpy(sample.mode,event.reading.mode,sizeof(sample.mode));
        meter_web::publish(sample);
    }
    meter_web::tick();
    const bool motionAllowed=!physicalContact&&!meter_calibration::busy()&&!meter_touch::autoRotationBlocked();
    meter_board::tick(motionAllowed);
    unsigned motionRotation=4;
    if(motionAllowed&&meter_board::takeRotation(motionRotation))meter_display::requestMotionRotation(motionRotation);
    meter_touch::tick();
    if(meter_display::tick(!meter_calibration::busy())){applyOrientation();drawAt=0;}
    if(meter_values::tick())drawAt=0;
    static unsigned appliedBrightness=55;
    const unsigned wantedBrightness=meter_display::lightTick(millis(),meter_calibration::active());
    if(wantedBrightness!=appliedBrightness){panel.brightness(wantedBrightness);appliedBrightness=wantedBrightness;}
    if(meter_touch::takeDrawRequest())drawAt=0;
    if(meter_touch::takeClearScopeRequest()){const unsigned slot=meter_ble::activeSlot();const auto current=meter_ble::displaySnapshot(slot,meter_touch::scopeChannel(slot));auto *history=trends[slot][current.channel];if(history)history->reset();drawAt=0;}
    if(meter_touch::takeValuesRequest()){fullScreen=true;scopeView=false;autoReader=false;drawAt=0;}
    if(meter_touch::takeScopeRequest()){fullScreen=true;scopeView=true;autoReader=false;drawAt=0;}
    const auto snapshot=meter_ble::displaySnapshot(meter_ble::activeSlot());
    if(autoReader && !meter_touch::isOpen() && fresh(snapshot)){fullScreen=true;autoReader=false;drawAt=0;}
    if(int32_t(millis()-drawAt)>=0){draw();drawAt=millis()+300;}
    delay(5);
}
