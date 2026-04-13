#pragma once

#include <cstdint>

/// Scans for a HiveSense wireless sensor tag via BLE advertisement.
namespace BleTagReader {

    /// Scan for the configured tag name. Blocks for timeoutMs.
    bool scan(uint16_t timeoutMs);

    /// Get last received temperature (°C). Returns NAN if no tag found.
    float getTemperature();

    /// Get last received humidity (%RH). Returns NAN if no tag found.
    float getHumidity();

    /// Get last received battery percentage. Returns 0 if no tag found.
    uint8_t getBattery();

}  // namespace BleTagReader
