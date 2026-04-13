# Wireless Sensor Tag — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESP32-C6 BLE sensor tag that broadcasts temp/humidity from inside the hive, and add a BLE scanner to the hive node to receive it.

**Architecture:** The sensor tag is a minimal firmware — wake, read SHT31, advertise BLE, deep sleep. The hive node gets a new `ble_tag_reader` module that scans for the tag by name during SENSOR_READ and populates the internal temp/humidity payload fields.

**Tech Stack:** PlatformIO, Arduino framework, ESP32-C6 (XIAO), NimBLE, Adafruit SHT31 Library

**Spec:** `docs/superpowers/specs/2026-04-12-sensor-tag-design.md`

**Ollama delegation:** Use Ollama for Tasks 1-2 (boilerplate scaffold/config). Review all output before committing.

---

## File Map

### New (firmware/sensor-tag/)
- `platformio.ini` — XIAO ESP32C6 board config
- `include/config.h` — Tag name, I2C pins, advertisement interval
- `src/main.cpp` — Wake, read, advertise, sleep loop

### New (firmware/hive-node/src/)
- `ble_tag_reader.h` — BLE scanner namespace declaration
- `ble_tag_reader.cpp` — Scan for tag, parse manufacturer data

### Modified
- `firmware/hive-node/platformio.ini` — Enable NimBLE observer role
- `firmware/hive-node/src/state_machine.cpp` — Call BLE tag reader during SENSOR_READ
- `firmware/hive-node/src/comms_ble.cpp` — Ensure scanner doesn't conflict with GATT server
- `.github/workflows/build.yml` — Add sensor-tag build job

### Shared (reused)
- `firmware/shared/serial_console.h/.cpp` — Provisioning for tag_name
- Symlink `serial_console.cpp` into sensor-tag/src/

---

## Task 1: Sensor Tag PlatformIO Scaffold

**Files:**
- Create: `firmware/sensor-tag/platformio.ini`
- Create: `firmware/sensor-tag/include/config.h`

- [ ] **Step 1: Create `firmware/sensor-tag/platformio.ini`**

```ini
[env:xiao-c6]
platform = espressif32@^6.5.0
board = seeed_xiao_esp32c6
framework = arduino
monitor_speed = 115200

lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    h2zero/NimBLE-Arduino@^1.4.0

lib_ldf_mode = deep+

build_flags =
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
```

- [ ] **Step 2: Create `firmware/sensor-tag/include/config.h`**

```cpp
#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Seeed XIAO ESP32C6
// =============================================================================

// I2C (SHT31)
constexpr uint8_t PIN_I2C_SDA = 4;   // D4
constexpr uint8_t PIN_I2C_SCL = 5;   // D5

// =============================================================================
// BLE Advertisement
// =============================================================================

constexpr uint16_t MANUFACTURER_ID     = 0xFFFF;  // Prototyping / unregistered
constexpr uint8_t  TAG_PROTOCOL_VERSION = 0x01;
constexpr uint16_t DEFAULT_ADV_INTERVAL_SEC = 60;
constexpr uint16_t ADV_DURATION_MS     = 200;

// =============================================================================
// NVS
// =============================================================================

constexpr const char* NVS_NAMESPACE    = "hivesense";
constexpr const char* NVS_KEY_TAG_NAME = "tag_name";
constexpr const char* NVS_KEY_ADV_INT  = "adv_interval";

// =============================================================================
// SHT31
// =============================================================================

constexpr uint8_t SHT31_ADDR = 0x44;
```

- [ ] **Step 3: Create stub `firmware/sensor-tag/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("[TAG] HiveSense Sensor Tag — starting");
}

void loop() {
    delay(1000);
}
```

- [ ] **Step 4: Symlink shared serial console**

```bash
cd firmware/sensor-tag/src && ln -sf ../../shared/serial_console.cpp serial_console.cpp
```

- [ ] **Step 5: Verify build**

Run: `cd firmware/sensor-tag && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 6: Commit**

```bash
git add firmware/sensor-tag/
git commit -m "feat: scaffold sensor tag firmware for XIAO ESP32C6"
```

---

## Task 2: Sensor Tag Main Firmware

**Files:**
- Modify: `firmware/sensor-tag/src/main.cpp`

- [ ] **Step 1: Write the full sensor tag firmware**

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "config.h"
#include "serial_console.h"

namespace {

Adafruit_SHT31 sht;
char tagName[32] = "HiveSense-Tag-001";
uint16_t advIntervalSec = DEFAULT_ADV_INTERVAL_SEC;

/// Load tag configuration from NVS.
void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String name = prefs.getString(NVS_KEY_TAG_NAME, "HiveSense-Tag-001");
    advIntervalSec = prefs.getUShort(NVS_KEY_ADV_INT, DEFAULT_ADV_INTERVAL_SEC);
    prefs.end();

    strncpy(tagName, name.c_str(), sizeof(tagName) - 1);
}

/// Read battery voltage via ADC and estimate percentage.
/// XIAO C6 can read its own battery voltage on a specific pin.
uint8_t readBatteryPercent() {
    // Simplified — CR2032 ranges 3.0V (full) to 2.0V (empty)
    uint32_t mv = analogReadMilliVolts(A0);
    if (mv >= 3000) return 100;
    if (mv <= 2000) return 0;
    return static_cast<uint8_t>((mv - 2000) * 100 / 1000);
}

/// Pack sensor data into BLE manufacturer-specific advertisement.
void advertiseData(float tempC, float humidity, uint8_t batteryPct) {
    NimBLEDevice::init(tagName);

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();

    // Build manufacturer data: ID(2) + version(1) + temp(2) + humidity(2) + battery(1) = 8 bytes
    uint8_t mfgData[8];

    // Manufacturer ID (little-endian)
    mfgData[0] = MANUFACTURER_ID & 0xFF;
    mfgData[1] = (MANUFACTURER_ID >> 8) & 0xFF;

    // Version
    mfgData[2] = TAG_PROTOCOL_VERSION;

    // Temperature: (tempC + 40.0) * 100 as int16
    int16_t rawTemp = static_cast<int16_t>((tempC + 40.0f) * 100.0f);
    mfgData[3] = rawTemp & 0xFF;
    mfgData[4] = (rawTemp >> 8) & 0xFF;

    // Humidity: humidity * 100 as uint16
    uint16_t rawHum = static_cast<uint16_t>(humidity * 100.0f);
    mfgData[5] = rawHum & 0xFF;
    mfgData[6] = (rawHum >> 8) & 0xFF;

    // Battery
    mfgData[7] = batteryPct;

    // Set manufacturer data — NimBLE expects the data WITHOUT the manufacturer ID prefix
    // since it adds it from the first 2 bytes automatically
    std::string mfgString(reinterpret_cast<char*>(mfgData), sizeof(mfgData));

    NimBLEAdvertisementData advData;
    advData.setName(tagName);
    advData.setManufacturerData(mfgString);

    advertising->setAdvertisementData(advData);
    advertising->start();

    delay(ADV_DURATION_MS);

    advertising->stop();
    NimBLEDevice::deinit(true);
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    // Ensure NVS namespace exists
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    SerialConsole::checkForConsole();

    loadConfig();
    Serial.printf("[TAG] Name: %s, Interval: %us\n", tagName, advIntervalSec);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!sht.begin(SHT31_ADDR)) {
        Serial.println("[TAG] SHT31 not found — sleeping");
        esp_deep_sleep(static_cast<uint64_t>(advIntervalSec) * 1000000ULL);
    }

    float temp = sht.readTemperature();
    float hum  = sht.readHumidity();
    uint8_t batt = readBatteryPercent();

    if (!isnan(temp) && !isnan(hum)) {
        Serial.printf("[TAG] T=%.1fC H=%.1f%% B=%u%%\n", temp, hum, batt);
        advertiseData(temp, hum, batt);
    } else {
        Serial.println("[TAG] SHT31 read failed");
    }

    Serial.printf("[TAG] Sleeping %us\n", advIntervalSec);
    Serial.flush();
    esp_deep_sleep(static_cast<uint64_t>(advIntervalSec) * 1000000ULL);
}

void loop() {
    // Never reached — deep sleep restarts from setup()
}
```

- [ ] **Step 2: Verify build**

Run: `cd firmware/sensor-tag && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/sensor-tag/src/main.cpp
git commit -m "feat: add sensor tag firmware — read SHT31, BLE advertise, deep sleep"
```

---

## Task 3: Hive Node — Enable NimBLE Observer

**Files:**
- Modify: `firmware/hive-node/platformio.ini`

- [ ] **Step 1: Enable observer role**

In `firmware/hive-node/platformio.ini`, change:
```
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
```
to:
```
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER
```

- [ ] **Step 2: Clean build to pick up NimBLE config change**

Run: `cd firmware/hive-node && pio run --target clean && pio run`
Expected: BUILD SUCCESS. Flash usage increases by ~50-100 KB.

- [ ] **Step 3: Commit**

```bash
git add firmware/hive-node/platformio.ini
git commit -m "feat: enable NimBLE observer role for BLE tag scanning"
```

---

## Task 4: Hive Node — BLE Tag Reader Module

**Files:**
- Create: `firmware/hive-node/src/ble_tag_reader.h`
- Create: `firmware/hive-node/src/ble_tag_reader.cpp`

- [ ] **Step 1: Create `firmware/hive-node/src/ble_tag_reader.h`**

```cpp
#pragma once

#include <cstdint>

/// Scans for a HiveSense wireless sensor tag via BLE advertisement.
/// Parses manufacturer data to extract temperature and humidity.
namespace BleTagReader {

    /// Scan for the configured tag name. Blocks for timeoutMs.
    /// Returns true if tag was found and data populated.
    bool scan(uint16_t timeoutMs);

    /// Get last received temperature (°C). Returns NAN if no tag found.
    float getTemperature();

    /// Get last received humidity (%RH). Returns NAN if no tag found.
    float getHumidity();

    /// Get last received battery percentage. Returns 0 if no tag found.
    uint8_t getBattery();

}  // namespace BleTagReader
```

- [ ] **Step 2: Create `firmware/hive-node/src/ble_tag_reader.cpp`**

```cpp
#include "ble_tag_reader.h"
#include "config.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <cstring>
#include <cmath>

namespace {

constexpr uint16_t MANUFACTURER_ID = 0xFFFF;
constexpr uint8_t  TAG_PROTOCOL_VERSION = 0x01;
constexpr uint8_t  MFG_DATA_LENGTH = 8;

float   lastTemp    = NAN;
float   lastHum     = NAN;
uint8_t lastBattery = 0;
bool    tagFound    = false;

char targetTagName[32] = "";

/// Parse manufacturer data from a BLE advertisement.
bool parseMfgData(const std::string& mfgData) {
    if (mfgData.length() < MFG_DATA_LENGTH) return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(mfgData.data());

    // Check manufacturer ID
    uint16_t mfgId = d[0] | (d[1] << 8);
    if (mfgId != MANUFACTURER_ID) return false;

    // Check version
    if (d[2] != TAG_PROTOCOL_VERSION) return false;

    // Decode temperature: (raw / 100.0) - 40.0
    int16_t rawTemp = d[3] | (d[4] << 8);
    lastTemp = (rawTemp / 100.0f) - 40.0f;

    // Decode humidity: raw / 100.0
    uint16_t rawHum = d[5] | (d[6] << 8);
    lastHum = rawHum / 100.0f;

    // Battery
    lastBattery = d[7];

    return true;
}

/// NimBLE scan callback — checks each advertisement for our tag.
class TagScanCallbacks : public NimBLEScanCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) override {
        if (strlen(targetTagName) == 0) return;

        // Filter by device name
        if (!device->haveName()) return;
        if (strcmp(device->getName().c_str(), targetTagName) != 0) return;

        // Check for manufacturer data
        if (!device->haveManufacturerData()) return;

        std::string mfgData = device->getManufacturerData();
        if (parseMfgData(mfgData)) {
            tagFound = true;
            Serial.printf("[TAGREADER] Found %s: T=%.1fC H=%.1f%% B=%u%%\n",
                          targetTagName, lastTemp, lastHum, lastBattery);

            // Stop scanning — we got what we need
            NimBLEDevice::getScan()->stop();
        }
    }
};

TagScanCallbacks scanCallbacks;

}  // anonymous namespace

namespace BleTagReader {

bool scan(uint16_t timeoutMs) {
    // Load tag name from NVS
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String name = prefs.getString("tag_name", "");
    prefs.end();

    if (name.length() == 0) {
        Serial.println("[TAGREADER] No tag_name configured — skipping scan");
        return false;
    }

    strncpy(targetTagName, name.c_str(), sizeof(targetTagName) - 1);

    // Reset state
    tagFound    = false;
    lastTemp    = NAN;
    lastHum     = NAN;
    lastBattery = 0;

    // Initialize BLE for scanning
    NimBLEDevice::init("");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks);
    scan->setActiveScan(false);  // Passive scan — lower power
    scan->setInterval(100);      // 100ms scan interval
    scan->setWindow(100);        // 100ms scan window (continuous)

    Serial.printf("[TAGREADER] Scanning for '%s' (%ums)...\n", targetTagName, timeoutMs);

    scan->start(timeoutMs / 1000, false);  // Blocking scan, duration in seconds

    scan->clearResults();
    NimBLEDevice::deinit(true);

    if (!tagFound) {
        Serial.println("[TAGREADER] Tag not found");
    }

    return tagFound;
}

float getTemperature() { return lastTemp; }
float getHumidity()    { return lastHum; }
uint8_t getBattery()   { return lastBattery; }

}  // namespace BleTagReader
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/hive-node/src/ble_tag_reader.h firmware/hive-node/src/ble_tag_reader.cpp
git commit -m "feat: add BLE tag reader — scans for sensor tag advertisements"
```

---

## Task 5: Hive Node — State Machine Integration

**Files:**
- Modify: `firmware/hive-node/src/state_machine.cpp`

- [ ] **Step 1: Add include**

At the top of `state_machine.cpp`, add:
```cpp
#include "ble_tag_reader.h"
```

- [ ] **Step 2: Add BLE tag scan in SENSOR_READ state**

In the `SENSOR_READ` case, after the battery read and before the SHT31 read, add:

```cpp
            // Read internal temp/humidity from wireless sensor tag
            if (BleTagReader::scan(5000)) {
                payload.temp_internal = BleTagReader::getTemperature();
                payload.humidity_internal = BleTagReader::getHumidity();
                Serial.printf("[SM] Tag data: T=%.1fC H=%.1f%%\n",
                              payload.temp_internal, payload.humidity_internal);
            }
```

The SHT31 code that follows fills `temp_external` and `humidity_external`. If the tag scan found data, it overwrites `temp_internal` and `humidity_internal` which may also be written by SHT31. To avoid the wired internal sensor overwriting the tag data, wrap the SHT31 internal values in a guard:

After `SensorSHT31::readMeasurements(payload)`, add:
```cpp
            // If tag provided internal readings, keep them — SHT31 provides external only
            if (!isnan(BleTagReader::getTemperature())) {
                payload.temp_internal = BleTagReader::getTemperature();
                payload.humidity_internal = BleTagReader::getHumidity();
            }
```

- [ ] **Step 3: Handle BLE conflict with GATT server**

The BLE tag scan runs `NimBLEDevice::init/deinit` which could conflict with the BLE GATT server in `BLE_CHECK` state. Since the tag scan runs during `SENSOR_READ` and the GATT server runs during `BLE_CHECK` (a later state), they don't overlap. The `deinit(true)` in the tag reader cleans up fully before the GATT server initializes. No code change needed — just verify both work in sequence.

- [ ] **Step 4: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add firmware/hive-node/src/state_machine.cpp
git commit -m "feat: integrate BLE tag reader into sensor read cycle"
```

---

## Task 6: Add tag_name to Serial Console Known Keys

**Files:**
- Modify: `firmware/shared/serial_console.cpp`

- [ ] **Step 1: Add tag_name and adv_interval to known keys list**

In `serial_console.cpp`, update the `knownKeys` array:

```cpp
const char* knownKeys[] = {
    "hive_id", "collector_mac", "day_start", "day_end", "read_interval",
    "weight_off", "weight_scl", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
    "tag_name", "adv_interval"
};
constexpr uint8_t NUM_KNOWN_KEYS = sizeof(knownKeys) / sizeof(knownKeys[0]);
```

- [ ] **Step 2: Verify both firmwares build**

Run: `cd firmware/hive-node && pio run`
Run: `cd firmware/sensor-tag && pio run`
Expected: Both BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/shared/serial_console.cpp
git commit -m "feat: add tag_name and adv_interval to serial console known keys"
```

---

## Task 7: Update CI for Third Firmware

**Files:**
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Add sensor-tag build job**

Add a new job after `build-collector`:

```yaml
  build-sensor-tag:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio
          key: pio-tag-${{ hashFiles('firmware/sensor-tag/platformio.ini') }}

      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install PlatformIO
        run: pip install platformio

      - name: Build sensor-tag firmware
        run: cd firmware/sensor-tag && pio run
```

Also update the `paths` filter at the top to include `firmware/**` (already does).

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: add sensor tag build to CI workflow"
```

---

## Task 8: Integration Build & Verify

- [ ] **Step 1: Clean build all three firmwares**

```bash
cd firmware/hive-node && pio run --target clean && pio run
cd ../collector && pio run --target clean && pio run
cd ../sensor-tag && pio run --target clean && pio run
```

Expected: All three BUILD SUCCESS.

- [ ] **Step 2: Verify hive node flash usage**

Note the flash increase from enabling NimBLE observer. Should still be well under 3.5 MB.

- [ ] **Step 3: Verify all files present**

```bash
find firmware/sensor-tag -name "*.cpp" -o -name "*.h" | grep -v ".pio" | sort
```

Expected:
```
firmware/sensor-tag/include/config.h
firmware/sensor-tag/src/main.cpp
firmware/sensor-tag/src/serial_console.cpp (symlink)
```

- [ ] **Step 4: Commit and push**

```bash
git add -A firmware/
git commit -m "feat: complete sensor tag firmware and hive node BLE scanner integration"
git push origin main
```

---

## Task 9: Update Project State

**Files:**
- Modify: `.mex/ROUTER.md`
- Modify: `.mex/context/decisions.md`

- [ ] **Step 1: Update ROUTER.md**

Add sensor tag to completed section. Update routing table with `firmware/sensor-tag/` entry.

- [ ] **Step 2: Add decisions**

Add entries for:
- ESP32-C6 over C3 for sensor tag (better deep sleep on dev board, BLE 5.3)
- DIY sensor tag over BroodMinder T2SM ($10 vs $48, open protocol, repairable)
- BLE advertisement for internal sensor data (no wires into hive)

- [ ] **Step 3: Commit and push**

```bash
git add .mex/
git commit -m "chore: update .mex — sensor tag firmware complete"
git push origin main
```
