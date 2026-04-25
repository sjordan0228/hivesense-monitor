# sensor-tag-wifi: fleet visibility & battery telemetry — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-reading firmware version (`v`), raw battery voltage (`vbat_mV`), and post-connect WiFi RSSI (`rssi`) to the sensor-tag-wifi MQTT payload, plus matching Telegraf parser entries and unit-test coverage.

**Architecture:** Decompose `Battery::readPercent()` into a pure inline `percentFromMillivolts()` (testable on the native env) plus an Arduino-dependent `readMillivolts()`. Add `vbat_mV` to the `Reading` POD (per-sample, RTC-persisted). Pass `fwVersion` and `rssi` as parameters to `Payload::serialize()` (per-publish, not per-sample). Telegraf gets all three as Influx fields, never tags.

**Tech Stack:** C++17, PlatformIO (Arduino + native test env), Unity test framework, Telegraf json_v2 parser, InfluxDB 2.x.

**Spec:** [docs/superpowers/specs/2026-04-25-sensor-tag-wifi-fleet-visibility-design.md](../specs/2026-04-25-sensor-tag-wifi-fleet-visibility-design.md)

**Branch policy:** Work on `dev`. Per-task commits. Never commit to `main`.

**After every commit (and before moving to the next task):** run `pio test -e native -d firmware/sensor-tag-wifi` and confirm 0 failures.

---

## File Structure

| File | Role | Action |
|---|---|---|
| `firmware/sensor-tag-wifi/src/battery.h` | Battery interface | **Modify** — add `readMillivolts()`, inline `percentFromMillivolts()` |
| `firmware/sensor-tag-wifi/src/battery.cpp` | Battery impl (Arduino-dependent) | **Modify** — split ADC sweep into `readMillivolts()`, `readPercent()` becomes composition |
| `firmware/sensor-tag-wifi/test/test_battery_math/test_battery_math.cpp` | Native unit tests for pure conversion | **Create** |
| `firmware/sensor-tag-wifi/include/reading.h` | Reading POD | **Modify** — add `uint16_t vbat_mV` field |
| `firmware/sensor-tag-wifi/include/payload.h` | Payload serializer interface | **Modify** — extend `serialize()` signature |
| `firmware/sensor-tag-wifi/src/payload.cpp` | Payload serializer impl | **Modify** — emit `v`, `vbat_mV`, `rssi` |
| `firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp` | Payload unit tests | **Modify** — update 6 tests + add 1 new |
| `firmware/sensor-tag-wifi/src/main.cpp` | Sample wake cycle | **Modify** — populate `r.vbat_mV` |
| `firmware/sensor-tag-wifi/src/mqtt_client.cpp` | MQTT publish | **Modify** — capture `WiFi.RSSI()`, pass `FIRMWARE_VERSION` and rssi to serialize |
| `firmware/sensor-tag-wifi/platformio.ini` | Build config | **Modify** — add `+<battery.cpp>` to native filter (ADC stub) — N/A, see Task 1 note |
| `deploy/tsdb/telegraf-combsense.conf` | Telegraf parser (mirror) | **Modify** — add 3 field blocks |

---

## Task 1: Pure `percentFromMillivolts()` with native unit tests (TDD)

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/battery.h`
- Create: `firmware/sensor-tag-wifi/test/test_battery_math/test_battery_math.cpp`

**Note on native build:** `percentFromMillivolts` is `inline` in `battery.h`. The native env doesn't include `battery.cpp` (Arduino-dependent), but inline functions in headers don't need a `.cpp` — the native test simply `#include "battery.h"` and the inline body is available at compile time. No `platformio.ini` change required. The other `battery.h` declaration (`readMillivolts`) is decl-only and the linker won't try to resolve it because the test never calls it.

- [ ] **Step 1: Write the failing native tests**

Create `firmware/sensor-tag-wifi/test/test_battery_math/test_battery_math.cpp`:

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail to compile**

```
cd firmware/sensor-tag-wifi && pio test -e native -f test_battery_math
```

Expected: compile error — `percentFromMillivolts` is not declared in `Battery::` namespace.

- [ ] **Step 3: Add inline `percentFromMillivolts` to battery.h**

Replace `firmware/sensor-tag-wifi/src/battery.h` entirely with:

```cpp
#pragma once

#include <cstdint>

namespace Battery {

constexpr uint16_t VBAT_FULL_MV  = 4200;
constexpr uint16_t VBAT_EMPTY_MV = 3300;

/// Read raw battery voltage via ADC, divider-corrected. Returns mV.
/// Implemented in battery.cpp; requires Arduino runtime.
uint16_t readMillivolts();

/// Pure conversion: clamp + linear-interpolate mV to 0..100% Li-ion SOC.
/// Inline so native unit tests can link without Arduino. The 4200/3300 mV
/// endpoints assume 18650 Li-ion; see issue #10 for replacing the linear
/// formula with a real discharge curve once vbat_mV telemetry is collected.
inline uint8_t percentFromMillivolts(uint16_t mV) {
    if (mV >= VBAT_FULL_MV)  return 100;
    if (mV <= VBAT_EMPTY_MV) return 0;
    return static_cast<uint8_t>(
        (static_cast<float>(mV) - VBAT_EMPTY_MV) * 100.0f /
        (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

/// Convenience: composes readMillivolts() + percentFromMillivolts().
uint8_t readPercent();

}  // namespace Battery
```

- [ ] **Step 4: Run tests to verify they pass**

```
cd firmware/sensor-tag-wifi && pio test -e native -f test_battery_math
```

Expected: 6 tests pass.

- [ ] **Step 5: Run all native tests (regression check)**

```
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: 30 + 6 = 36 tests pass. (Existing 30 from payload/ota_*; 6 new from battery_math.)

- [ ] **Step 6: Commit**

```bash
git add firmware/sensor-tag-wifi/src/battery.h firmware/sensor-tag-wifi/test/test_battery_math/
git commit -m "test(sensor-tag-wifi): add native tests for Battery::percentFromMillivolts

Extracts the pure percent conversion as an inline header function so it
can be unit-tested on the native env. Behavior unchanged."
```

---

## Task 2: Refactor `battery.cpp` to expose `readMillivolts()`

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/battery.cpp`

- [ ] **Step 1: Replace battery.cpp with the refactored implementation**

Replace `firmware/sensor-tag-wifi/src/battery.cpp` entirely with:

```cpp
#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace {

constexpr float DIVIDER_RATIO = 2.0f;   // 100k / 100k → 2:1
constexpr uint8_t ADC_SAMPLES = 8;

}  // anonymous namespace

namespace Battery {

uint16_t readMillivolts() {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
        acc += analogReadMilliVolts(PIN_BATTERY_ADC);
    }
    float vbat = (acc / static_cast<float>(ADC_SAMPLES)) * DIVIDER_RATIO;
    if (vbat < 0.0f) return 0;
    if (vbat > UINT16_MAX) return UINT16_MAX;
    return static_cast<uint16_t>(vbat);
}

uint8_t readPercent() {
    return percentFromMillivolts(readMillivolts());
}

}  // namespace Battery
```

- [ ] **Step 2: Compile firmware to confirm Arduino target still builds**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20
```

Expected: `SUCCESS`. No errors. (Pick `xiao-c6-ds18b20` — it's the deployed yard variant per ROUTER.)

- [ ] **Step 3: Run all native tests (regression check)**

```
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: 36 tests pass.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/src/battery.cpp
git commit -m "refactor(sensor-tag-wifi): split Battery::readPercent into mV + pure conv

readMillivolts() does the ADC sweep; readPercent() composes it with the
inline percentFromMillivolts(). Single ADC sweep, one source of truth
for the conversion math, vbat_mV now exposable for telemetry."
```

---

## Task 3: Extend `Reading` struct + `Payload::serialize()` signature (TDD)

**Files:**
- Modify: `firmware/sensor-tag-wifi/include/reading.h`
- Modify: `firmware/sensor-tag-wifi/include/payload.h`
- Modify: `firmware/sensor-tag-wifi/src/payload.cpp`
- Modify: `firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp`
- Modify: `firmware/sensor-tag-wifi/src/mqtt_client.cpp` (call-site update — placeholder rssi=0 here, real value in Task 4)

- [ ] **Step 1: Update `test_payload.cpp` to use the new signature and expected output**

Replace `firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp` entirely with:

```cpp
#include <unity.h>
#include <cstring>
#include <cmath>
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
        .vbat_mV = 3987,
        .battery_pct = 87,
    };
    char buf[200];
    int n = Payload::serialize("ab12cd34", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"v\":\"5423c04\",\"t\":1712345678,"
        "\"t1\":22.40,\"t2\":24.10,\"h1\":52.30,\"h2\":55.10,"
        "\"vbat_mV\":3987,\"rssi\":-58,\"b\":87}",
        buf);
}

void test_serialize_ds18b20_reading_omits_humidity() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .vbat_mV = 3987,
        .battery_pct = 87,
    };
    char buf[200];
    int n = Payload::serialize("ab12cd34", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"v\":\"5423c04\",\"t\":1712345678,"
        "\"t1\":22.40,\"t2\":24.10,\"vbat_mV\":3987,\"rssi\":-58,\"b\":87}",
        buf);
}

void test_serialize_returns_negative_on_undersized_buffer() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f, .temp2 = 24.1f,
        .humidity1 = 52.3f, .humidity2 = 55.1f,
        .vbat_mV = 3987,
        .battery_pct = 87,
    };
    char buf[8];
    int n = Payload::serialize("ab12cd34", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);
}

void test_serialize_emits_only_valid_humidity_channel() {
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = 52.3f,
        .humidity2 = NAN,      // top SHT31 failed
        .vbat_mV = 3987,
        .battery_pct = 87,
    };
    char buf[200];
    int n = Payload::serialize("ab12cd34", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"ab12cd34\",\"v\":\"5423c04\",\"t\":1712345678,"
        "\"t1\":22.40,\"t2\":24.10,\"h1\":52.30,"
        "\"vbat_mV\":3987,\"rssi\":-58,\"b\":87}",
        buf);
}

void test_serialize_emits_null_for_nan_temps() {
    // Firmware must emit JSON `null` (not `nan`) so downstream JSON parsers
    // (Telegraf json_v2, Swift JSONDecoder, etc.) don't reject the message.
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 21.50f,
        .temp2 = NAN,           // external DS18B20 not wired
        .humidity1 = NAN,
        .humidity2 = NAN,
        .vbat_mV = 0,
        .battery_pct = 0,
    };
    char buf[200];
    int n = Payload::serialize("c5fffe12", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":\"c5fffe12\",\"v\":\"5423c04\",\"t\":1712345678,"
        "\"t1\":21.50,\"t2\":null,\"vbat_mV\":0,\"rssi\":-58,\"b\":0}",
        buf);
}

void test_serialize_emits_t_zero_when_timestamp_unset() {
    // Readings buffered before the first NTP sync have timestamp=0. The payload
    // must serialize this as "t":0 so Telegraf can apply the t=0 fallback rule
    // and substitute the ingest time rather than silently emitting `nan` or
    // omitting the field.
    Reading r {
        .timestamp = 0,
        .temp1 = 20.00f,
        .temp2 = 21.00f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .vbat_mV = 3987,
        .battery_pct = 50,
    };
    char buf[200];
    int n = Payload::serialize("ab12cd34", "5423c04", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"t\":0,"));
}

void test_serialize_with_unknown_version() {
    // get_version.py falls back to the literal "unknown" when git describe
    // fails. The payload must carry that string verbatim so the operator can
    // still distinguish a no-git build from a real version.
    Reading r {
        .timestamp = 1712345678,
        .temp1 = 22.4f,
        .temp2 = 24.1f,
        .humidity1 = NAN,
        .humidity2 = NAN,
        .vbat_mV = 3987,
        .battery_pct = 87,
    };
    char buf[200];
    int n = Payload::serialize("ab12cd34", "unknown", -58, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"v\":\"unknown\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_full_reading_with_humidity);
    RUN_TEST(test_serialize_ds18b20_reading_omits_humidity);
    RUN_TEST(test_serialize_returns_negative_on_undersized_buffer);
    RUN_TEST(test_serialize_emits_only_valid_humidity_channel);
    RUN_TEST(test_serialize_emits_null_for_nan_temps);
    RUN_TEST(test_serialize_emits_t_zero_when_timestamp_unset);
    RUN_TEST(test_serialize_with_unknown_version);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

```
cd firmware/sensor-tag-wifi && pio test -e native -f test_payload
```

Expected: compile errors — `vbat_mV` not a member of `Reading`, `serialize` signature mismatch.

- [ ] **Step 3: Add `vbat_mV` to `Reading`**

Replace `firmware/sensor-tag-wifi/include/reading.h` entirely with:

```cpp
#pragma once

#include <cstdint>

/// A single sensor sample. POD — safe to put in RTC_DATA_ATTR memory.
///
/// `t1`/`t2` are the two temperature channels (brood / top). `h1`/`h2` are the
/// two humidity channels. NAN means the channel is unavailable (DS18B20 build,
/// or a single SHT31 failing on a dual-sensor board) and is omitted from the
/// serialized payload.
///
/// `vbat_mV` is the raw ADC reading (divider-corrected) at sample time, kept
/// alongside `battery_pct` so downstream consumers can apply a better SOC
/// curve later without a firmware change. See issue #10.
struct Reading {
    uint32_t timestamp;    // unix seconds
    float    temp1;        // brood (°C)
    float    temp2;        // top   (°C)
    float    humidity1;    // brood (%RH) — NAN if unavailable
    float    humidity2;    // top   (%RH) — NAN if unavailable
    uint16_t vbat_mV;      // raw battery voltage at sample time
    uint8_t  battery_pct;  // 0..100
};
```

- [ ] **Step 4: Extend `payload.h` signature**

Replace `firmware/sensor-tag-wifi/include/payload.h` entirely with:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include "reading.h"

namespace Payload {

/// Serialize a reading into a JSON string.
/// @param deviceId   8-char hex device ID (null-terminated)
/// @param fwVersion  null-terminated firmware version string (from FIRMWARE_VERSION
///                   build flag; `"unknown"` when git describe failed)
/// @param rssi       WiFi RSSI captured post-connect at publish time, in dBm
/// @param r          reading to serialize
/// @param buf        output buffer
/// @param bufLen     size of output buffer
/// @return           number of bytes written (excluding null), or -1 on overflow
int serialize(const char* deviceId,
              const char* fwVersion,
              int8_t      rssi,
              const Reading& r,
              char* buf, size_t bufLen);

}  // namespace Payload
```

- [ ] **Step 5: Update `payload.cpp` to emit the new fields**

Replace `firmware/sensor-tag-wifi/src/payload.cpp` entirely with:

```cpp
#include "payload.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Payload {

/// Append a formatted fragment to buf, returning true on success. Fails if the
/// buffer would overflow.
static bool appendf(char*& p, char* end, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(p, end - p, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= end - p) return false;
    p += n;
    return true;
}

/// Serialize a float field as either a number (`%.2f`) or JSON `null` when NaN.
/// `%.2f` on NaN prints `nan`, which is not valid JSON and breaks downstream
/// parsers (Telegraf json_v2, Swift JSONDecoder, PostgreSQL json type).
static bool appendNumOrNull(char*& p, char* end, const char* key, float v) {
    if (std::isnan(v)) return appendf(p, end, ",\"%s\":null", key);
    return appendf(p, end, ",\"%s\":%.2f", key, v);
}

int serialize(const char* deviceId,
              const char* fwVersion,
              int8_t      rssi,
              const Reading& r,
              char* buf, size_t bufLen) {
    if (bufLen == 0) return -1;
    char* p   = buf;
    char* end = buf + bufLen;

    if (!appendf(p, end, "{\"id\":\"%s\",\"v\":\"%s\",\"t\":%lu",
                 deviceId, fwVersion,
                 static_cast<unsigned long>(r.timestamp))) return -1;
    if (!appendNumOrNull(p, end, "t1", r.temp1)) return -1;
    if (!appendNumOrNull(p, end, "t2", r.temp2)) return -1;
    if (!std::isnan(r.humidity1) && !appendf(p, end, ",\"h1\":%.2f", r.humidity1)) return -1;
    if (!std::isnan(r.humidity2) && !appendf(p, end, ",\"h2\":%.2f", r.humidity2)) return -1;
    if (!appendf(p, end, ",\"vbat_mV\":%u,\"rssi\":%d,\"b\":%u}",
                 static_cast<unsigned>(r.vbat_mV),
                 static_cast<int>(rssi),
                 static_cast<unsigned>(r.battery_pct))) return -1;

    return static_cast<int>(p - buf);
}

}  // namespace Payload
```

- [ ] **Step 6: Run native tests — they should pass now**

```
cd firmware/sensor-tag-wifi && pio test -e native -f test_payload
```

Expected: 7 payload tests pass.

- [ ] **Step 7: Compile firmware to surface the mqtt_client.cpp call-site mismatch**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20
```

Expected: compile error in `mqtt_client.cpp` — `Payload::serialize` is now called with the wrong number of arguments.

- [ ] **Step 8: Update the `mqtt_client.cpp` call site (placeholder rssi=0; real wiring in Task 4)**

In `firmware/sensor-tag-wifi/src/mqtt_client.cpp`, find the line (currently around line 63):

```cpp
    int n = Payload::serialize(deviceId, r, payload, sizeof(payload));
```

Replace with:

```cpp
    // RSSI captured post-connect — wired in next task; placeholder 0 here
    // makes the call site type-check during the payload-shape rollout.
    int n = Payload::serialize(deviceId, FIRMWARE_VERSION, 0, r, payload, sizeof(payload));
```

Also at the top of `mqtt_client.cpp`, ensure the FIRMWARE_VERSION macro fallback is present (mirrors the pattern in [src/ota.cpp:17-18](firmware/sensor-tag-wifi/src/ota.cpp#L17-L18)). If not already there, add immediately after the existing `#include` block:

```cpp
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
```

- [ ] **Step 9: Compile firmware again — should succeed**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20
```

Expected: `SUCCESS`.

- [ ] **Step 10: Run full native test suite (regression)**

```
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: 37 tests pass (30 prior + 6 battery_math + 1 new payload-unknown-version, minus 0 — original 6 payload tests still pass).

- [ ] **Step 11: Commit**

```bash
git add firmware/sensor-tag-wifi/include/reading.h \
        firmware/sensor-tag-wifi/include/payload.h \
        firmware/sensor-tag-wifi/src/payload.cpp \
        firmware/sensor-tag-wifi/src/mqtt_client.cpp \
        firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp
git commit -m "feat(sensor-tag-wifi): add v, vbat_mV, rssi to reading payload

v: firmware version string (from get_version.py, 'unknown' fallback)
vbat_mV: raw ADC voltage at sample time, persists in RTC ring buffer
rssi: WiFi RSSI passed by caller at publish time (wired in next commit)

Telegraf parser update + main.cpp/mqtt_client.cpp wiring follow."
```

---

## Task 4: Wire `vbat_mV` at sample time and capture RSSI at publish time

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/main.cpp`
- Modify: `firmware/sensor-tag-wifi/src/mqtt_client.cpp`

- [ ] **Step 1: Populate `r.vbat_mV` in `sampleAndEnqueue`**

In `firmware/sensor-tag-wifi/src/main.cpp`, find the `sampleAndEnqueue()` function (around line 87). Locate these two lines:

```cpp
    r.battery_pct = Battery::readPercent();
    lastBatteryPct = r.battery_pct;
```

Replace with:

```cpp
    r.vbat_mV     = Battery::readMillivolts();
    r.battery_pct = Battery::percentFromMillivolts(r.vbat_mV);
    lastBatteryPct = r.battery_pct;
```

Also update the diagnostic Serial.printf below it (currently logs `b=%u`) to include the raw mV. Find:

```cpp
    Serial.printf("[MAIN] sample t1=%.2f t2=%.2f h1=%.2f h2=%.2f b=%u buffered=%u\n",
                  r.temp1, r.temp2, r.humidity1, r.humidity2, r.battery_pct,
                  RingBuffer::size());
```

Replace with:

```cpp
    Serial.printf("[MAIN] sample t1=%.2f t2=%.2f h1=%.2f h2=%.2f vbat=%umV b=%u buffered=%u\n",
                  r.temp1, r.temp2, r.humidity1, r.humidity2,
                  r.vbat_mV, r.battery_pct, RingBuffer::size());
```

- [ ] **Step 2: Capture `WiFi.RSSI()` and pass it through the publish path in `mqtt_client.cpp`**

Open `firmware/sensor-tag-wifi/src/mqtt_client.cpp`. Find the function that publishes a single reading (the one containing the `Payload::serialize` call updated in Task 3). The structure is roughly:

```cpp
bool publish(const char* deviceId, const Reading& r) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s%s/reading", MQTT_TOPIC_PREFIX, deviceId);

    char payload[PAYLOAD_MAX_LEN];
    int n = Payload::serialize(deviceId, FIRMWARE_VERSION, 0, r, payload, sizeof(payload));
    ...
}
```

Make `publish()` accept the rssi value as a parameter rather than hard-coding 0. Update the function signature and forward it to `serialize`:

```cpp
bool publish(const char* deviceId, const Reading& r, int8_t rssi) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s%s/reading", MQTT_TOPIC_PREFIX, deviceId);

    char payload[PAYLOAD_MAX_LEN];
    int n = Payload::serialize(deviceId, FIRMWARE_VERSION, rssi, r, payload, sizeof(payload));
    ...
}
```

Update the matching declaration in `mqtt_client.h` (or wherever `publish` is declared — likely `mqtt_client.h` next to the cpp). Open the header and update the signature to match:

```cpp
bool publish(const char* deviceId, const Reading& r, int8_t rssi);
```

- [ ] **Step 3: Capture RSSI in `uploadAndCheckOta` and pass it through to `publish()`**

The drain loop lives in [src/main.cpp::uploadAndCheckOta](firmware/sensor-tag-wifi/src/main.cpp), not `mqtt_client.cpp`. RSSI must be captured AFTER `WifiManager::connect()` succeeds and BEFORE `MqttClient::publish()` is called.

In `firmware/sensor-tag-wifi/src/main.cpp`, find the existing `uploadAndCheckOta()` function. Locate this block:

```cpp
    if (RingBuffer::size() > 0) {
        if (MqttClient::connect(deviceId)) {
            uint8_t sent = 0;
            while (RingBuffer::size() > 0) {
                Reading r;
                if (!RingBuffer::peekOldest(r)) break;
                if (!MqttClient::publish(deviceId, r)) break;
                RingBuffer::popOldest();
                sent++;
                Ota::onPublishSuccess();
            }
            Serial.printf("[MAIN] sent %u / remaining %u\n", sent, RingBuffer::size());
            MqttClient::disconnect();
        } else {
            Serial.println("[MAIN] no mqtt — keeping buffer");
        }
    }
```

Replace with (capture RSSI right after MQTT connect succeeds, then forward to each publish):

```cpp
    if (RingBuffer::size() > 0) {
        if (MqttClient::connect(deviceId)) {
            int8_t sessionRssi = static_cast<int8_t>(WiFi.RSSI());
            Serial.printf("[MAIN] mqtt connected rssi=%d dBm\n", sessionRssi);
            uint8_t sent = 0;
            while (RingBuffer::size() > 0) {
                Reading r;
                if (!RingBuffer::peekOldest(r)) break;
                if (!MqttClient::publish(deviceId, r, sessionRssi)) break;
                RingBuffer::popOldest();
                sent++;
                Ota::onPublishSuccess();
            }
            Serial.printf("[MAIN] sent %u / remaining %u\n", sent, RingBuffer::size());
            MqttClient::disconnect();
        } else {
            Serial.println("[MAIN] no mqtt — keeping buffer");
        }
    }
```

If `WiFi.RSSI()` fails to compile, add `#include <WiFi.h>` near the top of `main.cpp` (likely pulled in transitively via `wifi_manager.h`, but add explicitly if needed).

This is the only `MqttClient::publish` call site in the codebase. After this step, the placeholder rssi=0 from Task 3 Step 8 is gone (the value now flows: `uploadAndCheckOta` → `MqttClient::publish` → `Payload::serialize`).

- [ ] **Step 4: Compile firmware to confirm Arduino target builds**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20
```

Expected: `SUCCESS`.

- [ ] **Step 5: Compile the SHT31 variant too (it shares the same files)**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31
```

Expected: `SUCCESS`.

- [ ] **Step 6: Compile the S3-Zero variant**

```
cd firmware/sensor-tag-wifi && pio run -e waveshare-s3zero-ds18b20
```

Expected: `SUCCESS`.

- [ ] **Step 7: Run full native test suite (regression)**

```
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: 37 tests pass.

- [ ] **Step 8: Commit**

```bash
git add firmware/sensor-tag-wifi/src/main.cpp \
        firmware/sensor-tag-wifi/src/mqtt_client.cpp \
        firmware/sensor-tag-wifi/src/mqtt_client.h
git commit -m "feat(sensor-tag-wifi): wire vbat_mV at sample time, capture rssi at publish

Sample loop populates Reading.vbat_mV from Battery::readMillivolts and
derives battery_pct from the same value (one ADC sweep per sample).
Drain loop captures WiFi.RSSI() once post-connect and forwards it to
every publish() call in the session."
```

---

## Task 5: Update Telegraf parser config (canonical mirror)

**Files:**
- Modify: `deploy/tsdb/telegraf-combsense.conf`

- [ ] **Step 1: Add three new field blocks to the json_v2 parser**

Open `deploy/tsdb/telegraf-combsense.conf`. Find the existing block (lines ~31-35):

```toml
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "t"
      rename = "sensor_ts"
      type = "int"
      optional = true
```

Immediately after that closing block, insert three new blocks:

```toml
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "v"
      rename = "fw_version"
      type = "string"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "vbat_mV"
      type = "int"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "rssi"
      type = "int"
      optional = true
```

All three are `optional = true` so older sensor-tag-wifi builds (pre-upgrade) continue to parse without errors during the rollout window.

- [ ] **Step 2: Commit the canonical-mirror update**

```bash
git add deploy/tsdb/telegraf-combsense.conf
git commit -m "feat(tsdb): parse v/vbat_mV/rssi from sensor-tag-wifi reading payload

Adds three optional json_v2 fields. fw_version is captured as a string
field (not a tag) to avoid Influx series-cardinality blow-up on every
OTA. All three are optional to allow rollout overlap with un-upgraded
tags."
```

---

## Task 6: End-to-end verification (bench + LXC deploy)

This task is operational, not code. Each step validates the changes in the live pipeline.

- [ ] **Step 1: Flash the SHT31 yard tag c5fffe12 with the new build**

```
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31 --target upload
```

Expected: upload succeeds. (Bench-tag the yard unit if it's at home; otherwise pick a bench tag.)

- [ ] **Step 2: Monitor serial output, confirm new payload shape and one publish cycle**

```
cd firmware/sensor-tag-wifi && pio device monitor
```

Wait for one wake → publish cycle. Expected log lines:
- `[MAIN] combsense sensor-tag-wifi id=<8-hex> version=<sha-or-tag>`
- `[MAIN] sample t1=... t2=... h1=... h2=... vbat=<n>mV b=<n> buffered=...`
- `[MQTT] connected rssi=<-100..-30> dBm`
- A `PUBLISH` log line containing the new JSON shape with `"v"`, `"vbat_mV"`, `"rssi"` fields.

If `vbat_mV` reads as `0`, you're on USB without a cell attached — that's expected per memory `project_sensor_tag_wifi_battery_gate.md`. Attach a cell to exercise the full path.

- [ ] **Step 3: Deploy the Telegraf config to combsense-tsdb LXC**

```bash
scp deploy/tsdb/telegraf-combsense.conf root@192.168.1.19:/etc/telegraf/telegraf.conf
ssh root@192.168.1.19 'systemctl restart telegraf && journalctl -u telegraf -n 50 --no-pager'
```

Expected: telegraf restarts cleanly, no parse errors in the last 50 log lines.

- [ ] **Step 4: Wait for the next bench-tag publish, verify Telegraf parses it**

```bash
ssh root@192.168.1.19 'journalctl -u telegraf -f --no-pager'
```

Wait for one publish cycle (≤5 min default cadence). Expected: no `json_v2: failed to parse` or similar errors.

- [ ] **Step 5: Confirm Influx received the new fields**

```bash
ssh root@192.168.1.19 'source /root/.combsense-tsdb-creds && \
  influx query "from(bucket:\"combsense\") |> range(start:-15m) |> filter(fn:(r) => r._measurement == \"sensor_reading\") |> filter(fn:(r) => r._field == \"fw_version\" or r._field == \"vbat_mV\" or r._field == \"rssi\") |> last()" \
  --org combsense --token \$admin_token'
```

Expected: three rows returned, one per new field, with the bench tag's `sensor_id`. `fw_version` value matches the build sha you flashed.

- [ ] **Step 6: Update ROUTER**

In `.mex/ROUTER.md`, find the sensor-tag-wifi section bullet about native test counts. Change:

```
  - Native Unity tests: 30 passing across payload (6), OTA manifest parser (9), OTA decision (6), OTA validate-on-boot (4), sha256 streamer (5)
```

to:

```
  - Native Unity tests: 37 passing across payload (7), battery math (6), OTA manifest parser (9), OTA decision (6), OTA validate-on-boot (4), sha256 streamer (5)
```

Add one bullet under the sensor-tag-wifi section:

```
  - **Fleet visibility:** reading payload self-identifies build (`v`), and carries raw battery mV (`vbat_mV`) plus post-connect WiFi RSSI (`rssi`) for diagnostics. Telegraf parses all three as Influx fields (`fw_version`, `vbat_mV`, `rssi`).
```

Update the `last_updated` line at the top.

- [ ] **Step 7: Commit ROUTER update**

```bash
git add .mex/ROUTER.md
git commit -m "docs(mex): record fleet-visibility payload fields and updated test count"
```

- [ ] **Step 8: Open PR from `dev` to `main`**

```bash
git push origin dev
gh pr create --base main --head dev --title "sensor-tag-wifi: fleet visibility (v + vbat_mV + rssi)" \
  --body "$(cat <<'EOF'
## Summary
- Adds firmware version (`v`), raw battery voltage (`vbat_mV`), and post-connect WiFi RSSI (`rssi`) to every reading payload.
- Refactors `Battery::readPercent()` into pure `percentFromMillivolts()` (native-testable) + Arduino-dependent `readMillivolts()`.
- Telegraf parser captures all three as Influx fields (string for fw_version → no series-cardinality churn on OTA).
- Heartbeat work deferred to issue #11 with full A/B/C analysis.

## Test plan
- [x] `pio test -e native` — 37 tests pass (was 30; +6 battery_math, +1 payload-unknown-version)
- [x] `pio run -e xiao-c6-sht31` — compiles
- [x] `pio run -e xiao-c6-ds18b20` — compiles
- [x] `pio run -e waveshare-s3zero-ds18b20` — compiles
- [x] Bench tag publishes payload with new fields, Influx records `fw_version`, `vbat_mV`, `rssi`
- [x] Telegraf restart on combsense-tsdb LXC clean, no parse errors

## Spec / plan
- Spec: `docs/superpowers/specs/2026-04-25-sensor-tag-wifi-fleet-visibility-design.md`
- Plan: `docs/superpowers/plans/2026-04-25-sensor-tag-wifi-fleet-visibility.md`
EOF
)"
```

---

## Self-review notes

- **Spec coverage:** every item in the spec has a corresponding task. Battery refactor → Tasks 1-2. Reading struct + serialize signature + tests → Task 3. main.cpp/mqtt_client.cpp wiring → Task 4. Telegraf → Task 5. Verification (native tests, three compile envs, bench, deploy, Influx confirm, ROUTER update) → Task 6.
- **Type consistency:** `serialize` signature is the same in `payload.h`, `payload.cpp`, all 7 test fixtures, and the `mqtt_client.cpp` call site. `vbat_mV` declared as `uint16_t` in `Reading`, used as `%u` (`unsigned`) in `payload.cpp` via cast. `rssi` declared `int8_t` in the signature, cast to `int` for `%d` in `payload.cpp`. `Battery::readMillivolts` returns `uint16_t`, consumed unchanged into `Reading::vbat_mV`.
- **Field positions in `Reading`:** `vbat_mV` placed between `humidity2` and `battery_pct`. C++20 designated initializers must be in declaration order, so all 7 test fixtures keep `.vbat_mV` between `.humidity2` and `.battery_pct`. RTC layout grows by 2 bytes (acceptable: `RTC_DATA_ATTR` budget is many KB).
- **No placeholders:** all code shown verbatim. No "similar to Task N", no TBD. The only deliberately-temporary value (placeholder rssi=0 in Task 3 Step 8) is replaced in Task 4 Step 3 — flagged inline as a placeholder with a comment.
