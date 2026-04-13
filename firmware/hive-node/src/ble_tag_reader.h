#pragma once

#include <cstdint>

/// Scans for up to two HiveSense wireless sensor tags via BLE advertisement.
/// Tag 1 = brood box (bottom), Tag 2 = top (super/upper box).
namespace BleTagReader {

    /// Scan for configured tags. Blocks for timeoutMs.
    /// Finds tag_name (brood) and tag_name_2 (top) in one scan pass.
    bool scan(uint16_t timeoutMs);

    /// Brood box tag (tag_name)
    float getBroodTemperature();
    float getBroodHumidity();
    uint8_t getBroodBattery();
    bool broodTagFound();

    /// Top tag (tag_name_2)
    float getTopTemperature();
    float getTopHumidity();
    uint8_t getTopBattery();
    bool topTagFound();

}  // namespace BleTagReader
