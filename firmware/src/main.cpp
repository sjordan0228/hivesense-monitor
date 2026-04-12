#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "power_manager.h"

// RTC memory survives light sleep, resets on deep sleep power-on
RTC_DATA_ATTR static NodeState currentState = NodeState::BOOT;
RTC_DATA_ATTR static uint32_t  bootCount = 0;

void setup() {
    Serial.begin(115200);
    bootCount++;

    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();

    Serial.printf("[BOOT] count=%u, wakeup_reason=%d\n", bootCount, wakeupReason);
    Serial.println("[BOOT] HiveSense Node — Phase 1 firmware");

    // Only reset state on cold boot — preserve RTC state across light sleep wakes
    if (wakeupReason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        currentState = NodeState::BOOT;
    }

    PowerManager::initialize();
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
            Serial.println("[STATE] DAYTIME_IDLE — entering light sleep");
            PowerManager::enableLightSleep();
            currentState = NodeState::SENSOR_READ;
            break;

        case NodeState::NIGHTTIME_SLEEP:
            Serial.println("[STATE] NIGHTTIME_SLEEP — entering deep sleep");
            PowerManager::enterDeepSleep(DEFAULT_READ_INTERVAL_MIN);
            // Does not return — wakes up through setup()
            break;

        case NodeState::BLE_SYNC:
            Serial.println("[STATE] BLE_SYNC — TODO: transfer logs");
            currentState = NodeState::DAYTIME_IDLE;
            break;
    }
}
