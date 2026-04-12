#pragma once

#include <cstdint>

/// Controls deep sleep, light sleep, and MOSFET power gating.
/// Called by the state machine to transition between power states.
namespace PowerManager {

    /// Initialize MOSFET gate pins as outputs, all OFF.
    void initialize();

    /// Enter deep sleep for the specified number of minutes.
    /// Does not return — wakes up through BOOT state.
    void enterDeepSleep(uint8_t minutes);

    /// Enter automatic light sleep (CPU sleeps between activity).
    /// Returns immediately — light sleep is managed by the hardware.
    void enableLightSleep();

    /// Power on the HX711 MOSFET gate and wait for stabilization.
    void powerOnWeightSensor();

    /// Power off the HX711 MOSFET gate.
    void powerOffWeightSensor();

    /// Disable WiFi and Bluetooth radios before sleep.
    void disableRadios();

    /// Check if current hour falls within daytime window.
    /// Uses NVS-stored day_start and day_end hours.
    bool isDaytime(uint8_t currentHour);

}  // namespace PowerManager
