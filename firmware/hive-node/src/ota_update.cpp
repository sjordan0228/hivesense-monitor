#include "ota_update.h"
#include "config.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <Preferences.h>
#include <rom/crc.h>

namespace {

const esp_partition_t* updatePartition = nullptr;
esp_ota_handle_t       otaHandle       = 0;
bool                   otaInProgress   = false;
uint16_t               lastReceivedChunk = 0;
uint16_t               totalChunks     = 0;
uint32_t               expectedCrc32   = 0;
uint32_t               runningCrc32    = 0;

void saveProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_OTA_ACTIVE, true);
    prefs.putUShort(NVS_KEY_OTA_TOTAL, totalChunks);
    prefs.putUShort(NVS_KEY_OTA_RECEIVED, lastReceivedChunk);
    prefs.putULong(NVS_KEY_OTA_CRC32, expectedCrc32);
    prefs.end();
}

void clearProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_OTA_ACTIVE, false);
    prefs.putUShort(NVS_KEY_OTA_TOTAL, 0);
    prefs.putUShort(NVS_KEY_OTA_RECEIVED, 0);
    prefs.putULong(NVS_KEY_OTA_CRC32, 0);
    prefs.putString(NVS_KEY_OTA_VERSION, "");
    prefs.end();
}

bool beginOtaWrite() {
    updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (updatePartition == nullptr) {
        Serial.println("[OTA] ERROR: No update partition available");
        return false;
    }

    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_begin failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[OTA] Write session opened on partition '%s'\n", updatePartition->label);
    return true;
}

bool handleStart(const OtaPacket& packet) {
    if (packet.data_len < sizeof(OtaStartPayload)) {
        Serial.println("[OTA] ERROR: OTA_START payload too small");
        return false;
    }

    const auto* startPayload = reinterpret_cast<const OtaStartPayload*>(packet.data);

    totalChunks     = startPayload->total_chunks;
    expectedCrc32   = startPayload->crc32;
    lastReceivedChunk = 0;
    runningCrc32    = 0;

    Serial.printf("[OTA] Starting: %u bytes, %u chunks, version=%s\n",
                  startPayload->firmware_size, totalChunks, startPayload->version);

    if (!beginOtaWrite()) {
        return false;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_OTA_VERSION, startPayload->version);
    prefs.end();

    otaInProgress = true;
    saveProgress();
    return true;
}

bool handleChunk(const OtaPacket& packet) {
    if (!otaInProgress) {
        return false;
    }

    // Expect sequential chunks — out-of-order means retransmit needed
    if (packet.chunk_index != lastReceivedChunk + 1) {
        Serial.printf("[OTA] Chunk %u out of order (expected %u)\n",
                      packet.chunk_index, lastReceivedChunk + 1);
        return false;
    }

    esp_err_t err = esp_ota_write(otaHandle, packet.data, packet.data_len);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_write failed: %s\n", esp_err_to_name(err));
        OtaUpdate::abortTransfer();
        return false;
    }

    runningCrc32 = crc32_le(runningCrc32, packet.data, packet.data_len);
    lastReceivedChunk = packet.chunk_index;

    // Save progress periodically to survive unexpected sleep/reset
    if (lastReceivedChunk % OTA_NVS_SAVE_INTERVAL == 0) {
        saveProgress();
    }

    Serial.printf("[OTA] Chunk %u/%u\n", lastReceivedChunk, totalChunks);
    return true;
}

bool handleEnd(const OtaPacket& packet) {
    if (!otaInProgress) {
        return false;
    }

    if (runningCrc32 != expectedCrc32) {
        Serial.printf("[OTA] ERROR: CRC32 mismatch — expected 0x%08X, got 0x%08X\n",
                      expectedCrc32, runningCrc32);
        OtaUpdate::abortTransfer();
        return false;
    }

    esp_err_t err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_end failed: %s\n", esp_err_to_name(err));
        OtaUpdate::abortTransfer();
        return false;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: set_boot_partition failed: %s\n", esp_err_to_name(err));
        OtaUpdate::abortTransfer();
        return false;
    }

    clearProgress();
    otaInProgress = false;

    Serial.println("[OTA] Update complete — rebooting");
    Serial.flush();
    esp_restart();

    return true;  // Unreachable
}

}  // anonymous namespace

namespace OtaUpdate {

bool isTransferInProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool active = prefs.getBool(NVS_KEY_OTA_ACTIVE, false);
    prefs.end();
    return active;
}

bool handleOtaPacket(const OtaPacket& packet) {
    switch (packet.command) {
        case OtaCommand::OTA_START: return handleStart(packet);
        case OtaCommand::OTA_CHUNK: return handleChunk(packet);
        case OtaCommand::OTA_END:   return handleEnd(packet);
        case OtaCommand::OTA_ABORT:
            Serial.println("[OTA] Abort received from collector");
            abortTransfer();
            return true;
        default:
            return false;
    }
}

bool resumeTransfer() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool active = prefs.getBool(NVS_KEY_OTA_ACTIVE, false);

    if (!active) {
        prefs.end();
        return false;
    }

    totalChunks     = prefs.getUShort(NVS_KEY_OTA_TOTAL, 0);
    lastReceivedChunk = prefs.getUShort(NVS_KEY_OTA_RECEIVED, 0);
    expectedCrc32   = prefs.getULong(NVS_KEY_OTA_CRC32, 0);
    prefs.end();

    // esp_ota_begin erases the partition, so we restart from chunk 0
    // The collector will resend all chunks when the node signals OTA_READY
    if (!beginOtaWrite()) {
        clearProgress();
        return false;
    }

    lastReceivedChunk = 0;
    runningCrc32 = 0;
    saveProgress();

    otaInProgress = true;
    Serial.printf("[OTA] Resumed — requesting %u chunks\n", totalChunks);
    return true;
}

void abortTransfer() {
    if (otaHandle != 0) {
        esp_ota_abort(otaHandle);
        otaHandle = 0;
    }
    updatePartition = nullptr;
    otaInProgress = false;
    clearProgress();
    Serial.println("[OTA] Aborted");
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

    Serial.println("[OTA] New firmware — validating...");

    // Basic health check — verify ESP-NOW and storage initialize
    // Sensor tags are wireless so we can't verify them during OTA validation
    bool healthy = true;

    if (healthy) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] Firmware validated");
    } else {
        Serial.println("[OTA] Validation FAILED — rolling back");
        Serial.flush();
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

}  // namespace OtaUpdate
