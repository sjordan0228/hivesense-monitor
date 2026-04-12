# Hive Node Firmware Phase 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working ESP32-WROOM-32 hive node firmware with state machine power management, SHT31 temp/humidity, HX711 weight, ESP-NOW transmission, BLE GATT server, and LittleFS storage.

**Architecture:** Modular design with central state machine dispatcher. Each sensor/subsystem is a standalone module exposing `initialize()`, `readMeasurements()`, `enterSleep()`. Modules interact only through a shared `HivePayload` struct. Power management is baked into the state machine — every transition explicitly controls which peripherals are powered.

**Tech Stack:** PlatformIO, Arduino framework, espressif/arduino-esp32 v2.x, LittleFS, ESP-NOW, ESP32 BLE Arduino, Adafruit SHT31 Library, HX711 (bogde)

**Spec:** `docs/superpowers/specs/2026-04-11-hive-node-firmware-phase1-design.md`

**Ollama delegation:** Use the Ollama server (http://192.168.1.16:11434, model qwen3-coder:30b) to generate boilerplate, sensor module implementations, and scaffolding. Review all output against coding conventions (`.mex/context/conventions.md`) before committing.

---

## File Map

```
firmware/
├── platformio.ini                 — Build config, board, libs, partitions
├── include/
│   ├── config.h                   — Pin definitions, timing constants, BLE UUIDs
│   ├── types.h                    — HivePayload struct, enums, NodeState
│   └── module.h                   — Module interface documentation (no base class)
├── src/
│   ├── main.cpp                   — setup(), loop(), state machine dispatcher
│   ├── state_machine.cpp          — State transition logic, RTC time tracking
│   ├── state_machine.h            — State enum, transition function declarations
│   ├── power_manager.cpp          — Deep sleep, light sleep, MOSFET helpers
│   ├── power_manager.h            — Power function declarations
│   ├── sensor_sht31.cpp           — SHT31 init, read (internal + external), sleep
│   ├── sensor_sht31.h             — SHT31 namespace declarations
│   ├── sensor_hx711.cpp           — HX711 init, read with tare/calibration, sleep
│   ├── sensor_hx711.h             — HX711 namespace declarations
│   ├── comms_espnow.cpp           — ESP-NOW init, send with retry, shutdown
│   ├── comms_espnow.h             — ESP-NOW namespace declarations
│   ├── comms_ble.cpp              — BLE GATT server, characteristics, callbacks
│   ├── comms_ble.h                — BLE namespace declarations
│   ├── storage.cpp                — LittleFS circular buffer read/write/clear
│   ├── storage.h                  — Storage namespace declarations
│   ├── battery.cpp                — ADC read, voltage-to-percentage conversion
│   └── battery.h                  — Battery namespace declarations
└── test/
    └── (PlatformIO native tests where applicable)
```

---

## Task 1: PlatformIO Project Scaffold

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/include/config.h`
- Create: `firmware/include/types.h`
- Create: `firmware/include/module.h`
- Create: `firmware/src/main.cpp`

- [ ] **Step 1: Create `firmware/platformio.ini`**

```ini
[env:esp32dev]
platform = espressif32@^6.5.0
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
board_build.partitions = default.csv

lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    bogde/HX711@^0.7.5

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_ARDUHAL_LOG_DEFAULT_LEVEL=3
```

- [ ] **Step 2: Create `firmware/include/config.h`**

```cpp
#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — ESP32-WROOM-32
// =============================================================================

// I2C bus (SHT31 x2)
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// HX711 weight ADC
constexpr uint8_t PIN_HX711_DOUT = 16;
constexpr uint8_t PIN_HX711_CLK  = 17;
constexpr uint8_t PIN_MOSFET_HX711 = 18;

// Battery ADC (ADC1_CH6, input-only)
constexpr uint8_t PIN_BATTERY_ADC = 34;

// Status LED
constexpr uint8_t PIN_STATUS_LED = 2;

// Phase 2 — IR array (reserved, do not use in Phase 1)
constexpr uint8_t PIN_MUX_S0     = 25;
constexpr uint8_t PIN_MUX_S1     = 26;
constexpr uint8_t PIN_MUX_S2     = 27;
constexpr uint8_t PIN_MUX_S3     = 14;
constexpr uint8_t PIN_MUX_EN_TX  = 32;
constexpr uint8_t PIN_MUX_EN_RX  = 33;
constexpr uint8_t PIN_MUX_SIG_TX = 4;
constexpr uint8_t PIN_MUX_SIG_RX = 35;
constexpr uint8_t PIN_MOSFET_IR  = 19;

// =============================================================================
// I2C Addresses
// =============================================================================

constexpr uint8_t SHT31_ADDR_INTERNAL = 0x44;
constexpr uint8_t SHT31_ADDR_EXTERNAL = 0x45;

// =============================================================================
// Timing Constants
// =============================================================================

constexpr uint8_t  DEFAULT_DAY_START_HOUR    = 6;   // 6 AM
constexpr uint8_t  DEFAULT_DAY_END_HOUR      = 20;  // 8 PM
constexpr uint8_t  DEFAULT_READ_INTERVAL_MIN = 30;
constexpr uint16_t MOSFET_STABILIZE_MS       = 100;
constexpr uint16_t BLE_ADVERTISE_TIMEOUT_MS  = 5000;
constexpr uint8_t  ESPNOW_MAX_RETRIES        = 3;
constexpr uint16_t ESPNOW_RETRY_DELAY_MS     = 2000;

// =============================================================================
// Storage
// =============================================================================

constexpr uint16_t MAX_STORED_READINGS = 500;

// =============================================================================
// BLE UUIDs — derived from "HiveSense" ASCII
// =============================================================================

#define BLE_SERVICE_UUID        "4E6F7200-7468-6976-6553-656E73650000"
#define BLE_CHAR_SENSOR_LOG     "4E6F7200-7468-6976-6553-656E73650001"
#define BLE_CHAR_READING_COUNT  "4E6F7200-7468-6976-6553-656E73650002"
#define BLE_CHAR_HIVE_ID        "4E6F7200-7468-6976-6553-656E73650003"
#define BLE_CHAR_CLEAR_LOG      "4E6F7200-7468-6976-6553-656E73650004"

// =============================================================================
// NVS Keys
// =============================================================================

#define NVS_NAMESPACE       "hivesense"
#define NVS_KEY_HIVE_ID     "hive_id"
#define NVS_KEY_COLLECTOR   "collector_mac"
#define NVS_KEY_DAY_START   "day_start"
#define NVS_KEY_DAY_END     "day_end"
#define NVS_KEY_INTERVAL    "read_interval"
#define NVS_KEY_WEIGHT_OFF  "weight_off"
#define NVS_KEY_WEIGHT_SCL  "weight_scl"

// =============================================================================
// Payload Version
// =============================================================================

constexpr uint8_t PAYLOAD_VERSION = 1;
```

- [ ] **Step 3: Create `firmware/include/types.h`**

```cpp
#pragma once

#include <cstdint>

/// Sensor payload transmitted via ESP-NOW and stored in LittleFS.
/// All fields are populated during SENSOR_READ state.
/// Phase 2 bee count fields are zeroed in Phase 1.
struct HivePayload {
    uint8_t  version;            // Payload format version
    char     hive_id[16];        // Null-terminated hive identifier
    uint32_t timestamp;          // Unix epoch seconds
    float    weight_kg;
    float    temp_internal;      // Celsius — SHT31 at 0x44
    float    temp_external;      // Celsius — SHT31 at 0x45
    float    humidity_internal;  // %RH
    float    humidity_external;  // %RH
    uint16_t bees_in;            // Phase 2 — zeroed in Phase 1
    uint16_t bees_out;           // Phase 2 — zeroed in Phase 1
    uint16_t bees_activity;      // Phase 2 — zeroed in Phase 1
    uint8_t  battery_pct;        // 0-100
    int8_t   rssi;               // ESP-NOW signal strength
} __attribute__((packed));

/// Operating states for the hive node state machine.
enum class NodeState : uint8_t {
    BOOT,
    DAYTIME_IDLE,
    NIGHTTIME_SLEEP,
    SENSOR_READ,
    ESPNOW_TRANSMIT,
    BLE_CHECK,
    BLE_SYNC
};

/// Metadata for the LittleFS circular buffer.
struct StorageMeta {
    uint16_t head;     // Index of next write position
    uint16_t count;    // Number of valid readings stored
    uint8_t  version;  // Storage format version
} __attribute__((packed));
```

- [ ] **Step 4: Create `firmware/include/module.h`**

```cpp
#pragma once

// =============================================================================
// Module Interface Convention
// =============================================================================
//
// Every sensor/subsystem module follows this pattern:
//
//   namespace ModuleName {
//       bool initialize();
//           — Power on hardware, configure, validate communication.
//           — Returns false if hardware not detected or init fails.
//
//       bool readMeasurements(HivePayload& payload);
//           — Read sensor data and populate relevant fields in payload.
//           — Returns false if read fails.
//
//       void enterSleep();
//           — Power off hardware, release resources, set pins low.
//   }
//
// Modules do not depend on each other. They interact only through
// the shared HivePayload struct passed by the state machine dispatcher.
// =============================================================================
```

- [ ] **Step 5: Create minimal `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "types.h"

// RTC memory survives light sleep, resets on deep sleep power-on
RTC_DATA_ATTR static NodeState currentState = NodeState::BOOT;
RTC_DATA_ATTR static uint32_t  bootCount = 0;

void setup() {
    Serial.begin(115200);
    bootCount++;

    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();

    Serial.printf("[BOOT] count=%u, wakeup_reason=%d\n", bootCount, wakeupReason);
    Serial.println("[BOOT] HiveSense Node — Phase 1 firmware");

    currentState = NodeState::BOOT;
}

void loop() {
    switch (currentState) {
        case NodeState::BOOT:
            Serial.println("[STATE] BOOT — TODO: determine daytime/nighttime");
            currentState = NodeState::SENSOR_READ;
            break;

        case NodeState::SENSOR_READ:
            Serial.println("[STATE] SENSOR_READ — TODO: read sensors");
            currentState = NodeState::ESPNOW_TRANSMIT;
            break;

        case NodeState::ESPNOW_TRANSMIT:
            Serial.println("[STATE] ESPNOW_TRANSMIT — TODO: send payload");
            currentState = NodeState::BLE_CHECK;
            break;

        case NodeState::BLE_CHECK:
            Serial.println("[STATE] BLE_CHECK — TODO: advertise BLE");
            currentState = NodeState::DAYTIME_IDLE;
            break;

        case NodeState::DAYTIME_IDLE:
            Serial.println("[STATE] DAYTIME_IDLE — TODO: light sleep");
            delay(5000);
            currentState = NodeState::SENSOR_READ;
            break;

        case NodeState::NIGHTTIME_SLEEP:
            Serial.println("[STATE] NIGHTTIME_SLEEP — TODO: deep sleep");
            delay(5000);
            currentState = NodeState::SENSOR_READ;
            break;

        case NodeState::BLE_SYNC:
            Serial.println("[STATE] BLE_SYNC — TODO: transfer logs");
            currentState = NodeState::DAYTIME_IDLE;
            break;
    }
}
```

- [ ] **Step 6: Verify build compiles**

Run: `pio run` from `firmware/` directory (or use esp32-devops MCP `esp32_build`)
Expected: BUILD SUCCESS with no errors. Warnings about unused variables are acceptable at this stage.

- [ ] **Step 7: Commit**

```bash
git add firmware/
git commit -m "feat: scaffold PlatformIO project with types, config, and stub state machine"
```

---

## Task 2: Power Manager Module

**Files:**
- Create: `firmware/src/power_manager.h`
- Create: `firmware/src/power_manager.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Create `firmware/src/power_manager.h`**

```cpp
#pragma once

#include <cstdint>

/// Controls deep sleep, light sleep, and MOSFET power gating.
/// Called by the state machine to transition between power states.
namespace PowerManager {

    /// Initialize MOSFET gate pins as outputs, all OFF.
    void initialize();

    /// Enter deep sleep for the specified number of minutes.
    /// Does not return — wakes up through BOOT state.
    void enterDeepSleep(uint8_t minutes);

    /// Enter automatic light sleep (CPU sleeps between activity).
    /// Returns immediately — light sleep is managed by the hardware.
    void enableLightSleep();

    /// Power on the HX711 MOSFET gate and wait for stabilization.
    void powerOnWeightSensor();

    /// Power off the HX711 MOSFET gate.
    void powerOffWeightSensor();

    /// Disable WiFi and Bluetooth radios before sleep.
    void disableRadios();

    /// Check if current hour falls within daytime window.
    /// Uses NVS-stored day_start and day_end hours.
    bool isDaytime(uint8_t currentHour);

}  // namespace PowerManager
```

- [ ] **Step 2: Create `firmware/src/power_manager.cpp`**

```cpp
#include "power_manager.h"
#include "config.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <Preferences.h>

namespace PowerManager {

void initialize() {
    pinMode(PIN_MOSFET_HX711, OUTPUT);
    digitalWrite(PIN_MOSFET_HX711, LOW);

    // Phase 2: IR MOSFET gate — ensure it's OFF
    pinMode(PIN_MOSFET_IR, OUTPUT);
    digitalWrite(PIN_MOSFET_IR, LOW);

    Serial.println("[POWER] MOSFET gates initialized — all OFF");
}

void enterDeepSleep(uint8_t minutes) {
    disableRadios();

    uint64_t sleepMicroseconds = static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepMicroseconds);

    Serial.printf("[POWER] Entering deep sleep for %u minutes\n", minutes);
    Serial.flush();

    esp_deep_sleep_start();
    // Does not return — next execution starts from setup()
}

void enableLightSleep() {
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(DEFAULT_READ_INTERVAL_MIN) * 60ULL * 1000000ULL
    );

    // Automatic light sleep: CPU sleeps when idle, wakes on interrupts/timers
    esp_pm_config_esp32_t pmConfig = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pmConfig);

    Serial.println("[POWER] Light sleep enabled — CPU will idle between activity");
}

void powerOnWeightSensor() {
    digitalWrite(PIN_MOSFET_HX711, HIGH);
    delay(MOSFET_STABILIZE_MS);
    Serial.println("[POWER] HX711 MOSFET ON — stabilized");
}

void powerOffWeightSensor() {
    digitalWrite(PIN_MOSFET_HX711, LOW);
    Serial.println("[POWER] HX711 MOSFET OFF");
}

void disableRadios() {
    esp_wifi_stop();
    esp_bt_controller_disable();
    Serial.println("[POWER] Radios disabled");
}

bool isDaytime(uint8_t currentHour) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    uint8_t dayStart = prefs.getUChar(NVS_KEY_DAY_START, DEFAULT_DAY_START_HOUR);
    uint8_t dayEnd   = prefs.getUChar(NVS_KEY_DAY_END, DEFAULT_DAY_END_HOUR);
    prefs.end();

    return (currentHour >= dayStart && currentHour < dayEnd);
}

}  // namespace PowerManager
```

- [ ] **Step 3: Wire power manager into `main.cpp` setup**

Update `setup()` in `firmware/src/main.cpp` to call `PowerManager::initialize()`:

```cpp
#include "power_manager.h"

// In setup(), after Serial.begin:
PowerManager::initialize();
```

Update the `DAYTIME_IDLE` case to call `PowerManager::enableLightSleep()` and the `NIGHTTIME_SLEEP` case to call `PowerManager::enterDeepSleep()`:

```cpp
case NodeState::DAYTIME_IDLE:
    Serial.println("[STATE] DAYTIME_IDLE");
    PowerManager::enableLightSleep();
    delay(5000);  // Placeholder — replaced by timer wakeup in Task 6
    currentState = NodeState::SENSOR_READ;
    break;

case NodeState::NIGHTTIME_SLEEP:
    Serial.println("[STATE] NIGHTTIME_SLEEP");
    PowerManager::enterDeepSleep(DEFAULT_READ_INTERVAL_MIN);
    // Does not return
    break;
```

- [ ] **Step 4: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add firmware/src/power_manager.h firmware/src/power_manager.cpp firmware/src/main.cpp
git commit -m "feat: add power manager with deep sleep, light sleep, and MOSFET gating"
```

---

## Task 3: Battery ADC Module

**Files:**
- Create: `firmware/src/battery.h`
- Create: `firmware/src/battery.cpp`

- [ ] **Step 1: Create `firmware/src/battery.h`**

```cpp
#pragma once

#include "types.h"

/// Reads battery voltage via ADC and estimates percentage.
/// Uses a voltage divider on PIN_BATTERY_ADC (ADC1_CH6).
namespace Battery {

    /// Configure ADC pin and attenuation.
    bool initialize();

    /// Read battery voltage, convert to percentage, populate payload.
    bool readMeasurements(HivePayload& payload);

    /// No hardware to power off — no-op for interface consistency.
    void enterSleep();

}  // namespace Battery
```

- [ ] **Step 2: Create `firmware/src/battery.cpp`**

```cpp
#include "battery.h"
#include "config.h"

#include <Arduino.h>
#include <esp_adc_cal.h>

namespace {

// Voltage divider ratio: if using 100k/100k divider, multiply ADC voltage by 2
constexpr float VOLTAGE_DIVIDER_RATIO = 2.0f;

// 18650 LiPo discharge curve (simplified linear approximation)
// Full: 4.2V, Empty: 3.0V (with cutoff margin)
constexpr float BATTERY_VOLTAGE_FULL  = 4.2f;
constexpr float BATTERY_VOLTAGE_EMPTY = 3.0f;

esp_adc_cal_characteristics_t adcCharacteristics;

/// Convert battery voltage to percentage using linear approximation.
/// Clamps to 0-100 range.
uint8_t voltageToPercent(float voltage) {
    if (voltage >= BATTERY_VOLTAGE_FULL) return 100;
    if (voltage <= BATTERY_VOLTAGE_EMPTY) return 0;

    float range = BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY;
    float normalized = (voltage - BATTERY_VOLTAGE_EMPTY) / range;
    return static_cast<uint8_t>(normalized * 100.0f);
}

}  // anonymous namespace

namespace Battery {

bool initialize() {
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);

    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,  // default Vref in mV
        &adcCharacteristics
    );

    Serial.println("[BATTERY] ADC initialized on GPIO 34");
    return true;
}

bool readMeasurements(HivePayload& payload) {
    // Average multiple samples to reduce noise
    constexpr int SAMPLE_COUNT = 16;
    uint32_t totalMillivolts = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        uint32_t rawValue = analogRead(PIN_BATTERY_ADC);
        uint32_t millivolts = esp_adc_cal_raw_to_voltage(rawValue, &adcCharacteristics);
        totalMillivolts += millivolts;
    }

    float averageMillivolts = static_cast<float>(totalMillivolts) / SAMPLE_COUNT;
    float batteryVoltage = (averageMillivolts / 1000.0f) * VOLTAGE_DIVIDER_RATIO;

    payload.battery_pct = voltageToPercent(batteryVoltage);

    Serial.printf("[BATTERY] Voltage=%.2fV, Percent=%u%%\n",
                  batteryVoltage, payload.battery_pct);
    return true;
}

void enterSleep() {
    // No hardware to power off — ADC pin is passive
    Serial.println("[BATTERY] Sleep — no action needed");
}

}  // namespace Battery
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/battery.h firmware/src/battery.cpp
git commit -m "feat: add battery ADC module with voltage-to-percent conversion"
```

---

## Task 4: SHT31 Temperature & Humidity Module

**Files:**
- Create: `firmware/src/sensor_sht31.h`
- Create: `firmware/src/sensor_sht31.cpp`

- [ ] **Step 1: Create `firmware/src/sensor_sht31.h`**

```cpp
#pragma once

#include "types.h"

/// Reads temperature and humidity from two SHT31 sensors on the I2C bus.
/// Internal sensor at 0x44, external sensor at 0x45.
namespace SensorSHT31 {

    /// Initialize I2C bus and verify both SHT31 sensors respond.
    /// Returns false if either sensor is not detected.
    bool initialize();

    /// Read temp and humidity from both sensors into payload.
    /// Fields: temp_internal, temp_external, humidity_internal, humidity_external.
    /// Returns false if either read fails.
    bool readMeasurements(HivePayload& payload);

    /// No MOSFET gate on SHT31 — sensors draw ~2uA in idle.
    void enterSleep();

}  // namespace SensorSHT31
```

- [ ] **Step 2: Create `firmware/src/sensor_sht31.cpp`**

```cpp
#include "sensor_sht31.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

namespace {

Adafruit_SHT31 sensorInternal;
Adafruit_SHT31 sensorExternal;

}  // anonymous namespace

namespace SensorSHT31 {

bool initialize() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!sensorInternal.begin(SHT31_ADDR_INTERNAL)) {
        Serial.println("[SHT31] ERROR: Internal sensor (0x44) not found");
        return false;
    }

    if (!sensorExternal.begin(SHT31_ADDR_EXTERNAL)) {
        Serial.println("[SHT31] ERROR: External sensor (0x45) not found");
        return false;
    }

    // Enable heater on internal sensor to combat condensation
    sensorInternal.heater(true);

    Serial.println("[SHT31] Both sensors initialized — internal heater ON");
    return true;
}

bool readMeasurements(HivePayload& payload) {
    float tempInternal = sensorInternal.readTemperature();
    float humInternal  = sensorInternal.readHumidity();

    if (isnan(tempInternal) || isnan(humInternal)) {
        Serial.println("[SHT31] ERROR: Internal sensor read failed");
        return false;
    }

    float tempExternal = sensorExternal.readTemperature();
    float humExternal  = sensorExternal.readHumidity();

    if (isnan(tempExternal) || isnan(humExternal)) {
        Serial.println("[SHT31] ERROR: External sensor read failed");
        return false;
    }

    payload.temp_internal     = tempInternal;
    payload.humidity_internal = humInternal;
    payload.temp_external     = tempExternal;
    payload.humidity_external = humExternal;

    Serial.printf("[SHT31] Internal: %.1f°C / %.1f%% RH\n",
                  tempInternal, humInternal);
    Serial.printf("[SHT31] External: %.1f°C / %.1f%% RH\n",
                  tempExternal, humExternal);
    return true;
}

void enterSleep() {
    // SHT31 draws ~2uA idle — no MOSFET gate needed
    // Turn off internal heater during sleep to save power
    sensorInternal.heater(false);
    Serial.println("[SHT31] Heater OFF — entering idle");
}

}  // namespace SensorSHT31
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/sensor_sht31.h firmware/src/sensor_sht31.cpp
git commit -m "feat: add SHT31 temp/humidity module with dual sensor and heater control"
```

---

## Task 5: HX711 Weight Module

**Files:**
- Create: `firmware/src/sensor_hx711.h`
- Create: `firmware/src/sensor_hx711.cpp`

- [ ] **Step 1: Create `firmware/src/sensor_hx711.h`**

```cpp
#pragma once

#include "types.h"

/// Reads weight from HX711 ADC with 4x 50kg load cells in Wheatstone bridge.
/// Power-gated via MOSFET on PIN_MOSFET_HX711.
namespace SensorHX711 {

    /// Power on MOSFET, initialize HX711, apply tare and scale from NVS.
    bool initialize();

    /// Read weight and populate payload.weight_kg.
    /// Averages multiple samples for stability.
    bool readMeasurements(HivePayload& payload);

    /// Power off MOSFET gate — HX711 and load cells draw zero current.
    void enterSleep();

}  // namespace SensorHX711
```

- [ ] **Step 2: Create `firmware/src/sensor_hx711.cpp`**

```cpp
#include "sensor_hx711.h"
#include "config.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>

namespace {

HX711 scale;

float weightOffset = 0.0f;
float weightScale  = 1.0f;

/// Load tare offset and calibration factor from NVS.
void loadCalibrationFromNVS() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    weightOffset = prefs.getFloat(NVS_KEY_WEIGHT_OFF, 0.0f);
    weightScale  = prefs.getFloat(NVS_KEY_WEIGHT_SCL, 1.0f);
    prefs.end();

    Serial.printf("[HX711] Calibration loaded — offset=%.2f, scale=%.2f\n",
                  weightOffset, weightScale);
}

}  // anonymous namespace

namespace SensorHX711 {

bool initialize() {
    // MOSFET gate is controlled by PowerManager — already ON when this is called
    scale.begin(PIN_HX711_DOUT, PIN_HX711_CLK);

    if (!scale.wait_ready_timeout(1000)) {
        Serial.println("[HX711] ERROR: HX711 not responding");
        return false;
    }

    loadCalibrationFromNVS();
    scale.set_offset(static_cast<long>(weightOffset));
    scale.set_scale(weightScale);

    Serial.println("[HX711] Initialized and calibrated");
    return true;
}

bool readMeasurements(HivePayload& payload) {
    if (!scale.is_ready()) {
        Serial.println("[HX711] ERROR: Not ready for reading");
        return false;
    }

    // Average 10 samples for stable weight reading
    constexpr int SAMPLE_COUNT = 10;
    float weightKg = scale.get_units(SAMPLE_COUNT);

    // Clamp to reasonable range — negative weight indicates calibration issue
    if (weightKg < 0.0f) {
        Serial.printf("[HX711] WARNING: Negative weight (%.2f kg) — possible calibration issue\n",
                      weightKg);
        weightKg = 0.0f;
    }

    payload.weight_kg = weightKg;

    Serial.printf("[HX711] Weight: %.2f kg\n", weightKg);
    return true;
}

void enterSleep() {
    scale.power_down();
    // MOSFET gate is controlled by PowerManager
    Serial.println("[HX711] Powered down");
}

}  // namespace SensorHX711
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/sensor_hx711.h firmware/src/sensor_hx711.cpp
git commit -m "feat: add HX711 weight module with NVS calibration and MOSFET gating"
```

---

## Task 6: State Machine Module

**Files:**
- Create: `firmware/src/state_machine.h`
- Create: `firmware/src/state_machine.cpp`
- Modify: `firmware/src/main.cpp` — replace stub dispatcher with full state machine

- [ ] **Step 1: Create `firmware/src/state_machine.h`**

```cpp
#pragma once

#include "types.h"
#include <cstdint>

/// Manages state transitions, RTC time tracking, and the sensor read cycle.
/// The state machine is the central coordinator — it calls into modules
/// but modules never call the state machine or each other.
namespace StateMachine {

    /// Determine initial state based on wake reason and time of day.
    /// Called once from setup().
    NodeState determineInitialState();

    /// Execute the current state and return the next state.
    /// Each state performs its actions and decides what comes next.
    NodeState executeState(NodeState current, HivePayload& payload);

    /// Get the current hour from the internal RTC counter.
    /// Approximate — drifts without NTP. Sufficient for day/night switching.
    uint8_t getCurrentHour();

    /// Set the internal RTC time (called once if NTP or BLE time sync available).
    void setTime(uint32_t epochSeconds);

    /// Get current epoch timestamp for payload.
    uint32_t getTimestamp();

}  // namespace StateMachine
```

- [ ] **Step 2: Create `firmware/src/state_machine.cpp`**

```cpp
#include "state_machine.h"
#include "config.h"
#include "power_manager.h"
#include "sensor_sht31.h"
#include "sensor_hx711.h"
#include "battery.h"
#include "comms_espnow.h"
#include "comms_ble.h"
#include "storage.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <Preferences.h>
#include <cstring>

// RTC memory — survives light sleep, resets on deep sleep power-on
RTC_DATA_ATTR static uint32_t rtcEpochAtSleep = 0;
RTC_DATA_ATTR static uint32_t rtcMillisAtSleep = 0;

namespace {

/// Load hive_id from NVS into the payload.
void populateHiveId(HivePayload& payload) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String hiveId = prefs.getString(NVS_KEY_HIVE_ID, "HIVE-001");
    prefs.end();

    memset(payload.hive_id, 0, sizeof(payload.hive_id));
    strncpy(payload.hive_id, hiveId.c_str(), sizeof(payload.hive_id) - 1);
}

}  // anonymous namespace

namespace StateMachine {

NodeState determineInitialState() {
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();

    if (wakeupReason == ESP_SLEEP_WAKEUP_TIMER) {
        // Timer wake — go straight to sensor read
        Serial.println("[SM] Timer wakeup — starting sensor read");
        return NodeState::SENSOR_READ;
    }

    // First boot or other wake reason — full initialization
    Serial.println("[SM] Fresh boot — initializing");
    return NodeState::SENSOR_READ;
}

NodeState executeState(NodeState current, HivePayload& payload) {
    switch (current) {

        case NodeState::BOOT:
            return determineInitialState();

        case NodeState::SENSOR_READ: {
            Serial.println("[SM] === SENSOR_READ ===");

            // Prepare payload
            memset(&payload, 0, sizeof(HivePayload));
            payload.version = PAYLOAD_VERSION;
            payload.timestamp = getTimestamp();
            populateHiveId(payload);

            // Zero out Phase 2 fields explicitly
            payload.bees_in = 0;
            payload.bees_out = 0;
            payload.bees_activity = 0;

            // Read battery first (no MOSFET needed)
            Battery::initialize();
            Battery::readMeasurements(payload);

            // Read temperature and humidity
            SensorSHT31::initialize();
            SensorSHT31::readMeasurements(payload);
            SensorSHT31::enterSleep();

            // Read weight (MOSFET controlled by PowerManager)
            PowerManager::powerOnWeightSensor();
            SensorHX711::initialize();
            SensorHX711::readMeasurements(payload);
            SensorHX711::enterSleep();
            PowerManager::powerOffWeightSensor();

            // Store reading to LittleFS
            Storage::storeReading(payload);

            return NodeState::ESPNOW_TRANSMIT;
        }

        case NodeState::ESPNOW_TRANSMIT: {
            Serial.println("[SM] === ESPNOW_TRANSMIT ===");

            CommsEspNow::initialize();
            bool sent = CommsEspNow::sendPayload(payload);
            CommsEspNow::shutdown();

            if (sent) {
                Serial.println("[SM] ESP-NOW payload sent successfully");
            } else {
                Serial.println("[SM] ESP-NOW send failed — data saved in flash");
            }

            return NodeState::BLE_CHECK;
        }

        case NodeState::BLE_CHECK: {
            Serial.println("[SM] === BLE_CHECK ===");

            uint16_t readingCount = Storage::getReadingCount();
            if (readingCount == 0) {
                Serial.println("[SM] No stored readings — skipping BLE");
                return PowerManager::isDaytime(getCurrentHour())
                    ? NodeState::DAYTIME_IDLE
                    : NodeState::NIGHTTIME_SLEEP;
            }

            CommsBle::initialize();
            bool phoneConnected = CommsBle::advertiseAndWait(BLE_ADVERTISE_TIMEOUT_MS);

            if (phoneConnected) {
                Serial.println("[SM] Phone connected — entering BLE_SYNC");
                return NodeState::BLE_SYNC;
            }

            CommsBle::shutdown();
            Serial.println("[SM] No phone detected — returning to idle");

            return PowerManager::isDaytime(getCurrentHour())
                ? NodeState::DAYTIME_IDLE
                : NodeState::NIGHTTIME_SLEEP;
        }

        case NodeState::BLE_SYNC: {
            Serial.println("[SM] === BLE_SYNC ===");

            // BLE sync is handled by callbacks in comms_ble.cpp
            // This state waits until the phone disconnects or times out
            CommsBle::waitForSyncComplete();
            CommsBle::shutdown();

            Serial.println("[SM] BLE sync complete");

            return PowerManager::isDaytime(getCurrentHour())
                ? NodeState::DAYTIME_IDLE
                : NodeState::NIGHTTIME_SLEEP;
        }

        case NodeState::DAYTIME_IDLE: {
            Serial.println("[SM] === DAYTIME_IDLE ===");

            // Save RTC state before sleeping
            rtcEpochAtSleep = getTimestamp();
            rtcMillisAtSleep = millis();

            PowerManager::enableLightSleep();

            // After light sleep wakeup, timer fires and we read sensors again
            return NodeState::SENSOR_READ;
        }

        case NodeState::NIGHTTIME_SLEEP: {
            Serial.println("[SM] === NIGHTTIME_SLEEP ===");

            // Save RTC state before sleeping
            rtcEpochAtSleep = getTimestamp();
            rtcMillisAtSleep = millis();

            Preferences prefs;
            prefs.begin(NVS_NAMESPACE, true);
            uint8_t interval = prefs.getUChar(NVS_KEY_INTERVAL, DEFAULT_READ_INTERVAL_MIN);
            prefs.end();

            PowerManager::enterDeepSleep(interval);
            // Does not return
            return NodeState::BOOT;  // Unreachable — satisfies compiler
        }
    }

    // Should never reach here
    Serial.println("[SM] ERROR: Unknown state — resetting to BOOT");
    return NodeState::BOOT;
}

uint8_t getCurrentHour() {
    uint32_t elapsed = (millis() - rtcMillisAtSleep) / 1000;
    uint32_t currentEpoch = rtcEpochAtSleep + elapsed;
    return static_cast<uint8_t>((currentEpoch / 3600) % 24);
}

void setTime(uint32_t epochSeconds) {
    rtcEpochAtSleep = epochSeconds;
    rtcMillisAtSleep = millis();
    Serial.printf("[SM] Time set to epoch %u\n", epochSeconds);
}

uint32_t getTimestamp() {
    uint32_t elapsed = (millis() - rtcMillisAtSleep) / 1000;
    return rtcEpochAtSleep + elapsed;
}

}  // namespace StateMachine
```

- [ ] **Step 3: Update `firmware/src/main.cpp` to use the state machine**

Replace the entire contents of `main.cpp`:

```cpp
#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "state_machine.h"
#include "power_manager.h"
#include "storage.h"

RTC_DATA_ATTR static uint32_t bootCount = 0;

void setup() {
    Serial.begin(115200);
    bootCount++;

    Serial.printf("\n[MAIN] HiveSense Node — Phase 1 | Boot #%u\n", bootCount);

    PowerManager::initialize();
    Storage::initialize();
}

void loop() {
    static NodeState currentState = StateMachine::determineInitialState();
    static HivePayload payload;

    currentState = StateMachine::executeState(currentState, payload);
}
```

- [ ] **Step 4: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: Will show linker errors for `CommsEspNow`, `CommsBle`, `Storage` — these are created in Tasks 7-9. Create minimal stub `.h`/`.cpp` files with empty function bodies to verify the state machine compiles. Remove stubs as real implementations land.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/state_machine.h firmware/src/state_machine.cpp firmware/src/main.cpp
git commit -m "feat: add state machine dispatcher with full sensor read cycle"
```

---

## Task 7: LittleFS Storage Module

**Files:**
- Create: `firmware/src/storage.h`
- Create: `firmware/src/storage.cpp`

- [ ] **Step 1: Create `firmware/src/storage.h`**

```cpp
#pragma once

#include "types.h"
#include <cstdint>

/// Circular buffer on LittleFS for storing HivePayload readings.
/// Stores up to MAX_STORED_READINGS entries. Oldest overwritten on overflow.
namespace Storage {

    /// Mount LittleFS and load or create metadata file.
    bool initialize();

    /// Append a reading to the circular buffer.
    bool storeReading(const HivePayload& payload);

    /// Read a specific reading by index (0 = oldest available).
    bool readReading(uint16_t index, HivePayload& payload);

    /// Get the number of readings currently stored.
    uint16_t getReadingCount();

    /// Clear all stored readings (called after BLE sync).
    bool clearAllReadings();

}  // namespace Storage
```

- [ ] **Step 2: Create `firmware/src/storage.cpp`**

```cpp
#include "storage.h"
#include "config.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace {

constexpr const char* META_PATH     = "/meta.bin";
constexpr const char* READINGS_PATH = "/readings.bin";

StorageMeta meta = {0, 0, PAYLOAD_VERSION};

/// Write current metadata to flash.
bool writeMeta() {
    File file = LittleFS.open(META_PATH, "w");
    if (!file) {
        Serial.println("[STORAGE] ERROR: Cannot open meta.bin for writing");
        return false;
    }
    file.write(reinterpret_cast<const uint8_t*>(&meta), sizeof(StorageMeta));
    file.close();
    return true;
}

/// Read metadata from flash. Returns false if file doesn't exist.
bool readMeta() {
    File file = LittleFS.open(META_PATH, "r");
    if (!file) {
        return false;
    }
    file.read(reinterpret_cast<uint8_t*>(&meta), sizeof(StorageMeta));
    file.close();
    return true;
}

}  // anonymous namespace

namespace Storage {

bool initialize() {
    if (!LittleFS.begin(true)) {  // true = format on first use
        Serial.println("[STORAGE] ERROR: LittleFS mount failed");
        return false;
    }

    if (!readMeta()) {
        // First boot — create fresh metadata
        meta = {0, 0, PAYLOAD_VERSION};
        writeMeta();
        Serial.println("[STORAGE] Initialized — fresh storage");
    } else {
        Serial.printf("[STORAGE] Loaded — %u readings stored\n", meta.count);
    }

    return true;
}

bool storeReading(const HivePayload& payload) {
    File file = LittleFS.open(READINGS_PATH, "r+");
    if (!file) {
        // File doesn't exist yet — create it
        file = LittleFS.open(READINGS_PATH, "w");
        if (!file) {
            Serial.println("[STORAGE] ERROR: Cannot create readings.bin");
            return false;
        }
    }

    // Seek to the write position in the circular buffer
    size_t offset = static_cast<size_t>(meta.head) * sizeof(HivePayload);
    file.seek(offset);
    file.write(reinterpret_cast<const uint8_t*>(&payload), sizeof(HivePayload));
    file.close();

    // Advance head pointer (wraps around)
    meta.head = (meta.head + 1) % MAX_STORED_READINGS;
    if (meta.count < MAX_STORED_READINGS) {
        meta.count++;
    }
    writeMeta();

    Serial.printf("[STORAGE] Stored reading — count=%u, head=%u\n",
                  meta.count, meta.head);
    return true;
}

bool readReading(uint16_t index, HivePayload& payload) {
    if (index >= meta.count) {
        Serial.printf("[STORAGE] ERROR: Index %u out of range (count=%u)\n",
                      index, meta.count);
        return false;
    }

    File file = LittleFS.open(READINGS_PATH, "r");
    if (!file) {
        Serial.println("[STORAGE] ERROR: Cannot open readings.bin");
        return false;
    }

    // Calculate actual position in circular buffer
    // Oldest reading is at (head - count) wrapped to buffer size
    uint16_t startIndex = (meta.head + MAX_STORED_READINGS - meta.count) % MAX_STORED_READINGS;
    uint16_t actualIndex = (startIndex + index) % MAX_STORED_READINGS;

    size_t offset = static_cast<size_t>(actualIndex) * sizeof(HivePayload);
    file.seek(offset);
    file.read(reinterpret_cast<uint8_t*>(&payload), sizeof(HivePayload));
    file.close();

    return true;
}

uint16_t getReadingCount() {
    return meta.count;
}

bool clearAllReadings() {
    meta.head = 0;
    meta.count = 0;
    writeMeta();

    Serial.println("[STORAGE] All readings cleared");
    return true;
}

}  // namespace Storage
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/storage.h firmware/src/storage.cpp
git commit -m "feat: add LittleFS circular buffer storage for sensor readings"
```

---

## Task 8: ESP-NOW Communication Module

**Files:**
- Create: `firmware/src/comms_espnow.h`
- Create: `firmware/src/comms_espnow.cpp`

- [ ] **Step 1: Create `firmware/src/comms_espnow.h`**

```cpp
#pragma once

#include "types.h"

/// Transmits HivePayload to the yard collector via ESP-NOW.
/// Sends up to ESPNOW_MAX_RETRIES attempts with delay between each.
namespace CommsEspNow {

    /// Initialize WiFi in station mode and register ESP-NOW peer.
    /// Loads collector MAC address from NVS.
    bool initialize();

    /// Send payload to collector. Retries on failure.
    /// Populates payload.rssi with signal strength on success.
    /// Returns true if ACK received.
    bool sendPayload(HivePayload& payload);

    /// Deregister peer and stop WiFi.
    void shutdown();

}  // namespace CommsEspNow
```

- [ ] **Step 2: Create `firmware/src/comms_espnow.cpp`**

```cpp
#include "comms_espnow.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <cstring>

namespace {

uint8_t collectorMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

volatile bool sendSuccess = false;
volatile bool sendComplete = false;

/// ESP-NOW send callback — called when transmission completes.
void onDataSent(const uint8_t* macAddr, esp_now_send_status_t status) {
    sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    sendComplete = true;

    Serial.printf("[ESPNOW] Send callback — %s\n",
                  sendSuccess ? "ACK received" : "FAILED");
}

/// Load collector MAC address from NVS.
void loadCollectorMac() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    size_t macLen = prefs.getBytesLength(NVS_KEY_COLLECTOR);
    if (macLen == 6) {
        prefs.getBytes(NVS_KEY_COLLECTOR, collectorMac, 6);
        Serial.printf("[ESPNOW] Collector MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      collectorMac[0], collectorMac[1], collectorMac[2],
                      collectorMac[3], collectorMac[4], collectorMac[5]);
    } else {
        Serial.println("[ESPNOW] WARNING: No collector MAC in NVS — using broadcast");
    }

    prefs.end();
}

}  // anonymous namespace

namespace CommsEspNow {

bool initialize() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] ERROR: esp_now_init failed");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    loadCollectorMac();

    // Register collector as peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, collectorMac, 6);
    peerInfo.channel = 0;  // Use current channel
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESPNOW] ERROR: Failed to add peer");
        return false;
    }

    Serial.println("[ESPNOW] Initialized");
    return true;
}

bool sendPayload(HivePayload& payload) {
    for (uint8_t attempt = 1; attempt <= ESPNOW_MAX_RETRIES; attempt++) {
        Serial.printf("[ESPNOW] Send attempt %u/%u\n", attempt, ESPNOW_MAX_RETRIES);

        sendComplete = false;
        sendSuccess = false;

        esp_err_t result = esp_now_send(
            collectorMac,
            reinterpret_cast<const uint8_t*>(&payload),
            sizeof(HivePayload)
        );

        if (result != ESP_OK) {
            Serial.printf("[ESPNOW] esp_now_send error: %d\n", result);
            delay(ESPNOW_RETRY_DELAY_MS);
            continue;
        }

        // Wait for send callback
        uint32_t waitStart = millis();
        while (!sendComplete && (millis() - waitStart) < 1000) {
            delay(1);
        }

        if (sendSuccess) {
            // Read RSSI from WiFi stats
            wifi_ap_record_t apInfo;
            if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) {
                payload.rssi = apInfo.rssi;
            } else {
                payload.rssi = 0;
            }
            return true;
        }

        if (attempt < ESPNOW_MAX_RETRIES) {
            Serial.printf("[ESPNOW] Retrying in %u ms\n", ESPNOW_RETRY_DELAY_MS);
            delay(ESPNOW_RETRY_DELAY_MS);
        }
    }

    Serial.println("[ESPNOW] All attempts failed");
    return false;
}

void shutdown() {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    Serial.println("[ESPNOW] Shutdown — WiFi OFF");
}

}  // namespace CommsEspNow
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/comms_espnow.h firmware/src/comms_espnow.cpp
git commit -m "feat: add ESP-NOW communication module with retry logic"
```

---

## Task 9: BLE GATT Server Module

**Files:**
- Create: `firmware/src/comms_ble.h`
- Create: `firmware/src/comms_ble.cpp`

- [ ] **Step 1: Create `firmware/src/comms_ble.h`**

```cpp
#pragma once

#include <cstdint>

/// BLE GATT server for direct phone communication at the yard.
/// Exposes sensor log download, reading count, hive ID config, and log clear.
namespace CommsBle {

    /// Initialize BLE stack, create GATT service and characteristics.
    bool initialize();

    /// Start advertising and wait for a connection.
    /// Returns true if a phone connects within timeoutMs.
    bool advertiseAndWait(uint16_t timeoutMs);

    /// Block until BLE sync is complete (phone disconnects or clear received).
    void waitForSyncComplete();

    /// Stop BLE advertising, deinit stack, free resources.
    void shutdown();

}  // namespace CommsBle
```

- [ ] **Step 2: Create `firmware/src/comms_ble.cpp`**

```cpp
#include "comms_ble.h"
#include "config.h"
#include "types.h"
#include "storage.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

namespace {

BLEServer*         bleServer = nullptr;
BLECharacteristic* charSensorLog    = nullptr;
BLECharacteristic* charReadingCount = nullptr;
BLECharacteristic* charHiveId       = nullptr;
BLECharacteristic* charClearLog     = nullptr;

volatile bool deviceConnected = false;
volatile bool syncComplete = false;

/// Server connection callbacks.
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        deviceConnected = true;
        syncComplete = false;
        Serial.println("[BLE] Phone connected");
    }

    void onDisconnect(BLEServer* server) override {
        deviceConnected = false;
        syncComplete = true;
        Serial.println("[BLE] Phone disconnected");
    }
};

/// Callback for Clear Log characteristic — write 0x01 to clear storage.
class ClearLogCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.length() == 1 && value[0] == 0x01) {
            Serial.println("[BLE] Clear command received — erasing stored readings");
            Storage::clearAllReadings();
            syncComplete = true;

            // Update reading count characteristic to 0
            uint16_t zero = 0;
            charReadingCount->setValue(reinterpret_cast<uint8_t*>(&zero), sizeof(uint16_t));
        }
    }
};

/// Callback for Hive ID characteristic — phone writes hive ID during pairing.
class HiveIdCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.length() > 0 && value.length() < 16) {
            Preferences prefs;
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putString(NVS_KEY_HIVE_ID, value.c_str());
            prefs.end();

            Serial.printf("[BLE] Hive ID updated to: %s\n", value.c_str());
        }
    }
};

/// Send all stored readings via notifications on the Sensor Log characteristic.
void sendStoredReadings() {
    uint16_t count = Storage::getReadingCount();
    Serial.printf("[BLE] Sending %u readings via notifications\n", count);

    HivePayload payload;
    for (uint16_t i = 0; i < count; i++) {
        if (!Storage::readReading(i, payload)) {
            Serial.printf("[BLE] ERROR: Failed to read index %u\n", i);
            continue;
        }

        charSensorLog->setValue(
            reinterpret_cast<uint8_t*>(&payload),
            sizeof(HivePayload)
        );
        charSensorLog->notify();

        delay(20);  // Small delay between notifications to avoid BLE congestion
    }

    Serial.println("[BLE] All readings sent");
}

}  // anonymous namespace

namespace CommsBle {

bool initialize() {
    // Load hive ID for advertising name
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String hiveId = prefs.getString(NVS_KEY_HIVE_ID, "HIVE-001");
    prefs.end();

    String deviceName = "HiveSense-" + hiveId;

    BLEDevice::init(deviceName.c_str());
    BLEDevice::setMTU(512);

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    // Sensor Log — read + notify
    charSensorLog = service->createCharacteristic(
        BLE_CHAR_SENSOR_LOG,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    charSensorLog->addDescriptor(new BLE2902());

    // Reading Count — read only
    charReadingCount = service->createCharacteristic(
        BLE_CHAR_READING_COUNT,
        BLECharacteristic::PROPERTY_READ
    );
    uint16_t count = Storage::getReadingCount();
    charReadingCount->setValue(reinterpret_cast<uint8_t*>(&count), sizeof(uint16_t));

    // Hive ID — read + write
    charHiveId = service->createCharacteristic(
        BLE_CHAR_HIVE_ID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    charHiveId->setCallbacks(new HiveIdCallback());
    charHiveId->setValue(hiveId.c_str());

    // Clear Log — write only
    charClearLog = service->createCharacteristic(
        BLE_CHAR_CLEAR_LOG,
        BLECharacteristic::PROPERTY_WRITE
    );
    charClearLog->setCallbacks(new ClearLogCallback());

    service->start();

    Serial.printf("[BLE] GATT server initialized — %s (%u readings available)\n",
                  deviceName.c_str(), count);
    return true;
}

bool advertiseAndWait(uint16_t timeoutMs) {
    deviceConnected = false;
    syncComplete = false;

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->start();

    Serial.printf("[BLE] Advertising for %u ms\n", timeoutMs);

    uint32_t startTime = millis();
    while (!deviceConnected && (millis() - startTime) < timeoutMs) {
        delay(100);
    }

    if (!deviceConnected) {
        advertising->stop();
        Serial.println("[BLE] No connection — advertising stopped");
        return false;
    }

    // Phone connected — send stored readings
    sendStoredReadings();
    return true;
}

void waitForSyncComplete() {
    Serial.println("[BLE] Waiting for sync to complete (disconnect or clear)...");

    while (!syncComplete) {
        delay(100);
    }
}

void shutdown() {
    BLEDevice::deinit(true);  // true = release memory
    bleServer = nullptr;
    charSensorLog = nullptr;
    charReadingCount = nullptr;
    charHiveId = nullptr;
    charClearLog = nullptr;

    Serial.println("[BLE] Shutdown — stack deinitialized");
}

}  // namespace CommsBle
```

- [ ] **Step 3: Verify build compiles**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/src/comms_ble.h firmware/src/comms_ble.cpp
git commit -m "feat: add BLE GATT server with sensor log sync and hive ID pairing"
```

---

## Task 10: Integration — Full Build & Serial Validation

**Files:**
- Modify: `firmware/src/main.cpp` (if any final integration adjustments needed)

- [ ] **Step 1: Full build with all modules**

Run: `pio run` from `firmware/`
Expected: BUILD SUCCESS with all modules linked. Note the flash usage and RAM usage from the build output.

- [ ] **Step 2: Review build warnings**

Check build output for any warnings. Fix any that indicate real issues (type mismatches, unused variables that suggest missing integration, etc.). Informational warnings about deprecated ESP-IDF APIs are acceptable.

- [ ] **Step 3: Verify serial output logic**

Read through `main.cpp` and `state_machine.cpp` to confirm the expected serial output sequence on boot:

```
[MAIN] HiveSense Node — Phase 1 | Boot #1
[POWER] MOSFET gates initialized — all OFF
[STORAGE] Initialized — fresh storage
[SM] Fresh boot — initializing
[SM] === SENSOR_READ ===
[BATTERY] ADC initialized on GPIO 34
[BATTERY] Voltage=X.XXV, Percent=XX%
[SHT31] Both sensors initialized — internal heater ON
[SHT31] Internal: XX.X°C / XX.X% RH
[SHT31] External: XX.X°C / XX.X% RH
[SHT31] Heater OFF — entering idle
[POWER] HX711 MOSFET ON — stabilized
[HX711] Initialized and calibrated
[HX711] Weight: XX.XX kg
[HX711] Powered down
[POWER] HX711 MOSFET OFF
[STORAGE] Stored reading — count=1, head=1
[SM] === ESPNOW_TRANSMIT ===
...
```

- [ ] **Step 4: Commit final integration**

```bash
git add -A firmware/
git commit -m "feat: complete Phase 1 hive node firmware — all modules integrated"
```

---

## Task 11: Update .mex Project State

**Files:**
- Modify: `.mex/ROUTER.md`
- Modify: `.mex/context/decisions.md` (if any new decisions were made during implementation)

- [ ] **Step 1: Update ROUTER.md project state**

Change the phase to reflect firmware scaffold complete:

```markdown
**Phase: Phase 1 Firmware — Scaffold Complete (awaiting hardware for validation)**
```

Update the "Not yet built" section to show completed items.

- [ ] **Step 2: Update routing table if needed**

Ensure `firmware/` directory entry in the routing table points correctly.

- [ ] **Step 3: Commit .mex updates**

```bash
git add .mex/
git commit -m "chore: update .mex project state — Phase 1 firmware scaffold complete"
```
