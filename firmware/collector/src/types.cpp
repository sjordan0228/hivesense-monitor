#include "types.h"
#include <cstring>

int8_t PayloadBuffer::findOrAllocate(const char* hiveId) {
    for (uint8_t i = 0; i < 20; i++) {
        if (entries[i].occupied &&
            strncmp(entries[i].payload.hive_id, hiveId, 16) == 0) {
            return static_cast<int8_t>(i);
        }
    }
    for (uint8_t i = 0; i < 20; i++) {
        if (!entries[i].occupied) {
            count++;
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void PayloadBuffer::clear() {
    for (auto& entry : entries) {
        entry.occupied = false;
    }
    count = 0;
}
