#pragma once

#include <cstddef>
#include "reading.h"

namespace Payload {

/// Serialize a reading into a JSON string.
/// @param deviceId  8-char hex device ID (null-terminated)
/// @param r         reading to serialize
/// @param buf       output buffer
/// @param bufLen    size of output buffer
/// @return          number of bytes written (excluding null), or -1 on overflow
int serialize(const char* deviceId, const Reading& r, char* buf, size_t bufLen);

}  // namespace Payload
