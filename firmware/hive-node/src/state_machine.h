#pragma once

#include "types.h"
#include <cstdint>

/// Manages state transitions, RTC time tracking, and the sensor read cycle.
/// The state machine is the central coordinator — it calls into modules
/// but modules never call the state machine or each other.
namespace StateMachine {

    /// Determine initial state based on wake reason and time of day.
    /// Called once from setup().
    NodeState determineInitialState();

    /// Execute the current state and return the next state.
    /// Each state performs its actions and decides what comes next.
    NodeState executeState(NodeState current, HivePayload& payload);

    /// Get the current hour from the internal RTC counter.
    /// Approximate — drifts without NTP. Sufficient for day/night switching.
    uint8_t getCurrentHour();

    /// Set the internal RTC time (called once if NTP or BLE time sync available).
    void setTime(uint32_t epochSeconds);

    /// Get current epoch timestamp for payload.
    uint32_t getTimestamp();

}  // namespace StateMachine
