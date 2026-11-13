#include <unity.h>
#include <meter_rules.h>
#include <cmath>
#include <vector>
using namespace meter_rules;
void setUp() {}
void tearDown() {}
static Config rule() { Config c; c.unit="V"; c.threshold=10; c.hysteresis=2; c.cooldownMs=1000; return c; }
static Reading reading(double value,uint64_t at=1000) { Reading r; r.unit="V"; r.value=value; r.numeric=true; r.uptimeMs=at; return r; }
void test_above_crossing_requires_hysteresis_rearm() {
    Config c=rule(); State s;
    TEST_ASSERT_FALSE(evaluate(c,s,reading(10)));
    TEST_ASSERT_TRUE(evaluate(c,s,reading(11)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(12,2000)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(9,2100)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(12,2200)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(8,2300)));
    TEST_ASSERT_TRUE(evaluate(c,s,reading(11,2400)));
}
void test_below_crossing_requires_hysteresis_rearm() {
    Config c=rule(); c.op=Operator::Below; State s;
    TEST_ASSERT_TRUE(evaluate(c,s,reading(9)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(11,2000)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(9,2100)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(12,2200)));
    TEST_ASSERT_TRUE(evaluate(c,s,reading(9,2300)));
}
void test_cooldown_consumes_crossing_without_delayed_flood() {
    Config c=rule(); State s;
    TEST_ASSERT_TRUE(evaluate(c,s,reading(11,0)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(8,100)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(11,200)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(11,2000)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(8,2100)));
    TEST_ASSERT_TRUE(evaluate(c,s,reading(11,2200)));
}
void test_wrong_identity_units_and_bad_values_do_not_fire_or_rearm() {
    Config c=rule(); State s; Reading r=reading(11);
    r.unit="mV"; TEST_ASSERT_FALSE(evaluate(c,s,r));
    r=reading(11);r.meter=1;TEST_ASSERT_FALSE(evaluate(c,s,r));
    r=reading(11);r.channel=1;TEST_ASSERT_FALSE(evaluate(c,s,r));
    r=reading(11);r.numeric=false;TEST_ASSERT_FALSE(evaluate(c,s,r));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(NAN)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(INFINITY)));
    TEST_ASSERT_TRUE(evaluate(c,s,reading(11)));
    r=reading(0,2000);r.unit="A";TEST_ASSERT_FALSE(evaluate(c,s,r));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(12,2100)));
    c.enabled=false;TEST_ASSERT_FALSE(evaluate(c,s,reading(0,2200)));
    c.enabled=true;TEST_ASSERT_FALSE(evaluate(c,s,reading(12,2300)));
}
void test_invalid_rule_and_backwards_clock_are_safe() {
    Config c=rule();State s;c.hysteresis=-1;
    TEST_ASSERT_FALSE(evaluate(c,s,reading(11)));
    c=rule();c.threshold=NAN;TEST_ASSERT_FALSE(evaluate(c,s,reading(11)));
    c=rule();TEST_ASSERT_TRUE(evaluate(c,s,reading(11,3000)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(8,3100)));
    TEST_ASSERT_FALSE(evaluate(c,s,reading(11,2000)));
}
void test_url_accepts_only_unambiguous_http_destinations() {
    TEST_ASSERT_TRUE(validUrl("http://192.168.4.2:8080/hook?x=1"));
    TEST_ASSERT_TRUE(validUrl("https://example.com/a"));
    TEST_ASSERT_FALSE(validUrl("file:///tmp/x"));
    TEST_ASSERT_FALSE(validUrl("http://user:password@example.com/a"));
    TEST_ASSERT_FALSE(validUrl("http:///a"));
    TEST_ASSERT_FALSE(validUrl("https://example.com/#fragment"));
    TEST_ASSERT_FALSE(validUrl("http://example.com\r\nInjected: true"));
    TEST_ASSERT_FALSE(validUrl("http://example.com\\evil"));
    TEST_ASSERT_FALSE(validUrl("http://example.com:99999/a"));
}
void test_headers_disallow_request_framing_and_injection() {
    TEST_ASSERT_TRUE(validHeader("Authorization","Bearer token"));
    TEST_ASSERT_TRUE(validHeader("X-Meter","m1"));
    TEST_ASSERT_FALSE(validHeader("Host","evil"));
    TEST_ASSERT_FALSE(validHeader("content-length","1000"));
    TEST_ASSERT_FALSE(validHeader("Transfer-Encoding","chunked"));
    TEST_ASSERT_FALSE(validHeader("Connection","keep-alive"));
    TEST_ASSERT_FALSE(validHeader("Bad Name","value"));
    TEST_ASSERT_FALSE(validHeader("X-Meter","value\nOther: bad"));
}
void test_percent_encoding_and_literal_template_substitution() {
    TEST_ASSERT_EQUAL_STRING("mV%20%26%20%CE%A9",urlEncode("mV & \xce\xa9").c_str());
    TEST_ASSERT_EQUAL_STRING("-1.25|mV|0|123|{{other}}",render("{{value}}|{{unit}}|{{meter}}|{{timestamp}}|{{other}}","-1.25","mV","0","123").c_str());
    TEST_ASSERT_EQUAL_STRING("{{meter}}",render("{{unit}}","0","{{meter}}","0","1").c_str());
}
void test_feed_cursor_reports_eviction_and_future_boot_cursor() {
    FeedCursor c=feedCursor(10,4,0);
    TEST_ASSERT_EQUAL_UINT64(7,c.oldest);TEST_ASSERT_EQUAL_UINT64(10,c.latest);
    TEST_ASSERT_EQUAL_UINT64(6,c.next);TEST_ASSERT_TRUE(c.gap);
    c=feedCursor(10,4,8);TEST_ASSERT_EQUAL_UINT64(8,c.next);TEST_ASSERT_FALSE(c.gap);
    c=feedCursor(10,4,10);TEST_ASSERT_EQUAL_UINT64(10,c.next);TEST_ASSERT_FALSE(c.gap);
    c=feedCursor(10,4,42);TEST_ASSERT_EQUAL_UINT64(6,c.next);TEST_ASSERT_TRUE(c.gap);
    c=feedCursor(0,4,0);TEST_ASSERT_EQUAL_UINT64(0,c.oldest);TEST_ASSERT_FALSE(c.gap);
    c=feedCursor(0,4,42);TEST_ASSERT_EQUAL_UINT64(0,c.next);TEST_ASSERT_TRUE(c.gap);
}
void test_failed_configuration_save_retains_previous_rules_and_cooldown() {
    struct StoredRule {int id;bool enabled;State state;};
    std::vector<StoredRule> live{{1,true,State{}}},staged=live;
    live[0].state.armed=false;live[0].state.hasFired=true;live[0].state.lastFireMs=1234;
    staged[0].enabled=false;staged[0].state=State{};staged.push_back({2,true,State{}});
    unsigned saves=0;
    TEST_ASSERT_FALSE(commitIfSaved(live,staged,[&](const std::vector<StoredRule> &candidate){++saves;return candidate.size()==99;}));
    TEST_ASSERT_EQUAL(1,saves);TEST_ASSERT_EQUAL(1,live.size());TEST_ASSERT_TRUE(live[0].enabled);
    TEST_ASSERT_FALSE(live[0].state.armed);TEST_ASSERT_TRUE(live[0].state.hasFired);TEST_ASSERT_EQUAL_UINT64(1234,live[0].state.lastFireMs);
    staged.clear();
    TEST_ASSERT_FALSE(commitIfSaved(live,staged,[&](const std::vector<StoredRule>&){++saves;return false;}));
    TEST_ASSERT_EQUAL(1,live.size());TEST_ASSERT_FALSE(live[0].state.armed);
    TEST_ASSERT_TRUE(commitIfSaved(live,staged,[&](const std::vector<StoredRule>&){++saves;return true;}));
    TEST_ASSERT_EQUAL(0,live.size());TEST_ASSERT_EQUAL(3,saves);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_above_crossing_requires_hysteresis_rearm);
    RUN_TEST(test_below_crossing_requires_hysteresis_rearm);
    RUN_TEST(test_cooldown_consumes_crossing_without_delayed_flood);
    RUN_TEST(test_wrong_identity_units_and_bad_values_do_not_fire_or_rearm);
    RUN_TEST(test_invalid_rule_and_backwards_clock_are_safe);
    RUN_TEST(test_url_accepts_only_unambiguous_http_destinations);
    RUN_TEST(test_headers_disallow_request_framing_and_injection);
    RUN_TEST(test_percent_encoding_and_literal_template_substitution);
    RUN_TEST(test_feed_cursor_reports_eviction_and_future_boot_cursor);
    RUN_TEST(test_failed_configuration_save_retains_previous_rules_and_cooldown);
    return UNITY_END();
}
