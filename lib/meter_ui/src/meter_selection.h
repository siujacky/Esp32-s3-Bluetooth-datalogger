#pragma once
#include "meter_ui.h"
namespace meter_ui {
struct SelectionLayout {bool valid=false;Rect box{},header{},cancel{},list{},up{},down{};unsigned visible=0,columns=1;int rowHeight=44,gap=0;bool scrollable=false;
 Rect row(unsigned visibleIndex)const{return valid&&visibleIndex<visible?Rect{list.x+int(visibleIndex%columns)*((list.w-gap*int(columns-1))/int(columns)+gap),list.y+int(visibleIndex/columns)*rowHeight,(list.w-gap*int(columns-1))/int(columns),rowHeight-gap}:Rect{};}
 int indexAt(int x,int y)const{if(!valid||!list.contains(x,y))return -1;const int cell=(list.w-gap*int(columns-1))/int(columns),column=(x-list.x)/(cell+gap),index=(y-list.y)/rowHeight*int(columns)+column;return column<int(columns)&&index<int(visible)&&row(unsigned(index)).contains(x,y)?index:-1;}
};
inline SelectionLayout selectionLayout(Rect area,unsigned count){SelectionLayout out;if(!count||count>64||area.w<132||area.h<108)return out;const int header=area.h>=220||area.w>=280?44:32,controls=area.h>=220?44:32;unsigned capacity=unsigned((area.h-header)/44);out.scrollable=count>std::min(5u,capacity);out.visible=out.scrollable?std::min(5u,unsigned((area.h-header-controls)/44)):count;if(!out.visible)return out;const int height=header+int(out.visible)*44+(out.scrollable?controls:0);if(height>area.h)return out;out.box={area.x,area.y+(area.h-height)/2,area.w,height};out.header={out.box.x,out.box.y,out.box.w,header};out.cancel={out.box.x+out.box.w-(area.w>=280?104:76),out.box.y,area.w>=280?104:76,header};out.list={out.box.x,out.box.y+header,out.box.w,int(out.visible)*44};if(out.scrollable){out.up={out.box.x,out.list.y+out.list.h,out.box.w/2,controls};out.down={out.up.x+out.up.w,out.up.y,out.box.w-out.up.w,controls};}out.valid=true;return out;}
inline SelectionLayout visualSelectionLayout(Rect area,unsigned count){
 SelectionLayout out;if(!count||count>64||area.w<132||area.h<108)return out;
 out.columns=area.w>=360?2:1;out.rowHeight=out.columns==2?80:64;out.gap=4;
 const int header=44,controls=area.h>=220?44:32;
 unsigned capacity=unsigned(std::max(0,(area.h-header)/out.rowHeight));
 out.scrollable=count>capacity*out.columns;
 if(out.scrollable)capacity=unsigned(std::max(0,(area.h-header-controls)/out.rowHeight));
 out.visible=std::min(count,capacity*out.columns);if(!out.visible)return out;
 const int rows=int((out.visible+out.columns-1)/out.columns),height=header+rows*out.rowHeight+(out.scrollable?controls:0);
 out.box={area.x,area.y+(area.h-height)/2,area.w,height};out.header={out.box.x,out.box.y,out.box.w,header};
 const int cancel=area.w>=280?104:76;out.cancel={out.box.x+out.box.w-cancel,out.box.y,cancel,header};
 out.list={out.box.x,out.box.y+header,out.box.w,rows*out.rowHeight};
 if(out.scrollable){out.up={out.box.x,out.list.y+out.list.h,out.box.w/2,controls};out.down={out.up.x+out.up.w,out.up.y,out.box.w-out.up.w,controls};}
 out.valid=true;return out;
}
class SelectionState {bool open_=false;unsigned count_=0,visible_=0;int current_=-1,pixels_=0,rowHeight_=44;unsigned columns_=1;
public:bool begin(unsigned count,int current,unsigned visible,int rowHeight=44,unsigned columns=1){if(!count||count>64||!visible||rowHeight<1||!columns){open_=false;return false;}count_=count;visible_=std::min(visible,count);rowHeight_=rowHeight;columns_=columns;current_=current>=0&&unsigned(current)<count?current:-1;pixels_=current_>=int(visible_)?(current_/int(columns_)-int((visible_+columns_-1)/columns_)+1)*rowHeight_:0;open_=true;return true;}
 bool isOpen()const{return open_;}int current()const{return current_;}unsigned first()const{return unsigned(pixels_/rowHeight_)*columns_;}void cancel(){open_=false;}
 void scrollBy(int delta){const int64_t next=int64_t(pixels_)+delta,maximum=int64_t((count_+columns_-1)/columns_-(visible_+columns_-1)/columns_)*rowHeight_;pixels_=int(std::max(int64_t(0),std::min(maximum,next)));}
 bool chooseVisible(unsigned row,int &out){if(!open_||row>=visible_||first()+row>=count_)return false;out=int(first()+row);open_=false;return true;}
};
}
