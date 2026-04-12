#pragma once

#include "types.h"

/// Reads battery voltage via ADC and estimates percentage.
/// Uses a voltage divider on PIN_BATTERY_ADC (ADC1_CH6).
namespace Battery {

    /// Configure ADC pin and attenuation.
    bool initialize();

    /// Read battery voltage, convert to percentage, populate payload.
    bool readMeasurements(HivePayload& payload);

    /// No hardware to power off — no-op for interface consistency.
    void enterSleep();

}  // namespace Battery
