#include <meter_overlay.h>
#include <cassert>
#include <type_traits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#ifndef METER_OVERLAY_STANDALONE
#include <unity.h>
#undef assert
#define assert(value) TEST_ASSERT_TRUE(value)
void setUp(){}
void tearDown(){}
#endif
#ifdef METER_OVERLAY_TEST_MAPPED
#include <meter_ui_config.h>
#endif

using namespace meter_ui;
static bool near(double a,double b){return std::fabs(a-b)<.000001;}
static ut_meter::Reading reading(double value,const char *unit="V"){
    ut_meter::Reading r;r.numeric=true;r.value=value;r.hasMetadata=true;r.modeKey=1;
    std::strncpy(r.unitLabel,unit,sizeof(r.unitLabel)-1);return r;
}
static OverlaySeries series(const char *unit,double low,double high){
    OverlaySeries s;s.unit=unit;s.observed.include(low);s.observed.include(high);return s;
}
// Giving each V trace its own scale would put both maxima at the same Y and
// erase the tenfold difference. Combine raw observations before adding padding.
static void equal_units_share_one_combined_scale(){
    OverlaySeries s[3]={series("V",1,2),series("V",10,20),series("A",100,200)};
    const auto a=overlayScale(s,3,0),b=overlayScale(s,3,1),c=overlayScale(s,3,2);
    assert(a.valid&&near(a.low,-.9)&&near(a.high,21.9));
    assert(near(b.low,a.low)&&near(b.high,a.high));
    assert(near(c.low,90)&&near(c.high,210));
    const Rect plot={12,50,456,100};
    OverlayCursor first,second;
    const auto p=first.next({1000,2},plot,a,1000,60000);
    const auto q=second.next({1000,20},plot,b,1000,60000);
    assert(p.visible&&q.visible&&p.x==q.x&&p.y>q.y+70);
}
static void different_prefixes_and_missing_traces_do_not_distort_scale(){
    OverlaySeries s[3]={series("V",10,20),series("mV",1000,2000),OverlaySeries{}};
    s[2].unit="V";
    auto a=overlayScale(s,3,0),b=overlayScale(s,3,1);
    assert(near(a.low,9)&&near(a.high,21));
    assert(near(b.low,900)&&near(b.high,2100));
    assert(near(overlayScale(s,3,2).low,9));
    s[0].observed=OverlayRange{};assert(!overlayScale(s,3,2).valid);
    assert(!overlayScale(s,3,3).valid);
}
static void flat_zero_and_extreme_values_have_finite_nonzero_scales(){
    OverlaySeries s[1]={series("V",0,0)};
    auto r=overlayScale(s,1,0);assert(near(r.low,-1)&&near(r.high,1));
    s[0]=series("V",20,20);r=overlayScale(s,1,0);assert(near(r.low,19)&&near(r.high,21));
    s[0]=series("V",-1e308,1e308);r=overlayScale(s,1,0);
    assert(r.valid&&std::isfinite(r.low)&&std::isfinite(r.high)&&r.low<r.high);
    OverlayCursor cursor;const auto middle=cursor.next({1000,0},{10,20,101,101},r,1000,60000);
    assert(middle.visible&&middle.y==70);
}
static void visible_history_excludes_expired_invalid_and_reset_samples(){
    ut_meter::Trend trend;
    trend.push(reading(1000),ut_meter::Model::UT60BT,0);
    trend.push(reading(10),ut_meter::Model::UT60BT,1000);
    auto invalid=reading(1);invalid.numeric=false;trend.push(invalid,ut_meter::Model::UT60BT,2000);
    auto r=overlayObserved(&trend,61000,60000);assert(r.valid&&near(r.low,10)&&near(r.high,10));
    assert(!overlayObserved(&trend,62001,60000).valid);
    trend.reset();trend.push(reading(2),ut_meter::Model::UT60BT,63000);
    r=overlayObserved(&trend,63000,60000);assert(near(r.low,2)&&near(r.high,2));
    trend.push(reading(5,"A"),ut_meter::Model::UT60BT,64000);
    r=overlayObserved(&trend,64000,60000);assert(near(r.low,5)&&near(r.high,5));
    assert(!overlayObserved(nullptr,64000,60000).valid);
}
static void shared_x_wrap_gaps_and_each_trace_start_are_truthful(){
    const Rect plot={10,20,101,81};OverlayRange r;r.include(-1);r.include(1);
    OverlayCursor cursor;
    auto p=cursor.next({0xfffffff0u,0},plot,r,20,100);
    assert(p.visible&&!p.join&&p.x==74&&p.y==60);
    p=cursor.next({20,1},plot,r,20,100);assert(p.visible&&p.join&&p.x==110&&p.y==20);
    p=cursor.next({21,NAN},plot,r,21,100);assert(!p.visible&&!p.join);
    p=cursor.next({22,0},plot,r,22,100);assert(p.visible&&!p.join);
    p=cursor.next({3023,1},plot,r,3023,60000);assert(p.visible&&!p.join);
    p=cursor.next({3024,0},plot,r,3024,60000);assert(p.join);
    p=cursor.next({3025,0},plot,r,63026,60000);assert(!p.visible);
    p=cursor.next({63026,0},plot,r,63026,60000);assert(p.visible&&!p.join);
    OverlayCursor another;
    p=another.next({63027,1},plot,r,63027,60000);assert(p.visible&&!p.join);
    assert(!another.next({63027,1},{0,0,0,10},r,63027,60000).visible);
    assert(!another.next({63027,1},plot,r,63027,0).visible);
}
static bool overlap(Rect a,Rect b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static void translucent_hud_retains_chart_pixels_without_rgb565_channel_bleed(){
    assert(overlayDim565(0)==0);
    assert(overlayDim565(0xffff)==0x5aeb);
    assert(overlayDim565(0xf800)==0x5800);
    assert(overlayDim565(0x07e0)==0x02e0);
    assert(overlayDim565(0x001f)==0x000b);
}
static void every_shared_chart_uses_the_entire_panel_including_hud_pixels(){
    const Rect panels[]={{0,0,480,320},{0,0,320,172},{0,0,466,466}};
    for(const auto &panel:panels)for(unsigned count=1;count<=3;++count){
        const auto layout=overlayLayout(panel,count);
        assert(layout.valid);
        assert(layout.plot.x==0&&layout.plot.y==0&&layout.plot.w==panel.w&&layout.plot.h==panel.h);
        assert(overlap(layout.plot,layout.time));
        for(unsigned i=0;i<count;++i)assert(overlap(layout.plot,layout.legend[i]));
        if(panel.w==320)for(unsigned i=0;i<count;++i)assert(layout.legend[i].w<=240);
    }
}
static void checkLayout(Rect panel,Rect area,Rect control,View view,unsigned count){
    const Transform tx(view,view.reference_width,view.reference_height);
    const auto layout=overlayLayout(panel,count,area,control);assert(layout.valid);
    assert(layout.plot.x==panel.x&&layout.plot.y==panel.y&&layout.plot.w==panel.w&&layout.plot.h==panel.h);
    assert(tx.safe(layout.time)&&overlap(layout.plot,layout.time));
    assert(!overlap(layout.time,control));
    for(unsigned i=0;i<count;++i){
        const Rect row=layout.legend[i];assert(tx.safe(row)&&row.w>=224&&row.h>=48);
        assert(row.w<=232&&row.h<=56);
        assert(row.x>=area.x&&row.y>=area.y&&row.x+row.w<=area.x+area.w&&row.y+row.h<=area.y+area.h);
        assert(overlap(row,layout.plot)&&!overlap(row,layout.time)&&!overlap(row,control));
        for(unsigned j=0;j<i;++j)assert(!overlap(row,layout.legend[j]));
        const auto hud=overlayHud(row);
        // Complete wire-format value + unit, source + MAC + state, and the
        // longest finite double bounds fit without sharing the same pixels.
        assert((23+1+11)*6<=hud.value.w&&hud.value.h>=8);
        assert((7+17+1+7)*6<=hud.source.w);
        assert((2+10+2+10+1+11)*6<=hud.range.w);
        // Full 39-character custom mode plus primary warning flags can wrap.
        assert((39+3+24)*6<=hud.metadata.w*2&&hud.metadata.h==16);
        const Rect fields[]={hud.source,hud.value,hud.metadata,hud.range};
        for(unsigned field=0;field<4;++field){
            assert(tx.safe(fields[field]));
            assert(fields[field].x>=row.x&&fields[field].y>=row.y&&fields[field].x+fields[field].w<=row.x+row.w&&fields[field].y+fields[field].h<=row.y+row.h);
            for(unsigned earlier=0;earlier<field;++earlier)assert(!overlap(fields[field],fields[earlier]));
        }
    }
}
static void mapped_hud_floats_inside_plot_safe_circle_and_away_from_count_button(){
    const Rect panels[]={{0,0,480,320},{0,0,320,172},{0,0,466,466}};
    const Rect areas[]={{4,4,472,312},{4,4,312,164},{80,104,306,278}};
    const Rect controls[]={{372,274,96,44},{244,130,70,40},{181,406,104,44}};
    const View views[]={{1,480,320,false,0},{1,320,172,false,0},{0,466,466,true,4}};
    for(unsigned board=0;board<3;++board)for(unsigned count=1;count<=3;++count){
        checkLayout(panels[board],areas[board],controls[board],views[board],count);
    }
#ifdef METER_OVERLAY_TEST_MAPPED
    const auto map=configuredMap();
    const auto view=meter_ui::view(map.reading);
    const Rect panel={0,0,view.reference_width,view.reference_height};
    const Rect area=view.safe_circle?map.overview.scope:Rect{4,4,panel.w-8,panel.h-8};
    const Rect control=map.overview.show_count_button?map.overview.count_button:Rect{};
    for(unsigned count=1;count<=3;++count)checkLayout(panel,area,control,view,count);
#endif
    assert(!overlayLayout({0,0,308,20},3).valid);
    // The native renderer falls back to Separate for a custom HUD too small to
    // fit its complete metadata. It must not treat a partial layout as usable.
    assert(!overlayLayout({0,0,300,160},3).valid);
    assert(!overlayLayout({0,0,308,96},0).valid);
    assert(!overlayLayout({0,0,308,96},5).valid);
    // A custom safe HUD subset controls only its labels, never graph extent.
    const Rect narrow={12,50,260,218};
    checkLayout(panels[0],narrow,{},views[0],3);
    assert(!overlayLayout(panels[0],3,{12,50,220,218}).valid);
    for(unsigned i=0;i<3;++i){
        const auto lane=stackedLane(narrow,3,i,8);
        assert(lane.w==260&&lane.h>=67&&lane.y>=50&&lane.y+lane.h<=268);
    }
}
static void four_huds_fit_small_round_and_landscape_canvases(){
#ifdef METER_OVERLAY_STANDALONE
 assert(std::extent<decltype(OverlayLayout{}.legend)>::value==4);
#else
 TEST_ASSERT_EQUAL(4,std::extent<decltype(OverlayLayout{}.legend)>::value);
#endif
 const Rect canvases[]={{0,0,480,320},{0,0,320,172},{0,0,466,466}};
 for(unsigned b=0;b<3;++b){auto v=overlayLayout(canvases[b],4,b==2?Rect{80,104,306,278}:Rect{},b==0?Rect{372,274,96,44}:Rect{});assert(v.valid);for(unsigned i=0;i<4;++i){assert(v.legend[i].x>=0&&v.legend[i].y>=0&&v.legend[i].x+v.legend[i].w<=canvases[b].w&&v.legend[i].y+v.legend[i].h<=canvases[b].h);assert(!overlayOverlap(v.legend[i],v.time));for(unsigned j=0;j<i;++j)assert(!overlayOverlap(v.legend[i],v.legend[j]));}}
}
int main(){
#ifdef METER_OVERLAY_STANDALONE
    equal_units_share_one_combined_scale();
    different_prefixes_and_missing_traces_do_not_distort_scale();
    flat_zero_and_extreme_values_have_finite_nonzero_scales();
    visible_history_excludes_expired_invalid_and_reset_samples();
    shared_x_wrap_gaps_and_each_trace_start_are_truthful();
    translucent_hud_retains_chart_pixels_without_rgb565_channel_bleed();
    every_shared_chart_uses_the_entire_panel_including_hud_pixels();
    mapped_hud_floats_inside_plot_safe_circle_and_away_from_count_button();
    puts("meter_overlay: scales, shared X, gaps, resets, full-canvas 1/2/3, safe HUD/control geometry passed");
#else
    UNITY_BEGIN();
    RUN_TEST(equal_units_share_one_combined_scale);
    RUN_TEST(different_prefixes_and_missing_traces_do_not_distort_scale);
    RUN_TEST(flat_zero_and_extreme_values_have_finite_nonzero_scales);
    RUN_TEST(visible_history_excludes_expired_invalid_and_reset_samples);
    RUN_TEST(shared_x_wrap_gaps_and_each_trace_start_are_truthful);
    RUN_TEST(translucent_hud_retains_chart_pixels_without_rgb565_channel_bleed);
    RUN_TEST(every_shared_chart_uses_the_entire_panel_including_hud_pixels);
    RUN_TEST(mapped_hud_floats_inside_plot_safe_circle_and_away_from_count_button);
    RUN_TEST(four_huds_fit_small_round_and_landscape_canvases);
    return UNITY_END();
#endif
}
