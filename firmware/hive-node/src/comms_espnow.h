#pragma once

#include "types.h"

/// Transmits HivePayload to the yard collector via ESP-NOW.
/// Sends up to ESPNOW_MAX_RETRIES attempts with delay between each.
namespace CommsEspNow {

    /// Initialize WiFi in station mode and register ESP-NOW peer.
    /// Loads collector MAC address from NVS.
    bool initialize();

    /// Send payload to collector. Retries on failure.
    /// Populates payload.rssi with signal strength on success.
    /// Returns true if ACK received.
    bool sendPayload(HivePayload& payload);

    /// Deregister peer and stop WiFi.
    void shutdown();

}  // namespace CommsEspNow
