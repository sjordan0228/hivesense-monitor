#include <unity.h>
#include <cstring>
#include "reading.h"
#include "payload.h"

void setUp() {}
void tearDown() {}

void test_serialize_full_reading_with_humidity() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = 52.3f,
        .humidity2 = 55.1f,
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,"
        "\"h1\":52.30,\"h2\":55.10,\"b\":87}",
        buf);
}

void test_serialize_ds18b20_reading_omits_humidity() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,\"b\":87}",
        buf);
}

void test_serialize_returns_negative_on_undersized_buffer() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f, .temp2 = 24.1f,
        .humidity1 = 52.3f, .humidity2 = 55.1f,
        .battery_pct = 87,
    };
    char buf[8];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);
}

void test_serialize_emits_only_valid_humidity_channel() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = 52.3f,
        .humidity2 = NAN,      // top SHT31 failed
        .battery_pct = 87,
    };
    char buf[160];
    int n = Payload::serialize("ab12cd34", r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"t\":1712345678,\"t1\":22.40,\"t2\":24.10,"
        "\"h1\":52.30,\"b\":87}",
        buf);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_full_reading_with_humidity);
    RUN_TEST(test_serialize_ds18b20_reading_omits_humidity);
    RUN_TEST(test_serialize_returns_negative_on_undersized_buffer);
    RUN_TEST(test_serialize_emits_only_valid_humidity_channel);
    return UNITY_END();
}
