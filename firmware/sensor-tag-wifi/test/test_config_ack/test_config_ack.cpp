#include <unity.h>
#include <cstring>
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

// --- preValidate PR-1 stub ---------------------------------------------------

void test_preValidate_returns_true_empty_parsed() {
    ConfigParser::ConfigUpdate parsed {};
    AckEntry entries[8];
    size_t count = 99;  // sentinel to confirm it's zeroed
    bool ok = preValidate(parsed, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_returns_true_full_parsed() {
    // Even with every known key set, PR-1 preValidate passes everything.
    ConfigParser::ConfigUpdate parsed {};
    parsed.has_sample_int   = true;  parsed.sample_int   = 300;
    parsed.has_upload_every = true;  parsed.upload_every = 2;
    parsed.has_tag_name     = true;  strncpy(parsed.tag_name, "hive-1", sizeof(parsed.tag_name) - 1);
    parsed.has_ota_host     = true;  strncpy(parsed.ota_host, "192.168.1.61", sizeof(parsed.ota_host) - 1);

    AckEntry entries[8];
    size_t count = 99;
    bool ok = preValidate(parsed, entries, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, count);
}

void test_preValidate_null_out_count_safe() {
    // Must not crash when outCount is null (caller may omit it).
    ConfigParser::ConfigUpdate parsed {};
    AckEntry entries[8];
    bool ok = preValidate(parsed, entries, nullptr);
    TEST_ASSERT_TRUE(ok);
}

// --- Unity runner ------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ack_entry_key_size);
    RUN_TEST(test_ack_entry_result_size);
    RUN_TEST(test_ack_entry_total_size);
    RUN_TEST(test_ack_entry_fields_writable);
    RUN_TEST(test_preValidate_returns_true_empty_parsed);
    RUN_TEST(test_preValidate_returns_true_full_parsed);
    RUN_TEST(test_preValidate_null_out_count_safe);
    return UNITY_END();
}
