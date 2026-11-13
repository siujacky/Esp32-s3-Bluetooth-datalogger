#include <unity.h>
#include <meter_value_layout.h>
void setUp(){}void tearDown(){}
using namespace meter_ui;
static bool overlaps(Rect a,Rect b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static void canonical_cctv_rectangles(){
    auto two=valueTiles({8,8,464,256},ValuePreset::LargeSmall,2,8);TEST_ASSERT_TRUE(two.valid);TEST_ASSERT_EQUAL(304,two.tiles[0].bounds.w);TEST_ASSERT_EQUAL(320,two.tiles[1].bounds.x);TEST_ASSERT_EQUAL(152,two.tiles[1].bounds.w);
    auto three=valueTiles({8,8,464,256},ValuePreset::LargeSmallSmall,3,8);TEST_ASSERT_TRUE(three.valid);TEST_ASSERT_EQUAL(124,three.tiles[1].bounds.h);TEST_ASSERT_EQUAL(140,three.tiles[2].bounds.y);
    auto top=valueTiles({8,8,464,256},ValuePreset::LargeLargeSmall,3,8);TEST_ASSERT_TRUE(top.valid);TEST_ASSERT_EQUAL(228,top.tiles[0].bounds.w);TEST_ASSERT_EQUAL(173,top.tiles[0].bounds.h);TEST_ASSERT_EQUAL(75,top.tiles[2].bounds.h);
}
static void all_roles_stay_inside_actual_screen_safe_areas(){
    const Rect areas[]={{8,8,464,256},{6,4,308,164},{80,104,306,278},{12,12,776,400}};
    const ValuePreset presets[]={ValuePreset::LargeSmall,ValuePreset::LargeSmallSmall,ValuePreset::LargeLargeSmall};
    for(unsigned b=0;b<4;++b)for(auto p:presets){unsigned n=p==ValuePreset::LargeSmall?2:3;auto out=valueTiles(areas[b],p,n,b==1?4:8);TEST_ASSERT_TRUE(out.valid);unsigned large=0,small=0;
        for(unsigned i=0;i<n;++i){auto r=out.tiles[i].bounds;TEST_ASSERT_EQUAL(i,out.tiles[i].sourceIndex);TEST_ASSERT_TRUE(r.w>0&&r.h>0&&r.x>=areas[b].x&&r.y>=areas[b].y&&r.x+r.w<=areas[b].x+areas[b].w&&r.y+r.h<=areas[b].y+areas[b].h);for(unsigned j=0;j<i;++j)TEST_ASSERT_FALSE(overlaps(r,out.tiles[j].bounds));large+=out.tiles[i].role==TileRole::Large;small+=out.tiles[i].role==TileRole::Small;}
        TEST_ASSERT_EQUAL(p==ValuePreset::LargeLargeSmall?2:1,large);TEST_ASSERT_EQUAL(p==ValuePreset::LargeSmallSmall?2:1,small);
    }
    auto lcd=valueTiles(areas[1],ValuePreset::LargeLargeSmall,3,4);TEST_ASSERT_EQUAL(54,lcd.tiles[0].bounds.h);TEST_ASSERT_EQUAL(48,lcd.tiles[2].bounds.h);
    auto round=valueTiles(areas[2],ValuePreset::LargeSmallSmall,3,8);TEST_ASSERT_EQUAL(130,round.tiles[0].bounds.h);TEST_ASSERT_EQUAL(66,round.tiles[2].bounds.h);
}
static void incompatible_and_tiny_layouts_are_rejected(){
    TEST_ASSERT_EQUAL(2,valuePresetCount(ValuePreset::Equal,1));
    TEST_ASSERT_FALSE(valueTiles({0,0,480,320},ValuePreset::LargeSmall,3,8).valid);TEST_ASSERT_FALSE(valueTiles({0,0,480,320},ValuePreset::Solo,2,8).valid);TEST_ASSERT_FALSE(valueTiles({0,0,480,10},ValuePreset::LargeSmallSmall,3,8).valid);TEST_ASSERT_FALSE(valueTiles({0,0,480,320},ValuePreset(255),3,8).valid);
    for(unsigned count=1;count<=3;++count){auto p=count==1?ValuePreset::Solo:ValuePreset::Equal;TEST_ASSERT_TRUE(valueTiles({8,8,464,256},p,count,8).valid);}
}
static void grid_four_uses_exact_quadrants_without_changing_legacy_presets(){
 const Rect area{8,8,464,256};auto v=valueTiles(area,ValuePreset::Grid,4,8);TEST_ASSERT_TRUE(v.valid);TEST_ASSERT_EQUAL(4,v.count);
 for(unsigned i=0;i<4;++i){const auto r=v.tiles[i].bounds;TEST_ASSERT_EQUAL(8+(i%2)*236,r.x);TEST_ASSERT_EQUAL(8+(i/2)*132,r.y);TEST_ASSERT_EQUAL(228,r.w);TEST_ASSERT_EQUAL(124,r.h);TEST_ASSERT_EQUAL(i,v.tiles[i].sourceIndex);}
 for(Rect safe: {Rect{6,4,308,164},Rect{80,104,306,278}}){auto out=valueTiles(safe,ValuePreset::Grid,4,4);TEST_ASSERT_TRUE(out.valid);for(unsigned i=0;i<4;++i){auto r=out.tiles[i].bounds;TEST_ASSERT_TRUE(r.x>=safe.x&&r.y>=safe.y&&r.x+r.w<=safe.x+safe.w&&r.y+r.h<=safe.y+safe.h);for(unsigned j=0;j<i;++j)TEST_ASSERT_FALSE(overlaps(r,out.tiles[j].bounds));}}
 TEST_ASSERT_FALSE(validValuePreset(ValuePreset::Equal,4));TEST_ASSERT_FALSE(validValuePreset(ValuePreset::Grid,3));TEST_ASSERT_EQUAL(4,valuePresetCount(ValuePreset::Grid));
}
int main(){UNITY_BEGIN();RUN_TEST(canonical_cctv_rectangles);RUN_TEST(all_roles_stay_inside_actual_screen_safe_areas);RUN_TEST(incompatible_and_tiny_layouts_are_rejected);RUN_TEST(grid_four_uses_exact_quadrants_without_changing_legacy_presets);return UNITY_END();}
