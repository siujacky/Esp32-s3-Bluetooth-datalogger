#include <unity.h>
#include <meter_value_format.h>
#include <limits>
#include <cstring>
#include "fixtures.inc"
void setUp(){} void tearDown(){}
static void shared_decimal_vectors(){
    for(const auto &c:formatCases){
        const auto out=meter_ui::formatNumber(c.value,c.format,c.available);
        TEST_ASSERT_EQUAL_MESSAGE(unsigned(c.state),unsigned(out.state),c.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.cells,out.cells,c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.decimal,out.decimalIndex,c.name);
    }
}
static void field_width_and_decimal_do_not_follow_reading_width(){
    const double samples[]={-9999.99,-123.45,-1.2,-0.001,0,1.2,9.99,12.34,1234.56,9999.99};
    for(double value:samples){const auto out=meter_ui::formatNumber(value,{});TEST_ASSERT_EQUAL(8,strlen(out.cells));TEST_ASSERT_EQUAL(5,out.decimalIndex);}
}
int main(){UNITY_BEGIN();RUN_TEST(shared_decimal_vectors);RUN_TEST(field_width_and_decimal_do_not_follow_reading_width);return UNITY_END();}
