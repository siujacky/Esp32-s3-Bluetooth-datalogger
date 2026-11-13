#include <meter_ui.h>
#include <cassert>
#include <climits>
#include <cstdio>
#ifndef METER_UI_STANDALONE
#include <unity.h>
#undef assert
#define assert(value) TEST_ASSERT_TRUE(value)
void setUp(){}
void tearDown(){}
#endif
#ifdef METER_UI_TEST_MAPPED
#include <meter_ui_config.h>
#endif

using namespace meter_ui;
static bool overlaps(Rect a,Rect b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static void exercise(int width,int height,View view,Rect viewport,KeyboardSpec spec){
    const Transform tx(view,width,height);
    Rect area=tx.rect(viewport);
    assert(area.x>=0&&area.y>=0&&area.x+area.w<=width&&area.y+area.h<=height);
    RowLayout rows(area,tx.yLength(64),tx.yLength(5));
    const int scroll=scrollOffset(0,INT_MAX,rows.contentHeight(96),area.h);
    Rect last=rows.visible(95,scroll);
    assert(last.h>0&&last.y+last.h<=area.y+area.h);
    assert(rows.indexAt(last.x+last.w/2,last.y+last.h/2,scroll,96)==95);
    assert(rows.indexAt(area.x-1,last.y,scroll,96)==-1);
    for(int row=0;row<96;++row){
        const int offset=scrollOffset(0,row*rows.step,rows.contentHeight(96),area.h);
        Rect drawn=rows.visible(row,offset);
        assert(drawn.h>0);
        assert(rows.indexAt(drawn.x,drawn.y,offset,96)==row);
        const Rect raw=rows.rect(row,offset);
        const int gapY=raw.y+raw.h;
        if(gapY>=area.y&&gapY<area.y+area.h)assert(rows.indexAt(raw.x,gapY,offset,96)==-1);
    }
    Keyboard keys(area,spec);
    assert(keys.valid);
    for(int i=0;i<24;++i){
        const Rect key=i<4?keys.control(i):i<20?keys.key(i-4):keys.footer(i-20);
        assert(key.w>=spec.minimum_key_width&&key.h>=spec.minimum_key_height);
        assert(key.x>=area.x&&key.y>=area.y&&key.x+key.w<=area.x+area.w&&key.y+key.h<=area.y+area.h);
        assert(keys.hit(key.x,key.y)==i);
        assert(keys.hit(key.x+key.w-1,key.y+key.h-1)==i);
        for(int j=0;j<i;++j){const Rect other=j<4?keys.control(j):j<20?keys.key(j-4):keys.footer(j-20);assert(!overlaps(key,other));}
    }
    if(view.safe_circle){assert(tx.safe(area));for(int i=0;i<16;++i)assert(tx.safe(keys.key(i)));}
}
static void mapped_layouts_remain_reachable_and_unambiguous(){
    const KeyboardSpec narrow={4,28,24,2,10,24,30,30,1};
    const KeyboardSpec wide={8,28,30,2,10,12,44,44,2};
    exercise(172,320,{0,172,320,false,0},{6,54,160,224},narrow);
    exercise(466,466,{0,466,466,true,4},{80,104,306,278},wide);
    exercise(480,320,{1,480,320,false,0},{6,54,468,224},wide);
    exercise(800,480,{1,480,320,false,0},{6,54,468,224},wide);
    // Wide screens with a short viewport must use fewer keyboard rows.
    exercise(800,240,{1,800,240,false,0},{8,38,784,180},{8,28,24,2,10,12,30,30,1});
    const KeyboardSpec compact={8,28,24,2,10,12,30,30,1};
    exercise(800,200,{1,800,200,false,0},{8,38,784,120},compact);
    assert(Keyboard({8,38,784,120},compact).columns==16);
    Transform round({0,466,466,true,4},466,466);
    assert(!round.safe({0,0,466,466}));assert(round.safe({80,104,306,278}));
    // Accounting for row gaps must not shrink the minimum drawn key height.
    assert(!Keyboard({0,0,160,198},narrow).valid);
    assert(scrollOffset(10,INT_MIN,1000,100)==0);
#ifdef METER_UI_TEST_MAPPED
    const Map map=configuredMap();
    const AdvancedSpec advanced=map.advanced;
    const Transform advancedTx(view(advanced),METER_UI_TEST_WIDTH,METER_UI_TEST_HEIGHT);
    exercise(METER_UI_TEST_WIDTH,METER_UI_TEST_HEIGHT,view(advanced),advanced.viewport,scaledKeyboard(advanced.keyboard,advancedTx));
    assert(advancedTx.safe(advancedTx.rect(advanced.back)));
    assert(advancedTx.safe(advancedTx.rect(advanced.up)));
    assert(advancedTx.safe(advancedTx.rect(advanced.down)));
    const Transform setupTx(view(map.setup),map.setup.reference_width,map.setup.reference_height);
    assert(setupTx.safe(map.setup.viewport));assert(setupTx.safe(map.setup.header));assert(setupTx.safe(map.setup.network.qr));
    const Transform readingTx(view(map.reading),map.reading.reference_width,map.reading.reference_height);
    assert(readingTx.safe(map.reading.scope.plot));
#endif
    puts("meter_ui: all mapped layout, reachability, exact hit and keyboard checks passed");
}
static void card_grids_keep_all_actions_reachable(){
    GridLayout grid({12,66,456,210},2,12,86,76,88);
    assert(grid.rect(0,0).x==12&&grid.rect(0,0).y==154&&grid.rect(0,0).w==222);
    assert(grid.rect(1,0).x==246&&grid.rect(1,0).y==154);
    assert(grid.indexAt(234,160,0,8)==-1);
    assert(grid.indexAt(20,235,0,8)==-1);
    assert(grid.indexAt(20,70,0,8)==-1);
    const int gridScroll=grid.contentHeight(8)-210;
    assert(gridScroll==212&&grid.rect(7,gridScroll).y+grid.rect(7,gridScroll).h==276);
    assert(grid.indexAt(467,275,gridScroll,8)==7);
    for(int i=0;i<10;++i){GridLayout home({12,56,456,208},2,12,100,88);int offset=scrollOffset(0,i/2*100,home.contentHeight(10),208);Rect r=home.visible(i,offset);assert(r.h>0&&home.indexAt(r.x,r.y,offset,10)==i);}
}
static void touch_axes_follow_the_current_view(){
    uint16_t tx=0,ty=0;
    assert(mapTouch(100,40,172,320,1,true,false,false,tx,ty)&&tx==40&&ty==100);
    assert(mapTouch(100,40,466,466,2,false,false,false,tx,ty)&&tx==365&&ty==425);
    assert(mapTouch(40,100,172,320,3,false,true,true,tx,ty)&&tx==40&&ty==100);
    assert(!mapTouch(172,0,172,320,0,false,false,false,tx,ty)&&tx==40&&ty==100);
}
int main(){
#ifdef METER_UI_STANDALONE
    mapped_layouts_remain_reachable_and_unambiguous();card_grids_keep_all_actions_reachable();touch_axes_follow_the_current_view();return 0;
#else
    UNITY_BEGIN();RUN_TEST(mapped_layouts_remain_reachable_and_unambiguous);RUN_TEST(card_grids_keep_all_actions_reachable);RUN_TEST(touch_axes_follow_the_current_view);return UNITY_END();
#endif
}
