#pragma once

#include <cstdint>

/// A single sensor sample. POD — safe to put in RTC_DATA_ATTR memory.
///
/// `t1`/`t2` are the two temperature channels (brood / top). `h1`/`h2` are the
/// two humidity channels. NAN means the channel is unavailable (DS18B20 build,
/// or a single SHT31 failing on a dual-sensor board) and is omitted from the
/// serialized payload.
///
/// `vbat_mV` is the raw ADC reading (divider-corrected) at sample time, kept
/// alongside `battery_pct` so downstream consumers can apply a better SOC
/// curve later without a firmware change. See issue #10.
struct Reading {
    uint32_t timestamp;    // unix seconds
    float    temp1;        // brood (°C)
    float    temp2;        // top   (°C)
    float    humidity1;    // brood (%RH) — NAN if unavailable
    float    humidity2;    // top   (%RH) — NAN if unavailable
    uint16_t vbat_mV;      // raw battery voltage at sample time
    uint8_t  battery_pct;  // 0..100
};

static_assert(sizeof(Reading) == 24,
              "Reading layout changed — bump RingBuffer MAGIC to invalidate "
              "stale RTC slots after OTA. See ring_buffer.cpp.");
