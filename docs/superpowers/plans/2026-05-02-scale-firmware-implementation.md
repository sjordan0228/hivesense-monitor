# Scale Calibration Firmware — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add HX711-based scale support and bidirectional MQTT calibration command/event handling to `firmware/sensor-tag-wifi/`, matching the iOS contract at `.mex/scale-mqtt-contract.md`.

**Architecture:** Self-contained `Scale` module under `src/scale.{h,cpp}`. Pure-C++ math + state-machine helpers split into `src/scale_math.{h,cpp}` for unit testability under the `native` env. Driver layer wraps `bogde/HX711@^0.7.5`. New build env `xiao-c6-ds18b20-scale` adds the feature behind `-DSENSOR_SCALE` so the existing `xiao-c6-ds18b20` build is untouched.

**Tech Stack:** C++17, Arduino-ESP32, PubSubClient (MQTT QoS 0), ArduinoJson 7, bogde/HX711, Preferences (NVS), Unity (native tests).

**Spec:** [docs/superpowers/specs/2026-05-02-scale-firmware-design.md](2026-05-02-scale-firmware-design.md)
**Contract:** [.mex/scale-mqtt-contract.md](../../../.mex/scale-mqtt-contract.md)

---

## File structure

### Files to create

| Path | Responsibility |
|---|---|
| `firmware/sensor-tag-wifi/src/scale.h` | Public API: `init`, `deinit`, `sample`, `subscribe`, `onMessage`, `tick`, `inExtendedAwakeMode`, `keepAliveUntil` |
| `firmware/sensor-tag-wifi/src/scale.cpp` | HX711 driver glue, MQTT subscribe + onMessage dispatch, command handlers, state machine, NVS persistence |
| `firmware/sensor-tag-wifi/src/scale_math.h` | Pure-C++ helpers: `applyCalibration`, `tareFromMean`, `scaleFactorFromMean`, `errorPct`, `isKeepAliveValid`, `formatRFC3339`, `StableDetector` |
| `firmware/sensor-tag-wifi/src/scale_math.cpp` | Implementations of the above |
| `firmware/sensor-tag-wifi/src/scale_commands.h` | `ScaleCommand` enum + parser declarations |
| `firmware/sensor-tag-wifi/src/scale_commands.cpp` | JSON → `ScaleCommand` parser; `ScaleStatusEvent` JSON serializers |
| `firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp` | Unit tests for `scale_math.cpp` |
| `firmware/sensor-tag-wifi/test/test_scale_commands/test_main.cpp` | Round-trip tests for command parsing + status event serializing |
| `firmware/sensor-tag-wifi/test/test_scale_payload/test_main.cpp` | Reading payload includes `w` correctly; null/missing/number cases |

### Files to modify

| Path | What changes |
|---|---|
| `firmware/sensor-tag-wifi/include/config.h` | Add `PIN_HX711_DT`, `PIN_HX711_SCK`, HX711 timing/threshold constants, `CLOCK_SKEW_TOLERANCE_SEC`, `KEEPALIVE_NTP_FALLBACK_SEC`, `HEARTBEAT_INTERVAL_MS`, etc. |
| `firmware/sensor-tag-wifi/include/reading.h` | Extend `Reading` POD: add `weight_kg` (float, NAN sentinel for missing). Update `static_assert(sizeof(Reading) == ...)`. |
| `firmware/sensor-tag-wifi/src/ring_buffer.cpp` | Bump `MAGIC` from `0xCB50A002` → `0xCB50A003` (invalidates stale RTC slots after `Reading` size change) |
| `firmware/sensor-tag-wifi/src/payload.cpp` | Serialize `w` field when finite; emit dedicated `weight` topic helper |
| `firmware/sensor-tag-wifi/src/main.cpp` | `Scale::init()` at startup, `Scale::sample()` integrated with reading; subscribe + onMessage route; extended-awake loop calling `Scale::tick()`; `Scale::deinit()` before deep-sleep; dual-publish reading + dedicated weight topic |
| `firmware/sensor-tag-wifi/platformio.ini` | New env `xiao-c6-ds18b20-scale` extending `xiao-c6-ds18b20` |
| `firmware/sensor-tag-wifi/test/test_payload/test_main.cpp` | Extend existing payload tests for the new `w` field |

---

## Task ordering rationale

TDD-friendly bottom-up: pure-C++ helpers first (cheapest tests, no hardware). Then JSON parsing/serializing (still pure C++). Then the driver layer. Then state machine. Then main integration. Bench validation last.

Each task lands a green commit. Commits are stand-alone — if a later task fails, earlier ones stay shipped.

---

### Task 1: Build env + config constants

**Files:**
- Modify: `firmware/sensor-tag-wifi/platformio.ini`
- Modify: `firmware/sensor-tag-wifi/include/config.h`

- [ ] **Step 1: Add scale-specific constants to config.h**

After the existing `DS18B20_*` constants (around line 78), append:

```cpp
// --- Scale (HX711) -----------------------------------------------------------
#ifndef PIN_HX711_DT
#define PIN_HX711_DT 16
#endif
#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK 17
#endif
constexpr uint8_t  PIN_HX711_DT_                = PIN_HX711_DT;
constexpr uint8_t  PIN_HX711_SCK_               = PIN_HX711_SCK;

constexpr uint8_t  HX711_GAIN                   = 128;
constexpr uint8_t  HX711_TARE_SAMPLE_COUNT      = 16;
constexpr uint8_t  HX711_VERIFY_SAMPLE_COUNT    = 16;
constexpr uint8_t  HX711_STABLE_WINDOW_LEN      = 5;
constexpr int32_t  HX711_STABLE_TOLERANCE_RAW   = 50;
constexpr uint16_t HX711_STREAM_INTERVAL_MS     = 1000;
constexpr uint16_t HX711_READ_TIMEOUT_MS        = 1000;
constexpr double   HX711_CALIBRATE_MIN_FACTOR   = 1.0;   // |scale_factor| sanity floor
constexpr double   HX711_DEFAULT_SCALE_FACTOR   = 1.0;   // NVS default — produces obviously-bad kg
constexpr float    MODIFY_DELTA_THRESHOLD_KG    = 0.2f;  // < this absolute → modify_warning

// --- Scale extended-awake / keep-alive --------------------------------------
constexpr uint16_t HEARTBEAT_INTERVAL_MS        = 60000;
constexpr int64_t  CLOCK_SKEW_TOLERANCE_SEC     = 300;   // ±5 min iOS clock skew
constexpr int64_t  KEEPALIVE_NTP_FALLBACK_SEC   = 600;   // grace if NTP not synced
constexpr uint16_t RETAINED_CONFIG_WAIT_MS      = 1500;  // post-subscribe wait
constexpr uint16_t MODIFY_DEFAULT_TIMEOUT_SEC   = 600;
```

- [ ] **Step 2: Add new build env to platformio.ini**

Append after the `[env:waveshare-s3zero-ds18b20]` block (after line 86):

```ini
[env:xiao-c6-ds18b20-scale]
extends = env:xiao-c6-ds18b20
lib_deps =
    ${env:xiao-c6-ds18b20.lib_deps}
    bogde/HX711@^0.7.5
build_flags =
    ${env:xiao-c6-ds18b20.build_flags}
    -DSENSOR_SCALE
    -DPIN_HX711_DT=16
    -DPIN_HX711_SCK=17
    -DOTA_VARIANT=\"ds18b20-scale\"
```

- [ ] **Step 3: Verify env builds (no scale code yet, just env scaffold)**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS (the build will be identical to xiao-c6-ds18b20 since SENSOR_SCALE has no consumers yet)

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/include/config.h firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(scale): add xiao-c6-ds18b20-scale build env + config constants"
```

---

### Task 2: StableDetector class with TDD

**Files:**
- Create: `firmware/sensor-tag-wifi/src/scale_math.h`
- Create: `firmware/sensor-tag-wifi/src/scale_math.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp`
- Modify: `firmware/sensor-tag-wifi/platformio.ini` (extend `[env:native]` build_src_filter)

- [ ] **Step 1: Write the failing test**

Create `firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp`:

```cpp
#include <unity.h>
#include "scale_math.h"

void setUp(void) {}
void tearDown(void) {}

void test_stable_detector_empty_is_not_stable() {
    StableDetector d;
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_partial_fill_is_not_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1010);
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_quiet_window_is_stable() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_within_tolerance_is_stable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1015);  // range = 1020 - 990 = 30, < HX711_STABLE_TOLERANCE_RAW (50)
    TEST_ASSERT_TRUE(d.isStable());
}

void test_stable_detector_outside_tolerance_is_unstable() {
    StableDetector d;
    d.push(1000);
    d.push(1020);
    d.push(1010);
    d.push(990);
    d.push(1100);  // range = 1100 - 990 = 110, > 50
    TEST_ASSERT_FALSE(d.isStable());
}

void test_stable_detector_recovers_after_disturbance() {
    StableDetector d;
    for (int i = 0; i < 5; i++) d.push(1000);
    d.push(1500);  // one disturbance
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);
    d.push(1000);
    d.push(1000);
    d.push(1000);  // ring buffer is now [1500, 1000, 1000, 1000, 1000] — range 500
    TEST_ASSERT_FALSE(d.isStable());
    d.push(1000);  // ring is now [1000, 1000, 1000, 1000, 1000] — stable
    TEST_ASSERT_TRUE(d.isStable());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stable_detector_empty_is_not_stable);
    RUN_TEST(test_stable_detector_partial_fill_is_not_stable);
    RUN_TEST(test_stable_detector_quiet_window_is_stable);
    RUN_TEST(test_stable_detector_within_tolerance_is_stable);
    RUN_TEST(test_stable_detector_outside_tolerance_is_unstable);
    RUN_TEST(test_stable_detector_recovers_after_disturbance);
    return UNITY_END();
}
```

- [ ] **Step 2: Create scale_math.h skeleton (will fail compile)**

Create `firmware/sensor-tag-wifi/src/scale_math.h`:

```cpp
#pragma once

#include <cstdint>
#include "config.h"

class StableDetector {
public:
    StableDetector() = default;
    void push(int32_t raw);
    bool isStable() const;
    void reset();

private:
    int32_t ring_[HX711_STABLE_WINDOW_LEN] = {};
    uint8_t count_ = 0;
    uint8_t head_  = 0;
};
```

- [ ] **Step 3: Update platformio.ini to compile scale_math in native env**

In `[env:native]` `build_src_filter`, add `+<scale_math.cpp>`:

```ini
build_src_filter =
    +<payload.cpp>
    +<ota_manifest.cpp>
    +<ota_decision.cpp>
    +<ota_sha256.cpp>
    +<config_parser.cpp>
    +<scale_math.cpp>
```

- [ ] **Step 4: Run test to confirm it fails (link error: undefined StableDetector)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_math 2>&1 | tail -20`
Expected: link error or test fail

- [ ] **Step 5: Implement StableDetector**

Create `firmware/sensor-tag-wifi/src/scale_math.cpp`:

```cpp
#include "scale_math.h"
#include <algorithm>

void StableDetector::push(int32_t raw) {
    ring_[head_] = raw;
    head_ = (head_ + 1) % HX711_STABLE_WINDOW_LEN;
    if (count_ < HX711_STABLE_WINDOW_LEN) count_++;
}

bool StableDetector::isStable() const {
    if (count_ < HX711_STABLE_WINDOW_LEN) return false;
    int32_t mn = ring_[0], mx = ring_[0];
    for (uint8_t i = 1; i < HX711_STABLE_WINDOW_LEN; i++) {
        mn = std::min(mn, ring_[i]);
        mx = std::max(mx, ring_[i]);
    }
    return (mx - mn) <= HX711_STABLE_TOLERANCE_RAW;
}

void StableDetector::reset() {
    count_ = 0;
    head_  = 0;
}
```

- [ ] **Step 6: Run test to confirm pass**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_math 2>&1 | tail -10`
Expected: 6 tests passing.

- [ ] **Step 7: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale_math.{h,cpp} \
        firmware/sensor-tag-wifi/test/test_scale_math \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(scale): StableDetector with native unit tests"
```

---

### Task 3: Calibration math (tare, scale_factor, error_pct)

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale_math.h`
- Modify: `firmware/sensor-tag-wifi/src/scale_math.cpp`
- Modify: `firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp`

- [ ] **Step 1: Add failing tests for the math helpers**

Append to `test_main.cpp`:

```cpp
void test_apply_calibration_basic() {
    // raw=12345, off=345, scale=200 → kg = (12345-345)/200 = 60.0
    double kg = applyCalibration(12345, 345, 200.0);
    TEST_ASSERT_EQUAL_DOUBLE(60.0, kg);
}

void test_apply_calibration_negative_load() {
    // raw=100, off=345, scale=200 → kg = (100-345)/200 = -1.225
    double kg = applyCalibration(100, 345, 200.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, -1.225, kg);
}

void test_tare_from_mean() {
    // mean of [1000, 1010, 990, 1005, 995] = 1000
    int32_t samples[] = {1000, 1010, 990, 1005, 995};
    int64_t off = tareFromMean(samples, 5);
    TEST_ASSERT_EQUAL_INT64(1000, off);
}

void test_scale_factor_from_mean() {
    // raw=10345, off=345, known_kg=10 → scale = (10345-345)/10 = 1000.0
    int32_t samples[] = {10345};
    double sf = scaleFactorFromMean(samples, 1, 345, 10.0);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, sf);
}

void test_error_pct_basic() {
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.6, errorPct(4.97, 5.0));
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.6, errorPct(5.03, 5.0));
}

void test_error_pct_zero_expected_returns_neg_one() {
    // Sentinel for "expected was zero — undefined error_pct"
    TEST_ASSERT_EQUAL_DOUBLE(-1.0, errorPct(1.0, 0.0));
}
```

Add to `main()`:

```cpp
RUN_TEST(test_apply_calibration_basic);
RUN_TEST(test_apply_calibration_negative_load);
RUN_TEST(test_tare_from_mean);
RUN_TEST(test_scale_factor_from_mean);
RUN_TEST(test_error_pct_basic);
RUN_TEST(test_error_pct_zero_expected_returns_neg_one);
```

- [ ] **Step 2: Add declarations to scale_math.h**

After `class StableDetector`, add:

```cpp
/// kg = (raw - off) / scale_factor
double applyCalibration(int32_t raw, int64_t off, double scale_factor);

/// Compute tare offset as integer mean of N raw samples.
int64_t tareFromMean(const int32_t* samples, uint8_t n);

/// Compute scale_factor = (mean(samples) - off) / known_kg.
/// Returns 0.0 if known_kg is zero (caller must handle).
double scaleFactorFromMean(const int32_t* samples, uint8_t n, int64_t off, double known_kg);

/// |measured - expected| / |expected| * 100. Returns -1.0 if expected == 0.
double errorPct(double measured, double expected);
```

- [ ] **Step 3: Run test (expect fail — undefined references)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_math 2>&1 | tail -10`

- [ ] **Step 4: Implement in scale_math.cpp**

Append to `scale_math.cpp`:

```cpp
#include <cmath>

double applyCalibration(int32_t raw, int64_t off, double scale_factor) {
    if (scale_factor == 0.0) return std::nan("");
    return static_cast<double>(static_cast<int64_t>(raw) - off) / scale_factor;
}

int64_t tareFromMean(const int32_t* samples, uint8_t n) {
    if (n == 0) return 0;
    int64_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += samples[i];
    return sum / n;
}

double scaleFactorFromMean(const int32_t* samples, uint8_t n, int64_t off, double known_kg) {
    if (n == 0 || known_kg == 0.0) return 0.0;
    int64_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += samples[i];
    double mean = static_cast<double>(sum) / n;
    return (mean - static_cast<double>(off)) / known_kg;
}

double errorPct(double measured, double expected) {
    if (expected == 0.0) return -1.0;
    return std::fabs(measured - expected) / std::fabs(expected) * 100.0;
}
```

- [ ] **Step 5: Run test (expect pass)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_math 2>&1 | tail -10`
Expected: 12 tests passing (6 prior + 6 new).

- [ ] **Step 6: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale_math.{h,cpp} \
        firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp
git commit -m "feat(scale): tare/calibrate/verify math helpers + tests"
```

---

### Task 4: Keep-alive validity check + RFC3339 formatter

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale_math.h`
- Modify: `firmware/sensor-tag-wifi/src/scale_math.cpp`
- Modify: `firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test_main.cpp`:

```cpp
void test_keep_alive_future_is_valid() {
    int64_t now = 1777750000;
    TEST_ASSERT_TRUE(isKeepAliveValid(now + 600, now));
}

void test_keep_alive_small_skew_past_is_valid() {
    int64_t now = 1777750000;
    TEST_ASSERT_TRUE(isKeepAliveValid(now - 60, now));         // 1 min skew
    TEST_ASSERT_TRUE(isKeepAliveValid(now - 299, now));        // just under 5 min
}

void test_keep_alive_large_past_is_invalid() {
    int64_t now = 1777750000;
    TEST_ASSERT_FALSE(isKeepAliveValid(now - 301, now));       // just over 5 min
    TEST_ASSERT_FALSE(isKeepAliveValid(now - 3600, now));      // 1 hour past
}

void test_keep_alive_zero_is_invalid() {
    int64_t now = 1777750000;
    TEST_ASSERT_FALSE(isKeepAliveValid(0, now));
}

void test_format_rfc3339_basic() {
    // 2026-05-02T18:00:00Z = epoch 1777759200
    char buf[32];
    size_t n = formatRFC3339(1777759200, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-05-02T18:00:00Z", buf);
    TEST_ASSERT_EQUAL(20, n);
}

void test_format_rfc3339_epoch_zero() {
    char buf[32];
    formatRFC3339(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", buf);
}
```

Add to `main()`:

```cpp
RUN_TEST(test_keep_alive_future_is_valid);
RUN_TEST(test_keep_alive_small_skew_past_is_valid);
RUN_TEST(test_keep_alive_large_past_is_invalid);
RUN_TEST(test_keep_alive_zero_is_invalid);
RUN_TEST(test_format_rfc3339_basic);
RUN_TEST(test_format_rfc3339_epoch_zero);
```

- [ ] **Step 2: Add declarations to scale_math.h**

```cpp
/// Returns true if keep_alive_until is in the future, OR within
/// CLOCK_SKEW_TOLERANCE_SEC of `now` (handles iOS clock drift).
bool isKeepAliveValid(int64_t keep_alive_until, int64_t now);

/// Format `epoch` (seconds) as RFC3339 UTC: "YYYY-MM-DDTHH:MM:SSZ".
/// Returns number of bytes written (excl null terminator), 0 on overflow.
size_t formatRFC3339(int64_t epoch, char* buf, size_t bufsz);
```

- [ ] **Step 3: Implement in scale_math.cpp**

```cpp
#include <ctime>

bool isKeepAliveValid(int64_t keep_alive_until, int64_t now) {
    if (keep_alive_until <= 0) return false;
    if (keep_alive_until > now) return true;
    return keep_alive_until > (now - CLOCK_SKEW_TOLERANCE_SEC);
}

size_t formatRFC3339(int64_t epoch, char* buf, size_t bufsz) {
    if (bufsz < 21) return 0;
    time_t t = static_cast<time_t>(epoch);
    struct tm utc;
    gmtime_r(&t, &utc);
    return strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
```

- [ ] **Step 4: Run tests**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_math 2>&1 | tail -10`
Expected: 18 tests passing.

- [ ] **Step 5: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale_math.{h,cpp} \
        firmware/sensor-tag-wifi/test/test_scale_math/test_main.cpp
git commit -m "feat(scale): isKeepAliveValid + formatRFC3339 helpers"
```

---

### Task 5: Reading struct extension (w field)

**Files:**
- Modify: `firmware/sensor-tag-wifi/include/reading.h`
- Modify: `firmware/sensor-tag-wifi/src/ring_buffer.cpp`
- Modify: `firmware/sensor-tag-wifi/test/test_payload/test_main.cpp` (or create `test_scale_payload`)

- [ ] **Step 1: Read existing payload test for context**

Run: `cat firmware/sensor-tag-wifi/test/test_payload/test_main.cpp | head -40`
Note the test pattern.

- [ ] **Step 2: Write failing tests for the new w field**

Create `firmware/sensor-tag-wifi/test/test_scale_payload/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include <cmath>
#include "payload.h"
#include "reading.h"

void setUp(void) {}
void tearDown(void) {}

void test_payload_includes_w_when_finite() {
    Reading r{};
    r.timestamp = 1777759713;
    r.temp1     = 23.38f;
    r.temp2     = NAN;
    r.humidity1 = NAN;
    r.humidity2 = NAN;
    r.vbat_mV   = 3505;
    r.battery_pct = 22;
    r.weight_kg = 47.32f;

    char buf[256];
    int n = Payload::serialize("c513131c", "abc1234", -87, r, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"w\":47.32"));
}

void test_payload_omits_w_when_nan() {
    Reading r{};
    r.timestamp = 1777759713;
    r.temp1     = 23.38f;
    r.temp2     = NAN;
    r.humidity1 = NAN;
    r.humidity2 = NAN;
    r.vbat_mV   = 3505;
    r.battery_pct = 22;
    r.weight_kg = NAN;

    char buf[256];
    Payload::serialize("c513131c", "abc1234", -87, r, buf, sizeof(buf));
    TEST_ASSERT_NULL(strstr(buf, "\"w\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_payload_includes_w_when_finite);
    RUN_TEST(test_payload_omits_w_when_nan);
    return UNITY_END();
}
```

- [ ] **Step 3: Update Reading struct**

Modify `firmware/sensor-tag-wifi/include/reading.h`:

```cpp
#pragma once

#include <cstdint>

struct Reading {
    uint32_t timestamp;
    float    temp1;
    float    temp2;
    float    humidity1;
    float    humidity2;
    uint16_t vbat_mV;
    uint8_t  battery_pct;
    uint8_t  _pad;        // alignment
    float    weight_kg;   // NAN if scale not present or sample failed
};

static_assert(sizeof(Reading) == 28,
              "Reading layout changed — bump RingBuffer MAGIC to invalidate "
              "stale RTC slots after OTA. See ring_buffer.cpp.");
```

- [ ] **Step 4: Bump RingBuffer MAGIC**

In `firmware/sensor-tag-wifi/src/ring_buffer.cpp`, find the MAGIC constant (currently `0xCB50A002`) and bump to `0xCB50A003`. Update any matching documentation comments.

- [ ] **Step 5: Run new test (expect fail — payload doesn't emit w yet)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_payload 2>&1 | tail -10`
Expected: `test_payload_includes_w_when_finite` fails.

- [ ] **Step 6: Update payload.cpp to emit w**

In `firmware/sensor-tag-wifi/src/payload.cpp`, in the `serialize` function near where other fields are written, after the battery_pct line, add:

```cpp
if (std::isfinite(r.weight_kg)) {
    serializer.writeFloat("w", r.weight_kg, 2);   // 2 decimal places
}
```

(If the existing payload uses ArduinoJson's idioms instead of a custom serializer, mirror those — emit `doc["w"] = r.weight_kg;` only when `std::isfinite(r.weight_kg)`.)

- [ ] **Step 7: Run tests (expect pass)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_payload 2>&1 | tail -10`
Expected: 2 tests pass.

- [ ] **Step 8: Run existing payload tests to confirm no regression**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_payload 2>&1 | tail -10`
Expected: all existing payload tests still pass (Reading default-zero leaves weight_kg at 0.0f, which is finite — those tests may need updating to set weight_kg=NAN explicitly if they don't already use designated init).

- [ ] **Step 9: Commit**

```bash
git add firmware/sensor-tag-wifi/include/reading.h \
        firmware/sensor-tag-wifi/src/ring_buffer.cpp \
        firmware/sensor-tag-wifi/src/payload.cpp \
        firmware/sensor-tag-wifi/test/test_scale_payload \
        firmware/sensor-tag-wifi/test/test_payload/test_main.cpp
git commit -m "feat(scale): Reading.weight_kg + payload w field, RingBuffer MAGIC bumped"
```

---

### Task 6: ScaleCommand parser

**Files:**
- Create: `firmware/sensor-tag-wifi/src/scale_commands.h`
- Create: `firmware/sensor-tag-wifi/src/scale_commands.cpp`
- Create: `firmware/sensor-tag-wifi/test/test_scale_commands/test_main.cpp`
- Modify: `firmware/sensor-tag-wifi/platformio.ini` (extend native env build_src_filter)

- [ ] **Step 1: Write the failing tests**

Create `firmware/sensor-tag-wifi/test/test_scale_commands/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "scale_commands.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_tare() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"tare"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Tare, cmd.type);
}

void test_parse_calibrate() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"calibrate","known_kg":10})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Calibrate, cmd.type);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, cmd.calibrate.known_kg);
}

void test_parse_verify() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"verify","expected_kg":5.0})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::Verify, cmd.type);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, cmd.verify.expected_kg);
}

void test_parse_stream_raw() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"stream_raw","duration_sec":90})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::StreamRaw, cmd.type);
    TEST_ASSERT_EQUAL(90, cmd.stream_raw.duration_sec);
}

void test_parse_stop_stream() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"stop_stream"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::StopStream, cmd.type);
}

void test_parse_modify_start() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_start","label":"added_super_deep"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyStart, cmd.type);
    TEST_ASSERT_EQUAL_STRING("added_super_deep", cmd.modify.label);
}

void test_parse_modify_end() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_end","label":"extracted_honey"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyEnd, cmd.type);
    TEST_ASSERT_EQUAL_STRING("extracted_honey", cmd.modify.label);
}

void test_parse_modify_cancel() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"modify_cancel"})", cmd);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(ScaleCommandType::ModifyCancel, cmd.type);
}

void test_parse_unknown_cmd_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"frobnicate"})", cmd);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_missing_required_field_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand(R"({"cmd":"calibrate"})", cmd);  // missing known_kg
    TEST_ASSERT_FALSE(ok);
}

void test_parse_invalid_json_fails() {
    ScaleCommand cmd;
    bool ok = parseScaleCommand("not json", cmd);
    TEST_ASSERT_FALSE(ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_tare);
    RUN_TEST(test_parse_calibrate);
    RUN_TEST(test_parse_verify);
    RUN_TEST(test_parse_stream_raw);
    RUN_TEST(test_parse_stop_stream);
    RUN_TEST(test_parse_modify_start);
    RUN_TEST(test_parse_modify_end);
    RUN_TEST(test_parse_modify_cancel);
    RUN_TEST(test_parse_unknown_cmd_fails);
    RUN_TEST(test_parse_missing_required_field_fails);
    RUN_TEST(test_parse_invalid_json_fails);
    return UNITY_END();
}
```

- [ ] **Step 2: Create scale_commands.h**

```cpp
#pragma once

#include <cstdint>

enum class ScaleCommandType : uint8_t {
    Tare,
    Calibrate,
    Verify,
    StreamRaw,
    StopStream,
    ModifyStart,
    ModifyEnd,
    ModifyCancel,
};

struct ScaleCommand {
    ScaleCommandType type;
    union {
        struct { double known_kg; }        calibrate;
        struct { double expected_kg; }     verify;
        struct { int32_t duration_sec; }   stream_raw;
        struct { char label[32]; }         modify;
    };
};

bool parseScaleCommand(const char* json, ScaleCommand& out);
```

- [ ] **Step 3: Add scale_commands.cpp to native env build_src_filter**

In `[env:native]` `build_src_filter`, append `+<scale_commands.cpp>`.

- [ ] **Step 4: Run tests (expect link fail)**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_commands 2>&1 | tail -10`

- [ ] **Step 5: Implement parser**

Create `firmware/sensor-tag-wifi/src/scale_commands.cpp`:

```cpp
#include "scale_commands.h"
#include <ArduinoJson.h>
#include <cstring>

bool parseScaleCommand(const char* json, ScaleCommand& out) {
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;

    const char* cmd = doc["cmd"].as<const char*>();
    if (!cmd) return false;

    if (strcmp(cmd, "tare") == 0) {
        out.type = ScaleCommandType::Tare;
        return true;
    }
    if (strcmp(cmd, "calibrate") == 0) {
        if (!doc["known_kg"].is<double>()) return false;
        out.type = ScaleCommandType::Calibrate;
        out.calibrate.known_kg = doc["known_kg"].as<double>();
        return true;
    }
    if (strcmp(cmd, "verify") == 0) {
        if (!doc["expected_kg"].is<double>()) return false;
        out.type = ScaleCommandType::Verify;
        out.verify.expected_kg = doc["expected_kg"].as<double>();
        return true;
    }
    if (strcmp(cmd, "stream_raw") == 0) {
        if (!doc["duration_sec"].is<int32_t>()) return false;
        out.type = ScaleCommandType::StreamRaw;
        out.stream_raw.duration_sec = doc["duration_sec"].as<int32_t>();
        return true;
    }
    if (strcmp(cmd, "stop_stream") == 0) {
        out.type = ScaleCommandType::StopStream;
        return true;
    }
    if (strcmp(cmd, "modify_start") == 0 || strcmp(cmd, "modify_end") == 0) {
        const char* label = doc["label"].as<const char*>();
        if (!label) return false;
        out.type = (strcmp(cmd, "modify_start") == 0)
            ? ScaleCommandType::ModifyStart
            : ScaleCommandType::ModifyEnd;
        std::strncpy(out.modify.label, label, sizeof(out.modify.label) - 1);
        out.modify.label[sizeof(out.modify.label) - 1] = '\0';
        return true;
    }
    if (strcmp(cmd, "modify_cancel") == 0) {
        out.type = ScaleCommandType::ModifyCancel;
        return true;
    }
    return false;
}
```

- [ ] **Step 6: Run tests**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_commands 2>&1 | tail -10`
Expected: 11 tests pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale_commands.{h,cpp} \
        firmware/sensor-tag-wifi/test/test_scale_commands \
        firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(scale): ScaleCommand JSON parser + 11 unit tests"
```

---

### Task 7: ScaleStatusEvent serializer

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale_commands.h` (add ScaleStatusEvent declarations)
- Modify: `firmware/sensor-tag-wifi/src/scale_commands.cpp`
- Modify: `firmware/sensor-tag-wifi/test/test_scale_commands/test_main.cpp`

- [ ] **Step 1: Write failing tests for each event type**

Append to `test_scale_commands/test_main.cpp`:

```cpp
#include <cstring>

void test_serialize_awake() {
    char buf[256];
    int n = serializeAwakeEvent(1777759800, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"awake\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"keep_alive_until\":\"2026-05-02T18:10:00Z\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-05-02T18:08:34Z\""));
}

void test_serialize_tare_saved() {
    char buf[256];
    serializeTareSavedEvent(1234567, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"tare_saved\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw_offset\":1234567"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-05-02T18:08:34Z\""));
}

void test_serialize_calibration_saved() {
    char buf[256];
    serializeCalibrationSavedEvent(4567.89, 1.8, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"calibration_saved\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"scale_factor\":4567.89"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"predicted_accuracy_pct\":1.8"));
}

void test_serialize_verify_result() {
    char buf[256];
    serializeVerifyResultEvent(4.97, 5.0, 0.6, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"verify_result\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"measured_kg\":4.97"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"expected_kg\":5"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"error_pct\":0.6"));
}

void test_serialize_raw_stream() {
    char buf[256];
    serializeRawStreamEvent(5678901, 47.32, true, 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"raw_stream\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw_value\":5678901"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"kg\":47.32"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stable\":true"));
}

void test_serialize_modify_complete() {
    char buf[256];
    serializeModifyCompleteEvent("added_super_deep", 47.3, 58.1, 10.8, 287, false,
                                 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"modify_complete\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"label\":\"added_super_deep\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"delta_kg\":10.8"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"tare_updated\":false"));
}

void test_serialize_modify_warning() {
    char buf[256];
    serializeModifyWarningEvent("inspection_only", 0.2, "no_significant_change_detected",
                                1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"modify_warning\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"warning\":\"no_significant_change_detected\""));
}

void test_serialize_error() {
    char buf[256];
    serializeErrorEvent("hx711_unresponsive", "no DOUT pulse for 1s", 1777759714, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"code\":\"hx711_unresponsive\""));
}
```

Add `RUN_TEST(...)` lines for all 8 tests.

- [ ] **Step 2: Add declarations to scale_commands.h**

```cpp
// Status event serializers. Each writes a complete JSON object to `buf` and
// returns the number of bytes written (excl null), or 0 on overflow.

int serializeAwakeEvent(int64_t keep_alive_until, int64_t ts,
                        char* buf, size_t bufsz);
int serializeTareSavedEvent(int64_t raw_offset, int64_t ts,
                            char* buf, size_t bufsz);
int serializeCalibrationSavedEvent(double scale_factor, double predicted_accuracy_pct,
                                   int64_t ts, char* buf, size_t bufsz);
int serializeVerifyResultEvent(double measured_kg, double expected_kg, double error_pct,
                               int64_t ts, char* buf, size_t bufsz);
int serializeRawStreamEvent(int32_t raw_value, double kg, bool stable,
                            int64_t ts, char* buf, size_t bufsz);
int serializeModifyStartedEvent(const char* label, double pre_event_kg,
                                int64_t ts, char* buf, size_t bufsz);
int serializeModifyCompleteEvent(const char* label, double pre_kg, double post_kg,
                                 double delta_kg, int32_t duration_sec, bool tare_updated,
                                 int64_t ts, char* buf, size_t bufsz);
int serializeModifyWarningEvent(const char* label, double delta_kg, const char* warning,
                                int64_t ts, char* buf, size_t bufsz);
int serializeModifyTimeoutEvent(const char* label, int64_t ts, char* buf, size_t bufsz);
int serializeErrorEvent(const char* code, const char* details,
                        int64_t ts, char* buf, size_t bufsz);
```

- [ ] **Step 3: Implement serializers in scale_commands.cpp**

Append to `scale_commands.cpp`:

```cpp
#include "scale_math.h"

namespace {
void writeTs(JsonDocument& doc, int64_t ts) {
    char buf[24];
    formatRFC3339(ts, buf, sizeof(buf));
    doc["ts"] = buf;
}
}

int serializeAwakeEvent(int64_t keep_alive_until, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "awake";
    char kau[24];
    formatRFC3339(keep_alive_until, kau, sizeof(kau));
    doc["keep_alive_until"] = kau;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeTareSavedEvent(int64_t raw_offset, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "tare_saved";
    doc["raw_offset"] = raw_offset;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeCalibrationSavedEvent(double scale_factor, double predicted_accuracy_pct,
                                   int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "calibration_saved";
    doc["scale_factor"] = scale_factor;
    doc["predicted_accuracy_pct"] = predicted_accuracy_pct;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeVerifyResultEvent(double measured_kg, double expected_kg, double error_pct_,
                               int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "verify_result";
    doc["measured_kg"] = measured_kg;
    doc["expected_kg"] = expected_kg;
    doc["error_pct"] = error_pct_;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeRawStreamEvent(int32_t raw_value, double kg, bool stable,
                            int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "raw_stream";
    doc["raw_value"] = raw_value;
    doc["kg"] = kg;
    doc["stable"] = stable;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyStartedEvent(const char* label, double pre_event_kg,
                                int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_started";
    doc["label"] = label;
    doc["pre_event_kg"] = pre_event_kg;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyCompleteEvent(const char* label, double pre_kg, double post_kg,
                                 double delta_kg, int32_t duration_sec, bool tare_updated,
                                 int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_complete";
    doc["label"] = label;
    doc["pre_kg"] = pre_kg;
    doc["post_kg"] = post_kg;
    doc["delta_kg"] = delta_kg;
    doc["duration_sec"] = duration_sec;
    doc["tare_updated"] = tare_updated;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyWarningEvent(const char* label, double delta_kg, const char* warning,
                                int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_warning";
    doc["label"] = label;
    doc["delta_kg"] = delta_kg;
    doc["warning"] = warning;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeModifyTimeoutEvent(const char* label, int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "modify_timeout";
    doc["label"] = label;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}

int serializeErrorEvent(const char* code, const char* details,
                        int64_t ts, char* buf, size_t bufsz) {
    JsonDocument doc;
    doc["event"] = "error";
    doc["code"] = code;
    doc["details"] = details;
    writeTs(doc, ts);
    return serializeJson(doc, buf, bufsz);
}
```

- [ ] **Step 4: Run tests**

Run: `pio test -e native -d firmware/sensor-tag-wifi -f test_scale_commands 2>&1 | tail -10`
Expected: 19 tests pass (11 + 8).

- [ ] **Step 5: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale_commands.{h,cpp} \
        firmware/sensor-tag-wifi/test/test_scale_commands/test_main.cpp
git commit -m "feat(scale): ScaleStatusEvent JSON serializers + 8 unit tests"
```

---

### Task 8: Scale module skeleton + HX711 driver wrapper

**Files:**
- Create: `firmware/sensor-tag-wifi/src/scale.h`
- Create: `firmware/sensor-tag-wifi/src/scale.cpp`

This task is on-device only — HX711 lib doesn't build under `native`. We'll guard the cpp with `#ifdef SENSOR_SCALE` so it's a no-op when the env doesn't define it. Keep tests for in-firmware behavior thin (most logic is in scale_math + scale_commands which we already tested).

- [ ] **Step 1: Create scale.h**

```cpp
#pragma once

#include <cstdint>

namespace Scale {

void init();
void deinit();
bool sample(int32_t& raw, double& kg);   // false on HX711 timeout

void subscribe();                         // call after MQTT connect
void onMessage(const char* topic, const char* payload, unsigned int len);
void tick();                              // call from extended-awake loop
bool inExtendedAwakeMode();
int64_t keepAliveUntil();

// Convenience for main.cpp
void onConnect();   // subscribe + check retained config (1.5s grace window)
bool ntpSynced();   // helper used to gate extended-awake entry

}  // namespace Scale
```

- [ ] **Step 2: Create scale.cpp skeleton (compiles in xiao-c6-ds18b20-scale env only)**

```cpp
#ifdef SENSOR_SCALE

#include "scale.h"
#include "scale_math.h"
#include "scale_commands.h"
#include "config.h"
#include "mqtt_client.h"
#include "payload.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <cstring>
#include <time.h>

namespace {

HX711 hx711;
Preferences prefs;

int64_t weight_off_   = 0;
double  weight_scl_   = HX711_DEFAULT_SCALE_FACTOR;
StableDetector stable_;

bool extended_awake_ = false;
int64_t keep_alive_until_ = 0;
uint32_t last_heartbeat_ms_ = 0;

bool streaming_ = false;
int64_t stream_until_ = 0;
uint32_t last_stream_ms_ = 0;

bool modify_active_ = false;
char modify_label_[32] = {};
double modify_pre_kg_ = 0.0;
int64_t modify_started_at_ = 0;
int64_t modify_timeout_at_ = 0;

constexpr const char* NVS_NS = "combsense";
constexpr const char* NVS_K_OFF = "weight_off";
constexpr const char* NVS_K_SCL = "weight_scl";

int64_t nowEpoch() {
    time_t t = time(nullptr);
    return static_cast<int64_t>(t);
}

void loadFromNvs() {
    prefs.begin(NVS_NS, /*readOnly=*/true);
    weight_off_ = prefs.getLong64(NVS_K_OFF, 0);
    weight_scl_ = prefs.getDouble(NVS_K_SCL, HX711_DEFAULT_SCALE_FACTOR);
    prefs.end();
}

void writeOffsetToNvs(int64_t off) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putLong64(NVS_K_OFF, off);
    prefs.end();
    weight_off_ = off;
}

void writeScaleToNvs(double scl) {
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putDouble(NVS_K_SCL, scl);
    prefs.end();
    weight_scl_ = scl;
}

}  // anonymous namespace

namespace Scale {

void init() {
    hx711.begin(PIN_HX711_DT_, PIN_HX711_SCK_, HX711_GAIN);
    hx711.power_up();
    loadFromNvs();
    stable_.reset();
    extended_awake_ = false;
    keep_alive_until_ = 0;
    streaming_ = false;
    modify_active_ = false;
}

void deinit() {
    hx711.power_down();
}

bool sample(int32_t& raw, double& kg) {
    if (!hx711.wait_ready_timeout(HX711_READ_TIMEOUT_MS)) {
        kg = NAN;
        raw = 0;
        return false;
    }
    raw = hx711.read();
    stable_.push(raw);
    kg = applyCalibration(raw, weight_off_, weight_scl_);
    return true;
}

bool ntpSynced() {
    // Heuristic: if epoch is well past 2020, NTP has fired.
    return nowEpoch() > 1577836800;  // 2020-01-01 UTC
}

bool inExtendedAwakeMode() {
    return extended_awake_;
}

int64_t keepAliveUntil() {
    return keep_alive_until_;
}

}  // namespace Scale

#else  // !SENSOR_SCALE — provide no-op stubs so main.cpp compiles unchanged

namespace Scale {
void init() {}
void deinit() {}
bool sample(int32_t&, double& kg) { kg = NAN; return false; }
void subscribe() {}
void onMessage(const char*, const char*, unsigned int) {}
void tick() {}
bool inExtendedAwakeMode() { return false; }
int64_t keepAliveUntil() { return 0; }
void onConnect() {}
bool ntpSynced() { return true; }
}

#endif  // SENSOR_SCALE
```

- [ ] **Step 3: Verify env builds with the skeleton**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS.

Also: `pio run -e xiao-c6-ds18b20 -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS (no SENSOR_SCALE → stubs path).

Also: `pio test -e native -d firmware/sensor-tag-wifi 2>&1 | tail -5`
Expected: all native tests still pass.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale.{h,cpp}
git commit -m "feat(scale): Scale module skeleton with HX711 driver glue + NVS load/store"
```

---

### Task 9: Command handlers — tare, calibrate, verify

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale.cpp` (add command handlers + dispatch in onMessage)

- [ ] **Step 1: Add command handlers**

Inside the `SENSOR_SCALE` block of scale.cpp, before `namespace Scale`, add:

```cpp
namespace {

void publishStatusEvent(const char* json) {
    char topic[80];
    snprintf(topic, sizeof(topic), "combsense/hive/%s/scale/status",
             /* deviceId — main.cpp owns this; will pass in later */ "");
    // Actual publish call deferred — wired in Task 13 when subscribe() is added.
    // For now this is a stub — full wiring in main integration task.
    (void)json; (void)topic;
}

bool readSamples(int32_t* out, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (!hx711.wait_ready_timeout(HX711_READ_TIMEOUT_MS)) return false;
        out[i] = hx711.read();
        stable_.push(out[i]);
    }
    return true;
}

void cmdTare() {
    int32_t samples[HX711_TARE_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_TARE_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "tare failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int64_t off = tareFromMean(samples, HX711_TARE_SAMPLE_COUNT);
    writeOffsetToNvs(off);

    char buf[160];
    serializeTareSavedEvent(off, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdCalibrate(double known_kg) {
    int32_t samples[HX711_TARE_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_TARE_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "calibrate failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    double sf = scaleFactorFromMean(samples, HX711_TARE_SAMPLE_COUNT, weight_off_, known_kg);
    if (std::fabs(sf) < HX711_CALIBRATE_MIN_FACTOR) {
        char buf[200];
        char detail[64];
        snprintf(detail, sizeof(detail), "scale_factor=%.4f below threshold", sf);
        serializeErrorEvent("calibrate_invalid", detail, nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    writeScaleToNvs(sf);

    // Stub predicted accuracy = sample stddev / mean * 100
    int64_t sum = 0;
    for (uint8_t i = 0; i < HX711_TARE_SAMPLE_COUNT; i++) sum += samples[i];
    double mean = double(sum) / HX711_TARE_SAMPLE_COUNT;
    double var = 0;
    for (uint8_t i = 0; i < HX711_TARE_SAMPLE_COUNT; i++) {
        double d = samples[i] - mean;
        var += d * d;
    }
    double stddev = std::sqrt(var / HX711_TARE_SAMPLE_COUNT);
    double predicted = (mean != 0.0) ? (stddev / std::fabs(mean) * 100.0) : 0.0;

    char buf[200];
    serializeCalibrationSavedEvent(sf, predicted, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdVerify(double expected_kg) {
    int32_t samples[HX711_VERIFY_SAMPLE_COUNT];
    if (!readSamples(samples, HX711_VERIFY_SAMPLE_COUNT)) {
        char buf[160];
        serializeErrorEvent("hx711_unresponsive", "verify failed: no DOUT pulse",
                            nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int64_t sum = 0;
    for (uint8_t i = 0; i < HX711_VERIFY_SAMPLE_COUNT; i++) sum += samples[i];
    int32_t avg = sum / HX711_VERIFY_SAMPLE_COUNT;
    double measured = applyCalibration(avg, weight_off_, weight_scl_);
    double err = errorPct(measured, expected_kg);

    char buf[200];
    serializeVerifyResultEvent(measured, expected_kg, err, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

}  // anonymous namespace
```

(Note: `#include <cmath>` at top of file if not already present.)

- [ ] **Step 2: Verify build still passes**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale.cpp
git commit -m "feat(scale): cmdTare/cmdCalibrate/cmdVerify command handlers"
```

---

### Task 10: Stream + modify command handlers

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale.cpp`

- [ ] **Step 1: Add stream + modify handlers**

In the anonymous namespace of scale.cpp, append:

```cpp
void cmdStreamRaw(int32_t duration_sec) {
    streaming_ = true;
    int64_t cap = std::min<int32_t>(duration_sec, 120);  // hard cap per spec
    stream_until_ = nowEpoch() + cap;
    last_stream_ms_ = millis();
}

void cmdStopStream() {
    streaming_ = false;
}

void cmdModifyStart(const char* label) {
    int32_t raw;
    double kg;
    sample(raw, kg);  // best-effort; OK if HX711 is flaky here
    modify_pre_kg_ = std::isfinite(kg) ? kg : 0.0;
    std::strncpy(modify_label_, label, sizeof(modify_label_) - 1);
    modify_label_[sizeof(modify_label_) - 1] = '\0';
    modify_started_at_ = nowEpoch();
    modify_timeout_at_ = modify_started_at_ + MODIFY_DEFAULT_TIMEOUT_SEC;
    modify_active_ = true;

    char buf[200];
    serializeModifyStartedEvent(modify_label_, modify_pre_kg_, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

void cmdModifyEnd(const char* label) {
    if (!modify_active_) return;
    if (strcmp(modify_label_, label) != 0) {
        char buf[200];
        serializeErrorEvent("modify_label_mismatch", label, nowEpoch(), buf, sizeof(buf));
        publishStatusEvent(buf);
        return;
    }
    int32_t raw;
    double kg;
    sample(raw, kg);
    double post_kg = std::isfinite(kg) ? kg : 0.0;
    double delta = post_kg - modify_pre_kg_;
    int32_t duration = static_cast<int32_t>(nowEpoch() - modify_started_at_);

    char buf[256];
    if (std::fabs(delta) < MODIFY_DELTA_THRESHOLD_KG) {
        serializeModifyWarningEvent(modify_label_, delta, "no_significant_change_detected",
                                    nowEpoch(), buf, sizeof(buf));
    } else {
        serializeModifyCompleteEvent(modify_label_, modify_pre_kg_, post_kg, delta,
                                     duration, false, nowEpoch(), buf, sizeof(buf));
    }
    publishStatusEvent(buf);
    modify_active_ = false;
}

void cmdModifyCancel() {
    modify_active_ = false;
}
```

- [ ] **Step 2: Build verify**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale.cpp
git commit -m "feat(scale): stream + modify command handlers"
```

---

### Task 11: Heartbeat + tick() + extended-awake state

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale.cpp`

- [ ] **Step 1: Implement tick(), heartbeat, extended-awake entry/exit**

In the anonymous namespace, append:

```cpp
void publishHeartbeat() {
    char buf[160];
    serializeAwakeEvent(keep_alive_until_, nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
    last_heartbeat_ms_ = millis();
}

void enterExtendedAwake(int64_t kau) {
    extended_awake_   = true;
    keep_alive_until_ = kau;
    last_heartbeat_ms_ = 0;  // force immediate heartbeat
    publishHeartbeat();
}

void exitExtendedAwake() {
    extended_awake_   = false;
    keep_alive_until_ = 0;
    streaming_        = false;
    modify_active_    = false;
}

void publishStreamSample() {
    int32_t raw;
    double kg;
    if (!sample(raw, kg)) return;
    char buf[200];
    serializeRawStreamEvent(raw, kg, stable_.isStable(), nowEpoch(), buf, sizeof(buf));
    publishStatusEvent(buf);
}

}  // close anon namespace from previous tasks
```

(Reopen `namespace Scale` for the public `tick()`:)

```cpp
namespace Scale {

void tick() {
    if (!extended_awake_) return;
    int64_t now = nowEpoch();

    // Exit if keep-alive expired (modulo skew)
    if (!isKeepAliveValid(keep_alive_until_, now)) {
        exitExtendedAwake();
        return;
    }

    // Heartbeat every 60s
    if ((millis() - last_heartbeat_ms_) >= HEARTBEAT_INTERVAL_MS) {
        publishHeartbeat();
    }

    // Stream sampling at 1Hz
    if (streaming_) {
        if (now >= stream_until_) {
            streaming_ = false;
        } else if ((millis() - last_stream_ms_) >= HX711_STREAM_INTERVAL_MS) {
            publishStreamSample();
            last_stream_ms_ = millis();
        }
    }

    // Modify timeout
    if (modify_active_ && now >= modify_timeout_at_) {
        char buf[160];
        serializeModifyTimeoutEvent(modify_label_, now, buf, sizeof(buf));
        publishStatusEvent(buf);
        modify_active_ = false;
    }
}

}  // namespace Scale
```

- [ ] **Step 2: Build verify**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale.cpp
git commit -m "feat(scale): tick() loop, heartbeat, extended-awake enter/exit"
```

---

### Task 12: subscribe() + onMessage() + onConnect() — MQTT wiring

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/scale.cpp`
- Possibly: `firmware/sensor-tag-wifi/src/mqtt_client.h` (to expose deviceId or publish)

- [ ] **Step 1: Inspect mqtt_client.h to learn publish + deviceId interface**

Run: `cat firmware/sensor-tag-wifi/src/mqtt_client.h`

Identify how `MqttClient::publishRaw` is called from elsewhere and how the deviceId is obtained. (Likely `extern char deviceId[9]` in main.cpp — confirm.)

- [ ] **Step 2: Add deviceId access**

If deviceId isn't already exposed: add to mqtt_client.h:

```cpp
const char* getDeviceId();   // returns the MAC-derived 8-char id
```

And in main.cpp, implement it returning the existing `deviceId` static. If already accessible, skip.

- [ ] **Step 3: Implement subscribe + onMessage + onConnect in scale.cpp**

Replace the stub `publishStatusEvent` with the real implementation. In the anonymous namespace:

```cpp
char status_topic_[80] = {};
char cmd_topic_[80]    = {};
char config_topic_[80] = {};

void buildTopics(const char* deviceId) {
    snprintf(status_topic_, sizeof(status_topic_),
             "combsense/hive/%s/scale/status", deviceId);
    snprintf(cmd_topic_, sizeof(cmd_topic_),
             "combsense/hive/%s/scale/cmd", deviceId);
    snprintf(config_topic_, sizeof(config_topic_),
             "combsense/hive/%s/scale/config", deviceId);
}

void publishStatusEvent(const char* json) {
    if (status_topic_[0] == '\0') return;
    MqttClient::publishRaw(status_topic_, json, /*retained=*/false);
}

void handleConfigMessage(const char* payload, unsigned int len) {
    if (len == 0) {
        // Empty payload = clear retain
        if (extended_awake_) exitExtendedAwake();
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload, len)) return;
    const char* kau_str = doc["keep_alive_until"].as<const char*>();
    if (!kau_str) return;

    // Parse RFC3339 → epoch
    struct tm tm_;
    memset(&tm_, 0, sizeof(tm_));
    if (!strptime(kau_str, "%Y-%m-%dT%H:%M:%SZ", &tm_)) return;
    int64_t kau = static_cast<int64_t>(timegm(&tm_));

    int64_t now = nowEpoch();
    if (Scale::ntpSynced()) {
        if (!isKeepAliveValid(kau, now)) return;
    } else {
        // NTP fallback grace
        kau = now + KEEPALIVE_NTP_FALLBACK_SEC;
    }
    enterExtendedAwake(kau);
}
```

Then in the public `Scale::` block:

```cpp
namespace Scale {

void subscribe() {
    if (cmd_topic_[0] == '\0') buildTopics(getDeviceId());
    MqttClient::subscribe(cmd_topic_);
    MqttClient::subscribe(config_topic_);
}

void onMessage(const char* topic, const char* payload, unsigned int len) {
    if (strcmp(topic, config_topic_) == 0) {
        handleConfigMessage(payload, len);
        return;
    }
    if (strcmp(topic, cmd_topic_) == 0) {
        ScaleCommand cmd;
        if (!parseScaleCommand(payload, cmd)) return;
        switch (cmd.type) {
            case ScaleCommandType::Tare:         cmdTare(); break;
            case ScaleCommandType::Calibrate:    cmdCalibrate(cmd.calibrate.known_kg); break;
            case ScaleCommandType::Verify:       cmdVerify(cmd.verify.expected_kg); break;
            case ScaleCommandType::StreamRaw:    cmdStreamRaw(cmd.stream_raw.duration_sec); break;
            case ScaleCommandType::StopStream:   cmdStopStream(); break;
            case ScaleCommandType::ModifyStart:  cmdModifyStart(cmd.modify.label); break;
            case ScaleCommandType::ModifyEnd:    cmdModifyEnd(cmd.modify.label); break;
            case ScaleCommandType::ModifyCancel: cmdModifyCancel(); break;
        }
    }
}

void onConnect() {
    subscribe();
    // Wait briefly for retained scale/config to land before deciding sleep vs extended-awake
    uint32_t deadline = millis() + RETAINED_CONFIG_WAIT_MS;
    while (millis() < deadline) {
        MqttClient::loop();
        delay(10);
        if (extended_awake_) break;  // entered already; can stop waiting
    }
}

}  // namespace Scale
```

- [ ] **Step 4: Verify subscribe API on mqtt_client**

If `MqttClient::subscribe` doesn't exist, add a thin wrapper around `pubsub.subscribe`. Check existing code:

Run: `grep -n "pubsub.subscribe\|MqttClient::subscribe" firmware/sensor-tag-wifi/src/*.cpp`

If missing, add to mqtt_client.{h,cpp}:

```cpp
// mqtt_client.h
bool subscribe(const char* topic);
void loop();

// mqtt_client.cpp
bool subscribe(const char* topic) { return pubsub.subscribe(topic); }
void loop() { pubsub.loop(); }
```

- [ ] **Step 5: Build verify**

Run: `pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add firmware/sensor-tag-wifi/src/scale.cpp \
        firmware/sensor-tag-wifi/src/mqtt_client.{h,cpp}
git commit -m "feat(scale): MQTT subscribe/onMessage/onConnect with retained config + dispatch"
```

---

### Task 13: main.cpp integration — sample weight, dual-publish, extended-awake loop

**Files:**
- Modify: `firmware/sensor-tag-wifi/src/main.cpp`

- [ ] **Step 1: Read main.cpp top + sample loop section**

Run: `wc -l firmware/sensor-tag-wifi/src/main.cpp; sed -n '1,30p;180,260p' firmware/sensor-tag-wifi/src/main.cpp`

Locate where sensor sampling currently happens, where MQTT publish is called, and where deep_sleep is invoked.

- [ ] **Step 2: Wire Scale into the wake cycle**

Apply these edits at the existing locations:

A) Near the top of `setup()` (after WiFi/MQTT framework init but before first sample):

```cpp
Scale::init();
```

B) In the sample-collection block (where `temp1`, `vbat_mV` etc. are set on the Reading), add:

```cpp
int32_t scale_raw = 0;
double scale_kg = NAN;
Scale::sample(scale_raw, scale_kg);
r.weight_kg = static_cast<float>(scale_kg);  // NaN if sample failed; payload omits the field
```

C) After `MqttClient::connect()` succeeds and before any extended-awake decision:

```cpp
Scale::onConnect();   // subscribes to scale/cmd and scale/config; waits ≤1.5s for retained
```

D) After publishing the `reading` JSON payload, also publish to the dedicated `weight` topic:

```cpp
if (std::isfinite(r.weight_kg)) {
    char weight_topic[80];
    char weight_payload[16];
    snprintf(weight_topic,   sizeof(weight_topic),
             "combsense/hive/%s/weight", deviceId);
    snprintf(weight_payload, sizeof(weight_payload),
             "%.3f", static_cast<double>(r.weight_kg));
    MqttClient::publishRaw(weight_topic, weight_payload, /*retained=*/false);
}
```

E) Before `deep_sleep()`, check for extended-awake and loop:

```cpp
if (Scale::inExtendedAwakeMode() && Scale::ntpSynced()) {
    while (Scale::inExtendedAwakeMode()) {
        MqttClient::loop();
        Scale::tick();
        delay(20);
    }
}

Scale::deinit();
deep_sleep(SAMPLE_INT_SEC * 1000ULL * 1000ULL);
```

F) In the existing PubSubClient callback, route scale topics:

```cpp
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    if (strncmp(topic, "combsense/hive/", 15) == 0 && strstr(topic, "/scale/")) {
        Scale::onMessage(topic, reinterpret_cast<const char*>(payload), len);
        return;
    }
    // ... existing handlers (config, ota, etc.)
}
```

(If the existing callback dispatches differently, adapt accordingly.)

- [ ] **Step 3: Confirm both envs build**

Run:
```
pio run -e xiao-c6-ds18b20 -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"
pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi 2>&1 | grep -E "SUCCESS|FAILED|error:"
```
Both expected: SUCCESS.

The non-scale env compiles `scale.cpp` against the `#else` stub branch — no HX711 code linked, no behavioral change.

- [ ] **Step 4: Confirm native tests still green**

Run: `pio test -e native -d firmware/sensor-tag-wifi 2>&1 | tail -10`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add firmware/sensor-tag-wifi/src/main.cpp
git commit -m "feat(scale): main.cpp integration — Scale::init/sample/tick/deinit, dual-publish"
```

---

### Task 14: Bench validation against the iOS contract

This is on-device manual testing. No code changes — verify behavior against the spec's section 17 procedure.

**Pre-requisites:**
- Bench tag (XIAO C6) running latest firmware
- HX711 module wired per spec section 5
- 4× 50 kg load cells wired in Wheatstone bridge to HX711
- Cell on TP4056, fully charged
- Mosquitto broker reachable
- `mosquitto_sub` and `mosquitto_pub` CLI installed

- [ ] **Step 1: Flash the bench tag with the new env**

```bash
pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi -t upload --upload-port /dev/cu.usbmodem3101
```

- [ ] **Step 2: Monitor serial + MQTT in two terminals**

Terminal A: `pio device monitor -p /dev/cu.usbmodem3101 -b 115200`
Terminal B: `mosquitto_sub -h 192.168.1.82 -u hivesense -P hivesense -t 'combsense/hive/<bench_id>/#' -v`

Wait for first wake. Verify in Terminal B:
- `combsense/hive/<id>/reading` payload includes `"w":<number>`
- `combsense/hive/<id>/weight` topic publishes a numeric value matching `w`

- [ ] **Step 3: Trigger extended-awake mode**

In a third terminal, publish a retained config:

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense -r -q 1 \
  -t 'combsense/hive/<id>/scale/config' \
  -m "{\"keep_alive_until\":\"$(date -u -v+10M +%Y-%m-%dT%H:%M:%SZ)\"}"
```

Wait for next wake (≤5 min). In Terminal B verify:
- `scale/status` event with `"event":"awake"` arrives shortly after wake
- `awake` event repeats every ~60 seconds

- [ ] **Step 4: Test stream_raw**

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense \
  -t 'combsense/hive/<id>/scale/cmd' \
  -m '{"cmd":"stream_raw","duration_sec":30}'
```

Verify ~30 `raw_stream` events arrive on `scale/status`, one per second. `stable` flips from false to true after the first 5 readings.

- [ ] **Step 5: Test tare**

With nothing on the scale:

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense \
  -t 'combsense/hive/<id>/scale/cmd' \
  -m '{"cmd":"tare"}'
```

Verify `tare_saved` event arrives within ~2 seconds. Note the `raw_offset` in the payload.

- [ ] **Step 6: Test calibrate with 1 kg reference**

Place a 1 kg known weight on the scale.

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense \
  -t 'combsense/hive/<id>/scale/cmd' \
  -m '{"cmd":"calibrate","known_kg":1.0}'
```

Verify `calibration_saved` event arrives. Note the `scale_factor`.

- [ ] **Step 7: Test verify**

Place a 0.5 kg known weight on the scale (or whatever reference you have).

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense \
  -t 'combsense/hive/<id>/scale/cmd' \
  -m '{"cmd":"verify","expected_kg":0.5}'
```

Verify `verify_result` event arrives with `error_pct` < 5%.

- [ ] **Step 8: Test extended-awake exit on cleared retain**

```bash
mosquitto_pub -h 192.168.1.82 -u hivesense -P hivesense -r -q 1 \
  -t 'combsense/hive/<id>/scale/config' \
  -m ''
```

On next tick (or at most ~60s), verify the device exits extended-awake mode (no more heartbeats) and resumes 5-min sleep cycle.

- [ ] **Step 9: Hand off to iOS for end-to-end wizard test**

Tell the iOS session: bench tag is ready, run the calibration wizard against `<bench_id>`. Verify Quick Re-tare works first-tap (after the initial wake-up wait).

- [ ] **Step 10: Document any issues found in `.mex/AGENTS.md` or as separate Issue tickets**

If anything didn't work, write it up. If everything passed, update `.mex/AGENTS.md` to note that scale support is shipping.

---

### Task 15: Final cleanup, PR prep

**Files:** review only

- [ ] **Step 1: Confirm full native suite + both envs build**

```bash
pio test -e native -d firmware/sensor-tag-wifi
pio run -e xiao-c6-ds18b20 -d firmware/sensor-tag-wifi
pio run -e xiao-c6-ds18b20-scale -d firmware/sensor-tag-wifi
```

All three: green.

- [ ] **Step 2: Review the full diff**

```bash
git -C /Users/sjordan/Code/hivesense-monitor log --oneline main..dev
git -C /Users/sjordan/Code/hivesense-monitor diff main..dev --stat
```

Expect: ~14–15 commits, additions concentrated under `firmware/sensor-tag-wifi/{src,test,include}` and one each of `platformio.ini`, `.mex/scale-mqtt-contract.md`, `docs/superpowers/specs/`.

- [ ] **Step 3: Open a PR**

```bash
gh pr create --base main --head dev --title "feat(scale): HX711 calibration with iOS MQTT contract" --body "$(cat <<'EOF'
## Summary
- Adds HX711 + 4-cell load cell support to sensor-tag-wifi (XIAO C6)
- Implements the iOS↔firmware MQTT contract at `.mex/scale-mqtt-contract.md` (sections 1–10) including the keep-alive addendum
- New `xiao-c6-ds18b20-scale` build env; existing `xiao-c6-ds18b20` env unchanged
- Bench-validated against mosquitto_pub-driven commands and the iOS session's calibration wizard

## Test plan
- [x] `pio test -e native -d firmware/sensor-tag-wifi` — all unit tests pass
- [x] `pio run -e xiao-c6-ds18b20` — clean build, no behavioral change
- [x] `pio run -e xiao-c6-ds18b20-scale` — clean build with scale code linked
- [x] On-device: bench tag flashes, publishes weight in reading + weight topics
- [x] On-device: retained scale/config triggers extended-awake; awake heartbeat publishes
- [x] On-device: tare/calibrate/verify happy paths via mosquitto_pub
- [x] iOS calibration wizard end-to-end against bench tag (handoff to iOS session)
EOF
)"
```

---

## Acceptance criteria

- [ ] All 15 tasks above marked complete
- [ ] Spec section 21 acceptance criteria all checked
- [ ] PR open against main, CI green (if any), reviewed and ready to merge
- [ ] Any new operational knowledge captured in `.mex/` or as memory entries

---

**Plan version:** 1.0
**Generated from spec:** [2026-05-02-scale-firmware-design.md](../specs/2026-05-02-scale-firmware-design.md)
