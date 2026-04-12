#include "espnow_receiver.h"
#include "config.h"
#include "espnow_protocol.h"
#include "hive_payload.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <cstring>

namespace {

    struct MacEntry {
        char    hiveId[16];
        uint8_t mac[6];
        bool    occupied;
    };

    PayloadBuffer buffer;
    MacEntry      knownMacs[20];

    void updateMacTable(const char* hiveId, const uint8_t* mac) {
        // Update existing entry if hive_id already known
        for (uint8_t i = 0; i < 20; i++) {
            if (knownMacs[i].occupied &&
                strncmp(knownMacs[i].hiveId, hiveId, 16) == 0) {
                memcpy(knownMacs[i].mac, mac, 6);
                return;
            }
        }
        // Allocate new slot
        for (uint8_t i = 0; i < 20; i++) {
            if (!knownMacs[i].occupied) {
                strncpy(knownMacs[i].hiveId, hiveId, 16);
                knownMacs[i].hiveId[15] = '\0';
                memcpy(knownMacs[i].mac, mac, 6);
                knownMacs[i].occupied = true;
                Serial.printf("[ESPNOW] New node registered: %s\n", hiveId);
                return;
            }
        }
        Serial.printf("[ESPNOW] MAC table full — could not register %s\n", hiveId);
    }

    void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
        if (len < static_cast<int>(sizeof(EspNowHeader))) {
            Serial.printf("[ESPNOW] Packet too short (%d bytes), dropping\n", len);
            return;
        }

        const EspNowHeader* header  = reinterpret_cast<const EspNowHeader*>(data);
        const uint8_t*      payload = data + sizeof(EspNowHeader);

        switch (header->type) {
            case EspNowPacketType::SENSOR_DATA: {
                if (len < static_cast<int>(sizeof(EspNowHeader) + sizeof(HivePayload))) {
                    Serial.printf("[ESPNOW] SENSOR_DATA packet too short (%d bytes)\n", len);
                    break;
                }
                const HivePayload* hp = reinterpret_cast<const HivePayload*>(payload);
                updateMacTable(hp->hive_id, mac);
                int8_t slot = buffer.findOrAllocate(hp->hive_id);
                if (slot < 0) {
                    Serial.printf("[ESPNOW] Buffer full — dropping payload from %s\n", hp->hive_id);
                    break;
                }
                buffer.entries[slot].payload    = *hp;
                buffer.entries[slot].occupied   = true;
                buffer.entries[slot].receivedAt = millis();
                Serial.printf("[ESPNOW] SENSOR_DATA stored for %s in slot %d\n",
                              hp->hive_id, slot);
                break;
            }

            case EspNowPacketType::OTA_PACKET:
                Serial.printf("[ESPNOW] OTA_PACKET received — relay handling deferred to OTA module\n");
                break;

            default:
                Serial.printf("[ESPNOW] Unknown packet type 0x%02X, dropping\n",
                              static_cast<uint8_t>(header->type));
                break;
        }
    }

} // anonymous namespace

namespace EspNowReceiver {

    bool initialize() {
        memset(&buffer, 0, sizeof(buffer));
        memset(knownMacs, 0, sizeof(knownMacs));

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

        if (esp_now_init() != ESP_OK) {
            Serial.printf("[ESPNOW] esp_now_init() failed\n");
            return false;
        }

        esp_now_register_recv_cb(onDataReceived);
        Serial.printf("[ESPNOW] Initialized — listening for nodes\n");
        return true;
    }

    PayloadBuffer& getBuffer() {
        return buffer;
    }

    bool getMacForHive(const char* hiveId, uint8_t* macOut) {
        for (uint8_t i = 0; i < 20; i++) {
            if (knownMacs[i].occupied &&
                strncmp(knownMacs[i].hiveId, hiveId, 16) == 0) {
                memcpy(macOut, knownMacs[i].mac, 6);
                return true;
            }
        }
        return false;
    }

} // namespace EspNowReceiver
