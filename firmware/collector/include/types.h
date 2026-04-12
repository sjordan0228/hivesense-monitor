#pragma once

#include <cstdint>
#include "hive_payload.h"
#include "espnow_protocol.h"

/// Buffer entry — stores latest payload per hive node.
struct BufferEntry {
    HivePayload payload;
    bool        occupied;
    uint32_t    receivedAt;  // millis() when received
};

/// Payload buffer — one entry per hive node, indexed by slot.
struct PayloadBuffer {
    BufferEntry entries[20];  // MAX_HIVE_NODES
    uint8_t     count;

    /// Find slot for hive_id, or allocate new slot. Returns index or -1 if full.
    int8_t findOrAllocate(const char* hiveId);

    /// Clear all entries after publish.
    void clear();
};
