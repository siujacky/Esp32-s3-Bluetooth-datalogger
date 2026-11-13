#include <unity.h>
#include <wifi_qr.h>
#include <string>
void setUp(){}
void tearDown(){}
static void test_phone_wifi_payload() {
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Meter-123456;P:12345678;;",meter_wifi::payload("Meter-123456","12345678").c_str());
}
static void test_reserved_wifi_characters_are_escaped() {
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Lab\\;A\\\\B;P:a\\:b\\,c\\\"d123;;",meter_wifi::payload("Lab;A\\B","a:b,c\"d123").c_str());
}
static void test_wifi_matrix_fits_the_small_display() {
    meter_wifi::Qr qr;TEST_ASSERT_TRUE(meter_wifi::encode("Meter-858428","abcdefghijklmnop",qr));
    TEST_ASSERT_LESS_OR_EQUAL(33,qr.size());
    TEST_ASSERT_TRUE(qr.dark(0,0));TEST_ASSERT_FALSE(qr.dark(1,1));TEST_ASSERT_TRUE(qr.dark(3,3));
}
static void test_invalid_credentials_clear_old_matrix() {
    meter_wifi::Qr qr;TEST_ASSERT_TRUE(meter_wifi::encode("Meter-123456","12345678",qr));
    TEST_ASSERT_FALSE(meter_wifi::encode("","12345678",qr));TEST_ASSERT_EQUAL_UINT(0,qr.size());
    TEST_ASSERT_FALSE(meter_wifi::encode("Meter-123456","short",qr));
    TEST_ASSERT_FALSE(meter_wifi::encode(std::string("bad\0name",8),"12345678",qr));
}
static void test_maximum_escaped_credentials_fit() {
    meter_wifi::Qr qr;TEST_ASSERT_TRUE(meter_wifi::encode(std::string(32,';'),std::string(63,'\\'),qr));
    TEST_ASSERT_LESS_OR_EQUAL(57,qr.size());
}
int main(){UNITY_BEGIN();RUN_TEST(test_phone_wifi_payload);RUN_TEST(test_reserved_wifi_characters_are_escaped);
    RUN_TEST(test_wifi_matrix_fits_the_small_display);RUN_TEST(test_invalid_credentials_clear_old_matrix);RUN_TEST(test_maximum_escaped_credentials_fit);return UNITY_END();}
