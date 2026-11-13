#include <unity.h>
#include "meter_value_settings.h"
#include "meter_display.h"
#include <Preferences.h>
FakeSerial Serial;
namespace fake {bool failWrite=false;unsigned writes=0;void (*unlock)()=nullptr;}
namespace meter_display {unsigned value=1;unsigned count(){return value;}bool requestCount(unsigned c){value=c;return true;}bool tick(){return true;}}
void setUp(){} void tearDown(){}
static bool premature=false;
static void observe(){const auto p=meter_values::snapshot();if(p.formats[0][0].digits==7&&meter_values::saved())premature=true;}
static void changed_value_is_never_acknowledged_before_durable_write(){
 meter_values::begin();TEST_ASSERT_TRUE(meter_values::saved());
 meter_ui::NumberFormat f;f.digits=7;
 TEST_ASSERT_TRUE(meter_values::requestFormat(0,0,f));TEST_ASSERT_EQUAL(0,fake::writes);TEST_ASSERT_FALSE(meter_values::saved());
 fake::failWrite=true;fake::unlock=observe;TEST_ASSERT_TRUE(meter_values::tick());fake::unlock=nullptr;
 TEST_ASSERT_FALSE(premature);TEST_ASSERT_FALSE(meter_values::saved());TEST_ASSERT_EQUAL(7,meter_values::snapshot().formats[0][0].digits);
 fake::failWrite=false;TEST_ASSERT_TRUE(meter_values::requestFormat(0,0,f));TEST_ASSERT_TRUE(meter_values::tick());TEST_ASSERT_TRUE(meter_values::saved());
 for(unsigned i=0;i<8;++i){TEST_ASSERT_TRUE(meter_values::requestFormat(0,0,f));}TEST_ASSERT_FALSE(meter_values::requestFormat(0,0,f));
 TEST_ASSERT_TRUE(meter_values::tick());TEST_ASSERT_TRUE(meter_values::saved());
}
static void old_key_survives_migration_and_failed_save(){
 auto &records=Preferences::records();records.clear();fake::failWrite=false;
 std::vector<unsigned char> old(83,0);old[0]='M';old[1]='V';old[2]=1;old[4]=0;old[5]=2;old[6]=3;unsigned at=7;
 for(unsigned s=0;s<3;++s)for(unsigned c=0;c<8;++c){old[at++]=s==0&&c==0?8:6;old[at++]=2;old[at++]=0;}
 auto crc=meter_ui::valuePreferencesCrc(old.data(),79);for(unsigned i=0;i<4;++i)old[79+i]=crc>>(8*i);
 records["config"]=old;meter_values::begin();TEST_ASSERT_TRUE(meter_values::saved());TEST_ASSERT_EQUAL(8,meter_values::snapshot().formats[0][0].digits);TEST_ASSERT_EQUAL(6,meter_values::snapshot().formats[3][0].digits);TEST_ASSERT_EQUAL(108,records["config4"].size());TEST_ASSERT_TRUE(records["config"]==old);
 auto f=meter_values::snapshot().formats[0][0];f.digits=7;fake::failWrite=true;TEST_ASSERT_TRUE(meter_values::requestFormat(0,0,f));TEST_ASSERT_TRUE(meter_values::tick());TEST_ASSERT_FALSE(meter_values::saved());TEST_ASSERT_TRUE(records["config"]==old);fake::failWrite=false;
}
int main(){UNITY_BEGIN();RUN_TEST(changed_value_is_never_acknowledged_before_durable_write);RUN_TEST(old_key_survives_migration_and_failed_save);return UNITY_END();}
