#pragma once

#include <cstdint>
#include "reading.h"

/// Circular buffer of `Reading`s backed by RTC slow memory. Survives deep
/// sleep but not power loss. Used to absorb upload failures across cycles.
namespace RingBuffer {

/// Append a reading. If full, the oldest entry is dropped.
void push(const Reading& r);

/// Peek at the oldest reading without removing it.
bool peekOldest(Reading& out);

/// Remove the oldest reading after a successful upload.
void popOldest();

/// Number of readings currently stored.
uint8_t size();

/// Maximum capacity (compile-time).
uint8_t capacity();

/// Reset buffer. Call from cold boot only — RTC memory is uninitialized then.
void initIfColdBoot();

}  // namespace RingBuffer
