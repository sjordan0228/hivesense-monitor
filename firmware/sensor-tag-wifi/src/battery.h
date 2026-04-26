#pragma once

#include <cstdint>

namespace Battery {

constexpr uint16_t VBAT_FULL_MV  = 4200;
constexpr uint16_t VBAT_EMPTY_MV = 3300;

/// Read raw battery voltage via ADC, divider-corrected. Returns mV.
/// Implemented in battery.cpp; requires Arduino runtime.
uint16_t readMillivolts();

/// Pure conversion: clamp + linear-interpolate mV to 0..100% Li-ion SOC.
/// Inline so native unit tests can link without Arduino. The 4200/3300 mV
/// endpoints assume 18650 Li-ion; see issue #10 for replacing the linear
/// formula with a real discharge curve once vbat_mV telemetry is collected.
inline uint8_t percentFromMillivolts(uint16_t mV) {
    if (mV >= VBAT_FULL_MV)  return 100;
    if (mV <= VBAT_EMPTY_MV) return 0;
    return static_cast<uint8_t>(
        (static_cast<float>(mV) - VBAT_EMPTY_MV) * 100.0f /
        (VBAT_FULL_MV - VBAT_EMPTY_MV));
}


}  // namespace Battery
