#pragma once

#include <cstdint>

/// BLE GATT server for direct phone communication at the yard.
/// Exposes sensor log download, reading count, hive ID config, and log clear.
namespace CommsBle {

    /// Initialize BLE stack, create GATT service and characteristics.
    bool initialize();

    /// Start advertising and wait for a connection.
    /// Returns true if a phone connects within timeoutMs.
    bool advertiseAndWait(uint16_t timeoutMs);

    /// Block until BLE sync is complete (phone disconnects or clear received).
    void waitForSyncComplete();

    /// Stop BLE advertising, deinit stack, free resources.
    void shutdown();

}  // namespace CommsBle
