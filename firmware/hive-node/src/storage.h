#pragma once

#include "types.h"
#include <cstdint>

/// Circular buffer on LittleFS for storing HivePayload readings.
/// Stores up to MAX_STORED_READINGS entries. Oldest overwritten on overflow.
namespace Storage {

    /// Mount LittleFS and load or create metadata file.
    bool initialize();

    /// Append a reading to the circular buffer.
    bool storeReading(const HivePayload& payload);

    /// Read a specific reading by index (0 = oldest available).
    bool readReading(uint16_t index, HivePayload& payload);

    /// Get the number of readings currently stored.
    uint16_t getReadingCount();

    /// Clear all stored readings (called after BLE sync).
    bool clearAllReadings();

}  // namespace Storage
