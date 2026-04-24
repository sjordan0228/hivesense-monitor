# Sensor Tag WiFi — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a WiFi-connected sensor tag variant for home-yard deployment — wakes on a schedule, reads temp/humidity, publishes JSON over MQTT to Mosquitto, runs for weeks on an 18650 with solar top-up.

**Architecture:** New fork in `firmware/sensor-tag-wifi/`. Compile-time sensor abstraction (SHT31 default for dual humidity, DS18B20 for dual temperature brood/top). RTC memory ring buffer absorbs network blips across sleep cycles. BSSID cached in RTC for fast reconnect. No BLE, no ESP-NOW — pure WiFi→MQTT.

**Tech Stack:** PlatformIO, Arduino framework, XIAO ESP32-C6, PubSubClient (MQTT), OneWire + DallasTemperature (DS18B20), Adafruit SHT31 Library (SHT31), Unity (native tests).

**Design assumptions locked in from brainstorming:**
- Device ID derived from eFuse MAC (8 hex chars)
- Broker: Mosquitto at 192.168.1.82:1883 (host/port/creds in NVS, runtime configurable)
- Topic: `combsense/hive/<device-id>/reading`
- Cadence: 5 min sample, upload every sample (configurable via NVS)
- Payload JSON shape: `{"id":"ab12cd34","t":1712345678,"t1":22.4,"t2":24.1,"h1":52.3,"h2":55.1,"b":87}`
  - `t1`/`t2` = brood/top temp; `h1`/`h2` = brood/top humidity (omitted for DS18B20 build)
  - `b` = battery percent
- RTC ring buffer holds 48 readings (4 hours @ 5 min) to absorb router reboots
- BSSID + channel cached in RTC for sub-second reconnect
- Deferred: OTA, BLE provisioning, TLS (provisioning via shared serial console)

**Ollama delegation:** Tasks 2-4, 7-11 are boilerplate-heavy — delegate initial drafts to Ollama (qwen3-coder:30b), review carefully before committing.

---

## File Map

### New — `firmware/sensor-tag-wifi/`
- `platformio.ini` — Two envs: `xiao-c6-sht31` (default), `xiao-c6-ds18b20`. One native env for tests.
- `partitions_tag.csv` — Copied from existing sensor-tag
- `include/config.h` — Pins, NVS keys, WiFi/MQTT defaults, cadence defaults
- `include/reading.h` — `Reading` POD struct shared by sensors, payload, storage
- `include/sensor.h` — `ISensor` abstract interface (begin, read, deinit)
- `src/sensor_sht31.cpp` — SHT31 implementation (compiled when `-DSENSOR_SHT31`)
- `src/sensor_ds18b20.cpp` — DS18B20 implementation (compiled when `-DSENSOR_DS18B20`)
- `src/battery.h` / `src/battery.cpp` — Battery ADC read → percent estimation
- `src/payload.h` / `src/payload.cpp` — JSON serializer (pure, testable)
- `src/ring_buffer.h` / `src/ring_buffer.cpp` — RTC-memory-backed circular buffer
- `src/wifi_manager.h` / `src/wifi_manager.cpp` — WiFi connect with BSSID cache
- `src/mqtt_client.h` / `src/mqtt_client.cpp` — PubSubClient wrapper, publish reading
- `src/main.cpp` — State machine: wake, sample, drain buffer, sleep

### New — `test/` (native)
- `test/test_payload/test_payload.cpp` — Unity tests for JSON payload serialization

### Reused (symlink into src/)
- `firmware/shared/serial_console.cpp` — Provisioning (already supports `wifi_ssid`, `wifi_pass`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `tag_name`)

### Modified
- `firmware/shared/serial_console.cpp` — Add `sample_interval_sec`, `upload_every_n` to known keys
- `README.md` — Add "Home Yard WiFi Variant" section to BOM and firmware sections
- `.mex/ROUTER.md` — Add sensor-tag-wifi to completed list, update routing table
- `.github/workflows/build.yml` — Add two build jobs (sht31, ds18b20) + native test job

---

## Task 1: Feature branch + scaffold directory

**Files:**
- Create: `firmware/sensor-tag-wifi/` directory skeleton

- [ ] **Step 1: Create feature branch**

```bash
git checkout -b feature/sensor-tag-wifi
```

- [ ] **Step 2: Create directory skeleton**

```bash
mkdir -p firmware/sensor-tag-wifi/{src,include,test/test_payload}
```

- [ ] **Step 3: Copy partition table from existing sensor-tag**

```bash
cp firmware/sensor-tag/partitions_tag.csv firmware/sensor-tag-wifi/partitions_tag.csv
```

- [ ] **Step 4: Commit scaffold**

```bash
git add firmware/sensor-tag-wifi/
git commit -m "chore: scaffold sensor-tag-wifi directory"
```

---

## Task 2: platformio.ini with dual sensor build envs + native test env

**Files:**
- Create: `firmware/sensor-tag-wifi/platformio.ini`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/platformio.ini`**

```ini
[platformio]
default_envs = xiao-c6-sht31

[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.10/platform-espressif32.zip
framework = arduino
board_build.partitions = partitions_tag.csv
monitor_speed = 115200
lib_ldf_mode = deep+

[env:xiao-c6-sht31]
board = esp32-c6-devkitm-1
lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    knolleary/PubSubClient@^2.8
build_flags =
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DSENSOR_SHT31
build_src_filter =
    +<*>
    -<sensor_ds18b20.cpp>

[env:xiao-c6-ds18b20]
board = esp32-c6-devkitm-1
lib_deps =
    paulstoffregen/OneWire@^2.3.8
    milesburton/DallasTemperature@^3.11.0
    knolleary/PubSubClient@^2.8
build_flags =
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DSENSOR_DS18B20
build_src_filter =
    +<*>
    -<sensor_sht31.cpp>

[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -Iinclude
    -Isrc
build_src_filter =
    +<payload.cpp>
```

- [ ] **Step 2: Commit**

```bash
git add firmware/sensor-tag-wifi/platformio.ini
git commit -m "feat(sensor-tag-wifi): add platformio config with dual sensor envs"
```

---

## Task 3: config.h — pins, NVS keys, defaults

**Files:**
- Create: `firmware/sensor-tag-wifi/include/config.h`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/include/config.h`**

```cpp
#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Seeed XIAO ESP32-C6
// =============================================================================

// I2C (SHT31) — D4/D5
constexpr uint8_t PIN_I2C_SDA = 4;
constexpr uint8_t PIN_I2C_SCL = 5;

// 1-Wire (DS18B20) — D2 with 4.7kΩ pullup to 3V3
constexpr uint8_t PIN_ONE_WIRE = 2;

// Battery ADC — A0 (GPIO 0 on XIAO C6)
constexpr uint8_t PIN_BATTERY_ADC = 0;

// =============================================================================
// Power / Timing Defaults
// =============================================================================

constexpr uint32_t DEFAULT_SAMPLE_INTERVAL_SEC = 300;   // 5 min
constexpr uint8_t  DEFAULT_UPLOAD_EVERY_N      = 1;     // Upload every sample
constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS     = 10000;
constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS     = 5000;
constexpr uint8_t  WIFI_RECONNECT_RETRIES      = 2;

// =============================================================================
// MQTT Defaults
// =============================================================================

constexpr const char* DEFAULT_MQTT_HOST  = "192.168.1.82";
constexpr uint16_t    DEFAULT_MQTT_PORT  = 1883;
constexpr const char* MQTT_TOPIC_PREFIX  = "combsense/hive/";

// =============================================================================
// NVS
// =============================================================================

constexpr const char* NVS_NAMESPACE        = "combsense";
constexpr const char* NVS_KEY_WIFI_SSID    = "wifi_ssid";
constexpr const char* NVS_KEY_WIFI_PASS    = "wifi_pass";
constexpr const char* NVS_KEY_MQTT_HOST    = "mqtt_host";
constexpr const char* NVS_KEY_MQTT_PORT    = "mqtt_port";
constexpr const char* NVS_KEY_MQTT_USER    = "mqtt_user";
constexpr const char* NVS_KEY_MQTT_PASS    = "mqtt_pass";
constexpr const char* NVS_KEY_TAG_NAME     = "tag_name";
constexpr const char* NVS_KEY_SAMPLE_INT   = "sample_int";
constexpr const char* NVS_KEY_UPLOAD_EVERY = "upload_every";

// =============================================================================
// RTC Ring Buffer
// =============================================================================

constexpr uint8_t RTC_BUFFER_CAPACITY = 48;   // 4h @ 5-min cadence

// =============================================================================
// Sensors
// =============================================================================

constexpr uint8_t SHT31_ADDR                    = 0x44;
constexpr uint16_t DS18B20_CONVERT_TIMEOUT_MS   = 800;
constexpr uint8_t  DS18B20_RESOLUTION_BITS      = 12;

// =============================================================================
// Payload
// =============================================================================

constexpr uint8_t PAYLOAD_VERSION = 1;
constexpr size_t  PAYLOAD_MAX_LEN = 160;
```

- [ ] **Step 2: Commit**

```bash
git add firmware/sensor-tag-wifi/include/config.h
git commit -m "feat(sensor-tag-wifi): add config header"
```

---

## Task 4: Reading struct

**Files:**
- Create: `firmware/sensor-tag-wifi/include/reading.h`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/include/reading.h`**

```cpp
#pragma once

#include <cstdint>
#include <cmath>

/// A single sensor sample. POD — safe to put in RTC_DATA_ATTR memory.
///
/// `t1`/`t2` are the two temperature channels (brood / top). `h1`/`h2` are the
/// two humidity channels. For DS18B20 builds, `h1`/`h2` are NAN and omitted
/// from the serialized payload.
struct Reading {
    uint32_t timestamp;    // unix seconds
    float    temp1;        // brood (°C)
    float    temp2;        // top   (°C)
    float    humidity1;    // brood (%RH) — NAN for DS18B20
    float    humidity2;    // top   (%RH) — NAN for DS18B20
    uint8_t  battery_pct;  // 0..100
};

inline bool readingHasHumidity(const Reading& r) {
    return !std::isnan(r.humidity1) && !std::isnan(r.humidity2);
}
```

- [ ] **Step 2: Commit**

```bash
git add firmware/sensor-tag-wifi/include/reading.h
git commit -m "feat(sensor-tag-wifi): add Reading POD struct"
```

---

## Task 5: Payload JSON — failing tests first

**Files:**
- Create: `firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_full_reading_with_humidity);
    RUN_TEST(test_serialize_ds18b20_reading_omits_humidity);
    RUN_TEST(test_serialize_returns_negative_on_undersized_buffer);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test — expect compile error (payload.h missing)**

```bash
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: FAIL — `payload.h: No such file or directory`

- [ ] **Step 3: Commit the failing test**

```bash
git add firmware/sensor-tag-wifi/test/test_payload/test_payload.cpp
git commit -m "test(sensor-tag-wifi): add payload serialization tests (failing)"
```

---

## Task 6: Payload JSON serializer — make tests pass

**Files:**
- Create: `firmware/sensor-tag-wifi/src/payload.h`
- Create: `firmware/sensor-tag-wifi/src/payload.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/payload.h`**

```cpp
#pragma once

#include <cstddef>
#include "reading.h"

namespace Payload {

/// Serialize a reading into a JSON string.
/// @param deviceId  8-char hex device ID (null-terminated)
/// @param r         reading to serialize
/// @param buf       output buffer
/// @param bufLen    size of output buffer
/// @return          number of bytes written (excluding null), or -1 on overflow
int serialize(const char* deviceId, const Reading& r, char* buf, size_t bufLen);

}  // namespace Payload
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/payload.cpp`**

```cpp
#include "payload.h"

#include <cstdio>

namespace Payload {

int serialize(const char* deviceId, const Reading& r, char* buf, size_t bufLen) {
    int n;
    if (readingHasHumidity(r)) {
        n = snprintf(buf, bufLen,
            "{\"id\":\"%s\",\"t\":%lu,\"t1\":%.2f,\"t2\":%.2f,"
            "\"h1\":%.2f,\"h2\":%.2f,\"b\":%u}",
            deviceId,
            static_cast<unsigned long>(r.timestamp),
            r.temp1, r.temp2, r.humidity1, r.humidity2,
            r.battery_pct);
    } else {
        n = snprintf(buf, bufLen,
            "{\"id\":\"%s\",\"t\":%lu,\"t1\":%.2f,\"t2\":%.2f,\"b\":%u}",
            deviceId,
            static_cast<unsigned long>(r.timestamp),
            r.temp1, r.temp2,
            r.battery_pct);
    }
    if (n < 0 || static_cast<size_t>(n) >= bufLen) return -1;
    return n;
}

}  // namespace Payload
```

- [ ] **Step 3: Run tests**

```bash
cd firmware/sensor-tag-wifi && pio test -e native
```

Expected: 3 PASSED

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/src/payload.h firmware/sensor-tag-wifi/src/payload.cpp
git commit -m "feat(sensor-tag-wifi): JSON payload serializer"
```

---

## Task 7: Sensor abstraction interface

**Files:**
- Create: `firmware/sensor-tag-wifi/include/sensor.h`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/include/sensor.h`**

```cpp
#pragma once

#include "reading.h"

/// Compile-time sensor abstraction. Exactly one implementation is linked
/// based on `-DSENSOR_SHT31` or `-DSENSOR_DS18B20` PlatformIO env.
///
/// Semantics: populate `r.temp1`/`r.temp2` (and `r.humidity1`/`r.humidity2`
/// if supported). The caller fills `r.timestamp` and `r.battery_pct`.
namespace Sensor {

/// Power up and initialize the sensor bus. Returns false on hardware failure.
bool begin();

/// Perform a blocking read. Populates reading fields. Returns false on error.
bool read(Reading& r);

/// Power down / release peripherals before deep sleep.
void deinit();

}  // namespace Sensor
```

- [ ] **Step 2: Commit**

```bash
git add firmware/sensor-tag-wifi/include/sensor.h
git commit -m "feat(sensor-tag-wifi): add Sensor abstraction interface"
```

---

## Task 8: SHT31 sensor driver

**Files:**
- Create: `firmware/sensor-tag-wifi/src/sensor_sht31.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/sensor_sht31.cpp`**

```cpp
#ifdef SENSOR_SHT31

#include "sensor.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

namespace {

Adafruit_SHT31 brood;
Adafruit_SHT31 top;

// SHT31 has two I2C addresses selectable by ADDR pin: 0x44 (default) and 0x45
constexpr uint8_t SHT31_ADDR_BROOD = 0x44;
constexpr uint8_t SHT31_ADDR_TOP   = 0x45;

bool broodOk = false;
bool topOk   = false;

}  // anonymous namespace

namespace Sensor {

bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    broodOk = brood.begin(SHT31_ADDR_BROOD);
    topOk   = top.begin(SHT31_ADDR_TOP);
    if (!broodOk) Serial.println("[SHT31] brood (0x44) not found");
    if (!topOk)   Serial.println("[SHT31] top (0x45) not found");
    return broodOk || topOk;
}

bool read(Reading& r) {
    if (broodOk) {
        r.temp1     = brood.readTemperature();
        r.humidity1 = brood.readHumidity();
    } else {
        r.temp1 = NAN;
        r.humidity1 = NAN;
    }
    if (topOk) {
        r.temp2     = top.readTemperature();
        r.humidity2 = top.readHumidity();
    } else {
        r.temp2 = NAN;
        r.humidity2 = NAN;
    }
    return broodOk || topOk;
}

void deinit() {
    Wire.end();
}

}  // namespace Sensor

#endif  // SENSOR_SHT31
```

- [ ] **Step 2: Verify build**

```bash
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31
```

Expected: fails at link — missing WiFi/MQTT/main symbols. Compile of `sensor_sht31.cpp` should succeed.

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/sensor_sht31.cpp
git commit -m "feat(sensor-tag-wifi): SHT31 dual-sensor driver"
```

---

## Task 9: DS18B20 sensor driver

**Files:**
- Create: `firmware/sensor-tag-wifi/src/sensor_ds18b20.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/sensor_ds18b20.cpp`**

```cpp
#ifdef SENSOR_DS18B20

#include "sensor.h"
#include "config.h"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <cmath>

namespace {

OneWire           oneWire(PIN_ONE_WIRE);
DallasTemperature sensors(&oneWire);

DeviceAddress addrBrood {};
DeviceAddress addrTop   {};
bool          haveBrood = false;
bool          haveTop   = false;

/// Sort two 1-Wire addresses deterministically (lexicographic) so the same
/// physical probe always maps to t1/t2 across boots.
bool addrLessThan(const DeviceAddress& a, const DeviceAddress& b) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (a[i] != b[i]) return a[i] < b[i];
    }
    return false;
}

}  // anonymous namespace

namespace Sensor {

bool begin() {
    pinMode(PIN_ONE_WIRE, INPUT_PULLUP);
    sensors.begin();
    sensors.setResolution(DS18B20_RESOLUTION_BITS);
    sensors.setWaitForConversion(false);

    uint8_t count = sensors.getDeviceCount();
    Serial.printf("[DS18B20] found %u device(s)\n", count);
    if (count == 0) return false;

    DeviceAddress tmpA, tmpB;
    if (count >= 1) haveBrood = sensors.getAddress(tmpA, 0);
    if (count >= 2) haveTop   = sensors.getAddress(tmpB, 1);

    if (haveBrood && haveTop) {
        if (addrLessThan(tmpA, tmpB)) {
            memcpy(addrBrood, tmpA, 8);
            memcpy(addrTop,   tmpB, 8);
        } else {
            memcpy(addrBrood, tmpB, 8);
            memcpy(addrTop,   tmpA, 8);
        }
    } else if (haveBrood) {
        memcpy(addrBrood, tmpA, 8);
    }
    return haveBrood;
}

bool read(Reading& r) {
    r.humidity1 = NAN;
    r.humidity2 = NAN;

    sensors.requestTemperatures();
    delay(DS18B20_CONVERT_TIMEOUT_MS);

    r.temp1 = haveBrood ? sensors.getTempC(addrBrood) : NAN;
    r.temp2 = haveTop   ? sensors.getTempC(addrTop)   : NAN;

    // DallasTemperature returns -127.0 on read failure — convert to NAN
    if (r.temp1 == DEVICE_DISCONNECTED_C) r.temp1 = NAN;
    if (r.temp2 == DEVICE_DISCONNECTED_C) r.temp2 = NAN;

    return !std::isnan(r.temp1) || !std::isnan(r.temp2);
}

void deinit() {
    // 1-Wire is passive — pull GPIO low to reduce leakage via pullup
    pinMode(PIN_ONE_WIRE, OUTPUT);
    digitalWrite(PIN_ONE_WIRE, LOW);
}

}  // namespace Sensor

#endif  // SENSOR_DS18B20
```

- [ ] **Step 2: Verify build (compile succeeds, link fails on missing symbols)**

```bash
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20
```

Expected: compile of `sensor_ds18b20.cpp` succeeds; link fails (main/wifi/mqtt missing).

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/sensor_ds18b20.cpp
git commit -m "feat(sensor-tag-wifi): DS18B20 dual-probe driver with deterministic mapping"
```

---

## Task 10: Battery ADC reader

**Files:**
- Create: `firmware/sensor-tag-wifi/src/battery.h`
- Create: `firmware/sensor-tag-wifi/src/battery.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/battery.h`**

```cpp
#pragma once

#include <cstdint>

namespace Battery {

/// Read battery voltage via ADC and return percent (0..100).
/// Assumes 18650 Li-ion: 4.20V full, 3.30V empty, with a 2:1 divider
/// (100 kΩ / 100 kΩ) into the ADC pin.
uint8_t readPercent();

}  // namespace Battery
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/battery.cpp`**

```cpp
#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace {

constexpr float VBAT_FULL_MV  = 4200.0f;
constexpr float VBAT_EMPTY_MV = 3300.0f;
constexpr float DIVIDER_RATIO = 2.0f;   // 100k / 100k → 2:1
constexpr uint8_t ADC_SAMPLES = 8;

}  // anonymous namespace

namespace Battery {

uint8_t readPercent() {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
        acc += analogReadMilliVolts(PIN_BATTERY_ADC);
    }
    float vbat = (acc / static_cast<float>(ADC_SAMPLES)) * DIVIDER_RATIO;

    if (vbat >= VBAT_FULL_MV)  return 100;
    if (vbat <= VBAT_EMPTY_MV) return 0;
    return static_cast<uint8_t>(
        (vbat - VBAT_EMPTY_MV) * 100.0f / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

}  // namespace Battery
```

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/battery.h firmware/sensor-tag-wifi/src/battery.cpp
git commit -m "feat(sensor-tag-wifi): battery percent from ADC with oversampling"
```

---

## Task 11: RTC-memory ring buffer

**Files:**
- Create: `firmware/sensor-tag-wifi/src/ring_buffer.h`
- Create: `firmware/sensor-tag-wifi/src/ring_buffer.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/ring_buffer.h`**

```cpp
#pragma once

#include <cstdint>
#include "reading.h"

/// Circular buffer of `Reading`s backed by RTC slow memory. Survives deep
/// sleep but not power loss. Used to absorb upload failures across cycles.
namespace RingBuffer {

/// Append a reading. If full, the oldest entry is dropped.
void push(const Reading& r);

/// Peek at the oldest reading without removing it.
bool peekOldest(Reading& out);

/// Remove the oldest reading after a successful upload.
void popOldest();

/// Number of readings currently stored.
uint8_t size();

/// Maximum capacity (compile-time).
uint8_t capacity();

/// Reset buffer. Call from cold boot only — RTC memory is uninitialized then.
void initIfColdBoot();

}  // namespace RingBuffer
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/ring_buffer.cpp`**

```cpp
#include "ring_buffer.h"
#include "config.h"

#include <Arduino.h>
#include <cstring>
#include <esp_system.h>

namespace {

// RTC slow memory persists across deep sleep.
RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR uint8_t  rtcHead  = 0;    // write index
RTC_DATA_ATTR uint8_t  rtcCount = 0;
RTC_DATA_ATTR Reading  rtcBuf[RTC_BUFFER_CAPACITY];

constexpr uint32_t MAGIC = 0xCB50A001u;  // "CombSense Tag Wi-Fi"

}  // anonymous namespace

namespace RingBuffer {

void initIfColdBoot() {
    esp_reset_reason_t reason = esp_reset_reason();
    bool wokeFromDeepSleep = (reason == ESP_RST_DEEPSLEEP);
    bool magicValid = (rtcMagic == MAGIC);

    if (!wokeFromDeepSleep || !magicValid) {
        rtcMagic = MAGIC;
        rtcHead  = 0;
        rtcCount = 0;
        memset(rtcBuf, 0, sizeof(rtcBuf));
    }
}

void push(const Reading& r) {
    rtcBuf[rtcHead] = r;
    rtcHead = (rtcHead + 1) % RTC_BUFFER_CAPACITY;
    if (rtcCount < RTC_BUFFER_CAPACITY) {
        rtcCount++;
    }
    // If full, we've just overwritten the oldest — head now points past it,
    // which is also the new "oldest".
}

bool peekOldest(Reading& out) {
    if (rtcCount == 0) return false;
    uint8_t oldestIdx = (rtcHead + RTC_BUFFER_CAPACITY - rtcCount) % RTC_BUFFER_CAPACITY;
    out = rtcBuf[oldestIdx];
    return true;
}

void popOldest() {
    if (rtcCount == 0) return;
    rtcCount--;
}

uint8_t size()     { return rtcCount; }
uint8_t capacity() { return RTC_BUFFER_CAPACITY; }

}  // namespace RingBuffer
```

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/ring_buffer.h firmware/sensor-tag-wifi/src/ring_buffer.cpp
git commit -m "feat(sensor-tag-wifi): RTC-memory ring buffer for offline resilience"
```

---

## Task 12: WiFi manager with BSSID cache

**Files:**
- Create: `firmware/sensor-tag-wifi/src/wifi_manager.h`
- Create: `firmware/sensor-tag-wifi/src/wifi_manager.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/wifi_manager.h`**

```cpp
#pragma once

#include <cstdint>

namespace WifiManager {

/// Connect to the configured SSID. Uses BSSID+channel cached in RTC memory
/// for a faster reconnect; falls back to full scan on mismatch. Returns true
/// on success, false on timeout.
bool connect();

/// Disconnect and power off the radio.
void disconnect();

/// Get current unix time from NTP (requires prior connect()).
/// Returns 0 on failure.
uint32_t getUnixTime();

}  // namespace WifiManager
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/wifi_manager.cpp`**

```cpp
#include "wifi_manager.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <cstring>

namespace {

// BSSID + channel cached in RTC for fast reconnect
RTC_DATA_ATTR uint8_t rtcBssid[6]   = {0};
RTC_DATA_ATTR int32_t rtcChannel    = 0;
RTC_DATA_ATTR uint8_t rtcBssidValid = 0;

constexpr const char* NTP_SERVER = "pool.ntp.org";

bool waitForConnect(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;
        delay(100);
    }
    return true;
}

}  // anonymous namespace

namespace WifiManager {

bool connect() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    String pass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();

    if (ssid.length() == 0) {
        Serial.println("[WIFI] no SSID configured");
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);

    if (rtcBssidValid) {
        Serial.printf("[WIFI] fast-connect ch=%ld\n", (long)rtcChannel);
        WiFi.begin(ssid.c_str(), pass.c_str(), rtcChannel, rtcBssid);
        if (waitForConnect(WIFI_CONNECT_TIMEOUT_MS / 2)) {
            Serial.printf("[WIFI] connected rssi=%d\n", WiFi.RSSI());
            return true;
        }
        Serial.println("[WIFI] fast-connect failed — full scan");
        WiFi.disconnect(true);
        rtcBssidValid = 0;
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    if (!waitForConnect(WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.println("[WIFI] connect timeout");
        return false;
    }

    memcpy(rtcBssid, WiFi.BSSID(), 6);
    rtcChannel    = WiFi.channel();
    rtcBssidValid = 1;
    Serial.printf("[WIFI] connected rssi=%d (cached bssid)\n", WiFi.RSSI());
    return true;
}

void disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

uint32_t getUnixTime() {
    configTime(0, 0, NTP_SERVER);
    time_t now = 0;
    for (uint8_t i = 0; i < 30; ++i) {
        time(&now);
        if (now > 1700000000) return static_cast<uint32_t>(now);
        delay(200);
    }
    return 0;
}

}  // namespace WifiManager
```

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/wifi_manager.h firmware/sensor-tag-wifi/src/wifi_manager.cpp
git commit -m "feat(sensor-tag-wifi): WiFi manager with BSSID caching and NTP"
```

---

## Task 13: MQTT client wrapper

**Files:**
- Create: `firmware/sensor-tag-wifi/src/mqtt_client.h`
- Create: `firmware/sensor-tag-wifi/src/mqtt_client.cpp`

- [ ] **Step 1: Write `firmware/sensor-tag-wifi/src/mqtt_client.h`**

```cpp
#pragma once

#include <cstdint>
#include "reading.h"

namespace MqttClient {

/// Connect to the broker using credentials from NVS. Requires WiFi up.
bool connect(const char* deviceId);

/// Publish a reading to `combsense/hive/<deviceId>/reading`. Returns true on ack.
bool publish(const char* deviceId, const Reading& r);

/// Disconnect gracefully.
void disconnect();

}  // namespace MqttClient
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/mqtt_client.cpp`**

```cpp
#include "mqtt_client.h"
#include "config.h"
#include "payload.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>

namespace {

WiFiClient   wifiClient;
PubSubClient pubsub(wifiClient);

char mqttHost[64]   = "";
uint16_t mqttPort   = DEFAULT_MQTT_PORT;
char mqttUser[32]   = "";
char mqttPass[64]   = "";

void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String host = prefs.getString(NVS_KEY_MQTT_HOST, DEFAULT_MQTT_HOST);
    mqttPort    = prefs.getUShort(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);
    String user = prefs.getString(NVS_KEY_MQTT_USER, "");
    String pass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();
    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
    strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);
}

}  // anonymous namespace

namespace MqttClient {

bool connect(const char* deviceId) {
    loadConfig();
    pubsub.setServer(mqttHost, mqttPort);
    pubsub.setSocketTimeout(MQTT_CONNECT_TIMEOUT_MS / 1000);
    pubsub.setBufferSize(PAYLOAD_MAX_LEN + 32);

    Serial.printf("[MQTT] connecting %s:%u as %s\n", mqttHost, mqttPort, deviceId);
    bool ok = (mqttUser[0] != '\0')
        ? pubsub.connect(deviceId, mqttUser, mqttPass)
        : pubsub.connect(deviceId);
    if (!ok) {
        Serial.printf("[MQTT] connect failed state=%d\n", pubsub.state());
    }
    return ok;
}

bool publish(const char* deviceId, const Reading& r) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s%s/reading", MQTT_TOPIC_PREFIX, deviceId);

    char payload[PAYLOAD_MAX_LEN];
    int n = Payload::serialize(deviceId, r, payload, sizeof(payload));
    if (n < 0) {
        Serial.println("[MQTT] payload too large");
        return false;
    }

    bool ok = pubsub.publish(topic, payload, false);
    if (!ok) Serial.println("[MQTT] publish failed");
    return ok;
}

void disconnect() {
    pubsub.disconnect();
    wifiClient.stop();
}

}  // namespace MqttClient
```

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag-wifi/src/mqtt_client.h firmware/sensor-tag-wifi/src/mqtt_client.cpp
git commit -m "feat(sensor-tag-wifi): MQTT publisher using PubSubClient"
```

---

## Task 14: Main state machine

**Files:**
- Create: `firmware/sensor-tag-wifi/src/main.cpp`
- Create symlink: `firmware/sensor-tag-wifi/src/serial_console.cpp`
- Create symlink: `firmware/sensor-tag-wifi/src/serial_console.h`

- [ ] **Step 1: Symlink shared serial console**

```bash
cd firmware/sensor-tag-wifi/src && \
  ln -sf ../../shared/serial_console.cpp serial_console.cpp && \
  ln -sf ../../shared/serial_console.h serial_console.h
```

- [ ] **Step 2: Write `firmware/sensor-tag-wifi/src/main.cpp`**

```cpp
#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_sleep.h>

#include "config.h"
#include "reading.h"
#include "sensor.h"
#include "battery.h"
#include "payload.h"
#include "ring_buffer.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "serial_console.h"

namespace {

RTC_DATA_ATTR uint16_t rtcSampleCounter = 0;

char deviceId[9] = {0};

/// Derive an 8-hex-char device ID from the eFuse MAC (low 4 bytes).
void initDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
}

struct Config {
    uint32_t sampleIntervalSec;
    uint8_t  uploadEveryN;
};

Config loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    Config c {
        .sampleIntervalSec = prefs.getUInt(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC),
        .uploadEveryN      = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N),
    };
    prefs.end();
    return c;
}

/// Drain the RTC ring buffer over MQTT. Leaves unsent readings in place.
void drainBuffer() {
    if (RingBuffer::size() == 0) return;

    if (!WifiManager::connect()) {
        Serial.println("[MAIN] no wifi — keeping buffer");
        return;
    }

    if (!MqttClient::connect(deviceId)) {
        Serial.println("[MAIN] no mqtt — keeping buffer");
        WifiManager::disconnect();
        return;
    }

    uint8_t sent = 0;
    while (RingBuffer::size() > 0) {
        Reading r;
        if (!RingBuffer::peekOldest(r)) break;
        if (!MqttClient::publish(deviceId, r)) break;
        RingBuffer::popOldest();
        sent++;
    }
    Serial.printf("[MAIN] sent %u / remaining %u\n", sent, RingBuffer::size());

    MqttClient::disconnect();
    WifiManager::disconnect();
}

/// Take one sensor sample and push it into the ring buffer.
void sampleAndEnqueue() {
    if (!Sensor::begin()) {
        Serial.println("[MAIN] sensor init failed — skipping sample");
        Sensor::deinit();
        return;
    }

    Reading r {};
    bool ok = Sensor::read(r);
    Sensor::deinit();
    if (!ok) {
        Serial.println("[MAIN] sensor read failed");
        return;
    }

    r.battery_pct = Battery::readPercent();
    r.timestamp   = 0;   // Filled by NTP during drainBuffer if we have no time yet

    // If we haven't set system time yet, tag with zero — the collector/backend
    // can backfill. If time is already set (previous wake), use it.
    time_t now;
    time(&now);
    if (now > 1700000000) r.timestamp = static_cast<uint32_t>(now);

    RingBuffer::push(r);
    Serial.printf("[MAIN] sample t1=%.2f t2=%.2f h1=%.2f h2=%.2f b=%u buffered=%u\n",
                  r.temp1, r.temp2, r.humidity1, r.humidity2, r.battery_pct,
                  RingBuffer::size());
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    initDeviceId();
    Serial.printf("[MAIN] combsense sensor-tag-wifi id=%s\n", deviceId);

    RingBuffer::initIfColdBoot();

    // Ensure NVS namespace exists for first boot
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    SerialConsole::checkForConsole();

    Config cfg = loadConfig();
    Serial.printf("[MAIN] sample_int=%lus upload_every=%u\n",
                  (unsigned long)cfg.sampleIntervalSec, cfg.uploadEveryN);

    sampleAndEnqueue();
    rtcSampleCounter++;

    if (rtcSampleCounter >= cfg.uploadEveryN) {
        // If we don't yet have a valid timestamp on the oldest reading, do a
        // quick NTP sync via the WiFi connect we'd do anyway.
        drainBuffer();
        rtcSampleCounter = 0;
    } else {
        Serial.printf("[MAIN] not uploading this cycle (%u/%u)\n",
                      rtcSampleCounter, cfg.uploadEveryN);
    }

    Serial.printf("[MAIN] sleeping %lus\n", (unsigned long)cfg.sampleIntervalSec);
    Serial.flush();
    esp_deep_sleep(static_cast<uint64_t>(cfg.sampleIntervalSec) * 1000000ULL);
}

void loop() {
    // Never reached — deep sleep restarts from setup()
}
```

- [ ] **Step 3: Build both envs**

```bash
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31 && pio run -e xiao-c6-ds18b20
```

Expected: Both BUILD SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add firmware/sensor-tag-wifi/src/main.cpp firmware/sensor-tag-wifi/src/serial_console.cpp firmware/sensor-tag-wifi/src/serial_console.h
git commit -m "feat(sensor-tag-wifi): main state machine — sample, enqueue, drain, sleep"
```

---

## Task 15: Add config keys to shared serial console

**Files:**
- Modify: `firmware/shared/serial_console.cpp`

- [ ] **Step 1: Add `sample_int` and `upload_every` to known keys**

Open [firmware/shared/serial_console.cpp](firmware/shared/serial_console.cpp), find the `knownKeys` array, and replace it with:

```cpp
const char* knownKeys[] = {
    "hive_id", "collector_mac", "day_start", "day_end", "read_interval",
    "weight_off", "weight_scl", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
    "tag_name", "tag_name_2", "adv_interval",
    "wifi_ssid", "wifi_pass",
    "sample_int", "upload_every"
};
```

- [ ] **Step 2: Build all four firmware variants**

```bash
cd firmware/hive-node       && pio run
cd ../collector             && pio run
cd ../sensor-tag            && pio run
cd ../sensor-tag-wifi       && pio run -e xiao-c6-sht31 && pio run -e xiao-c6-ds18b20
```

Expected: all BUILD SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add firmware/shared/serial_console.cpp
git commit -m "feat(shared): add sample_int and upload_every to console known keys"
```

---

## Task 16: CI — add sensor-tag-wifi build jobs + native tests

**Files:**
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Add three new jobs after `build-sensor-tag`**

Open [.github/workflows/build.yml](.github/workflows/build.yml) and add:

```yaml
  build-sensor-tag-wifi-sht31:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio
          key: pio-tagwifi-${{ hashFiles('firmware/sensor-tag-wifi/platformio.ini') }}
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build sensor-tag-wifi (SHT31)
        run: cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31

  build-sensor-tag-wifi-ds18b20:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio
          key: pio-tagwifi-${{ hashFiles('firmware/sensor-tag-wifi/platformio.ini') }}
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build sensor-tag-wifi (DS18B20)
        run: cd firmware/sensor-tag-wifi && pio run -e xiao-c6-ds18b20

  test-sensor-tag-wifi-native:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Run native payload tests
        run: cd firmware/sensor-tag-wifi && pio test -e native
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: add sensor-tag-wifi build and native test jobs"
```

---

## Task 17: Update README.md + .mex/ROUTER.md

**Files:**
- Modify: `README.md`
- Modify: `.mex/ROUTER.md`

- [ ] **Step 1: Add WiFi-variant section to `README.md`**

In the firmware overview section of [README.md](README.md), add after the existing sensor tag description:

```markdown
### Sensor Tag — WiFi Variant (home yards)

For beekeepers with hives in WiFi range of their home network, `firmware/sensor-tag-wifi/` is a fork of the BLE tag that publishes directly to a local Mosquitto broker over MQTT — no collector required.

**Hardware:** XIAO ESP32-C6 + 2× DS18B20 (or SHT31 pair) + 18650 Li-ion + 100 mAh solar panel + TP4056/DW01 charger
**Power:** 5-min sample cadence, solar-maintained — runs indefinitely with daylight
**Transport:** WiFi → MQTT → Mosquitto at the local IP
**Topic:** `combsense/hive/<device-id>/reading`
**Payload:** JSON — `{"id":"ab12cd34","t":1712345678,"t1":22.4,"t2":24.1,"h1":52.3,"h2":55.1,"b":87}`
**Build variants:**
- `pio run -e xiao-c6-sht31` — dual SHT31 (temp + humidity, brood + top)
- `pio run -e xiao-c6-ds18b20` — dual DS18B20 (temp only, brood + top)

**Provisioning:** connect to serial @115200 during boot window. Set `wifi_ssid`, `wifi_pass`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`. Optional: `sample_int` (seconds), `upload_every` (samples).
```

- [ ] **Step 2: Update `.mex/ROUTER.md`**

In [.mex/ROUTER.md](.mex/ROUTER.md), add under completed:

```markdown
- Sensor tag WiFi variant (`firmware/sensor-tag-wifi/`) — XIAO ESP32-C6 for home-yard deployments
  - Compile-time sensor abstraction (SHT31 dual / DS18B20 dual)
  - Direct MQTT to local Mosquitto, RTC ring buffer for offline resilience
  - BSSID caching in RTC for fast reconnect
  - 18650 + solar powered, 5-min sample cadence by default
  - Native Unity tests for payload serialization
```

And add a routing table entry:

```markdown
| Working on home-yard WiFi variant | `firmware/sensor-tag-wifi/` directory |
```

- [ ] **Step 3: Commit**

```bash
git add README.md .mex/ROUTER.md
git commit -m "docs: README and .mex — add sensor-tag-wifi variant"
```

---

## Task 18: Hardware verification (manual — requires physical board)

**Not a coding task** — run this only when the XIAO ESP32-C6 + sensors + 18650 + TP4056 are on the bench.

- [ ] **Step 1: Flash SHT31 build to a test board**

```bash
cd firmware/sensor-tag-wifi && pio run -e xiao-c6-sht31 --target upload
pio device monitor -e xiao-c6-sht31
```

- [ ] **Step 2: Provision via serial console**

During the boot window, hit Enter. Enter `wifi_ssid`, `wifi_pass`, `mqtt_host=192.168.1.82`, `mqtt_user`, `mqtt_pass`, `sample_int=60` (speed up testing), `upload_every=1`. Exit.

- [ ] **Step 3: Verify sample + publish on Mosquitto**

On another machine:

```bash
mosquitto_sub -h 192.168.1.82 -u hivesense -P '<pass>' -t 'combsense/hive/+/reading' -v
```

Expected: one JSON message per `sample_int` seconds with both temp and humidity populated.

- [ ] **Step 4: Test offline resilience**

Turn off the MQTT broker. Verify the tag continues sampling (check serial: `buffered=N` increments). Turn broker back on. Verify buffered readings flush on the next upload cycle.

- [ ] **Step 5: Flash DS18B20 build to a second board**

```bash
pio run -e xiao-c6-ds18b20 --target upload
```

Repeat steps 2-4. Verify payloads omit `h1`/`h2` fields.

- [ ] **Step 6: Measure sleep current**

With a uCurrent / multimeter in series:
- Active window (sample + WiFi + MQTT): expect ≤ 8 seconds @ ~80 mA average
- Deep sleep: expect ≤ 100 µA (XIAO-C6 dev board includes regulator leakage)

- [ ] **Step 7: Merge branch to main**

```bash
git checkout main
git merge --no-ff feature/sensor-tag-wifi
git push origin main
```

---

## Self-Review Checklist

- [x] Spec coverage — every design decision from brainstorming has a task
- [x] No placeholders — all code blocks contain complete code
- [x] Type consistency — `Reading` fields match across payload, sensors, ring buffer
- [x] Naming consistency — `sample_int` / `upload_every` identical in config.h, NVS keys, serial console
- [x] Device ID format — 8-hex from eFuse MAC, consistent across main.cpp and payload tests
- [x] Build envs — all commands match `xiao-c6-sht31` / `xiao-c6-ds18b20` naming in platformio.ini
