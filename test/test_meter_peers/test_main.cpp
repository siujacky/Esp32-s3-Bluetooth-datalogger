#include <unity.h>
#include <meter_peer_preferences.h>
#include <cstring>
using namespace meter_ui;
void setUp(){}void tearDown(){}
static void four_peers_roundtrip_and_legacy_three_migrate(){
 PeerPreferences p;strcpy(p.peers[0].address,"18:90:67:fb:a3:b9");strcpy(p.peers[0].profile,"ut60bt");p.peers[0].enabled=1;
 strcpy(p.peers[3].address,"40:c1:f6:16:b2:d1");strcpy(p.peers[3].profile,"ut202bt");p.peers[3].type=1;p.peers[3].enabled=1;
 uint8_t record[peerRecordBytes];TEST_ASSERT_TRUE(encodePeerPreferences(p,record,sizeof(record)));PeerPreferences next;bool legacy=true;TEST_ASSERT_TRUE(decodePeerPreferences(record,sizeof(record),next,&legacy));TEST_ASSERT_FALSE(legacy);TEST_ASSERT_EQUAL_STRING(p.peers[3].address,next.peers[3].address);TEST_ASSERT_EQUAL(1,next.peers[3].enabled);
 uint8_t old[160]={0x31,0x52,0x54,0x4d};memcpy(old+4,&p.peers[0],52);TEST_ASSERT_TRUE(decodePeerPreferences(old,sizeof(old),next,&legacy));TEST_ASSERT_TRUE(legacy);TEST_ASSERT_EQUAL_STRING(p.peers[0].address,next.peers[0].address);TEST_ASSERT_EQUAL(1,next.peers[0].enabled);TEST_ASSERT_EQUAL(0,next.peers[3].enabled);TEST_ASSERT_EQUAL_STRING("",next.peers[3].address);
}
static void corrupt_records_fail_without_partial_publication(){PeerPreferences p;uint8_t b[peerRecordBytes];TEST_ASSERT_TRUE(encodePeerPreferences(p,b,sizeof(b)));PeerPreferences next;next.peers[0].enabled=7;for(unsigned i=0;i<sizeof(b);++i){b[i]^=1;TEST_ASSERT_FALSE(decodePeerPreferences(b,sizeof(b),next));TEST_ASSERT_EQUAL(7,next.peers[0].enabled);b[i]^=1;}TEST_ASSERT_FALSE(decodePeerPreferences(b,sizeof(b)-1,next));p.peers[0].enabled=1;TEST_ASSERT_FALSE(encodePeerPreferences(p,b,sizeof(b)));}
int main(){UNITY_BEGIN();RUN_TEST(four_peers_roundtrip_and_legacy_three_migrate);RUN_TEST(corrupt_records_fail_without_partial_publication);return UNITY_END();}
