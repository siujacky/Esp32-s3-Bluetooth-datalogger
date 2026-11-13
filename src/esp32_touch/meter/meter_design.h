#pragma once
#include "../panel.h"
#include <meter_ui.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

// Presentation only: actions and hit regions stay in the mapped UI geometry.
namespace meter_design {
constexpr bool modern=METER_THEME_STYLE==1;
constexpr uint16_t BG=METER_THEME_BACKGROUND,SURFACE=METER_THEME_SURFACE,
    RAISED=METER_THEME_RAISED,FG=METER_THEME_TEXT,MUTED=METER_THEME_MUTED,
    ACCENT=METER_THEME_ACCENT,WARN=METER_THEME_WARNING,BORDER=METER_THEME_BORDER;
enum class Icon {Scan,Next,Link,Model,Scope,Meter,Wifi,Settings,Record,Card,Trigger,Power,Events,Tools,Help,Web};
inline void frame(DeskPanel &p,meter_ui::Rect r,uint16_t fill=SURFACE,uint16_t outline=BORDER){
    const int radius=std::min(METER_THEME_RADIUS,std::min(r.w,r.h)/2);
    p.fillRoundRect(r.x,r.y,r.w,r.h,radius,fill);
    if(outline!=fill)p.drawRoundRect(r.x,r.y,r.w,r.h,radius,outline);
}
inline void heading(DeskPanel &p,meter_ui::Rect r,String value,uint16_t color=FG){
    if(r.w<=0||r.h<=0)return;
    p.setTextWrap(false);p.setTextSize(1);p.setFont(r.h>=26?&FreeSansBold12pt7b:&FreeSansBold9pt7b);
    int16_t bx=0,by=0;uint16_t width=0,height=0;
    auto measure=[&](){p.getTextBounds(value,0,0,&bx,&by,&width,&height);};measure();
    if(height>r.h){p.setFont(&FreeSansBold9pt7b);measure();}
    if(width>r.w){String base=value;while(base.length()){base.remove(base.length()-1);value=base+"..";measure();if(width<=r.w)break;}}
    p.setTextColor(color);p.setCursor(r.x-bx,r.y+(r.h-int(height))/2-by);p.print(value);p.setFont(nullptr);p.setTextSize(1);
}
inline void badge(DeskPanel &p,meter_ui::Rect r,const String &value,uint16_t color,uint8_t size=1){
    frame(p,r,RAISED,BORDER);p.setFont(nullptr);p.setTextSize(size);p.setTextColor(color);
    p.setCursor(r.x+(r.w-int(value.length())*6*size)/2,r.y+(r.h-8*size)/2);p.print(value);p.setTextSize(1);
}
inline void icon(DeskPanel &p,Icon kind,int cx,int cy,uint16_t color=ACCENT,int size=20){
    const int d=std::max(6,size/2),q=std::max(3,d/2);
    auto line=[&](int x,int y,int a,int b){p.drawLine(cx+x,cy+y,cx+a,cy+b,color);};
    switch(kind){
    case Icon::Scan:p.drawCircle(cx-2,cy-2,q+1,color);line(q-1,q-1,d,d);line(q,q-1,d+1,d);break;
    case Icon::Next:p.drawCircle(cx,cy,q+2,color);p.fillTriangle(cx+d,cy-q,cx+d,cy+q,cx+d+q,cy,color);break;
    case Icon::Link:p.drawRoundRect(cx-d,cy-q,d+3,2*q+1,q,color);p.drawRoundRect(cx-2,cy-q,d+3,2*q+1,q,color);line(-q,0,q,0);break;
    case Icon::Scope:line(-d,d,d,d);line(-d,-d,-d,d);line(-d+2,q,-q,-q);line(-q,-q,0,q);line(0,q,q,-d);line(q,-d,d,0);break;
    case Icon::Wifi:for(int i=0;i<3;++i){const int y=-d+i*q,w=d-i*q;line(-w,y+q,-w/2,y);line(-w/2,y,w/2,y);line(w/2,y,w,y+q);}p.fillCircle(cx,cy+d,2,color);break;
    case Icon::Record:p.drawCircle(cx,cy,d,color);p.fillCircle(cx,cy,q,color);break;
    case Icon::Card:p.drawRoundRect(cx-d+2,cy-d,2*d-4,2*d,2,color);for(int i=-1;i<=1;++i)line(i*4,-d+3,i*4,-q);line(-q,q,q,q);break;
    case Icon::Trigger:case Icon::Power:p.fillTriangle(cx+2,cy-d,cx-d+2,cy+2,cx+1,cy+2,color);p.fillTriangle(cx-1,cy-2,cx+d-2,cy-2,cx-2,cy+d,color);break;
    case Icon::Events:for(int i=-1;i<=1;++i){p.fillCircle(cx-d+2,cy+i*q,1,color);line(-q, i*q,d,i*q);}break;
    case Icon::Settings:case Icon::Tools:p.drawCircle(cx,cy,q+1,color);p.drawCircle(cx,cy,2,color);for(int i=-1;i<=1;i+=2){line(i*d,0,i*(q+1),0);line(0,i*d,0,i*(q+1));line(i*d,i*d,i*q,i*q);line(i*d,-i*d,i*q,-i*q);}break;
    case Icon::Help:p.drawCircle(cx,cy,d,color);line(-q,-q,0,-q-2);line(0,-q-2,q,-q);line(q,-q,q,0);line(q,0,0,2);p.fillCircle(cx,cy+q+2,1,color);break;
    case Icon::Web:p.drawCircle(cx,cy,d,color);p.drawCircle(cx,cy,q,color);line(-d,0,d,0);line(0,-d,0,d);break;
    case Icon::Meter:case Icon::Model:p.drawRoundRect(cx-d+2,cy-d,2*d-4,2*d,2,color);p.drawRect(cx-q,cy-d+3,2*q,q,color);p.drawCircle(cx,cy+q,q-1,color);break;
    }
}
}
