#pragma once

#include "ota_protocol.h"

/// Receives chunked firmware updates over ESP-NOW from the collector.
/// Progress stored in NVS to survive sleep cycles.
/// Uses esp_ota_ops to write to the inactive OTA partition.
namespace OtaUpdate {

    /// Check NVS for an in-progress OTA transfer.
    bool isTransferInProgress();

    /// Handle an incoming OTA packet from the collector.
    /// Returns true if the packet was handled.
    bool handleOtaPacket(const OtaPacket& packet);

    /// Resume a partial transfer after waking from sleep.
    /// Returns true if OTA is active and node should signal OTA_READY.
    bool resumeTransfer();

    /// Abort the current transfer, clear NVS progress, free OTA handle.
    void abortTransfer();

    /// Run health checks after a fresh OTA boot.
    /// Marks firmware valid on success, rolls back on failure.
    void validateNewFirmware();

}  // namespace OtaUpdate
