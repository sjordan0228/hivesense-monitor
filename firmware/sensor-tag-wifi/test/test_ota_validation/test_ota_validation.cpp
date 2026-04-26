#include <unity.h>
#include "ota_decision.h"

void setUp() {}
void tearDown() {}

void test_noop_when_attempted_empty() {
    TEST_ASSERT_TRUE(validateOnBootAction("v0.2.0", "", false) == ValidateAction::NoOp);
    TEST_ASSERT_TRUE(validateOnBootAction("v0.2.0", "", true)  == ValidateAction::NoOp);
}

void test_noop_when_pending_and_versions_match() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.3.0", "v0.3.0", true) == ValidateAction::NoOp);
}

void test_clear_attempted_when_already_validated() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.3.0", "v0.3.0", false) == ValidateAction::ClearAttempted);
}

void test_record_failed_when_running_old_firmware_after_attempt() {
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.2.0", "v0.3.0", false) == ValidateAction::RecordFailed);
    TEST_ASSERT_TRUE(
        validateOnBootAction("v0.2.0", "v0.3.0", true)  == ValidateAction::RecordFailed);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_noop_when_attempted_empty);
    RUN_TEST(test_noop_when_pending_and_versions_match);
    RUN_TEST(test_clear_attempted_when_already_validated);
    RUN_TEST(test_record_failed_when_running_old_firmware_after_attempt);
    return UNITY_END();
}
