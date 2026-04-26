#pragma once

#include <cstdint>

namespace Ota {
    void validateOnBoot();
    void onPublishSuccess();
    void checkAndApply(uint8_t batteryPct);
}
