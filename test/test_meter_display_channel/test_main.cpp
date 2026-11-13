#include "../../src/esp32_touch/meter/meter_ble.h"
#include <unity.h>
void setUp(){} void tearDown(){}

static void explicit_automatic_stale_missing_and_wrapped_channels(){
    bool has[meter_ble::MAX_CHANNELS]={true,true,true};
    uint32_t at[meter_ble::MAX_CHANNELS]={100,4000,4500};
    // Automatic selection moves from an expired preferred channel to a fresh
    // channel, while explicit selection never substitutes a different unit.
    TEST_ASSERT_EQUAL(1,meter_ble::displayChannel(has,at,0,-1,5000));
    TEST_ASSERT_EQUAL(2,meter_ble::displayChannel(has,at,2,-1,5000));
    TEST_ASSERT_EQUAL(0,meter_ble::displayChannel(has,at,0,0,5000));
    TEST_ASSERT_EQUAL(7,meter_ble::displayChannel(has,at,0,7,5000));
    // With no fresh reading, retain the preferred channel's stale value.
    TEST_ASSERT_EQUAL(2,meter_ble::displayChannel(has,at,2,-1,10000));
    // A missing preferred channel falls back to the first available stale one.
    TEST_ASSERT_EQUAL(0,meter_ble::displayChannel(has,at,7,-1,10000));
    // Freshness arithmetic must handle the device clock wrapping.
    at[0]=0xfffffff0u;at[1]=0xffff0000u;at[2]=0xffff0000u;
    TEST_ASSERT_EQUAL(0,meter_ble::displayChannel(has,at,1,-1,20));
    // Invalid requests use automatic resolution and never index out of bounds.
    TEST_ASSERT_EQUAL(0,meter_ble::displayChannel(has,at,99,99,20));
    for(unsigned i=0;i<meter_ble::MAX_CHANNELS;++i)has[i]=false;
    TEST_ASSERT_EQUAL(3,meter_ble::displayChannel(has,at,3,-1,20));
    static_assert(sizeof(meter_ble::DisplaySnapshot)<=256,"display snapshot exceeds the loop stack budget");
}
int main(){UNITY_BEGIN();RUN_TEST(explicit_automatic_stale_missing_and_wrapped_channels);return UNITY_END();}
