#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace {

constexpr float DIVIDER_RATIO = 2.0f;   // 100k / 100k → 2:1
constexpr uint8_t ADC_SAMPLES = 8;

}  // anonymous namespace

namespace Battery {

uint16_t readMillivolts() {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
        acc += analogReadMilliVolts(PIN_BATTERY_ADC);
    }
    float vbat = (acc / static_cast<float>(ADC_SAMPLES)) * DIVIDER_RATIO;
    if (vbat < 0.0f) return 0;
    if (vbat > UINT16_MAX) return UINT16_MAX;
    return static_cast<uint16_t>(vbat);
}


}  // namespace Battery
