#include "time_sync.h"
#include "espnow_protocol.h"

#include <Arduino.h>
#include <esp_now.h>
#include <cstring>

namespace TimeSync {

bool broadcast(uint32_t epochSeconds) {
    uint8_t packet[sizeof(EspNowHeader) + sizeof(TimeSyncPayload)];

    auto* header = reinterpret_cast<EspNowHeader*>(packet);
    header->type = EspNowPacketType::TIME_SYNC;
    header->data_len = sizeof(TimeSyncPayload);

    auto* payload = reinterpret_cast<TimeSyncPayload*>(packet + sizeof(EspNowHeader));
    payload->epoch_seconds = epochSeconds;

    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);  // Ignore if already exists

    esp_err_t result = esp_now_send(broadcastMac, packet, sizeof(packet));
    if (result == ESP_OK) {
        Serial.printf("[TIMESYNC] Broadcast epoch %u\n", epochSeconds);
        return true;
    }

    Serial.printf("[TIMESYNC] Broadcast failed: %d\n", result);
    return false;
}

}  // namespace TimeSync
