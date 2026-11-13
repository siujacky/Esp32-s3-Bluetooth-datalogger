#include <unity.h>
#include "../../src/esp32_touch/meter/meter_touch_app.h"
#include <limits.h>
void setUp(){}
void tearDown(){}
static void scroll_limits_keep_last_control_reachable(){
    TEST_ASSERT_EQUAL(0,meter_touch::scrollOffset(0,-100,700,228));
    TEST_ASSERT_EQUAL(472,meter_touch::scrollOffset(0,INT_MAX,700,228));
    TEST_ASSERT_EQUAL(0,meter_touch::scrollOffset(472,INT_MIN,700,228));
    TEST_ASSERT_EQUAL(0,meter_touch::scrollOffset(999,3,100,228));
}
static void row_hit_testing_excludes_fixed_header_and_footer(){
    TEST_ASSERT_EQUAL(-1,meter_touch::rowAt(71,72,300,0,60,8));
    TEST_ASSERT_EQUAL(-1,meter_touch::rowAt(300,72,300,0,60,8));
    TEST_ASSERT_EQUAL(0,meter_touch::rowAt(72,72,300,0,60,8));
    TEST_ASSERT_EQUAL(7,meter_touch::rowAt(299,72,300,252,60,8));
    TEST_ASSERT_EQUAL(-1,meter_touch::rowAt(299,72,300,999,60,8));
}
static void keyboard_limits_and_cursor_are_bounded(){
    TEST_ASSERT_EQUAL(0,meter_touch::cursorOffset(0,-1,100));
    TEST_ASSERT_EQUAL(100,meter_touch::cursorOffset(100,INT_MAX,100));
    TEST_ASSERT_EQUAL(0,meter_touch::cursorOffset(50,INT_MIN,100));
    TEST_ASSERT_FALSE(meter_touch::canInsert(4096,4096));
    TEST_ASSERT_TRUE(meter_touch::canInsert(4095,4096));
}
static void every_printable_ascii_character_has_a_keyboard_key(){
    unsigned count=0;
    for(unsigned page=0;page<6;++page)for(unsigned key=0;key<16;++key){int code=meter_touch::keyboardCode(page,key);if(code>=0){TEST_ASSERT_EQUAL(32+count,code);++count;}}
    TEST_ASSERT_EQUAL(95,count);TEST_ASSERT_EQUAL(-1,meter_touch::keyboardCode(6,0));TEST_ASSERT_EQUAL(-1,meter_touch::keyboardCode(0,16));
}
static void every_row_is_reachable_on_each_board_viewport(){
    const int viewports[]={224,224,298};
    for(int viewport:viewports)for(int row=0;row<96;++row){int offset=meter_touch::scrollOffset(0,row*64,96*64,viewport);int y=54+row*64-offset;TEST_ASSERT_EQUAL(row,meter_touch::rowAt(y,54,54+viewport,offset,64,96));}
}
static void wide_keyboard_has_large_keys_and_every_key_is_reachable(){
    meter_touch::KeyboardGeometry keys(468,54,278);
    TEST_ASSERT_EQUAL(8,keys.columns);TEST_ASSERT_EQUAL(2,keys.rows);TEST_ASSERT_GREATER_OR_EQUAL(44,keys.keyHeight-2);TEST_ASSERT_GREATER_OR_EQUAL(44,keys.controlHeight);TEST_ASSERT_GREATER_OR_EQUAL(44,keys.footerHeight);TEST_ASSERT_LESS_OR_EQUAL(278,keys.footerY+keys.footerHeight);
    for(int index=0;index<16;++index){int x=6+(index%8)*(468/8)+20,y=keys.keyTop+(index/8)*keys.keyHeight+20;TEST_ASSERT_EQUAL(index,meter_touch::keyboardIndexAt(x,y,6,468,keys));}
    TEST_ASSERT_EQUAL(-1,meter_touch::keyboardIndexAt(5,keys.keyTop,6,468,keys));TEST_ASSERT_EQUAL(-1,meter_touch::keyboardIndexAt(6,keys.keyTop-1,6,468,keys));TEST_ASSERT_EQUAL(-1,meter_touch::keyboardIndexAt(6,keys.footerY,6,468,keys));TEST_ASSERT_EQUAL(7,meter_touch::keyboardIndexAt(473,keys.keyTop,6,468,keys));
}
int main(){UNITY_BEGIN();RUN_TEST(scroll_limits_keep_last_control_reachable);RUN_TEST(row_hit_testing_excludes_fixed_header_and_footer);RUN_TEST(keyboard_limits_and_cursor_are_bounded);RUN_TEST(every_printable_ascii_character_has_a_keyboard_key);RUN_TEST(every_row_is_reachable_on_each_board_viewport);RUN_TEST(wide_keyboard_has_large_keys_and_every_key_is_reachable);return UNITY_END();}
