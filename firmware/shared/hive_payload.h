#pragma once

#include <cstdint>

/// Sensor payload transmitted via ESP-NOW and stored in LittleFS.
/// Shared between hive node and collector firmware.
/// temp/humidity_brood = brood box sensor tag
/// temp/humidity_top = upper box sensor tag (optional)
/// External weather comes from the iOS app via API, not hardware.
struct HivePayload {
    uint8_t  version;            // Payload format version
    char     hive_id[16];        // Null-terminated hive identifier
    uint32_t timestamp;          // Unix epoch seconds
    float    weight_kg;
    float    temp_brood;         // Celsius — brood box sensor tag
    float    temp_top;           // Celsius — top/super sensor tag (0 if not present)
    float    humidity_brood;     // %RH — brood box sensor tag
    float    humidity_top;       // %RH — top/super sensor tag (0 if not present)
    uint16_t bees_in;            // Phase 2 — zeroed in Phase 1
    uint16_t bees_out;           // Phase 2 — zeroed in Phase 1
    uint16_t bees_activity;      // Phase 2 — zeroed in Phase 1
    uint8_t  battery_pct;        // Hive node battery 0-100
    int8_t   rssi;               // ESP-NOW signal strength
} __attribute__((packed));
