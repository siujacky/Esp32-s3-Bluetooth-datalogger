#include <meter_ui_config.h>
#include "../../src/esp32_touch/meter/meter_touch_app.h"
#include <cassert>
#include <climits>
#include <cstdio>

#ifndef METER_DISPLAY_WIDTH
#define METER_DISPLAY_WIDTH 172
#define METER_DISPLAY_HEIGHT 320
#endif
#ifndef METER_NATIVE_SWIPE_STANDALONE
#include <unity.h>
#undef assert
#define assert(condition) TEST_ASSERT_TRUE(condition)
void setUp(){}void tearDown(){}
#endif
using namespace meter_ui;
using namespace meter_touch;
static bool overlaps(Rect a,Rect b){return intersect(a,b).w>0&&intersect(a,b).h>0;}
static void allRowsReachable(Rect area,const AdvancedSpec &spec){
    RowLayout rows(area,spec.row.step,spec.row.gap);
    for(int i=0;i<96;++i){
        const int offset=meter_touch::scrollOffset(0,i*rows.step,rows.contentHeight(96),area.h);
        const Rect r=rows.visible(i,offset);
        assert(r.h>=std::min(44,rows.rect(i,offset).h));
        assert(rows.indexAt(r.x+r.w/2,r.y+r.h/2,offset,96)==i);
    }
}
static void allKeyboardCharactersReachable(Rect area,const AdvancedSpec &spec,const Transform &tx){
    NativeKeyboard keys(area,scaledKeyboard(spec.keyboard,tx));assert(keys.valid);
    unsigned expected=32;
    for(unsigned page=0;page<keys.pages();++page)for(unsigned i=0;i<keys.keyCount;++i){
        const int code=keys.code(page,i);if(code<0)continue;
        assert(code==int(expected++));const Rect key=keys.key(int(i));
        assert(tx.safe(key)&&key.h>=22&&key.w>=14);
        assert(keys.hit(key.x,key.y)==int(i)+4);
        assert(keys.hit(key.x+key.w-1,key.y+key.h-1)==int(i)+4);
        for(int n=0;n<4;++n){assert(!overlaps(key,keys.control(n)));assert(!overlaps(key,keys.footer(n)));}
        if(std::min(tx.width,tx.height)>=300&&!spec.safe_circle)assert(key.h>=44&&key.w>=44);
    }
    assert(expected==127);
    for(int i=0;i<4;++i){
        const Rect control=keys.control(i),footer=keys.footer(i);assert(tx.safe(control)&&tx.safe(footer));
        assert(keys.hit(control.x,control.y)==i&&keys.hit(footer.x,footer.y)==20+i);
        if(std::min(tx.width,tx.height)>=300&&!spec.safe_circle)assert(control.h>=44&&footer.h>=44);
    }
    if(tx.height<200)assert(keys.keyCount==8);
}
static void allChoicesReachable(Rect area,unsigned count,bool visual){
    const SelectionLayout layout=swipeSelectionLayout(area,count,visual);assert(layout.valid);
    assert(layout.up.w==0&&layout.down.w==0&&layout.cancel.h>=44);
    assert(layout.box.y>=area.y&&layout.box.y+layout.box.h<=area.y+area.h);
    for(unsigned wanted=0;wanted<count;++wanted){
        SelectionState state;assert(state.begin(count,-1,layout.visible,layout.rowHeight,layout.columns));
        state.scrollBy(int(wanted/layout.columns)*layout.rowHeight);
        assert(wanted>=state.first()&&wanted<state.first()+layout.visible);
        const unsigned visible=wanted-state.first();const Rect r=layout.row(visible);
        assert(layout.indexAt(r.x,r.y)==int(visible));assert(r.h>=44);
        int chosen=-1;assert(state.chooseVisible(visible,chosen)&&chosen==int(wanted));
    }
}
static void all_native_rotations_are_reachable(){
    const Map baseline=configuredMap();
    for(unsigned rotation=0;rotation<4;++rotation){
        const Map map=nativeMenuMap(baseline,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,rotation);
        const auto &spec=map.advanced;const Transform tx(view(spec),spec.reference_width,spec.reference_height);
        const Rect viewport=tx.rect(spec.viewport);assert(tx.safe(viewport));
        if(!spec.safe_circle)assert(viewport.y+viewport.h==tx.height-24);
        const MeterTabs tabs(viewport);assert(tabs.columns>0);
        for(unsigned slot=0;slot<4;++slot){
            const Rect r=tabs.rect(slot);assert(tx.safe(r)&&r.w>=44&&r.h>=44);
            assert(tabs.hit(r.x,r.y)==int(slot)&&tabs.hit(r.x+r.w-1,r.y+r.h-1)==int(slot));
            assert(tabs.hit(r.x+r.w,r.y)==-1);
        }
        Rect form=viewport;form.y+=tabs.height;form.h-=tabs.height;assert(form.h>=44&&tx.safe(form));
        allRowsReachable(viewport,spec);allRowsReachable(form,spec);
        allKeyboardCharactersReachable(viewport,spec,tx);
        if(spec.home_grid.height>=80&&!spec.safe_circle){
            GridLayout grid(viewport,spec.home_grid.columns,spec.home_grid.column_gap,spec.home_grid.step,spec.home_grid.height);
            assert(grid.columns==(tx.width>tx.height?2:1));
            for(int i=0;i<13;++i){
                const int offset=meter_touch::scrollOffset(0,i/grid.columns*grid.step,grid.contentHeight(13),viewport.h);
                const Rect r=grid.visible(i,offset);assert(r.h>=44&&grid.indexAt(r.x,r.y,offset,13)==i);
            }
        }
        const Rect popup=spec.safe_circle?viewport:Rect{8,8,tx.width-16,tx.height-16};
        for(unsigned count:{1u,4u,6u,16u,64u}){allChoicesReachable(popup,count,false);allChoicesReachable(popup,count,true);}
        std::printf("rotation=%u canvas=%dx%d native rows/tabs/keyboard/choices reachable\n",rotation,tx.width,tx.height);
    }
}

int main(){
#ifdef METER_NATIVE_SWIPE_STANDALONE
 all_native_rotations_are_reachable();return 0;
#else
 UNITY_BEGIN();RUN_TEST(all_native_rotations_are_reachable);return UNITY_END();
#endif
}
