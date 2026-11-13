#include <unity.h>
#include <meter_value_preferences.h>
#include <cstring>
#include <type_traits>
void setUp(){}void tearDown(){}
static void preferences_roundtrip_without_struct_padding(){
    meter_ui::ValuePreferences prefs;prefs.presets[2]=meter_ui::ValuePreset::LargeLargeSmall;prefs.formats[2][7]={8,6,meter_ui::Padding::Zero};uint8_t bytes[meter_ui::valueRecordBytes];TEST_ASSERT_TRUE(meter_ui::encodeValuePreferences(prefs,bytes,sizeof(bytes)));meter_ui::ValuePreferences next;TEST_ASSERT_TRUE(meter_ui::decodeValuePreferences(bytes,sizeof(bytes),next));TEST_ASSERT_EQUAL(4,unsigned(next.presets[2]));TEST_ASSERT_EQUAL(8,next.formats[2][7].digits);TEST_ASSERT_EQUAL(6,next.formats[2][7].decimals);TEST_ASSERT_EQUAL(1,unsigned(next.formats[2][7].padding));
}
static void corrupt_short_and_invalid_records_are_rejected(){
    meter_ui::ValuePreferences prefs;uint8_t bytes[meter_ui::valueRecordBytes];TEST_ASSERT_TRUE(meter_ui::encodeValuePreferences(prefs,bytes,sizeof(bytes)));meter_ui::ValuePreferences next;TEST_ASSERT_FALSE(meter_ui::decodeValuePreferences(bytes,sizeof(bytes)-1,next));for(unsigned i=0;i<sizeof(bytes);++i){bytes[i]^=1;TEST_ASSERT_FALSE(meter_ui::decodeValuePreferences(bytes,sizeof(bytes),next));bytes[i]^=1;}prefs.formats[1][2].digits=2;TEST_ASSERT_FALSE(meter_ui::encodeValuePreferences(prefs,bytes,sizeof(bytes)));
}
static void legacy_three_formats_keep_values_and_default_fourth(){
    meter_ui::ValuePreferences next;TEST_ASSERT_EQUAL(4,std::extent<decltype(next.formats)>::value);
    uint8_t old[83]={'M','V',1,0,0,2,4};unsigned at=7;
    for(unsigned s=0;s<3;++s)for(unsigned c=0;c<8;++c){old[at++]=(s==2&&c==7)?8:6;old[at++]=(s==2&&c==7)?6:2;old[at++]=(s==2&&c==7)?1:0;}
    auto crc=meter_ui::valuePreferencesCrc(old,79);for(unsigned i=0;i<4;++i)old[79+i]=crc>>(i*8);
    TEST_ASSERT_TRUE(meter_ui::decodeValuePreferences(old,sizeof(old),next));TEST_ASSERT_EQUAL(8,next.formats[2][7].digits);TEST_ASSERT_EQUAL(6,next.formats[2][7].decimals);TEST_ASSERT_EQUAL(1,unsigned(next.formats[2][7].padding));TEST_ASSERT_EQUAL(6,next.formats[3][7].digits);TEST_ASSERT_EQUAL(2,next.formats[3][7].decimals);TEST_ASSERT_EQUAL(0,unsigned(next.formats[3][7].padding));TEST_ASSERT_EQUAL(5,unsigned(next.presets[3]));
}
int main(){UNITY_BEGIN();RUN_TEST(preferences_roundtrip_without_struct_padding);RUN_TEST(corrupt_short_and_invalid_records_are_rejected);RUN_TEST(legacy_three_formats_keep_values_and_default_fourth);return UNITY_END();}
