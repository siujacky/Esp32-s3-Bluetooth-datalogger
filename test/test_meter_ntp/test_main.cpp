#include <unity.h>
#include "../../src/esp32_touch/meter/meter_ntp_state.h"
#include <cstring>

void setUp(){}
void tearDown(){}
static void accepts_dns_and_unambiguous_unicast_ipv4(){
    const char *valid[]={"pool.ntp.org","time.cloudflare.com","NTP-1.local","ntp","192.168.4.2","1.1.1.1"};
    for(const char *server:valid)TEST_ASSERT_TRUE_MESSAGE(meter_ntp::validServer(server),server);
    uint32_t ip=0;TEST_ASSERT_TRUE(meter_ntp::parseIpv4("192.168.4.2",ip));TEST_ASSERT_EQUAL_HEX32(0xc0a80402,ip);
}
static void rejects_urls_ports_ambiguous_and_non_host_addresses(){
    const char *invalid[]={nullptr,""," pool.ntp.org","pool.ntp.org ","a/b","https://pool.ntp.org","pool.ntp.org:123","a\nb","a_b","-ntp.org","ntp-.org","ntp..org","ntp.org.","192.168.004.2","256.1.2.3","1.2.3","0.0.0.0","0.1.2.3","127.0.0.1","224.0.0.1","255.255.255.255","1234","::1"};
    for(const char *server:invalid)TEST_ASSERT_FALSE_MESSAGE(meter_ntp::validServer(server),server?server:"null");
    char label[65];memset(label,'a',64);label[64]=0;TEST_ASSERT_FALSE(meter_ntp::validServer(label));
    char longHost[255];memset(longHost,'a',254);longHost[254]=0;TEST_ASSERT_FALSE(meter_ntp::validServer(longHost));
}
static void only_explicit_softap_peer_bypasses_station_requirement(){
    TEST_ASSERT_TRUE(meter_ntp::localApServer("192.168.4.2",0xc0a80401,0xffffff00));
    const char *invalid[]={"pool.ntp.org","192.168.1.2","192.168.4.0","192.168.4.255","192.168.4.1"};
    for(const char *server:invalid)TEST_ASSERT_FALSE(meter_ntp::localApServer(server,0xc0a80401,0xffffff00));
    TEST_ASSERT_FALSE(meter_ntp::localApServer("192.168.4.2",0,0));
}
static void persisted_config_roundtrips_and_detects_corruption(){
    meter_ntp::Config original;original.enabled=false;strcpy(original.server,"192.168.4.2");
    uint8_t bytes[meter_ntp::configBytes];TEST_ASSERT_TRUE(meter_ntp::encodeConfig(original,bytes,sizeof(bytes)));
    meter_ntp::Config decoded;TEST_ASSERT_TRUE(meter_ntp::decodeConfig(bytes,sizeof(bytes),decoded));
    TEST_ASSERT_FALSE(decoded.enabled);TEST_ASSERT_EQUAL_STRING("192.168.4.2",decoded.server);
    TEST_ASSERT_FALSE(meter_ntp::decodeConfig(bytes,sizeof(bytes)-1,decoded));
    for(size_t i=0;i<sizeof(bytes);++i){bytes[i]^=1;TEST_ASSERT_FALSE(meter_ntp::decodeConfig(bytes,sizeof(bytes),decoded));bytes[i]^=1;}
    strcpy(original.server,"http://bad");TEST_ASSERT_FALSE(meter_ntp::encodeConfig(original,bytes,sizeof(bytes)));
}
static void clock_accepts_only_valid_ntp_event_and_preserves_milliseconds(){
    meter_ntp::TimeState clock;TEST_ASSERT_FALSE(clock.synced);TEST_ASSERT_EQUAL_UINT64(0,clock.current(123));
    TEST_ASSERT_TRUE(clock.accept(1794480000LL,123456,1000));
    TEST_ASSERT_TRUE(clock.synced);TEST_ASSERT_EQUAL_UINT64(1794480000123ULL,clock.lastSyncUnixMs);
    TEST_ASSERT_EQUAL_UINT64(1794480000173ULL,clock.current(1050));
    TEST_ASSERT_EQUAL_UINT64(0,clock.current(999));
    TEST_ASSERT_FALSE(clock.accept(1577836799LL,999999,2000));
    TEST_ASSERT_FALSE(clock.accept(4102444800LL,1000,2000));
    TEST_ASSERT_FALSE(clock.accept(1794480000LL,-1,2000));
    TEST_ASSERT_FALSE(clock.accept(1794480000LL,1000000,2000));
    TEST_ASSERT_EQUAL_UINT64(1794480000123ULL,clock.lastSyncUnixMs);
}
static void epoch_boundaries_do_not_overflow_or_restore_synced_on_reboot(){
    meter_ntp::TimeState clock;TEST_ASSERT_TRUE(clock.accept(1577836800LL,0,0));
    TEST_ASSERT_EQUAL_UINT64(1577836800001ULL,clock.current(1));
    TEST_ASSERT_TRUE(clock.accept(4102444800LL,0,4));TEST_ASSERT_EQUAL_UINT64(0,clock.current(5));
    TEST_ASSERT_FALSE(clock.accept(INT64_MAX,0,5));
    meter_ntp::TimeState rebooted;TEST_ASSERT_FALSE(rebooted.synced);TEST_ASSERT_EQUAL_UINT64(0,rebooted.current(100));
}
#ifdef METER_NTP_ADAPTER_TEST
void runAdapterTests();
#endif
int main(){UNITY_BEGIN();RUN_TEST(accepts_dns_and_unambiguous_unicast_ipv4);RUN_TEST(rejects_urls_ports_ambiguous_and_non_host_addresses);RUN_TEST(only_explicit_softap_peer_bypasses_station_requirement);RUN_TEST(persisted_config_roundtrips_and_detects_corruption);RUN_TEST(clock_accepts_only_valid_ntp_event_and_preserves_milliseconds);RUN_TEST(epoch_boundaries_do_not_overflow_or_restore_synced_on_reboot);
#ifdef METER_NTP_ADAPTER_TEST
runAdapterTests();
#endif
return UNITY_END();}
