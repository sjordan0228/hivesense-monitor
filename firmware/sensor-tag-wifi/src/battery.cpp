#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace {

constexpr float VBAT_FULL_MV  = 4200.0f;
constexpr float VBAT_EMPTY_MV = 3300.0f;
constexpr float DIVIDER_RATIO = 2.0f;   // 100k / 100k → 2:1
constexpr uint8_t ADC_SAMPLES = 8;

}  // anonymous namespace

namespace Battery {

uint8_t readPercent() {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
        acc += analogReadMilliVolts(PIN_BATTERY_ADC);
    }
    float vbat = (acc / static_cast<float>(ADC_SAMPLES)) * DIVIDER_RATIO;

    if (vbat >= VBAT_FULL_MV)  return 100;
    if (vbat <= VBAT_EMPTY_MV) return 0;
    return static_cast<uint8_t>(
        (vbat - VBAT_EMPTY_MV) * 100.0f / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

}  // namespace Battery
