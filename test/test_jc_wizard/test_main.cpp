#include <unity.h>
#include <jc3248_wizard.h>
#include <cmath>
using namespace jc3248;
void setUp(){}
void tearDown(){}
static uint32_t clockMs;
static void hold(CalibrationWizard &w,double x,double y){for(unsigned i=0;i<10;++i){w.update(x,y,true,clockMs);clockMs+=20;}}
static void release(CalibrationWizard &w){w.update(803,803,false,clockMs);clockMs+=20;}
static void fourCorners(CalibrationWizard &w){w.update(0,0,false,clockMs++);for(unsigned i=0;i<4;++i){Point target=w.target();hold(w,319-target.y,target.x);TEST_ASSERT_TRUE(w.captureReady());release(w);}}
static void no_point_until_stable_hold_and_release(){
    CalibrationWizard w;clockMs=100;hold(w,287,32);release(w);TEST_ASSERT_EQUAL(0,w.step());
    w.update(287,32,true,clockMs++);release(w);TEST_ASSERT_EQUAL(0,w.step());
    hold(w,287,32);TEST_ASSERT_TRUE(w.captureReady());TEST_ASSERT_EQUAL(0,w.step());release(w);TEST_ASSERT_EQUAL(1,w.step());
}
static void four_corners_need_centre_validation_before_ready(){
    CalibrationWizard w;clockMs=10;fourCorners(w);TEST_ASSERT_EQUAL(4,w.step());TEST_ASSERT_FALSE(w.ready());
    Point centre=w.target();hold(w,319-centre.y,centre.x);TEST_ASSERT_FALSE(w.ready());release(w);TEST_ASSERT_TRUE(w.ready());
    Point mapped{};TEST_ASSERT_TRUE(mapCalibration(w.calibration(),159,240,mapped));TEST_ASSERT_FLOAT_WITHIN(1,240,mapped.x);TEST_ASSERT_FLOAT_WITHIN(1,160,mapped.y);
}
static void motion_stale_release_and_invalid_values_cannot_advance(){
    CalibrationWizard w;clockMs=100;w.update(0,0,false,clockMs++);hold(w,287,32);clockMs+=1000;release(w);TEST_ASSERT_EQUAL(0,w.step());
    hold(w,287,32);w.update(200,200,true,clockMs++);release(w);TEST_ASSERT_EQUAL(0,w.step());
    w.update(NAN,32,true,clockMs++);release(w);TEST_ASSERT_EQUAL(0,w.step());
}
static void repeated_bad_centre_restarts_and_reset_discards_candidate(){
    CalibrationWizard w;clockMs=100;fourCorners(w);for(unsigned i=0;i<3;++i){hold(w,287,32);release(w);}TEST_ASSERT_EQUAL(0,w.step());TEST_ASSERT_FALSE(w.ready());TEST_ASSERT_TRUE(w.error()[0]!=0);
    w.reset();TEST_ASSERT_EQUAL(0,w.step());TEST_ASSERT_FALSE(w.ready());TEST_ASSERT_FALSE(validCalibration(w.calibration()));
}
static void stable_capture_handles_clock_wrap_and_retry_preserves_fit(){
    CalibrationWizard w;clockMs=0xffffff80;fourCorners(w);Point p=w.target();hold(w,319-p.y,p.x);release(w);TEST_ASSERT_TRUE(w.ready());
    w.retryVerification();TEST_ASSERT_EQUAL(4,w.step());TEST_ASSERT_FALSE(w.ready());TEST_ASSERT_TRUE(validCalibration(w.calibration()));release(w);hold(w,319-p.y,p.x);release(w);TEST_ASSERT_TRUE(w.ready());
}
static void mapped_dimensions_drive_targets_reset_and_centre(){
    CalibrationWizard w;TEST_ASSERT_TRUE(w.configure(800,480));clockMs=10;
    TEST_ASSERT_EQUAL(32,w.target().x);TEST_ASSERT_EQUAL(32,w.target().y);
    w.update(0,0,false,clockMs++);
    for(unsigned i=0;i<4;++i){Point target=w.target();hold(w,target.x,target.y);release(w);}
    TEST_ASSERT_EQUAL(4,w.step());Point centre=w.target();
    TEST_ASSERT_FLOAT_WITHIN(.01,399.5,centre.x);TEST_ASSERT_FLOAT_WITHIN(.01,239.5,centre.y);
    hold(w,centre.x,centre.y);release(w);TEST_ASSERT_TRUE(w.ready());
    w.reset();TEST_ASSERT_EQUAL(0,w.step());
    w.update(0,0,false,clockMs++);hold(w,32,32);release(w);TEST_ASSERT_EQUAL(767,w.target().x);
    TEST_ASSERT_FALSE(w.configure(20,20));TEST_ASSERT_EQUAL(767,w.target().x);
}
int main(){UNITY_BEGIN();RUN_TEST(no_point_until_stable_hold_and_release);RUN_TEST(four_corners_need_centre_validation_before_ready);RUN_TEST(motion_stale_release_and_invalid_values_cannot_advance);RUN_TEST(repeated_bad_centre_restarts_and_reset_discards_candidate);RUN_TEST(stable_capture_handles_clock_wrap_and_retry_preserves_fit);RUN_TEST(mapped_dimensions_drive_targets_reset_and_centre);return UNITY_END();}
