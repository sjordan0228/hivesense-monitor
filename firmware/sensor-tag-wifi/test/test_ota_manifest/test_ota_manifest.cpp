#include <unity.h>
#include <cstring>
#include <cstdio>
#include "ota_manifest.h"

void setUp() {}
void tearDown() {}

void test_parses_valid_manifest() {
    const char* j =
        "{\"version\":\"v0.2.0\","
        "\"url\":\"http://192.168.1.61/firmware/sensor-tag-wifi/sht31/v0.2.0/firmware.bin\","
        "\"sha256\":\"abc123def456abc123def456abc123def456abc123def456abc123def4561234\","
        "\"size\":1046912}";
    Manifest m {};
    TEST_ASSERT_TRUE(parseManifest(j, strlen(j), m));
    TEST_ASSERT_EQUAL_STRING("v0.2.0", m.version);
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.1.61/firmware/sensor-tag-wifi/sht31/v0.2.0/firmware.bin", m.url);
    TEST_ASSERT_EQUAL_STRING(
        "abc123def456abc123def456abc123def456abc123def456abc123def4561234", m.sha256);
    TEST_ASSERT_EQUAL_UINT32(1046912, m.size);
}

void test_rejects_missing_field() {
    const char* j = "{\"version\":\"v0.2.0\",\"url\":\"http://x/y\",\"size\":100}";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_malformed_json() {
    const char* j = "{not valid json";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_oversize_url() {
    char j[600];
    char url[300];
    memset(url, 'a', sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    snprintf(j, sizeof(j),
        "{\"version\":\"v1\",\"url\":\"%s\",\"sha256\":\"%s\",\"size\":1}",
        url,
        "0000000000000000000000000000000000000000000000000000000000000000");
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

void test_rejects_wrong_sha_length() {
    const char* j =
        "{\"version\":\"v1\",\"url\":\"http://x/y\","
        "\"sha256\":\"deadbeef\",\"size\":1}";
    Manifest m {};
    TEST_ASSERT_FALSE(parseManifest(j, strlen(j), m));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_valid_manifest);
    RUN_TEST(test_rejects_missing_field);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_rejects_oversize_url);
    RUN_TEST(test_rejects_wrong_sha_length);
    return UNITY_END();
}
