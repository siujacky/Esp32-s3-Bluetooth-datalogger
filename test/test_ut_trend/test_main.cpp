#include <unity.h>
#include <ut_trend.h>
#include <math.h>
#include <string.h>
using namespace ut_meter;
void setUp() {}
void tearDown() {}
static Reading voltage(double value) {Reading r;r.value=value;r.mode=2;r.numeric=true;return r;}
void test_history_keeps_latest_samples_in_order() {
    Trend t;
    for(unsigned i=0;i<130;++i)t.push(voltage(i),Model::UT60BT,i*500);
    TEST_ASSERT_EQUAL(128,t.size());
    TEST_ASSERT_FLOAT_WITHIN(.0001,2,t.point(0).value);
    TEST_ASSERT_EQUAL(1000,t.point(0).at);
    TEST_ASSERT_FLOAT_WITHIN(.0001,129,t.point(127).value);
}
void test_auto_range_ignores_expired_points_and_adds_headroom() {
    Trend t;TrendRange r;
    t.push(voltage(1000),Model::UT60BT,0);
    t.push(voltage(1),Model::UT60BT,1000);
    t.push(voltage(3),Model::UT60BT,2000);
    TEST_ASSERT_TRUE(t.range(61000,r));
    TEST_ASSERT_FLOAT_WITHIN(.0001,.8,r.low);
    TEST_ASSERT_FLOAT_WITHIN(.0001,3.2,r.high);
    TEST_ASSERT_FALSE(t.range(63000,r));
}
void test_flat_signals_have_a_visible_nonzero_axis() {
    Trend t;TrendRange r;
    t.push(voltage(0),Model::UT60BT,100);
    TEST_ASSERT_TRUE(t.range(100,r));
    TEST_ASSERT_FLOAT_WITHIN(.0001,-1,r.low);
    TEST_ASSERT_FLOAT_WITHIN(.0001,1,r.high);
    t.push(voltage(20),Model::UT60BT,200);
    t.push(voltage(20),Model::UT60BT,61000);
    TEST_ASSERT_TRUE(t.range(61000,r));
    TEST_ASSERT_FLOAT_WITHIN(.0001,19,r.low);
    TEST_ASSERT_FLOAT_WITHIN(.0001,21,r.high);
}
void test_unit_channel_and_model_changes_clear_history() {
    Trend t;Reading r=voltage(2);
    t.push(r,Model::UT60BT,100);
    r.mode=3;t.push(r,Model::UT60BT,200);
    TEST_ASSERT_EQUAL(1,t.size());
    r.channel=1;t.push(r,Model::UT60BT,300);
    TEST_ASSERT_EQUAL(1,t.size());
    t.push(r,Model::UT61Plus,400);
    TEST_ASSERT_EQUAL(1,t.size());
    r.range=5;t.push(r,Model::UT61Plus,500);
    TEST_ASSERT_EQUAL(2,t.size()); // normalized units remain the same
}
void test_invalid_data_and_long_gaps_are_not_joined() {
    Trend t;TrendRange range;
    Reading r=voltage(2);t.push(r,Model::UT60BT,100);
    r.numeric=false;t.push(r,Model::UT60BT,200);
    TEST_ASSERT_TRUE(isnan(t.point(1).value));
    TEST_ASSERT_FALSE(Trend::join(t.point(0),t.point(1)));
    TEST_ASSERT_FALSE(Trend::join(TrendPoint{100,1},TrendPoint{3101,2}));
    TEST_ASSERT_TRUE(Trend::join(TrendPoint{100,1},TrendPoint{3100,2}));
    TEST_ASSERT_TRUE(t.range(200,range));
    TEST_ASSERT_FLOAT_WITHIN(.0001,1.9,range.low);
    TEST_ASSERT_FLOAT_WITHIN(.0001,2.1,range.high);
}
void test_scope_window_survives_clock_wrap() {
    Trend t;TrendRange r;
    t.push(voltage(-2),Model::UT60BT,0xfffffff0u);
    t.push(voltage(2),Model::UT60BT,20);
    TEST_ASSERT_TRUE(t.range(30,r));
    TEST_ASSERT_FLOAT_WITHIN(.0001,-2.4,r.low);
    TEST_ASSERT_FLOAT_WITHIN(.0001,2.4,r.high);
    TEST_ASSERT_TRUE(Trend::join(t.point(0),t.point(1)));
    TEST_ASSERT_FALSE(t.range(60021,r));
}
void test_prefixed_unit_change_resets_even_when_mode_is_unchanged() {
    Trend t;Reading r=voltage(999);r.hasMetadata=true;r.modeKey=0x01000600;
    strcpy(r.unitLabel,"Ohm");t.push(r,Model::UT60BT,100);
    r.value=1;strcpy(r.unitLabel,"kOhm");t.push(r,Model::UT60BT,200);
    TEST_ASSERT_EQUAL(1,t.size());TEST_ASSERT_FLOAT_WITHIN(.0001,1,t.point(0).value);
}
void test_explicit_reset_clears_previous_meter_history() {
    Trend t;t.push(voltage(99),Model::UT60BT,100);t.reset();TrendRange r;
    TEST_ASSERT_EQUAL(0,t.size());TEST_ASSERT_FALSE(t.range(200,r));
    t.push(voltage(1),Model::UT60BT,200);TEST_ASSERT_EQUAL(1,t.size());
    TEST_ASSERT_FLOAT_WITHIN(.0001,1,t.point(0).value);
}
void test_snapshot_identity_must_match_history_before_plotting() {
    Trend t;Reading r=voltage(10);r.hasMetadata=true;r.modeKey=1;strcpy(r.unitLabel,"V");
    TEST_ASSERT_FALSE(t.matchesReading(r));t.push(r,Model::UT60BT,100);TEST_ASSERT_TRUE(t.matchesReading(r));
    Reading pending=r;strcpy(pending.unitLabel,"A");TEST_ASSERT_FALSE(t.matchesReading(pending));
    pending=r;pending.modeKey=2;TEST_ASSERT_FALSE(t.matchesReading(pending));
    pending=r;pending.channel=1;TEST_ASSERT_FALSE(t.matchesReading(pending));
    pending=r;strcpy(pending.unitLabel,"mV");TEST_ASSERT_FALSE(t.matchesReading(pending));
    t.push(pending,Model::UT60BT,200);TEST_ASSERT_TRUE(t.matchesReading(pending));TEST_ASSERT_FALSE(t.matchesReading(r));
    t.reset();TEST_ASSERT_FALSE(t.matchesReading(pending));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_history_keeps_latest_samples_in_order);
    RUN_TEST(test_auto_range_ignores_expired_points_and_adds_headroom);
    RUN_TEST(test_flat_signals_have_a_visible_nonzero_axis);
    RUN_TEST(test_unit_channel_and_model_changes_clear_history);
    RUN_TEST(test_invalid_data_and_long_gaps_are_not_joined);
    RUN_TEST(test_scope_window_survives_clock_wrap);
    RUN_TEST(test_prefixed_unit_change_resets_even_when_mode_is_unchanged);
    RUN_TEST(test_explicit_reset_clears_previous_meter_history);
    RUN_TEST(test_snapshot_identity_must_match_history_before_plotting);
    return UNITY_END();
}
