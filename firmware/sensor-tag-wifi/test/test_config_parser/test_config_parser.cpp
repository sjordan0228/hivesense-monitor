#include <unity.h>

#include "config_parser.h"

#include <cstring>

using ConfigParser::ConfigUpdate;
using ConfigParser::parse;

namespace {

bool isInRejectedList(const ConfigUpdate& u, const char* key) {
    for (uint8_t i = 0; i < u.num_rejected; ++i) {
        if (strcmp(u.rejected[i], key) == 0) return true;
    }
    return false;
}

}  // namespace

// --- Happy paths -----------------------------------------------------

void test_sample_int_at_min() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":30}", u));
    TEST_ASSERT_TRUE(u.has_sample_int);
    TEST_ASSERT_EQUAL_UINT16(30, u.sample_int);
    TEST_ASSERT_EQUAL_UINT8(0, u.num_rejected);
}

void test_sample_int_typical() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":300}", u));
    TEST_ASSERT_TRUE(u.has_sample_int);
    TEST_ASSERT_EQUAL_UINT16(300, u.sample_int);
}

void test_sample_int_at_max() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":3600}", u));
    TEST_ASSERT_TRUE(u.has_sample_int);
    TEST_ASSERT_EQUAL_UINT16(3600, u.sample_int);
}

void test_upload_every_typical() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"upload_every\":3}", u));
    TEST_ASSERT_TRUE(u.has_upload_every);
    TEST_ASSERT_EQUAL_UINT8(3, u.upload_every);
}

void test_tag_name_typical() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"tag_name\":\"yard-1\"}", u));
    TEST_ASSERT_TRUE(u.has_tag_name);
    TEST_ASSERT_EQUAL_STRING("yard-1", u.tag_name);
}

void test_ota_host_typical() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"ota_host\":\"192.168.1.61\"}", u));
    TEST_ASSERT_TRUE(u.has_ota_host);
    TEST_ASSERT_EQUAL_STRING("192.168.1.61", u.ota_host);
}

void test_multiple_keys_partial_apply() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse(
        "{\"sample_int\":300,\"upload_every\":2,\"tag_name\":\"hive-3\"}", u));
    TEST_ASSERT_TRUE(u.has_sample_int);
    TEST_ASSERT_EQUAL_UINT16(300, u.sample_int);
    TEST_ASSERT_TRUE(u.has_upload_every);
    TEST_ASSERT_EQUAL_UINT8(2, u.upload_every);
    TEST_ASSERT_TRUE(u.has_tag_name);
    TEST_ASSERT_EQUAL_STRING("hive-3", u.tag_name);
    TEST_ASSERT_EQUAL_UINT8(0, u.num_rejected);
}

// --- Validation rejections -----------------------------------------------

void test_sample_int_below_range_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":29}", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
    TEST_ASSERT_TRUE(isInRejectedList(u, "sample_int"));
}

void test_sample_int_above_range_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":3601}", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
    TEST_ASSERT_TRUE(isInRejectedList(u, "sample_int"));
}

void test_sample_int_zero_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":0}", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
    TEST_ASSERT_TRUE(isInRejectedList(u, "sample_int"));
}

void test_sample_int_string_type_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"sample_int\":\"300\"}", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
    TEST_ASSERT_TRUE(isInRejectedList(u, "sample_int"));
}

void test_upload_every_zero_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"upload_every\":0}", u));
    TEST_ASSERT_FALSE(u.has_upload_every);
    TEST_ASSERT_TRUE(isInRejectedList(u, "upload_every"));
}

void test_tag_name_too_long_rejected() {
    // 64 chars — equal to max, leaves no room for NUL → rejected
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse(
        "{\"tag_name\":\"" "0123456789012345678901234567890123456789012345678901234567890123" "\"}",
        u));
    TEST_ASSERT_FALSE(u.has_tag_name);
    TEST_ASSERT_TRUE(isInRejectedList(u, "tag_name"));
}

void test_tag_name_int_type_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"tag_name\":42}", u));
    TEST_ASSERT_FALSE(u.has_tag_name);
    TEST_ASSERT_TRUE(isInRejectedList(u, "tag_name"));
}

// --- Excluded-by-policy keys ---------------------------------------------

void test_wifi_ssid_excluded() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"wifi_ssid\":\"hostile\"}", u));
    TEST_ASSERT_TRUE(isInRejectedList(u, "wifi_ssid"));
}

void test_wifi_pass_excluded() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"wifi_pass\":\"123\"}", u));
    TEST_ASSERT_TRUE(isInRejectedList(u, "wifi_pass"));
}

void test_mqtt_host_excluded() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"mqtt_host\":\"10.0.0.1\"}", u));
    TEST_ASSERT_TRUE(isInRejectedList(u, "mqtt_host"));
}

void test_mqtt_pass_excluded() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"mqtt_pass\":\"secret\"}", u));
    TEST_ASSERT_TRUE(isInRejectedList(u, "mqtt_pass"));
}

void test_unknown_key_rejected() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{\"random_key\":\"x\"}", u));
    TEST_ASSERT_TRUE(isInRejectedList(u, "random_key"));
}

// --- Mixed valid + invalid -------------------------------------------------

void test_valid_and_invalid_apply_partial() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse(
        "{\"sample_int\":300,\"wifi_ssid\":\"nope\",\"upload_every\":2}", u));
    TEST_ASSERT_TRUE(u.has_sample_int);
    TEST_ASSERT_EQUAL_UINT16(300, u.sample_int);
    TEST_ASSERT_TRUE(u.has_upload_every);
    TEST_ASSERT_EQUAL_UINT8(2, u.upload_every);
    TEST_ASSERT_TRUE(isInRejectedList(u, "wifi_ssid"));
    // sample_int + upload_every must NOT be in rejected
    TEST_ASSERT_FALSE(isInRejectedList(u, "sample_int"));
    TEST_ASSERT_FALSE(isInRejectedList(u, "upload_every"));
}

// --- Malformed JSON ----------------------------------------------------------

void test_malformed_json_returns_false() {
    ConfigUpdate u;
    TEST_ASSERT_FALSE(parse("not json at all", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
}

void test_array_top_level_returns_false() {
    ConfigUpdate u;
    TEST_ASSERT_FALSE(parse("[1,2,3]", u));
}

void test_string_top_level_returns_false() {
    ConfigUpdate u;
    TEST_ASSERT_FALSE(parse("\"not an object\"", u));
}

void test_null_input_returns_false() {
    ConfigUpdate u;
    TEST_ASSERT_FALSE(parse(nullptr, u));
}

void test_empty_object_returns_true_no_apply() {
    ConfigUpdate u;
    TEST_ASSERT_TRUE(parse("{}", u));
    TEST_ASSERT_FALSE(u.has_sample_int);
    TEST_ASSERT_FALSE(u.has_upload_every);
    TEST_ASSERT_FALSE(u.has_tag_name);
    TEST_ASSERT_FALSE(u.has_ota_host);
    TEST_ASSERT_EQUAL_UINT8(0, u.num_rejected);
}

// --- Rejected-list capacity --------------------------------------------------

void test_rejected_list_does_not_overflow() {
    ConfigUpdate u;
    // 9 unknown keys exceeds MAX_REJECTED_KEYS (8). Parser must NOT crash;
    // the overflow ones are dropped silently.
    TEST_ASSERT_TRUE(parse(
        "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9}",
        u));
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(ConfigParser::MAX_REJECTED_KEYS, u.num_rejected);
}

// --- Unity test runner ------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_sample_int_at_min);
    RUN_TEST(test_sample_int_typical);
    RUN_TEST(test_sample_int_at_max);
    RUN_TEST(test_upload_every_typical);
    RUN_TEST(test_tag_name_typical);
    RUN_TEST(test_ota_host_typical);
    RUN_TEST(test_multiple_keys_partial_apply);

    RUN_TEST(test_sample_int_below_range_rejected);
    RUN_TEST(test_sample_int_above_range_rejected);
    RUN_TEST(test_sample_int_zero_rejected);
    RUN_TEST(test_sample_int_string_type_rejected);
    RUN_TEST(test_upload_every_zero_rejected);
    RUN_TEST(test_tag_name_too_long_rejected);
    RUN_TEST(test_tag_name_int_type_rejected);

    RUN_TEST(test_wifi_ssid_excluded);
    RUN_TEST(test_wifi_pass_excluded);
    RUN_TEST(test_mqtt_host_excluded);
    RUN_TEST(test_mqtt_pass_excluded);
    RUN_TEST(test_unknown_key_rejected);

    RUN_TEST(test_valid_and_invalid_apply_partial);

    RUN_TEST(test_malformed_json_returns_false);
    RUN_TEST(test_array_top_level_returns_false);
    RUN_TEST(test_string_top_level_returns_false);
    RUN_TEST(test_null_input_returns_false);
    RUN_TEST(test_empty_object_returns_true_no_apply);

    RUN_TEST(test_rejected_list_does_not_overflow);

    return UNITY_END();
}
