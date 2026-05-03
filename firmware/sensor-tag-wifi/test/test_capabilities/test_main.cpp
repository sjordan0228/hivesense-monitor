#include <unity.h>
#include <ArduinoJson.h>
#include <cstring>
#include "capabilities.h"

void setUp(void) {}
void tearDown(void) {}

// Helper: parse JSON from buildPayload output
static JsonDocument parsePayload(const Capabilities::FeatureFlags& flags, int64_t bootEpoch) {
    char buf[512];
    size_t n = Capabilities::buildPayload(flags, bootEpoch, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);
    JsonDocument doc;
    auto err = deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_INT(0, (int)err.code());
    return doc;
}

void test_buildPayload_includes_all_required_fields() {
    Capabilities::FeatureFlags flags { 1, 0, 0, 0 };
    char buf[512];
    size_t n = Capabilities::buildPayload(flags, 1714780800LL, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);

    JsonDocument doc;
    auto err = deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_INT(0, (int)err.code());

    // Per contract §3.1, capabilities has no `event` discriminator (its own dedicated topic).
    TEST_ASSERT_TRUE(doc["event"].isNull());
    TEST_ASSERT_FALSE(doc["feat_ds18b20"].isNull());
    TEST_ASSERT_FALSE(doc["feat_sht31"].isNull());
    TEST_ASSERT_FALSE(doc["feat_scale"].isNull());
    TEST_ASSERT_FALSE(doc["feat_mic"].isNull());
    TEST_ASSERT_FALSE(doc["hw_board"].isNull());
    TEST_ASSERT_FALSE(doc["fw_version"].isNull());
    TEST_ASSERT_FALSE(doc["last_boot_ts"].isNull());
    TEST_ASSERT_FALSE(doc["ts"].isNull());
}

void test_buildPayload_omits_event_field() {
    // Contract §3.1: capabilities is on its own dedicated topic — no
    // discriminator field needed. iOS decoder enforces strict v1.1 schema.
    Capabilities::FeatureFlags flags { 1, 0, 0, 0 };
    auto doc = parsePayload(flags, 0LL);
    TEST_ASSERT_TRUE(doc["event"].isNull());
}

void test_buildPayload_last_boot_ts_rfc3339() {
    // 1714780800 → 2024-05-04T00:00:00Z (verified via `date -u -r 1714780800`)
    Capabilities::FeatureFlags flags { 1, 0, 0, 0 };
    auto doc = parsePayload(flags, 1714780800LL);
    const char* lbt = doc["last_boot_ts"].as<const char*>();
    TEST_ASSERT_NOT_NULL(lbt);
    TEST_ASSERT_EQUAL_STRING("2024-05-04T00:00:00Z", lbt);
}

void test_buildPayload_last_boot_ts_zero_emits_epoch_string() {
    Capabilities::FeatureFlags flags { 1, 0, 0, 0 };
    auto doc = parsePayload(flags, 0LL);
    const char* lbt = doc["last_boot_ts"].as<const char*>();
    TEST_ASSERT_NOT_NULL(lbt);
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", lbt);
}

void test_buildPayload_feat_flags_match_config() {
    Capabilities::FeatureFlags flags { 1, 0, 1, 0 };
    auto doc = parsePayload(flags, 0LL);
    TEST_ASSERT_EQUAL_INT(1, doc["feat_ds18b20"].as<int>());
    TEST_ASSERT_EQUAL_INT(0, doc["feat_sht31"].as<int>());
    TEST_ASSERT_EQUAL_INT(1, doc["feat_scale"].as<int>());
    TEST_ASSERT_EQUAL_INT(0, doc["feat_mic"].as<int>());
}

void test_buildPayload_feat_flags_all_enabled() {
    Capabilities::FeatureFlags flags { 1, 1, 1, 1 };
    auto doc = parsePayload(flags, 0LL);
    TEST_ASSERT_EQUAL_INT(1, doc["feat_ds18b20"].as<int>());
    TEST_ASSERT_EQUAL_INT(1, doc["feat_sht31"].as<int>());
    TEST_ASSERT_EQUAL_INT(1, doc["feat_scale"].as<int>());
    TEST_ASSERT_EQUAL_INT(1, doc["feat_mic"].as<int>());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_buildPayload_includes_all_required_fields);
    RUN_TEST(test_buildPayload_omits_event_field);
    RUN_TEST(test_buildPayload_last_boot_ts_rfc3339);
    RUN_TEST(test_buildPayload_last_boot_ts_zero_emits_epoch_string);
    RUN_TEST(test_buildPayload_feat_flags_match_config);
    RUN_TEST(test_buildPayload_feat_flags_all_enabled);
    return UNITY_END();
}
