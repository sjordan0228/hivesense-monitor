#include <unity.h>
#include <cstring>
#include "ota_sha256.h"

void setUp() {}
void tearDown() {}

// Known SHA-256 fixtures
// "" -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
// "abc" -> ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

void test_empty_input_hash() {
    Sha256Streamer s;
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
}

void test_single_chunk_abc() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

void test_streaming_matches_single_chunk() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("a"), 1);
    s.update(reinterpret_cast<const uint8_t*>("b"), 1);
    s.update(reinterpret_cast<const uint8_t*>("c"), 1);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

void test_matches_compares_case_insensitive() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_TRUE(s.matches(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    TEST_ASSERT_TRUE(s.matches(
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    TEST_ASSERT_FALSE(s.matches("0000000000000000000000000000000000000000000000000000000000000000"));
}

void test_reset_allows_reuse() {
    Sha256Streamer s;
    s.update(reinterpret_cast<const uint8_t*>("ignored"), 7);
    s.reset();
    s.update(reinterpret_cast<const uint8_t*>("abc"), 3);
    char hex[65] = {};
    s.finalizeToHex(hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_input_hash);
    RUN_TEST(test_single_chunk_abc);
    RUN_TEST(test_streaming_matches_single_chunk);
    RUN_TEST(test_matches_compares_case_insensitive);
    RUN_TEST(test_reset_allows_reuse);
    return UNITY_END();
}
