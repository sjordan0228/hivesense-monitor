#pragma once

#include "types.h"

/// Reads weight from HX711 ADC with 4x 50kg load cells in Wheatstone bridge.
/// Power-gated via MOSFET on PIN_MOSFET_HX711 (managed by PowerManager).
namespace SensorHX711 {

    /// Initialize HX711, apply tare and scale from NVS.
    /// MOSFET must already be ON (called by PowerManager before this).
    bool initialize();

    /// Read weight and populate payload.weight_kg.
    /// Averages multiple samples for stability.
    bool readMeasurements(HivePayload& payload);

    /// Power down HX711 internal circuitry.
    /// MOSFET gate is managed by PowerManager.
    void enterSleep();

}  // namespace SensorHX711
