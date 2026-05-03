#include <unity.h>
#include <cstring>
#include <ArduinoJson.h>
#include "config_ack.h"
#include "config_parser.h"

void setUp(void) {}
void tearDown(void) {}

// --- AckEntry struct shape ---------------------------------------------------

void test_ack_entry_key_size() {
    // key must hold at least a 15-char NVS key name + NUL.
    AckEntry e {};
    TEST_ASSERT_EQUAL_size_t(16u, sizeof(e.key));
}

void test_ack_entry_result_size() {
    // result must hold longest expected string "excluded:security" + NUL (≤32).
    AckEntry e {};
    TEST_ASSERT_EQUAL_size_t(32u, sizeof(e.result));
}

void test_ack_entry_total_size() {
    // Struct is 48 bytes (16 + 32); no padding expected between char arrays.
    TEST_ASSERT_EQUAL_size_t(48u, sizeof(AckEntry));
}

void test_ack_entry_fields_writable() {
    AckEntry e {};
    strncpy(e.key,    "sample_int",  sizeof(e.key)    - 1);
    strncpy(e.result, "ok",          sizeof(e.result) - 1);
    TEST_ASSERT_EQUAL_STRING("sample_int", e.key);
    TEST_ASSERT_EQUAL_STRING("ok",         e.result);
}

// --- preValidate — no-conflict paths -----------------------------------------

void test_preValidate_returns_true_empty_parsed() {
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    size_t count = 99;  // sentinel to confirm it's zeroed
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_returns_true_full_parsed_no_feat() {
    // Non-feat keys present; no feat conflict.
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    parsed.has_sample_int   = true;  parsed.sample_int   = 300;
    parsed.has_upload_every = true;  parsed.upload_every = 2;
    parsed.has_tag_name     = true;  strncpy(parsed.tag_name, "hive-1", sizeof(parsed.tag_name) - 1);
    parsed.has_ota_host     = true;  strncpy(parsed.ota_host, "192.168.1.61", sizeof(parsed.ota_host) - 1);
    TemperatureNvsState nvs { true, false };  // ds18b20 on, sht31 off

    AckEntry entries[8];
    size_t count = 99;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_null_out_count_safe() {
    // Must not crash when outCount is null (caller may omit it).
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Absent;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    bool ok = preValidate(parsed, nvs, entries, nullptr);
    TEST_ASSERT_TRUE(ok);
}

// --- preValidate — mutual-exclusion conflict tests ---------------------------

void test_preValidate_rejects_both_temp_sensors_enabled() {
    // parsed has both at 1, NVS empty (both off) → post-apply both on → conflict
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::On;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { false, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(2u, count);
    // Both keys must appear in entries
    bool foundSht31 = false, foundDs = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, "feat_sht31")   == 0) foundSht31 = true;
        if (strcmp(entries[i].key, "feat_ds18b20") == 0) foundDs    = true;
    }
    TEST_ASSERT_TRUE(foundSht31);
    TEST_ASSERT_TRUE(foundDs);
    // Check the conflict results point at each other
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, "feat_sht31") == 0) {
            TEST_ASSERT_EQUAL_STRING("conflict:feat_ds18b20", entries[i].result);
        }
        if (strcmp(entries[i].key, "feat_ds18b20") == 0) {
            TEST_ASSERT_EQUAL_STRING("conflict:feat_sht31", entries[i].result);
        }
    }
}

void test_preValidate_allows_disabling_both_temp() {
    // Both 0 is legal (no active temp sensor is a valid config).
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Off;
    parsed.feat_sht31   = ConfigParser::FeatFlag::Off;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_allows_swap_via_two_keys() {
    // Swap: parsed ds18b20=0, sht31=1; NVS ds18b20=1 → post-apply ds18b20=0, sht31=1 → OK
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Off;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_rejects_one_key_against_existing() {
    // Parsed has only sht31=1; NVS ds18b20=1 → post-apply both on → conflict
    ConfigParser::ConfigUpdate parsed {};
    parsed.feat_ds18b20 = ConfigParser::FeatFlag::Absent;
    parsed.feat_sht31   = ConfigParser::FeatFlag::On;
    parsed.feat_scale   = ConfigParser::FeatFlag::Absent;
    parsed.feat_mic     = ConfigParser::FeatFlag::Absent;
    TemperatureNvsState nvs { true, false };
    AckEntry entries[8];
    size_t count = 0;
    bool ok = preValidate(parsed, nvs, entries, &count);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(2u, count);
}

// --- buildRichAck ------------------------------------------------------------

void test_ack_rich_format_serializes() {
    AckEntry entries[3];
    strncpy(entries[0].key,    "feat_scale",  sizeof(entries[0].key)    - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "ok",          sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    strncpy(entries[1].key,    "sample_int",  sizeof(entries[1].key)    - 1); entries[1].key[sizeof(entries[1].key) - 1] = '\0';
    strncpy(entries[1].result, "unchanged",   sizeof(entries[1].result) - 1); entries[1].result[sizeof(entries[1].result) - 1] = '\0';
    strncpy(entries[2].key,    "feat_sht31",  sizeof(entries[2].key)    - 1); entries[2].key[sizeof(entries[2].key) - 1] = '\0';
    strncpy(entries[2].result, "conflict:feat_ds18b20", sizeof(entries[2].result) - 1); entries[2].result[sizeof(entries[2].result) - 1] = '\0';

    char buf[512];
    size_t n = buildRichAck(entries, 3, 1714780800LL, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);

    JsonDocument doc;
    auto err = deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_INT(0, (int)err.code());

    TEST_ASSERT_EQUAL_STRING("config_applied", doc["event"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ok",             doc["results"]["feat_scale"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("unchanged",      doc["results"]["sample_int"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("conflict:feat_ds18b20", doc["results"]["feat_sht31"].as<const char*>());
    TEST_ASSERT_FALSE(doc["ts"].isNull());
}

void test_ack_rich_format_includes_ts_rfc3339() {
    // 1714780800 → "2024-05-04T00:00:00Z"
    AckEntry entries[1];
    strncpy(entries[0].key,    "feat_scale", sizeof(entries[0].key)    - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "ok",         sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    char buf[256];
    size_t n = buildRichAck(entries, 1, 1714780800LL, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);
    JsonDocument doc;
    deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_STRING("2024-05-04T00:00:00Z", doc["ts"].as<const char*>());
}

void test_ack_rich_format_zero_epoch_emits_sentinel() {
    AckEntry entries[1];
    strncpy(entries[0].key,    "sample_int", sizeof(entries[0].key)    - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "ok",         sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    char buf[256];
    size_t n = buildRichAck(entries, 1, 0LL, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);
    JsonDocument doc;
    deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", doc["ts"].as<const char*>());
}

void test_unchanged_category_serializes_in_rich_ack() {
    // Simulate a no-op apply: same value passed twice would produce "unchanged".
    // This verifies the "unchanged" category serializes correctly in the ack.
    AckEntry entries[2];
    strncpy(entries[0].key,    "feat_scale",  sizeof(entries[0].key)    - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "unchanged",   sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    strncpy(entries[1].key,    "sample_int",  sizeof(entries[1].key)    - 1); entries[1].key[sizeof(entries[1].key) - 1] = '\0';
    strncpy(entries[1].result, "unchanged",   sizeof(entries[1].result) - 1); entries[1].result[sizeof(entries[1].result) - 1] = '\0';

    char buf[256];
    size_t n = buildRichAck(entries, 2, 1714780800LL, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0u, n);
    JsonDocument doc;
    auto err = deserializeJson(doc, buf, n);
    TEST_ASSERT_EQUAL_INT(0, (int)err.code());
    TEST_ASSERT_EQUAL_STRING("unchanged", doc["results"]["feat_scale"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("unchanged", doc["results"]["sample_int"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("config_applied", doc["event"].as<const char*>());
}

// --- anyFeatKeyPresent (capabilities re-publish gate) ------------------------

void test_should_republish_capabilities_when_feat_key_present() {
    AckEntry entries[3];
    strncpy(entries[0].key,    "sample_int",  sizeof(entries[0].key)  - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "ok",          sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    strncpy(entries[1].key,    "feat_scale",  sizeof(entries[1].key)  - 1); entries[1].key[sizeof(entries[1].key) - 1] = '\0';
    strncpy(entries[1].result, "ok",          sizeof(entries[1].result) - 1); entries[1].result[sizeof(entries[1].result) - 1] = '\0';
    strncpy(entries[2].key,    "tag_name",    sizeof(entries[2].key)  - 1); entries[2].key[sizeof(entries[2].key) - 1] = '\0';
    strncpy(entries[2].result, "unchanged",   sizeof(entries[2].result) - 1); entries[2].result[sizeof(entries[2].result) - 1] = '\0';
    TEST_ASSERT_TRUE(anyFeatKeyPresent(entries, 3));
}

void test_should_not_republish_when_only_non_feat_keys() {
    AckEntry entries[2];
    strncpy(entries[0].key,    "sample_int",  sizeof(entries[0].key)  - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "ok",          sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    strncpy(entries[1].key,    "tag_name",    sizeof(entries[1].key)  - 1); entries[1].key[sizeof(entries[1].key) - 1] = '\0';
    strncpy(entries[1].result, "unchanged",   sizeof(entries[1].result) - 1); entries[1].result[sizeof(entries[1].result) - 1] = '\0';
    TEST_ASSERT_FALSE(anyFeatKeyPresent(entries, 2));
}

void test_should_republish_when_feat_key_is_unchanged() {
    // Even "unchanged" result on a feat_* key should trigger re-publish.
    AckEntry entries[1];
    strncpy(entries[0].key,    "feat_ds18b20", sizeof(entries[0].key)  - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "unchanged",    sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    TEST_ASSERT_TRUE(anyFeatKeyPresent(entries, 1));
}

void test_should_republish_when_feat_key_is_conflict() {
    // "conflict" result on a feat_* key — re-publish so iOS sees authoritative state.
    AckEntry entries[1];
    strncpy(entries[0].key,    "feat_sht31",          sizeof(entries[0].key)  - 1); entries[0].key[sizeof(entries[0].key) - 1] = '\0';
    strncpy(entries[0].result, "conflict:feat_ds18b20", sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    TEST_ASSERT_TRUE(anyFeatKeyPresent(entries, 1));
}

void test_anyFeatKeyPresent_includes_conflict_entries() {
    // §3.2 trigger 3: preValidate-rejected feat_* entry in allEntries must cause re-publish.
    // The combined entry array contains only a conflict-class feat_* entry.
    AckEntry entries[2];
    strncpy(entries[0].key,    "feat_sht31",           sizeof(entries[0].key)    - 1); entries[0].key[sizeof(entries[0].key) - 1]       = '\0';
    strncpy(entries[0].result, "conflict:feat_ds18b20", sizeof(entries[0].result) - 1); entries[0].result[sizeof(entries[0].result) - 1] = '\0';
    strncpy(entries[1].key,    "feat_ds18b20",          sizeof(entries[1].key)    - 1); entries[1].key[sizeof(entries[1].key) - 1]       = '\0';
    strncpy(entries[1].result, "conflict:feat_sht31",   sizeof(entries[1].result) - 1); entries[1].result[sizeof(entries[1].result) - 1] = '\0';
    TEST_ASSERT_TRUE(anyFeatKeyPresent(entries, 2));
}

// --- Unity runner ------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ack_entry_key_size);
    RUN_TEST(test_ack_entry_result_size);
    RUN_TEST(test_ack_entry_total_size);
    RUN_TEST(test_ack_entry_fields_writable);
    RUN_TEST(test_preValidate_returns_true_empty_parsed);
    RUN_TEST(test_preValidate_returns_true_full_parsed_no_feat);
    RUN_TEST(test_preValidate_null_out_count_safe);
    RUN_TEST(test_preValidate_rejects_both_temp_sensors_enabled);
    RUN_TEST(test_preValidate_allows_disabling_both_temp);
    RUN_TEST(test_preValidate_allows_swap_via_two_keys);
    RUN_TEST(test_preValidate_rejects_one_key_against_existing);
    RUN_TEST(test_ack_rich_format_serializes);
    RUN_TEST(test_ack_rich_format_includes_ts_rfc3339);
    RUN_TEST(test_ack_rich_format_zero_epoch_emits_sentinel);
    RUN_TEST(test_unchanged_category_serializes_in_rich_ack);
    RUN_TEST(test_should_republish_capabilities_when_feat_key_present);
    RUN_TEST(test_should_not_republish_when_only_non_feat_keys);
    RUN_TEST(test_should_republish_when_feat_key_is_unchanged);
    RUN_TEST(test_should_republish_when_feat_key_is_conflict);
    RUN_TEST(test_anyFeatKeyPresent_includes_conflict_entries);
    return UNITY_END();
}
