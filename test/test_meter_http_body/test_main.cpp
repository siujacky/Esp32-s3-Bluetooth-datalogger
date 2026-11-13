#include <unity.h>
#include "../../src/esp32_touch/meter/meter_http_body.h"
#include <string>
#include <cstring>
#include <algorithm>
void setUp(){} void tearDown(){}
struct Client {
    std::string bytes;size_t at=0,maxRead=0,chunk=1;uint32_t *clock;uint32_t readyAt=0;bool linked=true;
    Client(std::string data,uint32_t &now):bytes(data),clock(&now){}
    int available(){return *clock>=readyAt?int(std::min(chunk,bytes.size()-at)):0;}
    bool connected(){return linked;}
    int read(uint8_t *out,size_t wanted){maxRead=std::max(maxRead,wanted);size_t count=std::min(wanted,bytes.size()-at);std::memcpy(out,bytes.data()+at,count);at+=count;return int(count);}
};
static void exact_short_body_leaves_following_bytes_untouched(){
    uint32_t now=0;Client client("{}NEXT",now);client.chunk=100;std::string output;size_t received=0;
    auto result=meter_http::readBody(client,2,8192,5000,[&](){return now;},[&](){++now;},[&](const uint8_t *p,size_t n){output.append(reinterpret_cast<const char*>(p),n);return true;},received);
    TEST_ASSERT_EQUAL(meter_http::BodyResult::Complete,result);TEST_ASSERT_EQUAL_STRING("{}",output.c_str());TEST_ASSERT_EQUAL(2,client.at);TEST_ASSERT_EQUAL(2,client.maxRead);TEST_ASSERT_EQUAL(0,now);
}
static void fragments_and_delayed_data_complete_without_buffer_size_wait(){
    uint32_t now=0;Client client(std::string(1437,'x'),now);client.chunk=128;client.readyAt=150;std::string output;size_t received=0;
    auto result=meter_http::readBody(client,1437,8192,5000,[&](){return now;},[&](){++now;},[&](const uint8_t *p,size_t n){TEST_ASSERT_EQUAL(0,p[n]);output.append(reinterpret_cast<const char*>(p),n);return true;},received);
    TEST_ASSERT_EQUAL(meter_http::BodyResult::Complete,result);TEST_ASSERT_EQUAL(1437,received);TEST_ASSERT_EQUAL(1437,output.size());TEST_ASSERT_EQUAL(150,now);TEST_ASSERT_LESS_OR_EQUAL(128,client.maxRead);
}
static void time_disconnect_limits_and_allocation_failure_are_explicit(){
    uint32_t now=0;Client client("",now);size_t received=0;auto clock=[&](){return now;};auto pause=[&](){++now;};auto append=[](const uint8_t*,size_t){return true;};
    TEST_ASSERT_EQUAL(meter_http::BodyResult::TooLarge,meter_http::readBody(client,8193,8192,5,clock,pause,append,received));TEST_ASSERT_EQUAL(0,now);
    TEST_ASSERT_EQUAL(meter_http::BodyResult::Timeout,meter_http::readBody(client,2,8192,5,clock,pause,append,received));TEST_ASSERT_EQUAL(5,now);
    client.linked=false;TEST_ASSERT_EQUAL(meter_http::BodyResult::Disconnected,meter_http::readBody(client,2,8192,5,clock,pause,append,received));
    Client data("{}",now);TEST_ASSERT_EQUAL(meter_http::BodyResult::NoMemory,meter_http::readBody(data,2,8192,5,clock,pause,[](const uint8_t*,size_t){return false;},received));
}
static void timeout_handles_clock_wrap(){
    uint32_t now=0xfffffffeu;Client client("",now);size_t received=0;
    TEST_ASSERT_EQUAL(meter_http::BodyResult::Timeout,meter_http::readBody(client,1,8192,5,[&](){return now;},[&](){++now;},[](const uint8_t*,size_t){return true;},received));TEST_ASSERT_EQUAL(3,now);
}
int main(){UNITY_BEGIN();RUN_TEST(exact_short_body_leaves_following_bytes_untouched);RUN_TEST(fragments_and_delayed_data_complete_without_buffer_size_wait);RUN_TEST(time_disconnect_limits_and_allocation_failure_are_explicit);RUN_TEST(timeout_handles_clock_wrap);return UNITY_END();}
