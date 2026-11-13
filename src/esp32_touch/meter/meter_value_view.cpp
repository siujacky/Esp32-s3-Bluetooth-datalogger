#include "meter_value_view.h"
#include "../panel.h"
#include "meter_value_settings.h"
#include "meter_touch_app.h"
#include "meter_design.h"
#include <meter_numerals.h>
#include <meter_ui_config.h>
#include <algorithm>
namespace meter_value_view {namespace {
using meter_ui::Rect;
String ascii(String s){s.replace("\xCE\xA9","ohm");s.replace("\xC2\xB5","u");s.replace("\xCE\xBC","u");s.replace("\xC2\xB0","deg");return s;}
void label(DeskPanel &p,int x,int y,const String &s,uint16_t color=meter_design::MUTED,unsigned size=1){p.setFont(nullptr);p.setTextSize(size);p.setTextColor(color);p.setCursor(x,y);p.print(ascii(s));}
String flags(const ut_meter::Reading &r){String out;if(r.flags[0]&2)out+="HOLD ";if(r.flags[0]&1)out+="REL ";if(r.flags[0]&4)out+="MIN ";if(r.flags[0]&8)out+="MAX ";if(r.flags[1]&2)out+="LOW BAT ";if(r.flags[1]&1)out+="CAUTION ";if(r.flags[1]&4)out+="MANUAL ";if(r.flags[2]&2)out+="P-MIN ";if(r.flags[2]&4)out+="P-MAX ";return out;}
const char *state(const meter_ble::DisplaySnapshot &v,uint32_t now){if(ut_meter::isFresh(v.state==meter_ble::State::Connected,v.hasReading,now,v.readingAt))return "LIVE";if(v.hasReading)return v.state==meter_ble::State::Connected?"STALE":"OFFLINE";switch(v.state){case meter_ble::State::Connecting:return "LINKING";case meter_ble::State::Retry:return "RETRY";case meter_ble::State::Connected:return "NO DATA";case meter_ble::State::Error:return "ERROR";default:return "NO LINK";}}
void wrapped(DeskPanel &p,Rect area,const String &input,uint16_t color){
    String rest=ascii(input);const unsigned columns=std::max(1,area.w/6),lines=std::max(0,area.h/8);
    for(unsigned line=0;line<lines&&rest.length();++line){unsigned split=std::min(columns,unsigned(rest.length()));if(rest.length()>columns){int space=rest.lastIndexOf(' ',columns);if(space>0)split=space;}
        String shown=rest.substring(0,split);if(line+1==lines&&rest.length()>split&&columns>=3){shown=rest.substring(0,columns-3)+"...";}
        label(p,area.x,area.y+line*8,shown,color);rest=rest.substring(split);rest.trim();
    }
}
meter_ble::DisplaySnapshot example(unsigned slot,uint32_t now){meter_ble::DisplaySnapshot v;v.state=meter_ble::State::Connected;v.hasReading=true;v.readingAt=now;v.reading.numeric=true;v.reading.hasMetadata=true;v.reading.value=slot==0?12.34:slot==1?1.25:slot==2?23.5:48.75;snprintf(v.reading.text,sizeof(v.reading.text),"%.2f",v.reading.value);snprintf(v.reading.unitLabel,sizeof(v.reading.unitLabel),"%s",slot==0?"V":slot==1?"A":slot==2?"degC":"Hz");snprintf(v.reading.modeLabel,sizeof(v.reading.modeLabel),"%s",slot==0?"DC voltage":slot==1?"DC current":slot==2?"Temperature":"Frequency");return v;}
}
meter_ble::DisplaySnapshot exampleReading(unsigned slot,uint32_t now){return example(slot,now);}
meter_ui::FormattedNumber formatted(const meter_ble::DisplaySnapshot &v,const meter_ui::NumberFormat &f){
    if(!v.hasReading)return meter_ui::formatNumber(0,f,false);
    if(v.reading.numeric)return meter_ui::formatNumber(v.reading.value,f);
    meter_ui::FormattedNumber out;if(!strcmp(v.reading.text,"OL"))strcpy(out.cells,"OL");return out;
}
bool textNumber(DeskPanel &panel,Rect area,const char *text,unsigned capacity,uint16_t color,unsigned maxEm){
    const auto metric=meter_numerals::metrics(area,capacity,maxEm);
    const meter_numerals::Surface surface={&panel,[](void *p,int x,int y)->uint16_t{return static_cast<DeskPanel*>(p)->getPixel(x,y);},[](void *p,int x,int y,uint16_t c){static_cast<DeskPanel*>(p)->drawPixel(x,y,c);}};
    return meter_numerals::paint(surface,metric,text,color);
}
bool number(DeskPanel &panel,Rect area,const meter_ui::FormattedNumber &value,const meter_ui::NumberFormat &format,uint16_t color,unsigned maxEm){return textNumber(panel,area,value.cells,meter_ui::numberCellCount(format),color,maxEm);}
void draw(DeskPanel &panel,Rect area,meter_ui::ValuePreset preset,unsigned focus,unsigned count,uint32_t now,int gap,bool examples){
    const auto layout=meter_ui::valueTiles(area,preset,count,gap);const auto prefs=meter_values::snapshot();
    const auto mapped=meter_ui::configuredMap().overview;const uint16_t colors[]={mapped.slot0_color,mapped.slot1_color,mapped.slot2_color,mapped.slot3_color};
    if(!layout.valid){label(panel,area.x,area.y,"Value layout cannot fit this viewport",meter_design::WARN);return;}
    for(unsigned i=0;i<count;++i){const unsigned slot=meter_ui::displaySlot(focus,i);const auto v=examples?example(slot,now):meter_ble::displaySnapshot(slot,meter_touch::scopeChannel(slot),now);
        const auto r=layout.tiles[i].bounds;const auto role=layout.tiles[i].role;const int pad=r.h<100?6:8;const auto &format=prefs.formats[slot][v.channel];const auto out=formatted(v,format);const char *status=examples?"EXAMPLE":state(v,now);
        const bool live=!strcmp(status,"LIVE")||examples;const uint16_t color=colors[slot];
        meter_design::frame(panel,r,meter_design::SURFACE,meter_design::BORDER);panel.fillRoundRect(r.x+1,r.y+6,3,r.h-12,1,color);
        label(panel,r.x+pad,r.y+4,String("M")+String(slot+1)+" / CH"+String(v.channel+1),color);
        label(panel,r.x+r.w-pad-int(strlen(status))*6,r.y+4,status,live?color:meter_design::WARN);
        const String unit=v.hasReading?ascii(ut_meter::unit(v.reading)):String("--");
        String metadata=v.hasReading?flags(v.reading):String();
        const bool raw=v.hasReading&&(out.state==meter_ui::NumberState::Overflow||(!v.reading.numeric&&strcmp(out.cells,"OL"))||out.state==meter_ui::NumberState::Invalid);
        if(raw&&out.state!=meter_ui::NumberState::Overflow)metadata+="RAW "+String(v.reading.text)+" "+unit+" / ";
        metadata+=v.hasReading?String(ut_meter::modeName(v.reading)):String("Waiting for reading");
        Rect valueArea{},metaArea{};unsigned maxEm=role==meter_ui::TileRole::Small?30:role==meter_ui::TileRole::Main?140:110;
        if(r.h<100){const int unitReserve=preset==meter_ui::ValuePreset::Grid?std::min(66,r.w/4):66;const int metaHeight=r.h>=75?24:16;valueArea={r.x+pad,r.y+14,std::max(8,r.w-2*pad-unitReserve-8),std::max(8,r.h-18-metaHeight)};metaArea={r.x+pad,r.y+r.h-metaHeight-2,r.w-2*pad,metaHeight};label(panel,r.x+r.w-pad-unitReserve,valueArea.y+(valueArea.h-8)/2,unit,color);}
        else {const int metaHeight=r.w<200?std::min(56,((r.h-72)/8)*8):32;const int unitHeight=role==meter_ui::TileRole::Small?8:16;valueArea={r.x+pad,r.y+20,r.w-2*pad,std::max(8,r.h-pad-20-unitHeight-7-metaHeight)};const int unitY=valueArea.y+valueArea.h+3;const int unitSize=unitHeight==16?2:1;label(panel,r.x+r.w-pad-int(unit.length())*6*unitSize,unitY,unit,color,unitSize);metaArea={r.x+pad,unitY+unitHeight+4,r.w-2*pad,metaHeight};}
        if(!number(panel,valueArea,out,format,live?meter_design::FG:meter_design::MUTED,maxEm)){label(panel,valueArea.x,valueArea.y,"FONT ERROR",meter_design::WARN);label(panel,valueArea.x,valueArea.y+9,v.hasReading?String(v.reading.text):String("--"),meter_design::FG);}
        // Reserve a complete original-value row even when currently in range.
        // Flags/mode cannot truncate an overflow reading or move its digit field.
        if(out.state==meter_ui::NumberState::Overflow){String original=v.reading.text;original.replace("e","E");textNumber(panel,{metaArea.x,metaArea.y,metaArea.w,8},original.c_str(),24,meter_design::WARN,12);}
        metaArea.y+=8;metaArea.h=std::max(0,metaArea.h-8);
        wrapped(panel,metaArea,metadata,meter_design::MUTED);
    }
}
}
