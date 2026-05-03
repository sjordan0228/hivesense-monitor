#define UNITY_INCLUDE_DOUBLE
#include <unity.h>
#include "scale_math.h"

void setUp(void) {}
void tearDown(void) {}

void test_stable_detector_empty_is_not_stable() {
    StableDetector d;
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_partial_fill_is_not_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1010);
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_quiet_window_is_stable() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_within_tolerance_is_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1015);  // range = 1020 - 990 = 30, < HX711_STABLE_TOLERANCE_RAW (50)
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_outside_tolerance_is_unstable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1100);  // range = 1100 - 990 = 110, > 50
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_recovers_after_disturbance() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    d.push(1500);  // one disturbance
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);
    d.push(1000);
    d.push(1000);
    d.push(1000);  // ring buffer is now [1500, 1000, 1000, 1000, 1000] — range 500
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);  // ring is now [1000, 1000, 1000, 1000, 1000] — stable
    TEST_ASSERT_TRUE(d.isStable());
}

void test_apply_calibration_basic() {
    // raw=12345, off=345, scale=200 → kg = (12345-345)/200 = 60.0
    double kg = applyCalibration(12345, 345, 200.0);
    TEST_ASSERT_EQUAL_DOUBLE(60.0, kg);
}

void test_apply_calibration_negative_load() {
    // raw=100, off=345, scale=200 → kg = (100-345)/200 = -1.225
    double kg = applyCalibration(100, 345, 200.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, -1.225, kg);
}

void test_tare_from_mean() {
    // mean of [1000, 1010, 990, 1005, 995] = 1000
    int32_t samples[] = {1000, 1010, 990, 1005, 995};
    int64_t off = tareFromMean(samples, 5);
    TEST_ASSERT_EQUAL_INT64(1000, off);
}

void test_scale_factor_from_mean() {
    // raw=10345, off=345, known_kg=10 → scale = (10345-345)/10 = 1000.0
    int32_t samples[] = {10345};
    double sf = scaleFactorFromMean(samples, 1, 345, 10.0);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, sf);
}

void test_error_pct_basic() {
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.6, errorPct(4.97, 5.0));
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.6, errorPct(5.03, 5.0));
}

void test_error_pct_zero_expected_returns_neg_one() {
    // Sentinel for "expected was zero — undefined error_pct"
    TEST_ASSERT_EQUAL_DOUBLE(-1.0, errorPct(1.0, 0.0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stable_detector_empty_is_not_stable);
    RUN_TEST(test_stable_detector_partial_fill_is_not_stable);
    RUN_TEST(test_stable_detector_quiet_window_is_stable);
    RUN_TEST(test_stable_detector_within_tolerance_is_stable);
    RUN_TEST(test_stable_detector_outside_tolerance_is_unstable);
    RUN_TEST(test_stable_detector_recovers_after_disturbance);
    RUN_TEST(test_apply_calibration_basic);
    RUN_TEST(test_apply_calibration_negative_load);
    RUN_TEST(test_tare_from_mean);
    RUN_TEST(test_scale_factor_from_mean);
    RUN_TEST(test_error_pct_basic);
    RUN_TEST(test_error_pct_zero_expected_returns_neg_one);
    return UNITY_END();
}
