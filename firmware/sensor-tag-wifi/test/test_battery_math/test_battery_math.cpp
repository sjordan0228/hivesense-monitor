#include <unity.h>
#include "battery.h"

void setUp() {}
void tearDown() {}

void test_full_voltage_returns_100() {
    TEST_ASSERT_EQUAL_UINT8(100, Battery::percentFromMillivolts(4200));
}

void test_above_full_clamps_to_100() {
    TEST_ASSERT_EQUAL_UINT8(100, Battery::percentFromMillivolts(4500));
}

void test_empty_voltage_returns_0() {
    TEST_ASSERT_EQUAL_UINT8(0, Battery::percentFromMillivolts(3300));
}

void test_below_empty_clamps_to_0() {
    TEST_ASSERT_EQUAL_UINT8(0, Battery::percentFromMillivolts(3100));
}

void test_midpoint_returns_50() {
    // (3750 - 3300) * 100 / (4200 - 3300) = 45000/900 = 50
    TEST_ASSERT_EQUAL_UINT8(50, Battery::percentFromMillivolts(3750));
}

void test_three_quarters_returns_75() {
    // (3975 - 3300) * 100 / 900 = 67500/900 = 75
    TEST_ASSERT_EQUAL_UINT8(75, Battery::percentFromMillivolts(3975));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_full_voltage_returns_100);
    RUN_TEST(test_above_full_clamps_to_100);
    RUN_TEST(test_empty_voltage_returns_0);
    RUN_TEST(test_below_empty_clamps_to_0);
    RUN_TEST(test_midpoint_returns_50);
    RUN_TEST(test_three_quarters_returns_75);
    return UNITY_END();
}
