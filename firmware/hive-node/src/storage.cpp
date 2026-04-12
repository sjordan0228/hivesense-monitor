#include "storage.h"
#include "config.h"
#include <LittleFS.h>
#include <Arduino.h>

static constexpr const char* META_PATH     = "/meta.bin";
static constexpr const char* READINGS_PATH = "/readings.bin";
static constexpr uint8_t     STORAGE_VERSION = 1;

namespace {

StorageMeta g_meta;

bool writeMeta() {
    File f = LittleFS.open(META_PATH, "w");
    if (!f) {
        Serial.printf("[STORAGE] Failed to open meta for write\n");
        return false;
    }
    f.write(reinterpret_cast<const uint8_t*>(&g_meta), sizeof(StorageMeta));
    f.close();
    return true;
}

bool readMeta() {
    File f = LittleFS.open(META_PATH, "r");
    if (!f) {
        return false;
    }
    const size_t bytesRead = f.read(reinterpret_cast<uint8_t*>(&g_meta), sizeof(StorageMeta));
    f.close();
    return bytesRead == sizeof(StorageMeta);
}

}  // anonymous namespace

namespace Storage {

bool initialize() {
    if (!LittleFS.begin(true)) {  // true = format on first use
        Serial.printf("[STORAGE] LittleFS mount failed\n");
        return false;
    }

    if (readMeta() && g_meta.version == STORAGE_VERSION) {
        Serial.printf("[STORAGE] Loaded metadata: head=%u count=%u\n",
                      g_meta.head, g_meta.count);
        return true;
    }

    // First boot or version mismatch — create fresh metadata
    g_meta = { .head = 0, .count = 0, .version = STORAGE_VERSION };
    if (!writeMeta()) {
        return false;
    }
    Serial.printf("[STORAGE] Initialized fresh storage\n");
    return true;
}

bool storeReading(const HivePayload& payload) {
    const uint32_t offset = static_cast<uint32_t>(g_meta.head) * sizeof(HivePayload);

    // Open with "r+" so existing content is preserved; fall back to "w" on first write.
    const char* mode = LittleFS.exists(READINGS_PATH) ? "r+" : "w";
    File f = LittleFS.open(READINGS_PATH, mode);
    if (!f) {
        Serial.printf("[STORAGE] Failed to open readings file (mode=%s)\n", mode);
        return false;
    }

    if (!f.seek(offset)) {
        Serial.printf("[STORAGE] Seek to offset %u failed\n", offset);
        f.close();
        return false;
    }

    f.write(reinterpret_cast<const uint8_t*>(&payload), sizeof(HivePayload));
    f.close();

    // Advance circular buffer pointers
    g_meta.head = (g_meta.head + 1) % MAX_STORED_READINGS;
    if (g_meta.count < MAX_STORED_READINGS) {
        g_meta.count++;
    }

    return writeMeta();
}

bool readReading(uint16_t index, HivePayload& payload) {
    if (index >= g_meta.count) {
        Serial.printf("[STORAGE] Index %u out of range (count=%u)\n", index, g_meta.count);
        return false;
    }

    // Oldest entry is at (head - count) wrapped; index 0 maps to that slot.
    const uint16_t slot =
        (g_meta.head - g_meta.count + index + MAX_STORED_READINGS) % MAX_STORED_READINGS;
    const uint32_t offset = static_cast<uint32_t>(slot) * sizeof(HivePayload);

    File f = LittleFS.open(READINGS_PATH, "r");
    if (!f) {
        Serial.printf("[STORAGE] Failed to open readings file for read\n");
        return false;
    }

    if (!f.seek(offset)) {
        Serial.printf("[STORAGE] Seek to offset %u failed\n", offset);
        f.close();
        return false;
    }

    const size_t bytesRead = f.read(reinterpret_cast<uint8_t*>(&payload), sizeof(HivePayload));
    f.close();

    return bytesRead == sizeof(HivePayload);
}

uint16_t getReadingCount() {
    return g_meta.count;
}

bool clearAllReadings() {
    g_meta.head  = 0;
    g_meta.count = 0;
    const bool ok = writeMeta();
    if (ok) {
        Serial.printf("[STORAGE] Cleared all readings\n");
    }
    return ok;
}

}  // namespace Storage
