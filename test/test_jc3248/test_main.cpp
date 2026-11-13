#include <unity.h>
#include <jc3248_touch.h>
#include <jc3248_calibration.h>
#include <ut_touch.h>
#include <algorithm>
#include <cmath>
void setUp(){}
void tearDown(){}
static void test_landscape_corners_match_canvas_rotation(){
    uint8_t b[]={0,1,0,0,0,0,0,0};uint16_t x=9,y=9;bool down=false;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));TEST_ASSERT_TRUE(down);TEST_ASSERT_EQUAL_UINT16(0,x);TEST_ASSERT_EQUAL_UINT16(319,y);
    b[2]=1;b[3]=63;b[4]=1;b[5]=223;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));TEST_ASSERT_EQUAL_UINT16(479,x);TEST_ASSERT_EQUAL_UINT16(0,y);
}
static void test_all_rotations_have_consistent_coordinates(){
    uint8_t b[]={0,1,0,10,0,20,0,0};uint16_t x=0,y=0;bool down=false;
    const uint16_t expected[][2]={{10,20},{20,309},{309,459},{459,10}};
    for(unsigned r=0;r<4;++r){TEST_ASSERT_TRUE(jc3248::decodeTouch(b,sizeof(b),r,x,y,down));TEST_ASSERT_EQUAL_UINT16(expected[r][0],x);TEST_ASSERT_EQUAL_UINT16(expected[r][1],y);}
}
static void test_zero_contacts_releases_without_reusing_position(){
    uint8_t b[8]={};uint16_t x=123,y=45;bool down=true;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));TEST_ASSERT_FALSE(down);TEST_ASSERT_EQUAL_UINT16(123,x);TEST_ASSERT_EQUAL_UINT16(45,y);
}
static void test_bad_frames_do_not_publish_partial_presses(){
    uint8_t b[]={0,1,1,64,0,20,0,0};uint16_t x=123,y=45;bool down=false;
    TEST_ASSERT_FALSE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));TEST_ASSERT_FALSE(down);TEST_ASSERT_EQUAL_UINT16(123,x);TEST_ASSERT_EQUAL_UINT16(45,y);
    b[2]=0;b[3]=10;b[4]=1;b[5]=224;TEST_ASSERT_FALSE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));
    b[4]=0;b[5]=20;b[1]=2;TEST_ASSERT_FALSE(jc3248::decodeTouch(b,sizeof(b),1,x,y,down));
    b[1]=1;TEST_ASSERT_FALSE(jc3248::decodeTouch(b,7,1,x,y,down));TEST_ASSERT_FALSE(jc3248::decodeTouch(nullptr,8,1,x,y,down));
}
static void test_captured_idle_marker_releases_a_real_contact(){
    const uint8_t contact[]={0,1,0x80,0x8c,1,0x0d,0x27,0x28},idle[]={0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23};
    uint16_t x=0,y=0;bool down=false;ut_meter::TouchGesture gesture;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(contact,8,1,x,y,down));TEST_ASSERT_TRUE(down);TEST_ASSERT_EQUAL(269,x);TEST_ASSERT_EQUAL(179,y);
    gesture.update(x,y,down,false,100);
    TEST_ASSERT_TRUE(jc3248::decodeTouch(idle,8,1,x,y,down));TEST_ASSERT_FALSE(down);
    const auto action=gesture.update(x,y,down,false,300);TEST_ASSERT_TRUE(action.tap);TEST_ASSERT_EQUAL(269,action.x);TEST_ASSERT_EQUAL(179,action.y);
    TEST_ASSERT_FALSE(gesture.update(x,y,down,false,400).tap);
}
static void test_up_event_releases_even_with_a_latched_contact_count(){
    const uint8_t up[]={0,1,0x40,0x3e,0,0x0e,0x75,0x76};uint16_t x=42,y=50;bool down=true;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(up,8,1,x,y,down));TEST_ASSERT_FALSE(down);TEST_ASSERT_EQUAL(42,x);TEST_ASSERT_EQUAL(50,y);
    uint8_t corrupt[8];for(auto &b:corrupt)b=0xff;down=true;
    TEST_ASSERT_FALSE(jc3248::decodeTouch(corrupt,8,1,x,y,down));TEST_ASSERT_TRUE(down);
}
static void test_captured_63_idle_releases_a_contact_once(){
    // Current board capture, 13 Nov: no contact, tx=0, bytes=8, all 0x63.
    const uint8_t contact[]={0,1,0x80,0x8c,1,0x0d,0x27,0x28};
    uint8_t idle[8];for(auto &b:idle)b=0x63;
    uint16_t x=0,y=0;bool down=false;ut_meter::TouchGesture gesture;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(contact,8,2,x,y,down));TEST_ASSERT_TRUE(down);
    gesture.update(x,y,down,false,100,320,480);
    const uint16_t lastX=x,lastY=y;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(idle,8,2,x,y,down));TEST_ASSERT_FALSE(down);
    TEST_ASSERT_EQUAL(lastX,x);TEST_ASSERT_EQUAL(lastY,y);
    TEST_ASSERT_TRUE(gesture.update(x,y,down,false,300,320,480).tap);
    TEST_ASSERT_FALSE(gesture.update(x,y,down,false,400,320,480).tap);
    idle[3]=0xff;down=true;
    TEST_ASSERT_FALSE(jc3248::decodeTouch(idle,8,2,x,y,down));TEST_ASSERT_TRUE(down);
}
static void calibratedPortraitPoint(unsigned rawX,unsigned rawY,uint16_t &x,uint16_t &y){
    // Exact saved model from the affected JC, rather than ideal screen coordinates.
    jc3248::Calibration cal;
    cal.x[0]=-0.00167621286047;cal.x[1]=0.929481921831;cal.x[2]=18.759981109;
    cal.y[0]=-0.965600387074;cal.y[1]=0.0183503574144;cal.y[2]=295.873458941;
    jc3248::Point point{};TEST_ASSERT_TRUE(jc3248::mapCalibration(cal,rawX,rawY,point));
    const auto cx=uint16_t(std::max(0,std::min(479,int(std::lround(point.x)))));
    const auto cy=uint16_t(std::max(0,std::min(319,int(std::lround(point.y)))));
    TEST_ASSERT_TRUE(jc3248::transformTouch(cx,cy,1,2,320,480,x,y));
}
static void test_saved_calibration_allows_natural_top_and_right_edge_starts(){
    // A fingertip centred 8-14 raw pixels inside the physical edge maps inward.
    const unsigned strokes[][5]={{160,466,160,366,unsigned(ut_meter::TouchEdge::Top)},
                                {8,240,108,240,unsigned(ut_meter::TouchEdge::Right)}};
    for(const auto &stroke:strokes){
        uint16_t x=0,y=0;ut_meter::TouchGesture gesture;
        calibratedPortraitPoint(stroke[0],stroke[1],x,y);gesture.update(x,y,true,false,100,320,480);
        calibratedPortraitPoint(stroke[2],stroke[3],x,y);gesture.update(x,y,true,false,200,320,480);
        const auto result=gesture.update(0,0,false,false,300,320,480);
        TEST_ASSERT_EQUAL(stroke[4],unsigned(result.edge));TEST_ASSERT_FALSE(result.tap);
    }
}
static void test_mapping_dimensions_rotate_without_a_fixed_panel_size(){
    const uint8_t b[]={0,1,0x83,0x1f,1,223,0,0};uint16_t x=9,y=8;bool down=false;
    TEST_ASSERT_TRUE(jc3248::decodeTouch(b,8,1,x,y,down,800,480));TEST_ASSERT_EQUAL(479,x);TEST_ASSERT_EQUAL(0,y);TEST_ASSERT_TRUE(down);
    TEST_ASSERT_FALSE(jc3248::decodeTouch(b,8,1,x,y,down,800,479));TEST_ASSERT_EQUAL(479,x);TEST_ASSERT_EQUAL(0,y);
    TEST_ASSERT_FALSE(jc3248::decodeTouch(b,8,1,x,y,down,0,480));
}
static void test_calibrated_point_moves_between_all_view_rotations(){
    const uint16_t expected[][2]={{10,20},{20,789},{789,459},{459,10}};
    for(unsigned from=0;from<4;++from)for(unsigned to=0;to<4;++to){
        uint16_t x=5,y=6;TEST_ASSERT_TRUE(jc3248::transformTouch(expected[from][0],expected[from][1],from,to,800,480,x,y));
        TEST_ASSERT_EQUAL(expected[to][0],x);TEST_ASSERT_EQUAL(expected[to][1],y);
    }
    uint16_t x=5,y=6;TEST_ASSERT_FALSE(jc3248::transformTouch(480,0,1,0,800,480,x,y));TEST_ASSERT_EQUAL(5,x);TEST_ASSERT_EQUAL(6,y);
    TEST_ASSERT_FALSE(jc3248::transformTouch(0,0,4,0,800,480,x,y));
}
int main(){UNITY_BEGIN();RUN_TEST(test_landscape_corners_match_canvas_rotation);RUN_TEST(test_all_rotations_have_consistent_coordinates);RUN_TEST(test_zero_contacts_releases_without_reusing_position);RUN_TEST(test_bad_frames_do_not_publish_partial_presses);RUN_TEST(test_captured_idle_marker_releases_a_real_contact);RUN_TEST(test_up_event_releases_even_with_a_latched_contact_count);RUN_TEST(test_captured_63_idle_releases_a_contact_once);RUN_TEST(test_saved_calibration_allows_natural_top_and_right_edge_starts);RUN_TEST(test_mapping_dimensions_rotate_without_a_fixed_panel_size);RUN_TEST(test_calibrated_point_moves_between_all_view_rotations);return UNITY_END();}
