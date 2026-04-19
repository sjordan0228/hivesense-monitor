#pragma once

#include <cstdint>
#include <cmath>

/// A single sensor sample. POD — safe to put in RTC_DATA_ATTR memory.
///
/// `t1`/`t2` are the two temperature channels (brood / top). `h1`/`h2` are the
/// two humidity channels. For DS18B20 builds, `h1`/`h2` are NAN and omitted
/// from the serialized payload.
struct Reading {
    uint32_t timestamp;    // unix seconds
    float    temp1;        // brood (°C)
    float    temp2;        // top   (°C)
    float    humidity1;    // brood (%RH) — NAN for DS18B20
    float    humidity2;    // top   (%RH) — NAN for DS18B20
    uint8_t  battery_pct;  // 0..100
};

inline bool readingHasHumidity(const Reading& r) {
    return !std::isnan(r.humidity1) && !std::isnan(r.humidity2);
}
