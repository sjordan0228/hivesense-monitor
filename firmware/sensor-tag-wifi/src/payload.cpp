#include "payload.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Payload {

/// Append a formatted fragment to buf, returning true on success. Fails if the
/// buffer would overflow.
static bool appendf(char*& p, char* end, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(p, end - p, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= end - p) return false;
    p += n;
    return true;
}

/// Serialize a float field as either a number (`%.2f`) or JSON `null` when NaN.
/// `%.2f` on NaN prints `nan`, which is not valid JSON and breaks downstream
/// parsers (Telegraf json_v2, Swift JSONDecoder, PostgreSQL json type).
static bool appendNumOrNull(char*& p, char* end, const char* key, float v) {
    if (std::isnan(v)) return appendf(p, end, ",\"%s\":null", key);
    return appendf(p, end, ",\"%s\":%.2f", key, v);
}

int serialize(const char* deviceId,
              const char* fwVersion,
              int8_t      rssi,
              const Reading& r,
              char* buf, size_t bufLen) {
    if (bufLen == 0) return -1;
    char* p   = buf;
    char* end = buf + bufLen;

    if (!appendf(p, end, "{\"id\":\"%s\",\"v\":\"%s\",\"t\":%lu",
                 deviceId, fwVersion,
                 static_cast<unsigned long>(r.timestamp))) return -1;
    if (!appendNumOrNull(p, end, "t1", r.temp1)) return -1;
    if (!appendNumOrNull(p, end, "t2", r.temp2)) return -1;
    if (!std::isnan(r.humidity1) && !appendf(p, end, ",\"h1\":%.2f", r.humidity1)) return -1;
    if (!std::isnan(r.humidity2) && !appendf(p, end, ",\"h2\":%.2f", r.humidity2)) return -1;
    if (!appendf(p, end, ",\"vbat_mV\":%u,\"rssi\":%d,\"b\":%u}",
                 static_cast<unsigned>(r.vbat_mV),
                 static_cast<int>(rssi),
                 static_cast<unsigned>(r.battery_pct))) return -1;

    return static_cast<int>(p - buf);
}

}  // namespace Payload
