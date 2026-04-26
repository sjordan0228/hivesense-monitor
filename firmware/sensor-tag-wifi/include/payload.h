#pragma once

#include <cstddef>
#include <cstdint>
#include "reading.h"

namespace Payload {

/// Serialize a reading into a JSON string.
/// @param deviceId   8-char hex device ID (null-terminated)
/// @param fwVersion  null-terminated firmware version string (from FIRMWARE_VERSION
///                   build flag; `"unknown"` when git describe failed)
/// @param rssi       WiFi RSSI captured post-connect at publish time, in dBm
/// @param r          reading to serialize
/// @param buf        output buffer
/// @param bufLen     size of output buffer
/// @return           number of bytes written (excluding null), or -1 on overflow
int serialize(const char* deviceId,
              const char* fwVersion,
              int8_t      rssi,
              const Reading& r,
              char* buf, size_t bufLen);

}  // namespace Payload
