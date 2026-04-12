#pragma once

#include "types.h"

namespace EspNowReceiver {
    bool initialize();
    PayloadBuffer& getBuffer();
    bool getMacForHive(const char* hiveId, uint8_t* macOut);
}
