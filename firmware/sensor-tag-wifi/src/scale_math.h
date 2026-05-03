#pragma once

#include <cstdint>
#include "config.h"

class StableDetector {
public:
    StableDetector() = default;
    void push(int32_t raw);
    bool isStable() const;
    void reset();

private:
    int32_t ring_[HX711_STABLE_WINDOW_LEN] = {};
    uint8_t count_ = 0;
    uint8_t head_  = 0;
};

/// kg = (raw - off) / scale_factor
double applyCalibration(int32_t raw, int64_t off, double scale_factor);

/// Compute tare offset as integer mean of N raw samples.
int64_t tareFromMean(const int32_t* samples, uint8_t n);

/// Compute scale_factor = (mean(samples) - off) / known_kg.
/// Returns 0.0 if known_kg is zero (caller must handle).
double scaleFactorFromMean(const int32_t* samples, uint8_t n, int64_t off, double known_kg);

/// |measured - expected| / |expected| * 100. Returns -1.0 if expected == 0.
double errorPct(double measured, double expected);
