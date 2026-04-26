#include <unity.h>
#include "ota_decision.h"

void setUp() {}
void tearDown() {}

void test_skip_when_versions_match() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.2.0", "", 100));
}

void test_skip_when_manifest_matches_failed() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.3.0", "v0.3.0", 100));
}

void test_skip_when_battery_below_floor() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "v0.3.0", "", 19));
}

void test_apply_when_new_version_and_healthy_battery() {
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "", 20));
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "", 100));
}

void test_apply_when_failed_is_different_version() {
    TEST_ASSERT_TRUE(shouldApply("v0.2.0", "v0.3.0", "v0.2.5", 80));
}

void test_skip_when_manifest_empty() {
    TEST_ASSERT_FALSE(shouldApply("v0.2.0", "", "", 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_skip_when_versions_match);
    RUN_TEST(test_skip_when_manifest_matches_failed);
    RUN_TEST(test_skip_when_battery_below_floor);
    RUN_TEST(test_apply_when_new_version_and_healthy_battery);
    RUN_TEST(test_apply_when_failed_is_different_version);
    RUN_TEST(test_skip_when_manifest_empty);
    return UNITY_END();
}
