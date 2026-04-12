#include "ota_relay.h"
#include "config.h"
#include "espnow_receiver.h"
#include "ota_protocol.h"
#include "espnow_protocol.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
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

bool readChunk(uint16_t chunkIndex, uint8_t* data, uint8_t& dataLen) {
    uint32_t offset = static_cast<uint32_t>(chunkIndex) * OTA_MAX_CHUNK_DATA;
    uint32_t remaining = firmwareSize - offset;
    dataLen = (remaining > OTA_MAX_CHUNK_DATA)
        ? OTA_MAX_CHUNK_DATA
        : static_cast<uint8_t>(remaining);

    return (esp_partition_read(storagePartition, offset, data, dataLen) == ESP_OK);
}

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
    esp_now_add_peer(&peer);

    return (esp_now_send(targetMac, packet, sizeof(packet)) == ESP_OK);
}

}  // anonymous namespace

namespace OtaRelay {

bool downloadFirmware(const char* tag) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s/hive-node.bin", GITHUB_RELEASE_BASE, tag);
    Serial.printf("[OTA-RELAY] Downloading: %s\n", url);

    // Store in the unused OTA partition (3.5 MB — plenty for hive node firmware)
    storagePartition = esp_ota_get_next_update_partition(nullptr);
    if (storagePartition == nullptr) {
        Serial.println("[OTA-RELAY] ERROR: No storage partition");
        return false;
    }

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

    esp_http_client_fetch_headers(httpClient);

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

    nextChunkToSend = 1;
    relayActive = true;

    Serial.printf("[OTA-RELAY] Started relay to %s — %u chunks\n", hiveId, totalChunks);
    return true;
}

bool continueRelay() {
    if (!relayActive) return false;

    if (nextChunkToSend > totalChunks) {
        OtaPacket endPkt = {};
        endPkt.command = OtaCommand::OTA_END;
        endPkt.total_chunks = totalChunks;
        sendOtaPacket(endPkt);

        relayActive = false;
        Serial.println("[OTA-RELAY] Complete — OTA_END sent");
        return false;
    }

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

    delay(10);  // Avoid flooding ESP-NOW
    return true;
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
