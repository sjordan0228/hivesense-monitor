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

int serialize(const char* deviceId, const Reading& r, char* buf, size_t bufLen) {
    if (bufLen == 0) return -1;
    char* p   = buf;
    char* end = buf + bufLen;

    if (!appendf(p, end,
            "{\"id\":\"%s\",\"t\":%lu,\"t1\":%.2f,\"t2\":%.2f",
            deviceId,
            static_cast<unsigned long>(r.timestamp),
            r.temp1, r.temp2)) {
        return -1;
    }
    if (!std::isnan(r.humidity1) && !appendf(p, end, ",\"h1\":%.2f", r.humidity1)) return -1;
    if (!std::isnan(r.humidity2) && !appendf(p, end, ",\"h2\":%.2f", r.humidity2)) return -1;
    if (!appendf(p, end, ",\"b\":%u}", r.battery_pct)) return -1;

    return static_cast<int>(p - buf);
}

}  // namespace Payload
