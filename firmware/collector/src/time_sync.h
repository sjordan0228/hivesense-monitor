#pragma once

#include <cstdint>

/// Broadcasts current epoch time to all hive nodes via ESP-NOW.
namespace TimeSync {

    /// Broadcast TIME_SYNC packet. Call after NTP sync each publish cycle.
    bool broadcast(uint32_t epochSeconds);

}  // namespace TimeSync
