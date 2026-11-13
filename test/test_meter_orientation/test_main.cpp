#include <meter_ui_config.h>
#include <meter_orientation.h>
#include <meter_value_layout.h>
#include <jc3248_touch.h>
#include <cassert>
#include <climits>
#include <cstdio>
#ifndef METER_ORIENTATION_STANDALONE
#include <unity.h>
#undef assert
#define assert(value) TEST_ASSERT_TRUE(value)
void setUp(){}void tearDown(){}
#endif
#ifndef METER_DISPLAY_WIDTH
#define METER_DISPLAY_WIDTH 172
#define METER_DISPLAY_HEIGHT 320
#endif

using namespace meter_ui;
static bool inside(Rect r,int w,int h){return r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=h;}
static void strict_setting_keeps_failed_saves_and_pending_requests_distinct(){
    OrientationSetting s(1);s.load(1,true);unsigned next=99;
    assert(s.request(0));assert(s.rotation()==1);assert(s.consume(next)&&next==0);assert(!s.saved());
    s.markSaved(false);assert(s.request(0));assert(s.consume(next)&&next==0);s.markSaved(true);
    assert(s.request(0));assert(!s.consume(next));
    assert(s.request(3));assert(!s.request(4));assert(!s.request(UINT_MAX));assert(s.consume(next)&&next==3);
    s.load(255,true);assert(s.rotation()==1&&!s.saved());
    s.load(2,false);assert(s.rotation()==2&&!s.saved());
    for(unsigned r=0;r<4;++r){assert(s.request(r));assert(s.consume(next)&&next==r);s.markSaved(true);}
}
static void all_rotations_resolve_to_pixels_and_keep_geometry_readable(){
    const Map base=configuredMap();
    for(unsigned rotation=0;rotation<4;++rotation){
        const Map m=orientationMap(base,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,rotation);
        const int w=(rotation&1)?METER_DISPLAY_HEIGHT:METER_DISPLAY_WIDTH;
        const int h=(rotation&1)?METER_DISPLAY_WIDTH:METER_DISPLAY_HEIGHT;
        const View views[]={view(m.reading),view(m.setup),view(m.advanced)};
        for(const auto &v:views){assert(v.rotation==int(rotation)&&v.reference_width==w&&v.reference_height==h);}
        assert(inside(m.reading.scope.plot,w,h));assert(inside(m.overview.values,w,h));assert(inside(m.overview.scope,w,h));
        assert(inside(m.setup.viewport,w,h));assert(inside(m.setup.header,w,h));assert(inside(m.setup.network.qr,w,h));
        assert(m.setup.network.qr.w==m.setup.network.qr.h);
        assert(inside(m.advanced.viewport,w,h));assert(inside(m.advanced.back,w,h));
        assert(m.reading.value.size_x>=base.reading.value.size_x&&m.reading.value.size_y>=base.reading.value.size_y);
        assert(m.setup.row.font_size>=base.setup.row.font_size);
        assert(m.advanced.row.title_size>=base.advanced.row.title_size&&m.advanced.row.detail_size>=base.advanced.row.detail_size);
        if(w>=300&&h>=300&&!base.reading.safe_circle){
            assert(m.overview.values.h>=h-20&&m.overview.values.w>=w-20);
            assert(m.setup.header.h>=44&&m.setup.row.height>=44&&m.advanced.back.h>=44);
            assert(m.advanced.row.title_size>=2&&m.advanced.row.detail_size>=2);
            assert(m.advanced.home_grid.columns==(w>=h?2:1));
            assert(m.advanced.viewport.y+m.advanced.viewport.h<=h-16);
            Keyboard keys(m.advanced.viewport,m.advanced.keyboard);assert(keys.valid);
            for(int i=0;i<16;++i){const auto key=keys.key(i);assert(key.h>=44&&inside(key,w,h));}
            const auto tiles=valueTiles(m.overview.values,ValuePreset::Grid,4,m.overview.gap);assert(tiles.valid);
            for(unsigned i=0;i<4;++i){const Rect tile=tiles.tiles[i].bounds;assert(tile.w>=140&&tile.h>=140&&inside(tile,w,h));}
        }
        if(base.reading.safe_circle){
            assert(m.setup.viewport.x==base.setup.viewport.x&&m.setup.viewport.y==base.setup.viewport.y);
            assert(m.advanced.viewport.w==base.advanced.viewport.w&&m.advanced.viewport.h==base.advanced.viewport.h);
            assert(Transform(view(m.setup),w,h).safe(m.setup.viewport));
        }
    }
    // Invalid dimensions/orientation must leave the configured rollback map intact.
    const Map bad=orientationMap(base,0,METER_DISPLAY_HEIGHT,0);
    assert(bad.reading.rotation==base.reading.rotation&&bad.setup.reference_width==base.setup.reference_width);
    assert(orientationMap(base,METER_DISPLAY_WIDTH,METER_DISPLAY_HEIGHT,4).reading.rotation==base.reading.rotation);
}
static void calibrated_baseline_corners_follow_absolute_gfx_rotation(){
    // Calibration is authored at JC baseline rotation 1 (480x320), never rewritten.
    const unsigned w=320,h=480;
    const uint16_t baseline[][2]={{0,319},{0,0},{479,0},{479,319}};
    const uint16_t expected[4][4][2]={
        {{0,0},{319,0},{319,479},{0,479}},
        {{0,319},{0,0},{479,0},{479,319}},
        {{319,479},{0,479},{0,0},{319,0}},
        {{479,0},{479,319},{0,319},{0,0}}
    };
    for(unsigned rotation=0;rotation<4;++rotation)for(unsigned corner=0;corner<4;++corner){
        uint16_t x=88,y=99;assert(jc3248::transformTouch(baseline[corner][0],baseline[corner][1],1,rotation,w,h,x,y));
        assert(x==expected[rotation][corner][0]&&y==expected[rotation][corner][1]);
        uint16_t bx=0,by=0;assert(jc3248::transformTouch(x,y,rotation,1,w,h,bx,by));
        assert(bx==baseline[corner][0]&&by==baseline[corner][1]);
    }
}
int main(){
#ifdef METER_ORIENTATION_STANDALONE
    strict_setting_keeps_failed_saves_and_pending_requests_distinct();all_rotations_resolve_to_pixels_and_keep_geometry_readable();calibrated_baseline_corners_follow_absolute_gfx_rotation();puts("meter_orientation: strict state, 4 rotations, geometry, readability and calibrated corners passed");return 0;
#else
    UNITY_BEGIN();RUN_TEST(strict_setting_keeps_failed_saves_and_pending_requests_distinct);RUN_TEST(all_rotations_resolve_to_pixels_and_keep_geometry_readable);RUN_TEST(calibrated_baseline_corners_follow_absolute_gfx_rotation);return UNITY_END();
#endif
}
