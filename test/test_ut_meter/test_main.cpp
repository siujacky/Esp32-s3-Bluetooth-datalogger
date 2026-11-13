#include <unity.h>
#include <ut_meter.h>
#include <math.h>
#include <string.h>
using namespace ut_meter;
void setUp() {}
void tearDown() {}
// Construct wire packets independently of the production decoder.
static void packet(uint8_t *f, uint8_t mode, char range, const char *text) {
    memset(f, 0, FRAME_SIZE);
    f[0]=0xab; f[1]=0xcd; f[2]=16; f[3]=mode; f[4]=range;
    memset(f+5, ' ', 7);
    memcpy(f+5, text, strlen(text));
    unsigned sum=0; for(int i=0;i<17;i++) sum+=f[i];
    f[17]=sum>>8; f[18]=sum;
}
static void checksum(uint8_t *f) {
    unsigned sum=0; for(int i=0;i<17;i++)sum+=f[i];
    f[17]=sum>>8; f[18]=sum;
}
void test_ut60_voltage_ranges_are_volts() {
    uint8_t f[19]; Reading r;
    packet(f, 2, '0', "123.4");
    TEST_ASSERT_TRUE(decode(f,19,Model::UT60BT,r));
    TEST_ASSERT_TRUE(r.numeric); TEST_ASSERT_FLOAT_WITHIN(.000001,.1234,r.value);
    packet(f,2,'1',"1.234");
    TEST_ASSERT_TRUE(decode(f,19,Model::UT60BT,r));
    TEST_ASSERT_FLOAT_WITHIN(.000001,1.234,r.value);
    TEST_ASSERT_EQUAL_STRING("V",unit(r.mode));
    TEST_ASSERT_TRUE(decode(f,19,Model::UT61Plus,r));
    TEST_ASSERT_FLOAT_WITHIN(.000001,1.234,r.value);
}
void test_resistance_capacitance_and_current_scales() {
    struct Case { Model model; uint8_t mode; char range; const char *text; double value; };
    const Case cases[] = {
        {Model::UT61Plus,6,'1',"1.234",1234},
        {Model::UT61Plus,9,'2',"1.234",1234},
        {Model::UT60BT,9,'2',"1.234",1.234},
        {Model::UT60BT,14,'1',"1.234",1234},
        {Model::UT61Plus,14,'1',"1.234",1.234},
        {Model::UT61Plus,16,'0',"-1.234",-1.234}
    };
    for(const auto &c:cases) { uint8_t f[19]; Reading r; packet(f,c.mode,c.range,c.text);
        TEST_ASSERT_TRUE(decode(f,19,c.model,r));
        TEST_ASSERT_FLOAT_WITHIN(.000001,c.value,r.value);
    }
}
void test_flags_alternate_channel_and_overload() {
    uint8_t f[19]; Reading r; packet(f,25,'0'," 2.345");
    f[14]=3; f[15]=2; f[16]=8; checksum(f);
    TEST_ASSERT_TRUE(decode(f,19,Model::UT61Plus,r));
    TEST_ASSERT_EQUAL(1,r.channel); TEST_ASSERT_EQUAL(3,r.flags[0]);
    TEST_ASSERT_EQUAL_STRING("AC voltage",modeName(r.mode,r.channel));
    packet(f,6,'0',"OL");
    TEST_ASSERT_TRUE(decode(f,19,Model::UT61Plus,r)); TEST_ASSERT_FALSE(r.numeric);
    TEST_ASSERT_EQUAL_STRING("OL",r.text);
}
void test_rejects_malformed_frames_without_publishing() {
    uint8_t f[19]; Reading r; r.value=42;
    packet(f,2,'0',"1.234"); f[18]^=1;
    TEST_ASSERT_FALSE(decode(f,19,Model::UT60BT,r)); TEST_ASSERT_EQUAL(42,r.value);
    TEST_ASSERT_FALSE(decode(nullptr,0,Model::UT60BT,r));
    packet(f,2,'0',"1.234");
    for(size_t n=0;n<19;n++) TEST_ASSERT_FALSE(decode(f,n,Model::UT60BT,r));
    f[2]=15; checksum(f); TEST_ASSERT_FALSE(decode(f,19,Model::UT60BT,r));
    packet(f,2,'x',"1.234"); TEST_ASSERT_FALSE(decode(f,19,Model::UT60BT,r));
    packet(f,2,'0',"nan"); TEST_ASSERT_TRUE(decode(f,19,Model::UT60BT,r));
    TEST_ASSERT_FALSE(r.numeric);
}
void test_stream_recovers_noise_bad_checksum_and_back_to_back() {
    uint8_t f[19],bad[19]; Reading r; Decoder d;
    packet(f,2,'1',"1.234"); memcpy(bad,f,19); bad[18]^=1;
    for(uint8_t b:bad) TEST_ASSERT_FALSE(d.push(b,Model::UT60BT,r));
    const uint8_t noise[]={0,0xab,0xab,0xcd,1,0xab};
    for(uint8_t b:noise) TEST_ASSERT_FALSE(d.push(b,Model::UT60BT,r));
    for(int repeat=0;repeat<2;repeat++) {
        for(int i=0;i<19;i++) TEST_ASSERT_EQUAL(i==18,d.push(f[i],Model::UT60BT,r));
        TEST_ASSERT_FLOAT_WITHIN(.000001,1.234,r.value);
    }
    d.push(0xab,Model::UT60BT,r); d.reset();
    for(int i=0;i<19;i++) TEST_ASSERT_EQUAL(i==18,d.push(f[i],Model::UT60BT,r));
}
void test_freshness_requires_connection_data_and_recent_receipt() {
    TEST_ASSERT_FALSE(isFresh(false,true,100,90));
    TEST_ASSERT_FALSE(isFresh(true,false,100,90));
    TEST_ASSERT_TRUE(isFresh(true,true,3100,100));
    TEST_ASSERT_FALSE(isFresh(true,true,3101,100));
    TEST_ASSERT_TRUE(isFresh(true,true,20,0xfffffff0u));
    TEST_ASSERT_FALSE(isFresh(true,true,3000,0xfffffff0u));
}
void test_physical_ut60bt_capture_with_spaces_and_negative_sign() {
    // Unmodified notifications from physical UT60BT 18:90:67:FB:A3:B9.
    // Mode 2 / range 0 is mV on the meter, normalized to V by dmmpy.
    const uint8_t positive[]={0xab,0xcd,0x10,0x02,0x30,0x20,0x20,0x20,0x20,0x30,0x2e,0x39,0,0,0,0,0,0x02,0xd1};
    const uint8_t negative[]={0xab,0xcd,0x10,0x02,0x30,0x20,0x2d,0x20,0x20,0x31,0x2e,0x37,0,0,0,0,0,0x02,0xdd};
    Reading r;
    TEST_ASSERT_TRUE(decode(positive,sizeof(positive),Model::UT60BT,r));
    TEST_ASSERT_TRUE(r.numeric);
    TEST_ASSERT_EQUAL_STRING("0.9",r.text);
    TEST_ASSERT_FLOAT_WITHIN(.0000001,.0009,r.value);
    TEST_ASSERT_TRUE(decode(negative,sizeof(negative),Model::UT60BT,r));
    TEST_ASSERT_TRUE(r.numeric);
    TEST_ASSERT_EQUAL_STRING("-1.7",r.text);
    TEST_ASSERT_FLOAT_WITHIN(.0000001,-.0017,r.value);
}
void test_explicit_unknown_unit_never_invents_legacy_units() {
    Reading r;r.mode=6;r.hasMetadata=true;
    TEST_ASSERT_EQUAL_STRING("",unit(r));
    r.hasMetadata=false;TEST_ASSERT_EQUAL_STRING("Ohm",unit(r));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ut60_voltage_ranges_are_volts);
    RUN_TEST(test_resistance_capacitance_and_current_scales);
    RUN_TEST(test_flags_alternate_channel_and_overload);
    RUN_TEST(test_rejects_malformed_frames_without_publishing);
    RUN_TEST(test_stream_recovers_noise_bad_checksum_and_back_to_back);
    RUN_TEST(test_freshness_requires_connection_data_and_recent_receipt);
    RUN_TEST(test_physical_ut60bt_capture_with_spaces_and_negative_sign);
    RUN_TEST(test_explicit_unknown_unit_never_invents_legacy_units);
    return UNITY_END();
}
