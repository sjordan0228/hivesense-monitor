#pragma once

#include <cstddef>
#include <cstdint>

namespace Capabilities {

/// Feature flag values passed to buildPayload — decouples JSON shaping from NVS.
struct FeatureFlags {
    int feat_ds18b20;
    int feat_sht31;
    int feat_scale;
    int feat_mic;
};

/// Build the capabilities JSON payload.
/// Fields per config-mqtt-contract.md §3.1:
///   event, feat_ds18b20, feat_sht31, feat_scale, feat_mic,
///   hw_board, fw_version, last_boot_ts, ts
/// last_boot_ts is RFC3339 UTC. NTP-not-synced sentinel: "1970-01-01T00:00:00Z".
/// ts is the current time at call.
/// Returns bytes written (excluding NUL), 0 on failure.
size_t buildPayload(const FeatureFlags& flags, int64_t bootEpoch,
                    char* buf, size_t bufsz);

/// Publish capabilities to combsense/hive/<deviceId>/capabilities, retained=true.
/// Reads feature flags from Config::isEnabled internally.
/// Returns true on successful publish.
bool publish(int64_t bootEpoch);

}  // namespace Capabilities
