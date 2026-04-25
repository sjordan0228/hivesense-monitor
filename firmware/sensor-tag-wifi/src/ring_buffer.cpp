#include "ring_buffer.h"
#include "config.h"

#include <Arduino.h>
#include <cstring>
#include <esp_system.h>

namespace {

// RTC slow memory persists across deep sleep.
RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR uint8_t  rtcHead  = 0;    // write index
RTC_DATA_ATTR uint8_t  rtcCount = 0;
RTC_DATA_ATTR Reading  rtcBuf[RTC_BUFFER_CAPACITY];

constexpr uint32_t MAGIC = 0xCB50A002u;  // bumped 2026-04-25: Reading layout grew vbat_mV — old slots invalid

}  // anonymous namespace

namespace RingBuffer {

void initIfColdBoot() {
    esp_reset_reason_t reason = esp_reset_reason();
    bool wokeFromDeepSleep = (reason == ESP_RST_DEEPSLEEP);
    bool magicValid = (rtcMagic == MAGIC);

    if (!wokeFromDeepSleep || !magicValid) {
        rtcMagic = MAGIC;
        rtcHead  = 0;
        rtcCount = 0;
        memset(rtcBuf, 0, sizeof(rtcBuf));
    }
}

void push(const Reading& r) {
    rtcBuf[rtcHead] = r;
    rtcHead = (rtcHead + 1) % RTC_BUFFER_CAPACITY;
    if (rtcCount < RTC_BUFFER_CAPACITY) {
        rtcCount++;
    }
    // If full, we've just overwritten the oldest — head now points past it,
    // which is also the new "oldest".
}

bool peekOldest(Reading& out) {
    if (rtcCount == 0) return false;
    uint8_t oldestIdx = (rtcHead + RTC_BUFFER_CAPACITY - rtcCount) % RTC_BUFFER_CAPACITY;
    out = rtcBuf[oldestIdx];
    return true;
}

void popOldest() {
    if (rtcCount == 0) return;
    rtcCount--;
}

uint8_t size()     { return rtcCount; }
uint8_t capacity() { return RTC_BUFFER_CAPACITY; }

}  // namespace RingBuffer
