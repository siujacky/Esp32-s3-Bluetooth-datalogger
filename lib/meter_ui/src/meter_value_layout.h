#pragma once
#include "meter_ui.h"
#include <cstring>
namespace meter_ui {
enum class ValuePreset:uint8_t {Solo,Equal,LargeSmall,LargeSmallSmall,LargeLargeSmall,Grid};
enum class TileRole:uint8_t {Main,Large,Small,Equal};
struct ValueTile {Rect bounds{};TileRole role=TileRole::Small;unsigned sourceIndex=0;};
struct ValueTiles {ValueTile tiles[ut_meter::METER_SLOTS]{};unsigned count=0;bool valid=false;};
inline bool validValuePreset(ValuePreset p,unsigned count){
    if(!validDisplayCount(count))return false;
    switch(p){case ValuePreset::Solo:return count==1;case ValuePreset::Equal:return count==2||count==3;case ValuePreset::LargeSmall:return count==2;case ValuePreset::LargeSmallSmall:case ValuePreset::LargeLargeSmall:return count==3;case ValuePreset::Grid:return count==4;}return false;
}
inline const char *valuePresetName(ValuePreset p){static const char *names[]={"solo","equal","large-small","large-small-small","large-large-small","grid"};return unsigned(p)<6?names[unsigned(p)]:"invalid";}
inline bool parseValuePreset(const char *s,size_t size,ValuePreset &p){if(!s)return false;for(unsigned i=0;i<6;++i){const char *name=valuePresetName(ValuePreset(i));if(std::strlen(name)==size&&!std::memcmp(s,name,size)){p=ValuePreset(i);return true;}}return false;}
inline unsigned valuePresetCount(ValuePreset p,unsigned equalCount=3){return p==ValuePreset::Grid?4:p==ValuePreset::Solo?1:p==ValuePreset::LargeSmall?2:p==ValuePreset::Equal?(equalCount==3?3:2):3;}
inline ValueTiles valueTiles(Rect area,ValuePreset preset,unsigned count,int gap){
    ValueTiles out;
    if(!validValuePreset(preset,count)||area.x<0||area.y<0||area.w<=0||area.h<=0||area.w>8192||area.h>8192||gap<0||gap>=area.w||gap>=area.h)return out;
    const auto set=[&](unsigned i,Rect r,TileRole role){out.tiles[i].bounds=r;out.tiles[i].role=role;out.tiles[i].sourceIndex=i;};
    if(preset==ValuePreset::Grid){const int left=(area.w-gap)/2,top=(area.h-gap)/2;
        for(unsigned i=0;i<4;++i){const unsigned col=i%2,row=i/2;set(i,{area.x+int(col)*(left+gap),area.y+int(row)*(top+gap),col?area.w-left-gap:left,row?area.h-top-gap:top},TileRole::Equal);}}
    else if(preset==ValuePreset::Solo)set(0,area,TileRole::Main);
    else if(preset==ValuePreset::Equal){for(unsigned i=0;i<count;++i)set(i,stackedLane(area,count,i,gap),TileRole::Equal);}
    else if(area.w>=400){
        if(preset==ValuePreset::LargeLargeSmall){const int top=(area.h-gap)*7/10,left=(area.w-gap)/2;set(0,{area.x,area.y,left,top},TileRole::Large);set(1,{area.x+left+gap,area.y,area.w-left-gap,top},TileRole::Large);set(2,{area.x,area.y+top+gap,area.w,area.h-top-gap},TileRole::Small);}
        else {const int main=(area.w-gap)*2/3,right=area.w-main-gap;set(0,{area.x,area.y,main,area.h},TileRole::Large);if(count==2)set(1,{area.x+main+gap,area.y,right,area.h},TileRole::Small);else {const int top=(area.h-gap)/2;set(1,{area.x+main+gap,area.y,right,top},TileRole::Small);set(2,{area.x+main+gap,area.y+top+gap,right,area.h-top-gap},TileRole::Small);}}
    }else{
        const int available=area.h-gap*int(count-1);if(available<=0)return out;
        int heights[3]={};
        if(count==2){heights[0]=area.h<200?available*5/8:available*29/45;heights[1]=available-heights[0];}
        else if(preset==ValuePreset::LargeSmallSmall){heights[1]=heights[2]=area.h<200?available*7/26:(available+2)/4;heights[0]=available-heights[1]-heights[2];}
        else {heights[0]=heights[1]=area.h<200?available*9/26:available*48/131;heights[2]=available-heights[0]-heights[1];}
        int y=area.y;for(unsigned i=0;i<count;++i){set(i,{area.x,y,area.w,heights[i]},i==0||(preset==ValuePreset::LargeLargeSmall&&i==1)?TileRole::Large:TileRole::Small);y+=heights[i]+gap;}
    }
    for(unsigned i=0;i<count;++i)if(out.tiles[i].bounds.w<16||out.tiles[i].bounds.h<16)return ValueTiles{};
    out.count=count;out.valid=true;return out;
}
}
