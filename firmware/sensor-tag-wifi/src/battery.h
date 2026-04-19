#pragma once

#include <cstdint>

namespace Battery {

/// Read battery voltage via ADC and return percent (0..100).
/// Assumes 18650 Li-ion: 4.20V full, 3.30V empty, with a 2:1 divider
/// (100 kΩ / 100 kΩ) into the ADC pin.
uint8_t readPercent();

}  // namespace Battery
