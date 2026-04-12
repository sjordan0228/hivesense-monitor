#pragma once

#include "types.h"

/// Reads temperature and humidity from two SHT31 sensors on the I2C bus.
/// Internal sensor at 0x44, external sensor at 0x45.
namespace SensorSHT31 {

    /// Initialize I2C bus and verify both SHT31 sensors respond.
    /// Returns false if either sensor is not detected.
    bool initialize();

    /// Read temp and humidity from both sensors into payload.
    /// Fields: temp_internal, temp_external, humidity_internal, humidity_external.
    /// Returns false if either read fails.
    bool readMeasurements(HivePayload& payload);

    /// No MOSFET gate on SHT31 — sensors draw ~2uA in idle.
    void enterSleep();

}  // namespace SensorSHT31
