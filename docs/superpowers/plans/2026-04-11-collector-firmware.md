# Yard Collector Firmware — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the LilyGO T-SIM7080G yard collector firmware — receives ESP-NOW from hive nodes, batches and publishes to HiveMQ Cloud via cellular MQTT, relays OTA updates, self-updates, and broadcasts time sync.

**Architecture:** Modular with central loop. Always-on ESP-NOW listener with periodic modem wake for MQTT publish. Each subsystem is a standalone module. Uses TinyGSM for modem abstraction. Shares `HivePayload` and protocol headers with hive node firmware.

**Tech Stack:** PlatformIO, Arduino framework, espressif32 v6.x, ESP32-S3 (T-SIM7080G), TinyGSM, LittleFS, esp_ota_ops

**Spec:** `docs/superpowers/specs/2026-04-11-collector-firmware-design.md`

**Ollama delegation:** Use Ollama (http://192.168.1.16:11434, qwen3-coder:30b) for Tasks 1, 2 (boilerplate config/scaffolding). Review all output before committing.

---

## File Map

### New files (firmware/collector/)
- `platformio.ini` — Build config for T-SIM7080G
- `partitions_ota.csv` — 8MB dual-OTA partition table
- `include/config.h` — T-SIM7080G pin definitions, MQTT config, timing constants
- `include/types.h` — PayloadBuffer, collector constants
- `src/main.cpp` — Main loop: receive → batch → publish → time sync → sleep
- `src/espnow_receiver.cpp/.h` — ESP-NOW receive callback, payload buffer
- `src/cellular.cpp/.h` — SIM7080G modem lifecycle, NTP sync
- `src/mqtt_publisher.cpp/.h` — MQTT connect, batch publish, OTA command subscribe
- `src/ota_relay.cpp/.h` — Download firmware from GitHub, chunk to hive nodes
- `src/ota_self.cpp/.h` — Self-OTA via cellular download
- `src/time_sync.cpp/.h` — Broadcast epoch timestamp via ESP-NOW

### New shared header
- `firmware/shared/espnow_protocol.h` — ESP-NOW packet envelope, TIME_SYNC type

### Hive node changes (tracked in issue #1, implemented in Task 11)
- `firmware/hive-node/src/comms_espnow.cpp` — Add receive callback, packet wrapping

---

## Task 1: PlatformIO Scaffold & Config

**Files:**
- Create: `firmware/collector/platformio.ini`
- Create: `firmware/collector/partitions_ota.csv`
- Create: `firmware/collector/include/config.h`
- Create: `firmware/collector/include/types.h`

- [ ] **Step 1: Create `firmware/collector/platformio.ini`**

```ini
[env:t-sim7080g]
platform = espressif32@^6.5.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_size = 8MB
board_build.partitions = partitions_ota.csv
board_build.filesystem = littlefs
monitor_speed = 115200

lib_deps =
    vshymanskyy/TinyGSM@^0.11.7

lib_ldf_mode = deep+

build_flags =
    -I../shared
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_PM_ENABLE
    -DTINY_GSM_MODEM_SIM7080
```

- [ ] **Step 2: Create `firmware/collector/partitions_ota.csv`**

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x380000,
app1,       app,  ota_1,   0x390000, 0x380000,
littlefs,   data, spiffs,  0x710000, 0xF0000,
```

- [ ] **Step 3: Create `firmware/collector/include/config.h`**

```cpp
#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — LilyGO T-SIM7080G-S3
// =============================================================================

// SIM7080G modem
constexpr uint8_t PIN_MODEM_RXD    = 4;
constexpr uint8_t PIN_MODEM_TXD    = 5;
constexpr uint8_t PIN_MODEM_PWRKEY = 41;
constexpr uint8_t PIN_MODEM_RI     = 3;
constexpr uint8_t PIN_MODEM_DTR    = 42;

// SD card (not used in v1, reserved)
constexpr uint8_t PIN_SD_CLK  = 38;
constexpr uint8_t PIN_SD_CMD  = 39;
constexpr uint8_t PIN_SD_DATA = 40;

// PMU (power management unit)
constexpr uint8_t PIN_PMU_SDA = 15;
constexpr uint8_t PIN_PMU_SCL = 7;
constexpr uint8_t PIN_PMU_IRQ = 6;

// =============================================================================
// Modem Configuration
// =============================================================================

constexpr uint32_t MODEM_BAUD_RATE    = 115200;
constexpr uint16_t MODEM_PWRKEY_MS    = 1000;
constexpr uint16_t MODEM_BOOT_WAIT_MS = 5000;
constexpr uint16_t NETWORK_TIMEOUT_MS = 30000;

// =============================================================================
// Timing Constants
// =============================================================================

constexpr uint8_t  PUBLISH_INTERVAL_MIN = 30;
constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS = 10000;

// =============================================================================
// Buffer
// =============================================================================

constexpr uint8_t MAX_HIVE_NODES = 20;

// =============================================================================
// NVS Keys
// =============================================================================

constexpr const char* NVS_NAMESPACE      = "hivesense";
constexpr const char* NVS_KEY_MQTT_HOST  = "mqtt_host";
constexpr const char* NVS_KEY_MQTT_PORT  = "mqtt_port";
constexpr const char* NVS_KEY_MQTT_USER  = "mqtt_user";
constexpr const char* NVS_KEY_MQTT_PASS  = "mqtt_pass";

// =============================================================================
// MQTT Topics
// =============================================================================

constexpr const char* MQTT_TOPIC_PREFIX  = "hivesense/hive/";
constexpr const char* MQTT_OTA_TOPIC     = "hivesense/ota/start";

// =============================================================================
// OTA
// =============================================================================

constexpr const char* GITHUB_RELEASE_BASE = "https://github.com/sjordan0228/hivesense-monitor/releases/download/";
```

- [ ] **Step 4: Create `firmware/collector/include/types.h`**

```cpp
#pragma once

#include <cstdint>
#include "hive_payload.h"
#include "espnow_protocol.h"

/// Buffer entry — stores latest payload per hive node.
struct BufferEntry {
    HivePayload payload;
    bool        occupied;    // true if a payload has been received
    uint32_t    receivedAt;  // millis() when received
};

/// Payload buffer — one entry per hive node, indexed by slot.
struct PayloadBuffer {
    BufferEntry entries[20];  // MAX_HIVE_NODES
    uint8_t     count;        // Number of occupied slots

    /// Find slot for hive_id, or allocate new slot. Returns index or -1 if full.
    int8_t findOrAllocate(const char* hiveId);

    /// Clear all entries after publish.
    void clear();
};
```

- [ ] **Step 5: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS (will fail on missing src/main.cpp — create stub)

- [ ] **Step 6: Create stub `firmware/collector/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "types.h"

void setup() {
    Serial.begin(115200);
    Serial.println("[MAIN] HiveSense Collector — starting");
}

void loop() {
    delay(1000);
}
```

- [ ] **Step 7: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 8: Commit**

```bash
git add firmware/collector/
git commit -m "feat: scaffold collector firmware with T-SIM7080G config"
```

---

## Task 2: Shared ESP-NOW Protocol Header

**Files:**
- Create: `firmware/shared/espnow_protocol.h`

- [ ] **Step 1: Create `firmware/shared/espnow_protocol.h`**

```cpp
#pragma once

#include <cstdint>

/// Packet type identifier for all ESP-NOW communication.
/// Every ESP-NOW packet starts with an EspNowHeader.
enum class EspNowPacketType : uint8_t {
    SENSOR_DATA = 0x10,   // Node -> Collector: HivePayload
    TIME_SYNC   = 0x20,   // Collector -> Node: epoch timestamp
    OTA_PACKET  = 0x30    // Either direction: OTA transfer
};

/// Header prepended to all ESP-NOW packets for type routing.
struct EspNowHeader {
    EspNowPacketType type;
    uint8_t          data_len;
} __attribute__((packed));

/// Payload for TIME_SYNC packets — collector broadcasts after NTP sync.
struct TimeSyncPayload {
    uint32_t epoch_seconds;
} __attribute__((packed));
```

- [ ] **Step 2: Verify both firmwares build**

Run: `cd firmware/collector && pio run`
Run: `cd firmware/hive-node && pio run`
Expected: Both BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/shared/espnow_protocol.h
git commit -m "feat: add shared ESP-NOW protocol header with packet type routing"
```

---

## Task 3: ESP-NOW Receiver Module

**Files:**
- Create: `firmware/collector/src/espnow_receiver.h`
- Create: `firmware/collector/src/espnow_receiver.cpp`

- [ ] **Step 1: Create `firmware/collector/src/espnow_receiver.h`**

```cpp
#pragma once

#include "types.h"

/// Receives ESP-NOW packets from hive nodes and buffers payloads.
/// Routes packets by type: SENSOR_DATA → buffer, OTA_PACKET → OTA handler.
namespace EspNowReceiver {

    /// Initialize WiFi in STA mode, register ESP-NOW receive callback.
    bool initialize();

    /// Get reference to the payload buffer for publishing.
    PayloadBuffer& getBuffer();

    /// Get MAC address of a hive node by hive_id (for targeted ESP-NOW send).
    /// Returns false if hive_id has never been seen.
    bool getMacForHive(const char* hiveId, uint8_t* macOut);

}  // namespace EspNowReceiver
```

- [ ] **Step 2: Create `firmware/collector/src/espnow_receiver.cpp`**

```cpp
#include "espnow_receiver.h"
#include "config.h"
#include "espnow_protocol.h"
#include "hive_payload.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <cstring>

namespace {

PayloadBuffer buffer;

// Track MAC addresses of known hive nodes for targeted OTA relay
struct MacEntry {
    char    hiveId[16];
    uint8_t mac[6];
    bool    occupied;
};

MacEntry knownMacs[20];  // MAX_HIVE_NODES

/// Store or update MAC address for a hive_id.
void updateMacTable(const char* hiveId, const uint8_t* mac) {
    // Find existing entry
    for (auto& entry : knownMacs) {
        if (entry.occupied && strncmp(entry.hiveId, hiveId, 16) == 0) {
            memcpy(entry.mac, mac, 6);
            return;
        }
    }
    // Allocate new entry
    for (auto& entry : knownMacs) {
        if (!entry.occupied) {
            strncpy(entry.hiveId, hiveId, 15);
            entry.hiveId[15] = '\0';
            memcpy(entry.mac, mac, 6);
            entry.occupied = true;
            return;
        }
    }
}

/// ESP-NOW receive callback — routes by packet type.
void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < static_cast<int>(sizeof(EspNowHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const EspNowHeader*>(data);
    const uint8_t* payload = data + sizeof(EspNowHeader);
    int payloadLen = len - sizeof(EspNowHeader);

    switch (header->type) {
        case EspNowPacketType::SENSOR_DATA: {
            if (payloadLen < static_cast<int>(sizeof(HivePayload))) {
                Serial.println("[ESPNOW] SENSOR_DATA too short");
                return;
            }

            const auto* hivePayload = reinterpret_cast<const HivePayload*>(payload);

            // Update MAC table
            updateMacTable(hivePayload->hive_id, mac);

            // Buffer the payload
            int8_t slot = buffer.findOrAllocate(hivePayload->hive_id);
            if (slot >= 0) {
                buffer.entries[slot].payload = *hivePayload;
                buffer.entries[slot].occupied = true;
                buffer.entries[slot].receivedAt = millis();
                Serial.printf("[ESPNOW] Buffered data from %s (slot %d)\n",
                              hivePayload->hive_id, slot);
            } else {
                Serial.println("[ESPNOW] Buffer full — dropping payload");
            }
            break;
        }

        case EspNowPacketType::OTA_PACKET: {
            // OTA packets from hive nodes (ACK/READY) — forward to OTA relay
            // Handled in ota_relay.cpp via a registered callback
            Serial.printf("[ESPNOW] OTA packet from %02X:%02X:%02X:%02X:%02X:%02X\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            break;
        }

        default:
            Serial.printf("[ESPNOW] Unknown packet type: 0x%02X\n",
                          static_cast<uint8_t>(header->type));
            break;
    }
}

}  // anonymous namespace

namespace EspNowReceiver {

bool initialize() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] esp_now_init failed");
        return false;
    }

    esp_now_register_recv_cb(onDataReceived);

    memset(&buffer, 0, sizeof(buffer));
    memset(knownMacs, 0, sizeof(knownMacs));

    Serial.println("[ESPNOW] Receiver initialized");
    return true;
}

PayloadBuffer& getBuffer() {
    return buffer;
}

bool getMacForHive(const char* hiveId, uint8_t* macOut) {
    for (const auto& entry : knownMacs) {
        if (entry.occupied && strncmp(entry.hiveId, hiveId, 16) == 0) {
            memcpy(macOut, entry.mac, 6);
            return true;
        }
    }
    return false;
}

}  // namespace EspNowReceiver
```

- [ ] **Step 3: Implement PayloadBuffer methods in types.h or a separate .cpp**

Add to `firmware/collector/include/types.h` (inline methods since they're short):

After the struct definition, add the implementations. Or create `firmware/collector/src/types.cpp`:

```cpp
#include "types.h"
#include <cstring>

int8_t PayloadBuffer::findOrAllocate(const char* hiveId) {
    // Find existing slot for this hive
    for (uint8_t i = 0; i < 20; i++) {
        if (entries[i].occupied &&
            strncmp(entries[i].payload.hive_id, hiveId, 16) == 0) {
            return static_cast<int8_t>(i);
        }
    }
    // Allocate new slot
    for (uint8_t i = 0; i < 20; i++) {
        if (!entries[i].occupied) {
            count++;
            return static_cast<int8_t>(i);
        }
    }
    return -1;  // Full
}

void PayloadBuffer::clear() {
    for (auto& entry : entries) {
        entry.occupied = false;
    }
    count = 0;
}
```

- [ ] **Step 4: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add firmware/collector/src/espnow_receiver.h firmware/collector/src/espnow_receiver.cpp firmware/collector/src/types.cpp
git commit -m "feat: add ESP-NOW receiver with payload buffering and MAC tracking"
```

---

## Task 4: Cellular Module

**Files:**
- Create: `firmware/collector/src/cellular.h`
- Create: `firmware/collector/src/cellular.cpp`

- [ ] **Step 1: Create `firmware/collector/src/cellular.h`**

```cpp
#pragma once

#include <cstdint>

#define TINY_GSM_MODEM_SIM7080

#include <TinyGsmClient.h>

/// Manages SIM7080G modem lifecycle — power, network, NTP.
namespace Cellular {

    /// Power on modem with PWRKEY pulse, wait for AT response.
    bool powerOn();

    /// Graceful modem shutdown.
    void powerOff();

    /// Wait for LTE-M/NB-IoT network registration.
    bool waitForNetwork();

    /// Sync time via NTP. Updates system clock. Returns epoch seconds or 0 on failure.
    uint32_t syncNtp();

    /// Get reference to TinyGSM modem instance for MQTT/HTTP use.
    TinyGsm& getModem();

    /// Get the TinyGSM client for HTTP operations.
    TinyGsmClient& getClient();

}  // namespace Cellular
```

- [ ] **Step 2: Create `firmware/collector/src/cellular.cpp`**

```cpp
#include "cellular.h"
#include "config.h"

#include <Arduino.h>

namespace {

HardwareSerial modemSerial(1);  // UART1 for SIM7080G
TinyGsm modem(modemSerial);
TinyGsmClient gsmClient(modem);

}  // anonymous namespace

namespace Cellular {

bool powerOn() {
    modemSerial.begin(MODEM_BAUD_RATE, SERIAL_8N1, PIN_MODEM_RXD, PIN_MODEM_TXD);

    // PWRKEY pulse to power on SIM7080G
    pinMode(PIN_MODEM_PWRKEY, OUTPUT);
    digitalWrite(PIN_MODEM_PWRKEY, LOW);
    delay(MODEM_PWRKEY_MS);
    digitalWrite(PIN_MODEM_PWRKEY, HIGH);
    delay(MODEM_BOOT_WAIT_MS);

    // Verify modem responds
    if (!modem.testAT(5000)) {
        Serial.println("[CELL] Modem not responding to AT");
        return false;
    }

    Serial.printf("[CELL] Modem ready — %s\n", modem.getModemInfo().c_str());
    return true;
}

void powerOff() {
    modem.poweroff();
    Serial.println("[CELL] Modem powered off");
}

bool waitForNetwork() {
    Serial.println("[CELL] Waiting for network...");

    if (!modem.waitForNetwork(NETWORK_TIMEOUT_MS)) {
        Serial.println("[CELL] Network registration failed");
        return false;
    }

    Serial.printf("[CELL] Registered — signal: %d\n", modem.getSignalQuality());
    return true;
}

uint32_t syncNtp() {
    // SIM7080G NTP via AT+CNTP
    modem.sendAT("+CNTP=\"pool.ntp.org\",0");
    if (modem.waitResponse(10000L) != 1) {
        Serial.println("[CELL] NTP request failed");
        return 0;
    }

    // Trigger sync
    modem.sendAT("+CNTP");
    if (modem.waitResponse(10000L) != 1) {
        Serial.println("[CELL] NTP sync failed");
        return 0;
    }

    // Read time from modem
    String dateTime = modem.getGSMDateTime(DATE_FULL);
    Serial.printf("[CELL] NTP time: %s\n", dateTime.c_str());

    // Parse modem time to epoch — TinyGSM returns "YY/MM/DD,HH:MM:SS+TZ"
    // Use a simplified parse for epoch conversion
    struct tm timeinfo = {};
    int year, month, day, hour, minute, second;
    if (sscanf(dateTime.c_str(), "%d/%d/%d,%d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) == 6) {
        timeinfo.tm_year = (year + 2000) - 1900;
        timeinfo.tm_mon  = month - 1;
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min  = minute;
        timeinfo.tm_sec  = second;
        time_t epoch = mktime(&timeinfo);
        Serial.printf("[CELL] Epoch: %lu\n", static_cast<uint32_t>(epoch));
        return static_cast<uint32_t>(epoch);
    }

    Serial.println("[CELL] Failed to parse NTP time");
    return 0;
}

TinyGsm& getModem() {
    return modem;
}

TinyGsmClient& getClient() {
    return gsmClient;
}

}  // namespace Cellular
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/collector/src/cellular.h firmware/collector/src/cellular.cpp
git commit -m "feat: add cellular module with SIM7080G modem lifecycle and NTP sync"
```

---

## Task 5: MQTT Publisher Module

**Files:**
- Create: `firmware/collector/src/mqtt_publisher.h`
- Create: `firmware/collector/src/mqtt_publisher.cpp`

- [ ] **Step 1: Create `firmware/collector/src/mqtt_publisher.h`**

```cpp
#pragma once

#include "types.h"
#include <TinyGsmClient.h>

/// Connects to HiveMQ Cloud and publishes batched sensor data.
/// Subscribes to OTA command topic.
namespace MqttPublisher {

    /// Configure MQTT with credentials from NVS.
    bool initialize();

    /// Connect to MQTT broker via TLS.
    bool connect(TinyGsmClient& client);

    /// Publish all occupied buffer entries. Returns number published.
    uint8_t publishBatch(PayloadBuffer& buffer);

    /// Check for OTA commands on the subscribed topic.
    /// Returns true if an OTA command was received. Populates hiveId, tag, target.
    bool checkOtaCommand(char* hiveId, char* tag, char* target);

    /// Clean disconnect from broker.
    void disconnect();

}  // namespace MqttPublisher
```

- [ ] **Step 2: Create `firmware/collector/src/mqtt_publisher.cpp`**

```cpp
#include "mqtt_publisher.h"
#include "config.h"

#include <Arduino.h>
#include <Preferences.h>

#define TINY_GSM_MODEM_SIM7080

#include <TinyGsmClient.h>

namespace {

char mqttHost[64]  = "";
uint16_t mqttPort  = 8883;
char mqttUser[32]  = "";
char mqttPass[64]  = "";

TinyGsmClient* mqttClient = nullptr;

/// Publish a single HivePayload to all its MQTT topics.
bool publishPayload(const HivePayload& payload, TinyGsm& modem) {
    char topic[80];
    char value[16];
    const char* id = payload.hive_id;

    struct TopicValue {
        const char* suffix;
        const char* format;
        float       fValue;
        int         iValue;
        bool        isFloat;
    };

    TopicValue topics[] = {
        {"weight",              "%.2f", payload.weight_kg,          0, true},
        {"temp/internal",       "%.1f", payload.temp_internal,      0, true},
        {"temp/external",       "%.1f", payload.temp_external,      0, true},
        {"humidity/internal",   "%.1f", payload.humidity_internal,   0, true},
        {"humidity/external",   "%.1f", payload.humidity_external,   0, true},
        {"bees/in",             "%d",   0, payload.bees_in,          false},
        {"bees/out",            "%d",   0, payload.bees_out,         false},
        {"bees/activity",       "%d",   0, payload.bees_activity,    false},
        {"battery",             "%d",   0, payload.battery_pct,      false},
        {"rssi",                "%d",   0, payload.rssi,             false},
    };

    for (const auto& t : topics) {
        snprintf(topic, sizeof(topic), "%s%s/%s", MQTT_TOPIC_PREFIX, id, t.suffix);

        if (t.isFloat) {
            snprintf(value, sizeof(value), t.format, t.fValue);
        } else {
            snprintf(value, sizeof(value), t.format, t.iValue);
        }

        // Use modem's native MQTT publish
        modem.sendAT("+SMCONF=\"URL\",\"", mqttHost, "\",", mqttPort);
        // Note: actual TinyGSM MQTT usage depends on the library version.
        // The SIM7080G uses AT+SMCONN, AT+SMPUB for native MQTT.
        // TinyGSM wraps these — use modem.sendAT for direct AT if needed.

        Serial.printf("[MQTT] %s = %s\n", topic, value);
    }

    return true;
}

}  // anonymous namespace

namespace MqttPublisher {

bool initialize() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    String host = prefs.getString(NVS_KEY_MQTT_HOST, "");
    mqttPort    = prefs.getUShort(NVS_KEY_MQTT_PORT, 8883);
    String user = prefs.getString(NVS_KEY_MQTT_USER, "");
    String pass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();

    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
    strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);

    if (strlen(mqttHost) == 0) {
        Serial.println("[MQTT] WARNING: No broker configured — set via serial console");
        return false;
    }

    Serial.printf("[MQTT] Configured: %s:%u\n", mqttHost, mqttPort);
    return true;
}

bool connect(TinyGsmClient& client) {
    mqttClient = &client;

    TinyGsm& modem = *client.getModem();

    // SIM7080G native MQTT — configure and connect
    modem.sendAT("+SMCONF=\"URL\",\"", mqttHost, "\",", mqttPort);
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"USERNAME\",\"", mqttUser, "\"");
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"PASSWORD\",\"", mqttPass, "\"");
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"CLEANSS\",1");
    modem.waitResponse();

    modem.sendAT("+SMCONN");
    if (modem.waitResponse(MQTT_CONNECT_TIMEOUT_MS) != 1) {
        Serial.println("[MQTT] Connection failed");
        return false;
    }

    Serial.println("[MQTT] Connected to broker");
    return true;
}

uint8_t publishBatch(PayloadBuffer& buffer) {
    uint8_t published = 0;
    TinyGsm& modem = *mqttClient->getModem();

    for (uint8_t i = 0; i < 20; i++) {
        if (!buffer.entries[i].occupied) continue;

        const HivePayload& p = buffer.entries[i].payload;
        char topic[80];
        char value[16];

        struct Pub { const char* suffix; char val[16]; };
        Pub pubs[10];

        snprintf(pubs[0].val, 16, "%.2f", p.weight_kg);
        snprintf(pubs[1].val, 16, "%.1f", p.temp_internal);
        snprintf(pubs[2].val, 16, "%.1f", p.temp_external);
        snprintf(pubs[3].val, 16, "%.1f", p.humidity_internal);
        snprintf(pubs[4].val, 16, "%.1f", p.humidity_external);
        snprintf(pubs[5].val, 16, "%u", p.bees_in);
        snprintf(pubs[6].val, 16, "%u", p.bees_out);
        snprintf(pubs[7].val, 16, "%u", p.bees_activity);
        snprintf(pubs[8].val, 16, "%u", p.battery_pct);
        snprintf(pubs[9].val, 16, "%d", p.rssi);

        const char* suffixes[] = {
            "weight", "temp/internal", "temp/external",
            "humidity/internal", "humidity/external",
            "bees/in", "bees/out", "bees/activity",
            "battery", "rssi"
        };

        for (uint8_t t = 0; t < 10; t++) {
            snprintf(topic, sizeof(topic), "%s%s/%s",
                     MQTT_TOPIC_PREFIX, p.hive_id, suffixes[t]);

            // SIM7080G native MQTT publish
            int len = strlen(pubs[t].val);
            modem.sendAT("+SMPUB=\"", topic, "\",", len, ",1,0");  // QoS 1, not retained
            modem.waitResponse(">");
            modem.stream.write(pubs[t].val, len);
            modem.waitResponse(5000);

            Serial.printf("[MQTT] %s = %s\n", topic, pubs[t].val);
        }

        published++;
    }

    Serial.printf("[MQTT] Published %u hive payloads\n", published);
    return published;
}

bool checkOtaCommand(char* hiveId, char* tag, char* target) {
    TinyGsm& modem = *mqttClient->getModem();

    // Subscribe to OTA topic
    modem.sendAT("+SMSUB=\"", MQTT_OTA_TOPIC, "\",1");
    if (modem.waitResponse(5000) != 1) {
        return false;
    }

    // Check for pending messages (brief wait)
    delay(2000);

    // Read any received message
    String response;
    if (modem.stream.available()) {
        response = modem.stream.readStringUntil('\n');
    }

    // Unsubscribe
    modem.sendAT("+SMUNSUB=\"", MQTT_OTA_TOPIC, "\"");
    modem.waitResponse();

    if (response.length() == 0) {
        return false;
    }

    // Parse JSON payload: {"hive_id":"X","tag":"X","target":"X"}
    // Simple parse without JSON library
    int hiveStart = response.indexOf("\"hive_id\":\"") + 11;
    int hiveEnd   = response.indexOf("\"", hiveStart);
    int tagStart  = response.indexOf("\"tag\":\"") + 7;
    int tagEnd    = response.indexOf("\"", tagStart);
    int tgtStart  = response.indexOf("\"target\":\"") + 10;
    int tgtEnd    = response.indexOf("\"", tgtStart);

    if (hiveStart < 11 || tagStart < 7 || tgtStart < 10) {
        return false;
    }

    response.substring(hiveStart, hiveEnd).toCharArray(hiveId, 16);
    response.substring(tagStart, tagEnd).toCharArray(tag, 32);
    response.substring(tgtStart, tgtEnd).toCharArray(target, 16);

    Serial.printf("[MQTT] OTA command: hive=%s tag=%s target=%s\n", hiveId, tag, target);
    return true;
}

void disconnect() {
    TinyGsm& modem = *mqttClient->getModem();
    modem.sendAT("+SMDISC");
    modem.waitResponse();
    Serial.println("[MQTT] Disconnected");
}

}  // namespace MqttPublisher
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/collector/src/mqtt_publisher.h firmware/collector/src/mqtt_publisher.cpp
git commit -m "feat: add MQTT publisher with batch publish and OTA command subscribe"
```

---

## Task 6: Time Sync Module

**Files:**
- Create: `firmware/collector/src/time_sync.h`
- Create: `firmware/collector/src/time_sync.cpp`

- [ ] **Step 1: Create `firmware/collector/src/time_sync.h`**

```cpp
#pragma once

#include <cstdint>

/// Broadcasts current epoch time to all hive nodes via ESP-NOW.
namespace TimeSync {

    /// Broadcast TIME_SYNC packet. Call after NTP sync each publish cycle.
    bool broadcast(uint32_t epochSeconds);

}  // namespace TimeSync
```

- [ ] **Step 2: Create `firmware/collector/src/time_sync.cpp`**

```cpp
#include "time_sync.h"
#include "espnow_protocol.h"

#include <Arduino.h>
#include <esp_now.h>
#include <cstring>

namespace TimeSync {

bool broadcast(uint32_t epochSeconds) {
    // Build the packet: header + TimeSyncPayload
    uint8_t packet[sizeof(EspNowHeader) + sizeof(TimeSyncPayload)];

    auto* header = reinterpret_cast<EspNowHeader*>(packet);
    header->type = EspNowPacketType::TIME_SYNC;
    header->data_len = sizeof(TimeSyncPayload);

    auto* payload = reinterpret_cast<TimeSyncPayload*>(packet + sizeof(EspNowHeader));
    payload->epoch_seconds = epochSeconds;

    // Broadcast to all nodes (FF:FF:FF:FF:FF:FF)
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Add broadcast peer if not already added
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);  // Ignore error if already exists

    esp_err_t result = esp_now_send(broadcastMac, packet, sizeof(packet));

    if (result == ESP_OK) {
        Serial.printf("[TIMESYNC] Broadcast epoch %u\n", epochSeconds);
        return true;
    }

    Serial.printf("[TIMESYNC] Broadcast failed: %d\n", result);
    return false;
}

}  // namespace TimeSync
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/collector/src/time_sync.h firmware/collector/src/time_sync.cpp
git commit -m "feat: add time sync broadcast to hive nodes via ESP-NOW"
```

---

## Task 7: OTA Self-Update Module

**Files:**
- Create: `firmware/collector/src/ota_self.h`
- Create: `firmware/collector/src/ota_self.cpp`

- [ ] **Step 1: Create `firmware/collector/src/ota_self.h`**

```cpp
#pragma once

/// Self-OTA: download collector firmware from GitHub and apply.
namespace OtaSelf {

    /// Download firmware for the given release tag, write to OTA partition, reboot.
    bool downloadAndApply(const char* tag);

    /// Run health checks after OTA boot. Mark valid or rollback.
    void validateNewFirmware();

}  // namespace OtaSelf
```

- [ ] **Step 2: Create `firmware/collector/src/ota_self.cpp`**

```cpp
#include "ota_self.h"
#include "config.h"
#include "cellular.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <rom/crc.h>

namespace OtaSelf {

bool downloadAndApply(const char* tag) {
    // Build URL: github.com/sjordan0228/hivesense-monitor/releases/download/{tag}/collector.bin
    char url[256];
    snprintf(url, sizeof(url), "%s%s/collector.bin", GITHUB_RELEASE_BASE, tag);

    Serial.printf("[OTA-SELF] Downloading: %s\n", url);

    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (updatePartition == nullptr) {
        Serial.println("[OTA-SELF] ERROR: No update partition");
        return false;
    }

    esp_ota_handle_t otaHandle;
    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: esp_ota_begin: %s\n", esp_err_to_name(err));
        return false;
    }

    // HTTP GET via TinyGSM client
    TinyGsmClient& client = Cellular::getClient();

    // Parse host and path from URL — simplified for GitHub releases
    // Use esp_http_client for proper HTTPS with TLS
    esp_http_client_config_t httpConfig = {};
    httpConfig.url = url;
    httpConfig.timeout_ms = 30000;

    esp_http_client_handle_t httpClient = esp_http_client_init(&httpConfig);
    err = esp_http_client_open(httpClient, 0);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: HTTP open: %s\n", esp_err_to_name(err));
        esp_ota_abort(otaHandle);
        esp_http_client_cleanup(httpClient);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(httpClient);
    Serial.printf("[OTA-SELF] Content length: %d\n", contentLength);

    uint32_t totalWritten = 0;
    uint32_t runningCrc = 0;
    uint8_t buf[1024];
    int bytesRead;

    while ((bytesRead = esp_http_client_read(httpClient, reinterpret_cast<char*>(buf), sizeof(buf))) > 0) {
        err = esp_ota_write(otaHandle, buf, bytesRead);
        if (err != ESP_OK) {
            Serial.printf("[OTA-SELF] ERROR: ota_write: %s\n", esp_err_to_name(err));
            esp_ota_abort(otaHandle);
            esp_http_client_cleanup(httpClient);
            return false;
        }
        runningCrc = crc32_le(runningCrc, buf, bytesRead);
        totalWritten += bytesRead;
    }

    esp_http_client_cleanup(httpClient);

    err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: ota_end: %s\n", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        Serial.printf("[OTA-SELF] ERROR: set_boot_partition: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[OTA-SELF] Downloaded %u bytes, CRC32=0x%08X — rebooting\n",
                  totalWritten, runningCrc);
    Serial.flush();
    esp_restart();

    return true;  // Unreachable
}

void validateNewFirmware() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    Serial.println("[OTA-SELF] New firmware — validating...");

    // Health check: verify modem responds
    bool healthy = Cellular::powerOn();
    if (healthy) {
        Cellular::powerOff();
    }

    if (healthy) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA-SELF] Firmware validated");
    } else {
        Serial.println("[OTA-SELF] Validation FAILED — rolling back");
        Serial.flush();
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

}  // namespace OtaSelf
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/collector/src/ota_self.h firmware/collector/src/ota_self.cpp
git commit -m "feat: add self-OTA module — download from GitHub and apply"
```

---

## Task 8: OTA Relay Module

**Files:**
- Create: `firmware/collector/src/ota_relay.h`
- Create: `firmware/collector/src/ota_relay.cpp`

- [ ] **Step 1: Create `firmware/collector/src/ota_relay.h`**

```cpp
#pragma once

#include <cstdint>

/// Downloads hive node firmware from GitHub and relays to target node via ESP-NOW.
namespace OtaRelay {

    /// Download firmware binary for the given tag. Stores in OTA partition temporarily.
    bool downloadFirmware(const char* tag);

    /// Begin chunked relay to target hive node.
    bool startRelay(const char* hiveId);

    /// Send next batch of chunks. Call repeatedly until complete.
    /// Returns true if relay is still in progress, false when done or failed.
    bool continueRelay();

    /// Check if a relay is currently active.
    bool isRelayInProgress();

    /// Cancel active relay and clean up.
    void abortRelay();

}  // namespace OtaRelay
```

- [ ] **Step 2: Create `firmware/collector/src/ota_relay.cpp`**

```cpp
#include "ota_relay.h"
#include "config.h"
#include "cellular.h"
#include "espnow_receiver.h"
#include "ota_protocol.h"
#include "espnow_protocol.h"

#include <Arduino.h>
#include <esp_partition.h>
#include <esp_http_client.h>
#include <esp_now.h>
#include <rom/crc.h>
#include <cstring>

namespace {

const esp_partition_t* storagePartition = nullptr;
bool     relayActive     = false;
uint16_t totalChunks     = 0;
uint16_t nextChunkToSend = 0;
uint32_t firmwareSize    = 0;
uint32_t firmwareCrc32   = 0;
uint8_t  targetMac[6]    = {};
char     targetHiveId[16] = {};

/// Read a chunk from the storage partition at the given chunk index.
bool readChunk(uint16_t chunkIndex, uint8_t* data, uint8_t& dataLen) {
    uint32_t offset = static_cast<uint32_t>(chunkIndex) * OTA_MAX_CHUNK_DATA;
    uint32_t remaining = firmwareSize - offset;
    dataLen = (remaining > OTA_MAX_CHUNK_DATA)
        ? OTA_MAX_CHUNK_DATA
        : static_cast<uint8_t>(remaining);

    esp_err_t err = esp_partition_read(storagePartition, offset, data, dataLen);
    return (err == ESP_OK);
}

/// Send an OTA packet wrapped in ESP-NOW header to the target node.
bool sendOtaPacket(const OtaPacket& otaPacket) {
    uint8_t packet[sizeof(EspNowHeader) + sizeof(OtaPacket)];

    auto* header = reinterpret_cast<EspNowHeader*>(packet);
    header->type = EspNowPacketType::OTA_PACKET;
    header->data_len = sizeof(OtaPacket);

    memcpy(packet + sizeof(EspNowHeader), &otaPacket, sizeof(OtaPacket));

    // Ensure target is registered as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, targetMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);  // Ignore if already exists

    esp_err_t result = esp_now_send(targetMac, packet, sizeof(packet));
    return (result == ESP_OK);
}

}  // anonymous namespace

namespace OtaRelay {

bool downloadFirmware(const char* tag) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s/hive-node.bin", GITHUB_RELEASE_BASE, tag);

    Serial.printf("[OTA-RELAY] Downloading: %s\n", url);

    // Use the unused OTA partition as temporary storage for the hive node firmware
    storagePartition = esp_ota_get_next_update_partition(nullptr);
    if (storagePartition == nullptr) {
        Serial.println("[OTA-RELAY] ERROR: No storage partition");
        return false;
    }

    // Erase partition before writing
    esp_err_t err = esp_partition_erase_range(storagePartition, 0, storagePartition->size);
    if (err != ESP_OK) {
        Serial.printf("[OTA-RELAY] ERROR: Partition erase: %s\n", esp_err_to_name(err));
        return false;
    }

    esp_http_client_config_t httpConfig = {};
    httpConfig.url = url;
    httpConfig.timeout_ms = 30000;

    esp_http_client_handle_t httpClient = esp_http_client_init(&httpConfig);
    err = esp_http_client_open(httpClient, 0);
    if (err != ESP_OK) {
        Serial.printf("[OTA-RELAY] ERROR: HTTP open: %s\n", esp_err_to_name(err));
        esp_http_client_cleanup(httpClient);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(httpClient);
    Serial.printf("[OTA-RELAY] Content length: %d\n", contentLength);

    uint32_t written = 0;
    uint32_t crc = 0;
    uint8_t buf[1024];
    int bytesRead;

    while ((bytesRead = esp_http_client_read(httpClient, reinterpret_cast<char*>(buf), sizeof(buf))) > 0) {
        err = esp_partition_write(storagePartition, written, buf, bytesRead);
        if (err != ESP_OK) {
            Serial.printf("[OTA-RELAY] ERROR: Partition write: %s\n", esp_err_to_name(err));
            esp_http_client_cleanup(httpClient);
            return false;
        }
        crc = crc32_le(crc, buf, bytesRead);
        written += bytesRead;
    }

    esp_http_client_cleanup(httpClient);

    firmwareSize  = written;
    firmwareCrc32 = crc;
    totalChunks   = (written + OTA_MAX_CHUNK_DATA - 1) / OTA_MAX_CHUNK_DATA;

    Serial.printf("[OTA-RELAY] Downloaded %u bytes, %u chunks, CRC32=0x%08X\n",
                  firmwareSize, totalChunks, firmwareCrc32);
    return true;
}

bool startRelay(const char* hiveId) {
    strncpy(targetHiveId, hiveId, 15);
    targetHiveId[15] = '\0';

    if (!EspNowReceiver::getMacForHive(hiveId, targetMac)) {
        Serial.printf("[OTA-RELAY] ERROR: No MAC known for %s\n", hiveId);
        return false;
    }

    // Send OTA_START
    OtaPacket startPkt = {};
    startPkt.command = OtaCommand::OTA_START;
    startPkt.total_chunks = totalChunks;

    OtaStartPayload startPayload = {};
    startPayload.firmware_size = firmwareSize;
    startPayload.total_chunks  = totalChunks;
    startPayload.crc32         = firmwareCrc32;
    memcpy(startPkt.data, &startPayload, sizeof(OtaStartPayload));
    startPkt.data_len = sizeof(OtaStartPayload);

    if (!sendOtaPacket(startPkt)) {
        Serial.println("[OTA-RELAY] ERROR: Failed to send OTA_START");
        return false;
    }

    nextChunkToSend = 1;  // Chunks are 1-indexed in the protocol
    relayActive = true;

    Serial.printf("[OTA-RELAY] Started relay to %s — %u chunks\n", hiveId, totalChunks);
    return true;
}

bool continueRelay() {
    if (!relayActive) return false;

    if (nextChunkToSend > totalChunks) {
        // All chunks sent — send OTA_END
        OtaPacket endPkt = {};
        endPkt.command = OtaCommand::OTA_END;
        endPkt.total_chunks = totalChunks;
        sendOtaPacket(endPkt);

        relayActive = false;
        Serial.println("[OTA-RELAY] Relay complete — OTA_END sent");
        return false;
    }

    // Send next chunk
    OtaPacket chunkPkt = {};
    chunkPkt.command = OtaCommand::OTA_CHUNK;
    chunkPkt.chunk_index = nextChunkToSend;
    chunkPkt.total_chunks = totalChunks;

    if (!readChunk(nextChunkToSend - 1, chunkPkt.data, chunkPkt.data_len)) {
        Serial.printf("[OTA-RELAY] ERROR: Failed to read chunk %u\n", nextChunkToSend);
        abortRelay();
        return false;
    }

    if (sendOtaPacket(chunkPkt)) {
        nextChunkToSend++;
    }

    // Small delay between chunks to avoid flooding
    delay(10);

    return true;  // Still in progress
}

bool isRelayInProgress() {
    return relayActive;
}

void abortRelay() {
    if (relayActive) {
        OtaPacket abortPkt = {};
        abortPkt.command = OtaCommand::OTA_ABORT;
        sendOtaPacket(abortPkt);
    }
    relayActive = false;
    nextChunkToSend = 0;
    Serial.println("[OTA-RELAY] Aborted");
}

}  // namespace OtaRelay
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/collector/src/ota_relay.h firmware/collector/src/ota_relay.cpp
git commit -m "feat: add OTA relay — download from GitHub, chunk to hive nodes via ESP-NOW"
```

---

## Task 9: Main Loop Integration

**Files:**
- Modify: `firmware/collector/src/main.cpp`

- [ ] **Step 1: Rewrite `firmware/collector/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "espnow_receiver.h"
#include "cellular.h"
#include "mqtt_publisher.h"
#include "time_sync.h"
#include "ota_relay.h"
#include "ota_self.h"

namespace {

uint32_t lastPublishTime = 0;
const uint32_t publishIntervalMs = static_cast<uint32_t>(PUBLISH_INTERVAL_MIN) * 60UL * 1000UL;

/// Execute one publish cycle: modem on → MQTT → time sync → modem off.
void runPublishCycle() {
    Serial.println("[MAIN] === PUBLISH CYCLE ===");

    if (!Cellular::powerOn()) {
        Serial.println("[MAIN] Modem failed — skipping cycle");
        return;
    }

    if (!Cellular::waitForNetwork()) {
        Cellular::powerOff();
        return;
    }

    // NTP sync — get accurate time
    uint32_t epoch = Cellular::syncNtp();

    // MQTT publish
    if (MqttPublisher::connect(Cellular::getClient())) {
        PayloadBuffer& buffer = EspNowReceiver::getBuffer();
        uint8_t count = MqttPublisher::publishBatch(buffer);
        Serial.printf("[MAIN] Published %u hive payloads\n", count);
        buffer.clear();

        // Check for OTA commands
        char hiveId[16], tag[32], target[16];
        if (MqttPublisher::checkOtaCommand(hiveId, tag, target)) {
            if (strcmp(target, "collector") == 0) {
                Serial.printf("[MAIN] Self-OTA requested: %s\n", tag);
                MqttPublisher::disconnect();
                OtaSelf::downloadAndApply(tag);
                // Does not return if successful
            } else if (strcmp(target, "node") == 0) {
                Serial.printf("[MAIN] OTA relay requested: %s → %s\n", tag, hiveId);
                OtaRelay::downloadFirmware(tag);
                OtaRelay::startRelay(hiveId);
                // Relay continues in main loop
            }
        }

        MqttPublisher::disconnect();
    }

    // Time sync broadcast to all hive nodes
    if (epoch > 0) {
        TimeSync::broadcast(epoch);
    }

    Cellular::powerOff();
    Serial.println("[MAIN] Publish cycle complete");
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    Serial.println("\n[MAIN] HiveSense Collector — starting");

    // Validate firmware after OTA boot
    OtaSelf::validateNewFirmware();

    // Initialize ESP-NOW receiver (always on)
    EspNowReceiver::initialize();

    // Load MQTT config from NVS
    MqttPublisher::initialize();

    lastPublishTime = millis();
    Serial.println("[MAIN] Ready — listening for ESP-NOW packets");
}

void loop() {
    // Check if publish cycle is due
    if (millis() - lastPublishTime >= publishIntervalMs) {
        runPublishCycle();
        lastPublishTime = millis();
    }

    // Continue OTA relay if active (send chunks between publish cycles)
    if (OtaRelay::isRelayInProgress()) {
        OtaRelay::continueRelay();
    }

    // Light sleep between activity
    delay(10);
}
```

- [ ] **Step 2: Verify build**

Run: `cd firmware/collector && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/collector/src/main.cpp
git commit -m "feat: integrate all collector modules into main loop"
```

---

## Task 10: Integration Build & Verify

- [ ] **Step 1: Clean build**

Run: `cd firmware/collector && pio run --target clean && pio run`
Expected: BUILD SUCCESS. Note flash and RAM usage.

- [ ] **Step 2: Verify all files present**

```bash
find firmware/collector -name "*.cpp" -o -name "*.h" | grep -v ".pio" | sort
```

Expected:
```
firmware/collector/include/config.h
firmware/collector/include/types.h
firmware/collector/src/cellular.cpp
firmware/collector/src/cellular.h
firmware/collector/src/espnow_receiver.cpp
firmware/collector/src/espnow_receiver.h
firmware/collector/src/main.cpp
firmware/collector/src/mqtt_publisher.cpp
firmware/collector/src/mqtt_publisher.h
firmware/collector/src/ota_relay.cpp
firmware/collector/src/ota_relay.h
firmware/collector/src/ota_self.cpp
firmware/collector/src/ota_self.h
firmware/collector/src/time_sync.cpp
firmware/collector/src/time_sync.h
firmware/collector/src/types.cpp
```

- [ ] **Step 3: Verify shared headers used by both firmwares**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS — hive node still builds with the new shared headers.

- [ ] **Step 4: Commit**

```bash
git add -A firmware/
git commit -m "feat: complete collector firmware — all modules integrated"
```

---

## Task 11: Hive Node ESP-NOW Updates (Issue #1)

**Files:**
- Create: `firmware/shared/espnow_protocol.h` (already done in Task 2)
- Modify: `firmware/hive-node/src/comms_espnow.cpp` — add receive callback, packet wrapping

- [ ] **Step 1: Add ESP-NOW receive callback to hive node**

In `firmware/hive-node/src/comms_espnow.cpp`, add a receive callback that handles TIME_SYNC and OTA_PACKET types:

```cpp
#include "espnow_protocol.h"
#include "ota_update.h"
#include "state_machine.h"
```

Add in anonymous namespace:

```cpp
/// ESP-NOW receive callback — handles TIME_SYNC and OTA packets.
void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < static_cast<int>(sizeof(EspNowHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const EspNowHeader*>(data);
    const uint8_t* payload = data + sizeof(EspNowHeader);

    switch (header->type) {
        case EspNowPacketType::TIME_SYNC: {
            if (header->data_len >= sizeof(TimeSyncPayload)) {
                const auto* ts = reinterpret_cast<const TimeSyncPayload*>(payload);
                StateMachine::setTime(ts->epoch_seconds);
                Serial.printf("[ESPNOW] Time synced: %u\n", ts->epoch_seconds);
            }
            break;
        }

        case EspNowPacketType::OTA_PACKET: {
            if (header->data_len >= sizeof(OtaPacket)) {
                const auto* otaPkt = reinterpret_cast<const OtaPacket*>(payload);
                OtaUpdate::handleOtaPacket(*otaPkt);
            }
            break;
        }

        default:
            break;
    }
}
```

Register the callback in `initialize()`:

```cpp
esp_now_register_recv_cb(onDataReceived);
```

- [ ] **Step 2: Wrap HivePayload in EspNowHeader for sending**

In `sendPayload()`, wrap the payload:

```cpp
bool sendPayload(HivePayload& payload) {
    // Build wrapped packet
    uint8_t packet[sizeof(EspNowHeader) + sizeof(HivePayload)];
    auto* header = reinterpret_cast<EspNowHeader*>(packet);
    header->type = EspNowPacketType::SENSOR_DATA;
    header->data_len = sizeof(HivePayload);
    memcpy(packet + sizeof(EspNowHeader), &payload, sizeof(HivePayload));

    for (uint8_t attempt = 1; attempt <= ESPNOW_MAX_RETRIES; ++attempt) {
        sendComplete = false;
        sendSuccess  = false;

        esp_err_t err = esp_now_send(collectorMac, packet, sizeof(packet));
        // ... rest of retry logic unchanged, using packet instead of raw payload
```

- [ ] **Step 3: Verify both firmwares build**

Run: `cd firmware/hive-node && pio run`
Run: `cd firmware/collector && pio run`
Expected: Both BUILD SUCCESS

- [ ] **Step 4: Commit and close issue**

```bash
git add firmware/hive-node/src/comms_espnow.cpp
git commit -m "feat: add ESP-NOW packet wrapping, TIME_SYNC receive, OTA routing (closes #1)"
```

---

## Task 12: Update Project State

**Files:**
- Modify: `.mex/ROUTER.md`
- Modify: `.mex/context/decisions.md`

- [ ] **Step 1: Update ROUTER.md**

Update phase to reflect collector firmware complete. Add collector to completed section.

- [ ] **Step 2: Update decisions.md**

Add entries for:
- TinyGSM for modem abstraction
- Batch MQTT publishing (modem on once per cycle)
- Always-listening ESP-NOW (no scheduled windows)
- Time sync broadcast every publish cycle

- [ ] **Step 3: Commit and push**

```bash
git add .mex/
git commit -m "chore: update .mex — collector firmware complete"
git push origin main
```
