#include <unity.h>
#include <jc3248_calibration.h>
#include <cmath>
#include <cstring>
#include <limits>

using jc3248::Point;
using jc3248::Calibration;
static const Point target[4] = {{24,24},{455,24},{455,295},{24,295}};

void setUp() {}
void tearDown() {}

// Keep double precision without requiring a project-wide Unity configuration.
static void assertClose(double expected,double actual) {
    TEST_ASSERT_TRUE(std::isfinite(actual) && expected==actual);
}
static void assertWithin(double tolerance,double expected,double actual) {
    TEST_ASSERT_TRUE(std::isfinite(actual) && std::fabs(expected-actual)<=tolerance);
}
static Calibration sentinel() {
    Calibration c;
    c.x[0]=1; c.x[1]=0; c.x[2]=17;
    c.y[0]=0; c.y[1]=1; c.y[2]=23;
    return c;
}
static void sameCalibration(const Calibration &a, const Calibration &b) {
    for (unsigned i=0;i<3;++i) {
        assertClose(a.x[i],b.x[i]);
        assertClose(a.y[i],b.y[i]);
    }
}
static void assertPoint(const Calibration &c,double x,double y,double wantX,double wantY,double tolerance=1e-8) {
    Point mapped={91,92};
    TEST_ASSERT_TRUE(jc3248::mapCalibration(c,x,y,mapped));
    assertWithin(tolerance,wantX,mapped.x);
    assertWithin(tolerance,wantY,mapped.y);
}
static void rejectedFit(const Point raw[4],const Point targets[4]=target) {
    Calibration c=sentinel(),before=c; double rms=91;
    TEST_ASSERT_FALSE(jc3248::fitCalibration(raw,targets,c,rms));
    sameCalibration(before,c);
    assertClose(91,rms);
}

static void test_rotated_raw_corners_map_to_landscape() {
    const Point raw[4]={{290,30},{290,450},{30,450},{30,30}};
    Calibration c; double rms=-1;
    TEST_ASSERT_TRUE(jc3248::fitCalibration(raw,target,c,rms));
    TEST_ASSERT_TRUE(jc3248::validCalibration(c));
    assertWithin(1e-8,0,rms);
    assertPoint(c,290,30,24,24);
    assertPoint(c,30,450,455,295);
    assertPoint(c,160,240,239.5,159.5);
}
static void test_mirrored_and_axis_swapped_controller_orientations() {
    const Point raw[][4]={
        {{30,30},{290,30},{290,450},{30,450}},
        {{290,450},{30,450},{30,30},{290,30}},
        {{30,450},{30,30},{290,30},{290,450}}
    };
    for (const auto &corners:raw) {
        Calibration c;double rms=-1;
        TEST_ASSERT_TRUE(jc3248::fitCalibration(corners,target,c,rms));
        for(unsigned i=0;i<4;++i) assertPoint(c,corners[i].x,corners[i].y,target[i].x,target[i].y);
        assertPoint(c,160,240,239.5,159.5);
    }
}
static void test_affine_skew_and_off_center_scale_are_recovered() {
    const Point skew[4]={{30,40},{280,65},{310,420},{60,395}};
    Calibration c;double rms=-1;
    TEST_ASSERT_TRUE(jc3248::fitCalibration(skew,target,c,rms));
    assertPoint(c,87.5,133.75,110.2,91.75);
    const Point offCenter[4]={{40,60},{280,60},{280,420},{40,420}};
    TEST_ASSERT_TRUE(jc3248::fitCalibration(offCenter,target,c,rms));
    assertPoint(c,100,150,131.75,91.75);
    // Extrapolation is explicit: this layer does not silently clamp pixels.
    assertPoint(c,20,30,-11.9166666666667,1.41666666666667);
}
static void test_small_corner_noise_is_fitted_with_a_measured_residual() {
    const Point raw[4]={{30,30},{290,31},{292,450},{31,451}};
    Calibration c;double rms=-1;
    TEST_ASSERT_TRUE(jc3248::fitCalibration(raw,target,c,rms));
    TEST_ASSERT_TRUE(rms>0.1 && rms<2.0);
    assertPoint(c,160.75,240.5,239.5,159.5,1e-8);
}
static void test_repeated_collinear_and_insufficient_span_do_not_replace_a_fit() {
    const Point repeated[4]={{20,20},{290,20},{290,450},{20,20}};
    const Point collinear[4]={{20,20},{100,100},{200,200},{290,290}};
    const Point narrow[4]={{20,20},{40,20},{40,450},{20,450}};
    rejectedFit(repeated);rejectedFit(collinear);rejectedFit(narrow);
}
static void test_crossed_and_concave_corner_order_is_rejected() {
    const Point bowTie[4]={{20,20},{290,450},{290,20},{20,450}};
    const Point concave[4]={{20,20},{290,20},{100,100},{20,450}};
    rejectedFit(bowTie);rejectedFit(concave);
}
static void test_large_corner_mistake_and_bad_targets_are_rejected() {
    const Point raw[4]={{30,30},{290,30},{180,450},{30,450}};
    rejectedFit(raw);
    const Point normal[4]={{30,30},{290,30},{290,450},{30,450}};
    Point bad[4];std::memcpy(bad,target,sizeof bad);
    bad[2].x=240;rejectedFit(normal,bad);
    std::memcpy(bad,target,sizeof bad);bad[1].x=500;
    rejectedFit(normal,bad);
}
static void test_nonfinite_and_extreme_source_values_are_rejected() {
    Point raw[4]={{30,30},{290,30},{290,450},{30,450}};
    raw[1].x=std::numeric_limits<double>::quiet_NaN();rejectedFit(raw);
    raw[1].x=std::numeric_limits<double>::infinity();rejectedFit(raw);
    raw[1].x=std::numeric_limits<double>::max();rejectedFit(raw);
    rejectedFit(nullptr);
    const Point normal[4]={{30,30},{290,30},{290,450},{30,450}};
    rejectedFit(normal,nullptr);
}
static void test_invalid_coefficients_and_overflow_leave_map_output_untouched() {
    Calibration c=sentinel();Point p={91,92};
    TEST_ASSERT_TRUE(jc3248::validCalibration(c));
    c.x[0]=2;
    TEST_ASSERT_FALSE(jc3248::mapCalibration(c,std::numeric_limits<double>::max(),1,p));
    assertClose(91,p.x);assertClose(92,p.y);
    TEST_ASSERT_FALSE(jc3248::mapCalibration(c,0,std::numeric_limits<double>::quiet_NaN(),p));
    assertClose(91,p.x);assertClose(92,p.y);
    c=Calibration();TEST_ASSERT_FALSE(jc3248::validCalibration(c));
    c=sentinel();c.y[1]=1e-12;TEST_ASSERT_FALSE(jc3248::validCalibration(c));
    c=sentinel();c.x[0]=1e100;TEST_ASSERT_FALSE(jc3248::validCalibration(c));
    c=sentinel();c.x[2]=std::numeric_limits<double>::infinity();TEST_ASSERT_FALSE(jc3248::validCalibration(c));
    TEST_ASSERT_FALSE(jc3248::mapCalibration(c,1,2,p));
    assertClose(91,p.x);assertClose(92,p.y);
}

// Golden record generated independently with Python struct.pack('<4s4H6d',
// b'JCC1',1,480,320,0,1,0,17,0,1,23) and zlib.crc32(payload).
static const uint8_t golden[64]={
    0x4a,0x43,0x43,0x31,0x01,0x00,0xe0,0x01,0x40,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x31,0x40,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f,
    0x00,0x00,0x00,0x00,0x00,0x00,0x37,0x40,0xf8,0xc2,0x5b,0xe1
};
static void assertRejectedRecord(const uint8_t *blob,size_t size) {
    Calibration c=sentinel(),before=c;
    c.x[2]=47;before=c;
    TEST_ASSERT_FALSE(jc3248::decodeCalibration(blob,size,c));
    sameCalibration(before,c);
}
static void setKnownChecksum(uint8_t *blob,uint32_t checksum) {
    for(unsigned i=0;i<4;++i) blob[60+i]=static_cast<uint8_t>(checksum>>(8*i));
}
static void test_record_encoding_matches_an_independent_portable_fixture() {
    uint8_t bytes[65];std::memset(bytes,0xa5,sizeof bytes);
    TEST_ASSERT_TRUE(jc3248::encodeCalibration(sentinel(),bytes,sizeof bytes));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden,bytes,64);
    TEST_ASSERT_EQUAL_UINT8(0xa5,bytes[64]);
    Calibration c;
    TEST_ASSERT_TRUE(jc3248::decodeCalibration(golden,sizeof golden,c));
    sameCalibration(sentinel(),c);
    assertPoint(c,100,200,117,223);
}
static void test_roundtrip_preserves_binary64_coefficients() {
    Calibration c=sentinel();
    c.x[0]=1.0000000000000002;c.x[1]=0.125;c.x[2]=-12.25;
    c.y[0]=-0.125;c.y[1]=1.5;c.y[2]=22.75;
    uint8_t bytes[64];Calibration decoded;
    TEST_ASSERT_TRUE(jc3248::encodeCalibration(c,bytes,sizeof bytes));
    TEST_ASSERT_TRUE(jc3248::decodeCalibration(bytes,sizeof bytes,decoded));
    TEST_ASSERT_EQUAL_MEMORY(c.x,decoded.x,sizeof c.x);
    TEST_ASSERT_EQUAL_MEMORY(c.y,decoded.y,sizeof c.y);
    assertPoint(decoded,100,200,112.75,310.25);
}
static void test_every_single_byte_corruption_and_truncation_is_rejected() {
    uint8_t bytes[65];
    for(unsigned i=0;i<64;++i) {
        std::memcpy(bytes,golden,64);bytes[i]^=0x01;
        assertRejectedRecord(bytes,64);
    }
    for(unsigned size=0;size<64;++size) assertRejectedRecord(golden,size);
    std::memcpy(bytes,golden,64);bytes[64]=0;
    assertRejectedRecord(bytes,65);assertRejectedRecord(nullptr,64);
}
static void test_checksummed_incompatible_metadata_is_rejected() {
    uint8_t bytes[64];
    std::memcpy(bytes,golden,64);bytes[4]=2;setKnownChecksum(bytes,0xc7e4f681);
    assertRejectedRecord(bytes,64);
    std::memcpy(bytes,golden,64);bytes[6]=0x40;setKnownChecksum(bytes,0x073a2f38);
    assertRejectedRecord(bytes,64);
    std::memcpy(bytes,golden,64);bytes[8]=0xe0;setKnownChecksum(bytes,0x3d09873b);
    assertRejectedRecord(bytes,64);
    std::memcpy(bytes,golden,64);bytes[10]=1;setKnownChecksum(bytes,0x656adb09);
    assertRejectedRecord(bytes,64);
}
static void test_checksummed_invalid_coefficients_cannot_be_loaded() {
    uint8_t bytes[64];
    std::memcpy(bytes,golden,64);bytes[18]=0xf8;bytes[19]=0x7f;setKnownChecksum(bytes,0x5875106e);
    assertRejectedRecord(bytes,64);
    std::memcpy(bytes,golden,64);std::memset(bytes+44,0,8);setKnownChecksum(bytes,0x009f4162);
    assertRejectedRecord(bytes,64);
    const uint8_t huge[8]={0x7d,0xc3,0x94,0x25,0xad,0x49,0xb2,0x54};
    std::memcpy(bytes,golden,64);std::memcpy(bytes+12,huge,8);setKnownChecksum(bytes,0xb69d9654);
    assertRejectedRecord(bytes,64);
}
static void test_failed_encoding_does_not_write_any_output_byte() {
    uint8_t bytes[64],before[64];std::memset(bytes,0x5a,sizeof bytes);std::memcpy(before,bytes,sizeof bytes);
    TEST_ASSERT_FALSE(jc3248::encodeCalibration(sentinel(),bytes,63));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before,bytes,64);
    TEST_ASSERT_FALSE(jc3248::encodeCalibration(Calibration(),bytes,64));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before,bytes,64);
    TEST_ASSERT_FALSE(jc3248::encodeCalibration(sentinel(),nullptr,64));
}

static void mapped_record_dimensions_are_checked_without_changing_the_legacy_record() {
    uint8_t bytes[64];Calibration decoded;
    TEST_ASSERT_TRUE(jc3248::encodeCalibration(sentinel(),bytes,sizeof bytes,800,480));
    TEST_ASSERT_TRUE(jc3248::decodeCalibration(bytes,sizeof bytes,decoded,800,480));
    sameCalibration(sentinel(),decoded);
    TEST_ASSERT_FALSE(jc3248::decodeCalibration(bytes,sizeof bytes,decoded));
    TEST_ASSERT_FALSE(jc3248::decodeCalibration(bytes,sizeof bytes,decoded,480,800));
    std::memcpy(bytes,golden,64);
    TEST_ASSERT_TRUE(jc3248::decodeCalibration(bytes,sizeof bytes,decoded));
    TEST_ASSERT_FALSE(jc3248::encodeCalibration(sentinel(),bytes,sizeof bytes,0,480));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden,bytes,64);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rotated_raw_corners_map_to_landscape);
    RUN_TEST(test_mirrored_and_axis_swapped_controller_orientations);
    RUN_TEST(test_affine_skew_and_off_center_scale_are_recovered);
    RUN_TEST(test_small_corner_noise_is_fitted_with_a_measured_residual);
    RUN_TEST(test_repeated_collinear_and_insufficient_span_do_not_replace_a_fit);
    RUN_TEST(test_crossed_and_concave_corner_order_is_rejected);
    RUN_TEST(test_large_corner_mistake_and_bad_targets_are_rejected);
    RUN_TEST(test_nonfinite_and_extreme_source_values_are_rejected);
    RUN_TEST(test_invalid_coefficients_and_overflow_leave_map_output_untouched);
    RUN_TEST(test_record_encoding_matches_an_independent_portable_fixture);
    RUN_TEST(test_roundtrip_preserves_binary64_coefficients);
    RUN_TEST(test_every_single_byte_corruption_and_truncation_is_rejected);
    RUN_TEST(test_checksummed_incompatible_metadata_is_rejected);
    RUN_TEST(test_checksummed_invalid_coefficients_cannot_be_loaded);
    RUN_TEST(test_failed_encoding_does_not_write_any_output_byte);
    RUN_TEST(mapped_record_dimensions_are_checked_without_changing_the_legacy_record);
    return UNITY_END();
}
