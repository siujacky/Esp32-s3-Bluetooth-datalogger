#pragma once
#include <stdint.h>
#include <algorithm>
#include "meter_display_count.h"

// Pure C++ geometry. Coordinates in Map are authored against each view's
// reference canvas, then resolved once to the actual rotated display canvas.
namespace meter_ui {
// Normalize controller axes first, then use the same GFX rotation as the view.
// Release packets bypass this helper in each backend and preserve position.
inline bool mapTouch(uint16_t rawX,uint16_t rawY,unsigned width,unsigned height,unsigned rotation,
                     bool mirrorX,bool mirrorY,bool swapXY,uint16_t &outX,uint16_t &outY){
    if(!width||!height||width>4096||height>4096||rotation>3)return false;
    if(swapXY)std::swap(rawX,rawY);
    if(rawX>=width||rawY>=height)return false;
    unsigned x=mirrorX?width-1-rawX:rawX,y=mirrorY?height-1-rawY:rawY;
    switch(rotation){case 1:outX=y;outY=width-1-x;break;
    case 2:outX=width-1-x;outY=height-1-y;break;
    case 3:outX=height-1-y;outY=x;break;
    default:outX=x;outY=y;break;}
    return true;
}
struct Rect {int x,y,w,h;bool contains(int px,int py)const{return w>0&&h>0&&px>=x&&py>=y&&px<x+w&&py<y+h;}};
inline Rect stackedLane(Rect area,unsigned count,unsigned index,int gap){
    if(!validDisplayCount(count)||index>=count||area.w<=0||gap<0||int64_t(area.h)<=int64_t(gap)*(count-1))return {0,0,0,0};
    const int available=area.h-gap*int(count-1),top=available*int(index)/int(count)+int(index)*gap;
    return {area.x,area.y+top,area.w,available*int(index+1)/int(count)-available*int(index)/int(count)};
}
inline Rect scopeLane(Rect area,unsigned count,unsigned index,int gap){
 if(count!=4)return stackedLane(area,count,index,gap);
 if(index>=4||area.w<=gap||area.h<=gap||gap<0)return {};
 const int w=(area.w-gap)/2,h=(area.h-gap)/2;
 return {area.x+int(index%2)*(w+gap),area.y+int(index/2)*(h+gap),index%2?area.w-w-gap:w,index/2?area.h-h-gap:h};
}
inline Rect intersect(Rect a,Rect b){const int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
struct Label {int x,y,size,align;};
struct View {int rotation,reference_width,reference_height;bool safe_circle;int safe_margin;};
struct ValueSpec {int baseline,max_width,size_x,size_y;};
struct ScopeSpec {Rect plot;Label value,status,mode,slot;};
struct ReadingSpec {int rotation,reference_width,reference_height;bool safe_circle;int safe_margin;ValueSpec value;Label profile,status,flags,unit,slot;ScopeSpec scope;};
struct SetupRow {int x,width,height,step,font_size,columns,column_gap;};
struct SetupInfo {Label state,title,address,model,rssi,message,message_second;};
struct NetworkSpec {Rect qr;Label heading,ssid,password_caption,password,ip_caption,ip,lan;};
struct SetupSpec {int rotation,reference_width,reference_height;bool safe_circle;int safe_margin;Rect viewport,header;SetupRow row;int info_height,header_font_size;Label footer;SetupInfo info;NetworkSpec network;};
struct AdvancedRow {int step,gap,title_size,detail_size,info_size,padding,title_y,detail_y;};
struct KeyboardSpec {int preferred_columns,minimum_key_width,minimum_key_height,gap,caption_height,text_height,control_height,footer_height,font_size;};
struct GridSpec {int columns,column_gap,step,height;};
struct AdvancedSpec {int rotation,reference_width,reference_height;bool safe_circle;int safe_margin;Rect viewport,back,up,down;Label header,message;AdvancedRow row;KeyboardSpec keyboard;GridSpec home_grid;};
struct OverviewSpec {Rect values,scope,count_button;bool show_count_button;int gap;uint16_t slot0_color,slot1_color,slot2_color,slot3_color;};
struct Map {ReadingSpec reading;SetupSpec setup;AdvancedSpec advanced;OverviewSpec overview;};
template<class T> View view(const T &v){return {v.rotation,v.reference_width,v.reference_height,v.safe_circle,v.safe_margin};}
struct Transform {
    View reference;int width,height;
    Transform(View v,int w,int h):reference(v),width(w),height(h){}
    int x(int n)const{return int(int64_t(n)*width/reference.reference_width);}
    int y(int n)const{return int(int64_t(n)*height/reference.reference_height);}
    int xLength(int n)const{return n>0?std::max(1,x(n)):0;}
    int yLength(int n)const{return n>0?std::max(1,y(n)):0;}
    int font(int n)const{return n<=0?0:std::max(1,std::min(8,int(int64_t(n)*std::min(width*1024/reference.reference_width,height*1024/reference.reference_height)/1024)));}
    Rect rect(Rect r)const{return {x(r.x),y(r.y),x(r.x+r.w)-x(r.x),y(r.y+r.h)-y(r.y)};}
    Label label(Label p)const{return {x(p.x),y(p.y),font(p.size),p.align};}
    bool safe(Rect r)const{
        if(r.w<=0||r.h<=0||r.x<0||r.y<0||r.x+r.w>width||r.y+r.h>height)return false;
        if(!reference.safe_circle)return true;
        // Use doubled coordinates so odd screen sizes retain their half-pixel centre.
        const int64_t radius=std::min(width,height)-2*std::max(xLength(reference.safe_margin),yLength(reference.safe_margin));
        for(int a=0;a<2;++a)for(int b=0;b<2;++b){const int64_t dx=2*(r.x+a*r.w)-width,dy=2*(r.y+b*r.h)-height;if(dx*dx+dy*dy>radius*radius)return false;}
        return true;
    }
};
inline int scrollOffset(int current,int delta,int content,int viewport){const int maximum=std::max(0,content-viewport);const int64_t next=int64_t(current)+delta;return next<0?0:next>maximum?maximum:int(next);}
struct RowLayout {
    Rect viewport;int step,gap;
    RowLayout(Rect area,int stride,int spacing):viewport(area),step(std::max(1,stride)),gap(std::max(0,std::min(spacing,stride-1))){}
    int contentHeight(int count)const{return count>0?count*step-gap:0;}
    Rect rect(int index,int scroll)const{return {viewport.x,viewport.y+index*step-scroll,viewport.w,step-gap};}
    Rect visible(int index,int scroll)const{return intersect(rect(index,scroll),viewport);}
    int indexAt(int x,int y,int scroll,int count)const{
        if(!viewport.contains(x,y))return -1;
        const int64_t position=int64_t(y)-viewport.y+scroll;
        const int index=int(position/step);
        return position>=0&&index>=0&&index<count&&rect(index,scroll).contains(x,y)?index:-1;
    }
};
struct GridLayout {
    Rect viewport;int columns,columnGap,step,height,topInset;
    GridLayout(Rect area,int cols,int gap,int stride,int cardHeight,int inset=0):viewport(area),
        columns(std::max(1,std::min(16,std::min(cols,std::max(1,area.w))))),
        columnGap(std::max(0,gap)),step(std::max(1,stride)),height(std::max(1,std::min(cardHeight,std::max(1,stride)))),topInset(std::max(0,inset)){
        if(columns>1)columnGap=std::min(columnGap,std::max(0,(area.w-columns)/(columns-1)));else columnGap=0;
    }
    Rect rect(int index,int scroll)const{
        const int col=index%columns,row=index/columns,available=std::max(0,viewport.w-(columns-1)*columnGap);
        const int left=viewport.x+available*col/columns+col*columnGap;
        const int right=viewport.x+available*(col+1)/columns+col*columnGap;
        return {left,viewport.y+topInset+row*step-scroll,right-left,height};
    }
    Rect visible(int index,int scroll)const{return intersect(rect(index,scroll),viewport);}
    int contentHeight(int count)const{return count>0?topInset+((count-1)/columns)*step+height:topInset;}
    int indexAt(int x,int y,int scroll,int count)const{
        if(!viewport.contains(x,y))return -1;
        const int64_t pos=int64_t(y)-viewport.y+scroll-topInset;
        if(pos<0||pos/step>count)return -1;
        const int64_t first=(pos/step)*columns;
        for(int col=0;col<columns;++col){const int64_t i=first+col;if(i>=0&&i<count&&rect(int(i),scroll).contains(x,y))return int(i);}
        return -1;
    }
};
struct Keyboard {
    Rect viewport;KeyboardSpec spec;int columns,rows,controlsY,controlHeight,keyTop,keyHeight,footerY,footerHeight;bool valid;
    Keyboard(Rect area,KeyboardSpec settings):viewport(area),spec(settings),columns(0),rows(0),controlsY(0),controlHeight(0),keyTop(0),keyHeight(0),footerY(0),footerHeight(0),valid(false){
        const int candidates[]={spec.preferred_columns,8,4,16,2,1};
        for(unsigned i=0;i<sizeof(candidates)/sizeof(candidates[0]);++i){
            const int cols=candidates[i];if(cols<1||16%cols||area.w/cols-spec.gap<spec.minimum_key_width||area.w/4-spec.gap<spec.minimum_key_width)continue;
            const int r=16/cols,remaining=area.h-spec.caption_height-spec.text_height-spec.gap*3;
            const int minimum=std::max(spec.minimum_key_height,spec.control_height)+std::max(spec.minimum_key_height,spec.footer_height)+r*(spec.minimum_key_height+spec.gap);
            if(remaining<minimum)continue;
            columns=cols;rows=r;controlsY=area.y+spec.caption_height+spec.text_height;
            controlHeight=std::max(spec.minimum_key_height,spec.control_height);footerHeight=std::max(spec.minimum_key_height,spec.footer_height);
            keyTop=controlsY+controlHeight+spec.gap;keyHeight=(remaining-controlHeight-footerHeight)/r;
            footerY=keyTop+r*keyHeight+spec.gap;valid=true;break;
        }
    }
    Rect cell(int index,int count,int top,int height)const{const int a=viewport.x+viewport.w*index/count,b=viewport.x+viewport.w*(index+1)/count;return {a,top,b-a-spec.gap,height};}
    Rect control(int index)const{return valid?cell(index,4,controlsY,controlHeight):Rect{0,0,0,0};}
    Rect key(int index)const{return valid?cell(index%columns,columns,keyTop+index/columns*keyHeight,keyHeight-spec.gap):Rect{0,0,0,0};}
    Rect footer(int index)const{return valid?cell(index,4,footerY,footerHeight):Rect{0,0,0,0};}
    // 0..3 control, 4..19 printable key, 20..23 footer. Gaps are inert.
    int hit(int x,int y)const{if(!valid)return -1;for(int i=0;i<4;++i)if(control(i).contains(x,y))return i;for(int i=0;i<16;++i)if(key(i).contains(x,y))return 4+i;for(int i=0;i<4;++i)if(footer(i).contains(x,y))return 20+i;return -1;}
};
inline KeyboardSpec scaledKeyboard(KeyboardSpec s,const Transform &t){s.minimum_key_width=t.xLength(s.minimum_key_width);s.minimum_key_height=t.yLength(s.minimum_key_height);s.gap=std::max(1,std::min(t.xLength(s.gap),t.yLength(s.gap)));s.caption_height=t.yLength(s.caption_height);s.text_height=t.yLength(s.text_height);s.control_height=t.yLength(s.control_height);s.footer_height=t.yLength(s.footer_height);s.font_size=t.font(s.font_size);return s;}
}
