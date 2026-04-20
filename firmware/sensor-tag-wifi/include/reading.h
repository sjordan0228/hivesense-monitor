#pragma once

#include <cstdint>

/// A single sensor sample. POD — safe to put in RTC_DATA_ATTR memory.
///
/// `t1`/`t2` are the two temperature channels (brood / top). `h1`/`h2` are the
/// two humidity channels. NAN means the channel is unavailable (DS18B20 build,
/// or a single SHT31 failing on a dual-sensor board) and is omitted from the
/// serialized payload.
struct Reading {
    uint32_t timestamp;    // unix seconds
    float    temp1;        // brood (°C)
    float    temp2;        // top   (°C)
    float    humidity1;    // brood (%RH) — NAN if unavailable
    float    humidity2;    // top   (%RH) — NAN if unavailable
    uint8_t  battery_pct;  // 0..100
};
