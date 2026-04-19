#include "payload.h"

#include <cstdio>

namespace Payload {

int serialize(const char* deviceId, const Reading& r, char* buf, size_t bufLen) {
    int n;
    if (readingHasHumidity(r)) {
        n = snprintf(buf, bufLen,
            "{\"id\":\"%s\",\"t\":%lu,\"t1\":%.2f,\"t2\":%.2f,"
            "\"h1\":%.2f,\"h2\":%.2f,\"b\":%u}",
            deviceId,
            static_cast<unsigned long>(r.timestamp),
            r.temp1, r.temp2, r.humidity1, r.humidity2,
            r.battery_pct);
    } else {
        n = snprintf(buf, bufLen,
            "{\"id\":\"%s\",\"t\":%lu,\"t1\":%.2f,\"t2\":%.2f,\"b\":%u}",
            deviceId,
            static_cast<unsigned long>(r.timestamp),
            r.temp1, r.temp2,
            r.battery_pct);
    }
    if (n < 0 || static_cast<size_t>(n) >= bufLen) return -1;
    return n;
}

}  // namespace Payload
