#pragma once

#include <cstdint>

/// Packet type identifier for all ESP-NOW communication.
enum class EspNowPacketType : uint8_t {
    SENSOR_DATA = 0x10,   // Node -> Collector: HivePayload
    TIME_SYNC   = 0x20,   // Collector -> Node: epoch timestamp
    OTA_PACKET  = 0x30    // Either direction: OTA transfer
};

/// Header prepended to all ESP-NOW packets for type routing.
struct EspNowHeader {
    EspNowPacketType type;
    uint8_t          data_len;
} __attribute__((packed));

/// Payload for TIME_SYNC packets.
struct TimeSyncPayload {
    uint32_t epoch_seconds;
} __attribute__((packed));
