#include "meter_numerals.h"
#include "meter_numeral_font.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
#include <algorithm>
#include <csetjmp>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace {
meter_numerals::Stats counters;
size_t byteLimit=512*1024;
struct alignas(std::max_align_t) Allocation {size_t bytes;Allocation *previous,*next;};
Allocation *allocations=nullptr;
std::jmp_buf rasterEscape;bool rasterActive=false;
void *allocationFailed(){++counters.failures;if(rasterActive)std::longjmp(rasterEscape,1);return nullptr;}
void *fontAllocate(size_t size,void*){
    const size_t total=size+sizeof(Allocation);
    if(total<size||total>byteLimit||counters.bytes>byteLimit-total)return allocationFailed();
#ifdef ARDUINO
    void *p=heap_caps_aligned_alloc(alignof(Allocation),total,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p)p=heap_caps_aligned_alloc(alignof(Allocation),total,MALLOC_CAP_8BIT);
#else
    void *p=std::malloc(total);
#endif
    if(!p)return allocationFailed();
    auto *a=static_cast<Allocation*>(p);a->bytes=total;a->previous=nullptr;a->next=allocations;if(allocations)allocations->previous=a;allocations=a;counters.bytes+=total;counters.peakBytes=std::max(counters.peakBytes,counters.bytes);return a+1;
}
void fontFree(void *p,void*){if(!p)return;auto *a=static_cast<Allocation*>(p)-1;if(a->previous)a->previous->next=a->next;else allocations=a->next;if(a->next)a->next->previous=a->previous;counters.bytes-=a->bytes;std::free(a);}
}
#define STBTT_malloc(n,u) fontAllocate(n,u)
#define STBTT_free(p,u) fontFree(p,u)
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "stb_truetype.h"
#pragma GCC diagnostic pop
namespace meter_numerals {namespace {
stbtt_fontinfo face{};bool ready=false;int advanceUnits=0,unitTop=0,unitBottom=0;float unitScale=0;
const char supported[]="0123456789.+- OLVREe";
struct Glyph {int font=0,character=0,w=0,h=0,x=0,y=0;uint8_t *mask=nullptr;uint32_t used=0;};
Glyph cache[128];uint32_t clock=0;
bool begin(){
    if(ready)return true;
    if(!stbtt_InitFont(&face,METER_NUMERAL_FONT,stbtt_GetFontOffsetForIndex(METER_NUMERAL_FONT,0)))return false;
    int bearing=0;stbtt_GetCodepointHMetrics(&face,'0',&advanceUnits,&bearing);unitScale=stbtt_ScaleForMappingEmToPixels(&face,1);
    unitTop=INT_MAX;unitBottom=INT_MIN;
    for(char c:supported){if(!c||c==' ')continue;int x0,y0,x1,y1;if(stbtt_GetCodepointBox(&face,c,&x0,&y0,&x1,&y1)){unitTop=std::min(unitTop,-y1);unitBottom=std::max(unitBottom,-y0);}}
    ready=advanceUnits>0&&unitBottom>unitTop;return ready;
}
Glyph *glyph(int font,int character){
    for(auto &g:cache)if(g.mask&&g.font==font&&g.character==character){g.used=++clock;return &g;}
    if(!std::strchr(supported,character))return nullptr;
    Glyph *target=nullptr;for(auto &g:cache)if(!g.mask){target=&g;break;}
    if(!target){target=&cache[0];for(auto &g:cache)if(g.used<target->used)target=&g;fontFree(target->mask,nullptr);*target=Glyph{};}
    int bx0,by0,bx1,by1;stbtt_GetCodepointBitmapBox(&face,character,unitScale*font,unitScale*font,&bx0,&by0,&bx1,&by1);
    const size_t reserve=size_t(std::max(0,bx1-bx0))*size_t(std::max(0,by1-by0))+32768;
    while(counters.bytes&&reserve<=byteLimit&&counters.bytes>byteLimit-reserve){Glyph *oldest=nullptr;for(auto &g:cache)if(g.mask&&(!oldest||g.used<oldest->used))oldest=&g;if(!oldest)break;fontFree(oldest->mask,nullptr);*oldest=Glyph{};}
    Allocation *checkpoint=allocations;
    // stb has internal allocation assertions. Escape a failed allocation before
    // it can assert, then reclaim only this raster transaction's temporaries.
    // All frames crossed here are C/POD; no C++ destructors are bypassed.
    if(setjmp(rasterEscape)){rasterActive=false;while(allocations!=checkpoint)fontFree(allocations+1,nullptr);return nullptr;}
    const unsigned failed=counters.failures;const float scale=unitScale*font;int w=0,h=0,x=0,y=0;
    rasterActive=true;
    uint8_t *mask=stbtt_GetCodepointBitmap(&face,scale,scale,character,&w,&h,&x,&y);
    rasterActive=false;
    if(!mask||counters.failures!=failed){fontFree(mask,nullptr);return nullptr;}
    target->font=font;target->character=character;target->w=w;target->h=h;target->x=x;target->y=y;target->mask=mask;target->used=++clock;++counters.misses;return target;
}
}
Metrics metrics(meter_ui::Rect area,unsigned capacity,unsigned maxEm){
    Metrics m;if(!begin()||!capacity||capacity>32||area.x<0||area.y<0||area.w<=0||area.h<=0||!maxEm)return m;
    int size=std::min(int(std::min(maxEm,160u)),std::min(int(area.w/(capacity*advanceUnits*unitScale)),int(area.h/((unitBottom-unitTop)*unitScale))));
    for(;size>=4;--size){const float scale=size*unitScale;int top=INT_MAX,bottom=INT_MIN,left=0,right=0;
        for(char c:supported){if(!c||c==' ')continue;int x0,y0,x1,y1;stbtt_GetCodepointBitmapBox(&face,c,scale,scale,&x0,&y0,&x1,&y1);top=std::min(top,y0);bottom=std::max(bottom,y1);left=std::min(left,x0);right=std::max(right,x1);}
        const int cell=std::max(int(std::ceil(advanceUnits*scale)),right-left);
        if(cell*int(capacity)>area.w||bottom-top>area.h)continue;
        m.valid=true;m.area=area;m.capacity=capacity;m.fontPixels=size;m.cellWidth=cell;m.leftInset=-left;m.x=area.x+area.w-cell*int(capacity);m.top=top;m.bottom=bottom;m.baseline=area.y+(area.h-(bottom-top))/2-top;return m;
    }
    return m;
}
uint16_t blend565(uint16_t bg,uint16_t fg,uint8_t a){
    const unsigned inverse=255-a;
    const unsigned r=(((bg>>11)*inverse+(fg>>11)*a+127)/255),g=((((bg>>5)&63)*inverse+((fg>>5)&63)*a+127)/255),b=(((bg&31)*inverse+(fg&31)*a+127)/255);
    return uint16_t((r<<11)|(g<<5)|b);
}
bool paint(const Surface &surface,const Metrics &m,const char *text,uint16_t color){
    if(!m.valid||!text||!surface.read||!surface.write)return false;
    const size_t length=std::strlen(text);if(length>m.capacity)return false;
    // Rasterize every needed glyph before painting any pixels, so an allocation
    // failure is reported without inventing a partial or zero reading.
    for(const char *p=text;*p;++p)if(*p!=' '&&!glyph(m.fontPixels,uint8_t(*p)))return false;
    int x=m.x+int(m.capacity-length)*m.cellWidth;
    for(const char *p=text;*p;++p,x+=m.cellWidth){if(*p==' ')continue;const auto *g=glyph(m.fontPixels,uint8_t(*p));if(!g)return false;
        for(int row=0;row<g->h;++row)for(int col=0;col<g->w;++col){const uint8_t alpha=g->mask[row*g->w+col];if(!alpha)continue;const int px=x+m.leftInset+g->x+col,py=m.baseline+g->y+row;if(px<m.area.x||py<m.area.y||px>=m.area.x+m.area.w||py>=m.area.y+m.area.h)continue;surface.write(surface.context,px,py,blend565(surface.read(surface.context,px,py),color,alpha));}
    }
    return true;
}
void clear(){for(auto &g:cache){fontFree(g.mask,nullptr);g=Glyph{};}counters=Stats{};clock=0;}
void allocationLimit(size_t size){clear();byteLimit=std::min(size,size_t(512*1024));}
Stats stats(){return counters;}
const char *fontSha(){return METER_NUMERAL_FONT_SHA;}
}
