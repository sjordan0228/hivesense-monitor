#include "comms_espnow.h"
#include "config.h"
#include "types.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

// Callback-to-task synchronization — volatile because written from ISR-like
// ESP-NOW callback context and read from task context.
volatile bool sendComplete = false;
volatile bool sendSuccess  = false;

uint8_t collectorMac[6];

/// Load collector MAC from NVS. Falls back to broadcast if the key is absent
/// so the node still functions before provisioning.
void loadCollectorMac() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);

    size_t len = prefs.getBytesLength(NVS_KEY_COLLECTOR);
    if (len == 6) {
        prefs.getBytes(NVS_KEY_COLLECTOR, collectorMac, 6);
        Serial.printf("[ESPNOW] Loaded collector MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      collectorMac[0], collectorMac[1], collectorMac[2],
                      collectorMac[3], collectorMac[4], collectorMac[5]);
    } else {
        // Broadcast allows the collector to receive even before provisioning.
        memset(collectorMac, 0xFF, 6);
        Serial.println("[ESPNOW] No collector MAC in NVS — using broadcast");
    }

    prefs.end();
}

/// ESP-NOW send callback. Runs in a WiFi task context — keep it minimal.
void onDataSent(const uint8_t* macAddr, esp_now_send_status_t status) {
    sendSuccess  = (status == ESP_NOW_SEND_SUCCESS);
    sendComplete = true;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace CommsEspNow {

bool initialize() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] esp_now_init failed");
        return false;
    }

    esp_now_register_send_cb(onDataSent);

    loadCollectorMac();

    // Register the collector as a peer. Channel 0 lets the ESP-NOW stack
    // inherit the current channel; encryption is unnecessary for sensor data.
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, collectorMac, 6);
    peer.channel = 0;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Failed to add peer");
        esp_now_deinit();
        return false;
    }

    Serial.println("[ESPNOW] Initialized");
    return true;
}

bool sendPayload(HivePayload& payload) {
    bool ackReceived = false;

    for (uint8_t attempt = 1; attempt <= ESPNOW_MAX_RETRIES; ++attempt) {
        sendComplete = false;
        sendSuccess  = false;

        esp_err_t err = esp_now_send(collectorMac,
                                     reinterpret_cast<const uint8_t*>(&payload),
                                     sizeof(HivePayload));
        if (err != ESP_OK) {
            Serial.printf("[ESPNOW] esp_now_send error on attempt %u: %d\n", attempt, err);
        } else {
            // Busy-wait for the send callback — maximum 1 second.
            const uint32_t deadline = millis() + 1000;
            while (!sendComplete && millis() < deadline) {
                delay(10);
            }

            if (sendComplete && sendSuccess) {
                Serial.printf("[ESPNOW] ACK on attempt %u\n", attempt);
                ackReceived = true;
                break;
            } else {
                Serial.printf("[ESPNOW] No ACK on attempt %u\n", attempt);
            }
        }

        if (attempt < ESPNOW_MAX_RETRIES) {
            delay(ESPNOW_RETRY_DELAY_MS);
        }
    }

    if (ackReceived) {
        // Best-effort RSSI read — fails gracefully if association info is
        // unavailable (node is not connected to an AP).
        wifi_ap_record_t apInfo{};
        if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) {
            payload.rssi = apInfo.rssi;
            Serial.printf("[ESPNOW] RSSI: %d dBm\n", payload.rssi);
        } else {
            payload.rssi = 0;
        }
    }

    return ackReceived;
}

void shutdown() {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    Serial.println("[ESPNOW] Shutdown");
}

}  // namespace CommsEspNow
