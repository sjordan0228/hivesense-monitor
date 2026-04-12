#pragma once

#include <cstdint>

/// Downloads hive node firmware from GitHub and relays to target node via ESP-NOW.
namespace OtaRelay {

    /// Download firmware binary for the given tag. Stores in unused OTA partition.
    bool downloadFirmware(const char* tag);

    /// Begin chunked relay to target hive node.
    bool startRelay(const char* hiveId);

    /// Send next batch of chunks. Returns true if relay still in progress.
    bool continueRelay();

    /// Check if a relay is currently active.
    bool isRelayInProgress();

    /// Cancel active relay and clean up.
    void abortRelay();

}  // namespace OtaRelay
