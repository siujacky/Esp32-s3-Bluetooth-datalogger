#include <unity.h>
#include <meter_numerals.h>
#include <vector>
#include <algorithm>
using namespace meter_numerals;
void setUp(){clear();allocationLimit(512*1024);}void tearDown(){clear();}
struct Frame {int w=240,h=160;std::vector<uint16_t> pixels=std::vector<uint16_t>(w*h,0);bool clipped=false;static uint16_t read(void *c,int x,int y){auto &f=*static_cast<Frame*>(c);return x>=0&&y>=0&&x<f.w&&y<f.h?f.pixels[y*f.w+x]:0;}static void write(void *c,int x,int y,uint16_t v){auto &f=*static_cast<Frame*>(c);if(x<0||y<0||x>=f.w||y>=f.h){f.clipped=true;return;}f.pixels[y*f.w+x]=v;}Surface surface(){return {this,read,write};}};
static void coverage_blends_each_rgb565_channel(){TEST_ASSERT_EQUAL_HEX16(0xf800,blend565(0xf800,0x07e0,0));TEST_ASSERT_EQUAL_HEX16(0x07e0,blend565(0xf800,0x07e0,255));TEST_ASSERT_EQUAL_HEX16(0x8410,blend565(0,0xffff,128));}
static void outline_has_smooth_edges_holes_and_no_outside_pixels(){
    Frame f;const auto m=metrics({12,8,210,130},3,130);TEST_ASSERT_TRUE(m.valid);TEST_ASSERT_TRUE(paint(f.surface(),m,"689",0xffff));unsigned solid=0,edge=0;for(auto pixel:f.pixels){solid+=pixel==0xffff;edge+=pixel!=0&&pixel!=0xffff;}TEST_ASSERT_GREATER_THAN(200,solid);TEST_ASSERT_GREATER_THAN(30,edge);TEST_ASSERT_FALSE(f.clipped);
    for(int y=0;y<f.h;++y)for(int x=0;x<f.w;++x)if(x<12||x>=222||y<8||y>=138)TEST_ASSERT_EQUAL_HEX16(0,f.pixels[y*f.w+x]);
    // Each 6/8/9 must retain an interior background region enclosed by ink.
    for(int cell=0;cell<3;++cell){bool enclosed=false;for(int y=20;y<120&&!enclosed;++y)for(int x=m.x+cell*m.cellWidth+3;x<m.x+(cell+1)*m.cellWidth-3;++x)if(f.pixels[y*f.w+x]==0){bool left=false,right=false,up=false,down=false;for(int xx=m.x+cell*m.cellWidth;xx<x;++xx)left|=f.pixels[y*f.w+xx]>0;for(int xx=x+1;xx<m.x+(cell+1)*m.cellWidth;++xx)right|=f.pixels[y*f.w+xx]>0;for(int yy=8;yy<y;++yy)up|=f.pixels[yy*f.w+x]>0;for(int yy=y+1;yy<138;++yy)down|=f.pixels[yy*f.w+x]>0;if(left&&right&&up&&down){enclosed=true;break;}}TEST_ASSERT_TRUE(enclosed);}
}
static void metrics_are_fixed_and_cache_reuses_masks(){
    Frame f;const auto m=metrics({4,4,232,64},8,90);TEST_ASSERT_TRUE(m.valid);TEST_ASSERT_TRUE(paint(f.surface(),m,"   12.34",0xffff));const auto before=stats();TEST_ASSERT_TRUE(paint(f.surface(),m,"   12.34",0x07ff));TEST_ASSERT_EQUAL(before.misses,stats().misses);TEST_ASSERT_LESS_OR_EQUAL(512*1024,stats().peakBytes);TEST_ASSERT_EQUAL(8,m.capacity);
}
static void allocation_failure_is_visible_to_caller_and_does_not_paint_zero(){Frame f;allocationLimit(0);const auto m=metrics({4,4,232,64},8,90);TEST_ASSERT_FALSE(paint(f.surface(),m,"   12.34",0xffff));TEST_ASSERT_EQUAL(0,std::count_if(f.pixels.begin(),f.pixels.end(),[](uint16_t v){return v!=0;}));}
static void partial_allocation_failures_reclaim_transaction(){
    for(unsigned budget: {96u,512u,1500u,3000u,8000u}){clear();Frame f;allocationLimit(budget);const auto m=metrics({8,8,220,140},1,160);TEST_ASSERT_FALSE(paint(f.surface(),m,"8",0xffff));TEST_ASSERT_EQUAL(0,stats().bytes);TEST_ASSERT_EQUAL(0,std::count_if(f.pixels.begin(),f.pixels.end(),[](uint16_t v){return v!=0;}));}
}
static void old_glyphs_are_evicted_when_different_sizes_fill_cache(){Frame f;allocationLimit(64*1024);for(unsigned size=18;size<=160;++size){const auto m=metrics({8,8,220,140},1,size);TEST_ASSERT_TRUE(paint(f.surface(),m,"8",0xffff));TEST_ASSERT_LESS_OR_EQUAL(64*1024,stats().peakBytes);}}
int main(){UNITY_BEGIN();RUN_TEST(coverage_blends_each_rgb565_channel);RUN_TEST(outline_has_smooth_edges_holes_and_no_outside_pixels);RUN_TEST(metrics_are_fixed_and_cache_reuses_masks);RUN_TEST(allocation_failure_is_visible_to_caller_and_does_not_paint_zero);RUN_TEST(partial_allocation_failures_reclaim_transaction);RUN_TEST(old_glyphs_are_evicted_when_different_sizes_fill_cache);return UNITY_END();}
