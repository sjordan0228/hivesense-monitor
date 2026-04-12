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
            CommsBle::waitForSyncComplete();
            CommsBle::shutdown();

            Serial.println("[SM] BLE sync complete");

            return PowerManager::isDaytime(getCurrentHour())
                ? NodeState::DAYTIME_IDLE
                : NodeState::NIGHTTIME_SLEEP;
        }

        case NodeState::DAYTIME_IDLE: {
            Serial.println("[SM] === DAYTIME_IDLE ===");

            rtcEpochAtSleep = getTimestamp();
            rtcMillisAtSleep = millis();

            PowerManager::enableLightSleep();

            return NodeState::SENSOR_READ;
        }

        case NodeState::NIGHTTIME_SLEEP: {
            Serial.println("[SM] === NIGHTTIME_SLEEP ===");

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
