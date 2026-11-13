#include <unity.h>
#include <meter_display_count.h>
#include <meter_idle_dim.h>
#include <meter_ui.h>
#include <climits>
void setUp(){} void tearDown(){}
static void counts_are_exactly_one_two_or_three(){
    for(unsigned n=1;n<=4;++n)TEST_ASSERT_TRUE(meter_ui::validDisplayCount(n));
    TEST_ASSERT_FALSE(meter_ui::validDisplayCount(0));TEST_ASSERT_FALSE(meter_ui::validDisplayCount(5));TEST_ASSERT_FALSE(meter_ui::validDisplayCount(UINT_MAX));
}
static void requests_apply_on_consume_and_do_not_pretend_to_be_saved(){
    meter_ui::DisplayCount state;state.load(1,true);unsigned value=0;
    TEST_ASSERT_TRUE(state.request(3));TEST_ASSERT_EQUAL(1,state.count());TEST_ASSERT_TRUE(state.consume(value));
    TEST_ASSERT_EQUAL(3,value);TEST_ASSERT_EQUAL(3,state.count());TEST_ASSERT_FALSE(state.saved());
    state.markSaved(true);TEST_ASSERT_TRUE(state.saved());TEST_ASSERT_FALSE(state.consume(value));
    state.request(2);TEST_ASSERT_FALSE(state.request(99));TEST_ASSERT_TRUE(state.consume(value));TEST_ASSERT_EQUAL(2,value);
    state.markSaved(false);TEST_ASSERT_TRUE(state.request(2));TEST_ASSERT_TRUE(state.consume(value));state.markSaved(true);
    state.request(2);TEST_ASSERT_FALSE(state.consume(value));
}
static void bad_stored_values_fall_back_and_every_pair_is_selectable(){
    meter_ui::DisplayCount state;state.load(255,true);TEST_ASSERT_EQUAL(1,state.count());TEST_ASSERT_FALSE(state.saved());
    for(unsigned focus=0;focus<4;++focus)for(unsigned i=0;i<4;++i)TEST_ASSERT_EQUAL((focus+i)%4,meter_ui::displaySlot(focus,i));
    TEST_ASSERT_EQUAL(0,meter_ui::displaySlot(3,1));
}
static void lane_division_fills_viewport_without_overlap(){
    for(unsigned count=1;count<=3;++count){
        const meter_ui::Rect area{12,50,456,218};int previous=area.y,total=0;
        for(unsigned i=0;i<count;++i){auto r=meter_ui::stackedLane(area,count,i,8);TEST_ASSERT_GREATER_THAN(0,r.h);TEST_ASSERT_EQUAL(area.x,r.x);TEST_ASSERT_EQUAL(area.w,r.w);TEST_ASSERT_EQUAL(previous,r.y);previous=r.y+r.h+8;total+=r.h;}
        TEST_ASSERT_EQUAL(area.h,total+8*(count-1));TEST_ASSERT_EQUAL(area.y+area.h,previous-8);
    }
    TEST_ASSERT_EQUAL(0,meter_ui::stackedLane({0,0,100,10},3,1,8).h);
}
static void overlay_defaults_and_migration_leave_count_independent(){
    meter_ui::OverlaySetting overlay;overlay.load(1,true);TEST_ASSERT_TRUE(overlay.overlay());TEST_ASSERT_TRUE(overlay.saved());
    overlay.load(0,true);TEST_ASSERT_FALSE(overlay.overlay());TEST_ASSERT_TRUE(overlay.saved());
    overlay.load(255,true);TEST_ASSERT_TRUE(overlay.overlay());TEST_ASSERT_FALSE(overlay.saved());
    overlay.load(1,false);TEST_ASSERT_FALSE(overlay.saved());
}
static void overlay_requests_wait_for_main_loop_and_support_failed_save_retry(){
    meter_ui::OverlaySetting overlay;overlay.load(1,true);bool next=true;
    overlay.request(false);TEST_ASSERT_TRUE(overlay.overlay());TEST_ASSERT_TRUE(overlay.consume(next));TEST_ASSERT_FALSE(next);TEST_ASSERT_FALSE(overlay.overlay());TEST_ASSERT_FALSE(overlay.saved());
    overlay.markSaved(false);overlay.request(false);TEST_ASSERT_TRUE(overlay.consume(next));overlay.markSaved(true);
    overlay.request(false);TEST_ASSERT_FALSE(overlay.consume(next));
    overlay.request(false);overlay.request(true);TEST_ASSERT_TRUE(overlay.consume(next));TEST_ASSERT_TRUE(next);TEST_ASSERT_FALSE(overlay.saved());
}
static void idle_dim_waits_then_touch_wakes_without_becoming_a_button_press(){
    meter_ui::IdleDimmer dim;dim.configure(true,100);
    TEST_ASSERT_EQUAL(55,dim.tick(120099));TEST_ASSERT_EQUAL(10,dim.tick(120100));
    TEST_ASSERT_TRUE(dim.contact(120101));TEST_ASSERT_EQUAL(55,dim.tick(120101));
    TEST_ASSERT_FALSE(dim.contact(120102));TEST_ASSERT_EQUAL(55,dim.tick(240101));
    TEST_ASSERT_EQUAL(10,dim.tick(240102));
}
static void disabled_calibration_and_timer_wrap_keep_dimming_predictable(){
    meter_ui::IdleDimmer dim;dim.configure(false,0);TEST_ASSERT_EQUAL(55,dim.tick(900000));
    dim.configure(true,0xffff0000u);TEST_ASSERT_EQUAL(55,dim.tick(uint32_t(0xffff0000u+119999u)));
    TEST_ASSERT_EQUAL(10,dim.tick(uint32_t(0xffff0000u+120000u)));
    TEST_ASSERT_EQUAL(55,dim.tick(900000,true));TEST_ASSERT_EQUAL(55,dim.tick(1019999));
    TEST_ASSERT_EQUAL(10,dim.tick(1020000));dim.configure(false,1020001);TEST_ASSERT_EQUAL(55,dim.tick(2000000));
}
int main(){UNITY_BEGIN();RUN_TEST(counts_are_exactly_one_two_or_three);RUN_TEST(requests_apply_on_consume_and_do_not_pretend_to_be_saved);RUN_TEST(bad_stored_values_fall_back_and_every_pair_is_selectable);RUN_TEST(lane_division_fills_viewport_without_overlap);RUN_TEST(overlay_defaults_and_migration_leave_count_independent);RUN_TEST(overlay_requests_wait_for_main_loop_and_support_failed_save_retry);RUN_TEST(idle_dim_waits_then_touch_wakes_without_becoming_a_button_press);RUN_TEST(disabled_calibration_and_timer_wrap_keep_dimming_predictable);return UNITY_END();}
