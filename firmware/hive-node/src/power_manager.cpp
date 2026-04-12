#include "power_manager.h"
#include "config.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_pm.h>
#include <Preferences.h>

namespace PowerManager {

void disableOnboardLed() {
    // WS2812 needs a data frame of zeros to turn off — simple LOW doesn't work
    neopixelWrite(PIN_ONBOARD_RGB, 0, 0, 0);
    Serial.println("[POWER] RGB LED off");
}

void initialize() {
    pinMode(PIN_MOSFET_HX711, OUTPUT);
    digitalWrite(PIN_MOSFET_HX711, LOW);

    // Phase 2: IR MOSFET gate — ensure it's OFF
    pinMode(PIN_MOSFET_IR, OUTPUT);
    digitalWrite(PIN_MOSFET_IR, LOW);

    disableOnboardLed();

    Serial.println("[POWER] MOSFET gates initialized — all OFF, RGB LED disabled");
}

void enterDeepSleep(uint8_t minutes) {
    disableRadios();

    uint64_t sleepMicroseconds = static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepMicroseconds);

    Serial.printf("[POWER] Entering deep sleep for %u minutes\n", minutes);
    Serial.flush();

    esp_deep_sleep_start();
    // Does not return — next execution starts from setup()
}

void enableLightSleep() {
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(DEFAULT_READ_INTERVAL_MIN) * 60ULL * 1000000ULL
    );

    // Automatic light sleep: CPU sleeps when idle, wakes on interrupts/timers
    esp_pm_config_esp32s3_t pmConfig = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pmConfig);

    Serial.println("[POWER] Light sleep enabled — CPU will idle between activity");
}

void powerOnWeightSensor() {
    digitalWrite(PIN_MOSFET_HX711, HIGH);
    delay(MOSFET_STABILIZE_MS);
    Serial.println("[POWER] HX711 MOSFET ON — stabilized");
}

void powerOffWeightSensor() {
    digitalWrite(PIN_MOSFET_HX711, LOW);
    Serial.println("[POWER] HX711 MOSFET OFF");
}

void disableRadios() {
    esp_wifi_stop();
    esp_bt_controller_disable();
    Serial.println("[POWER] Radios disabled");
}

bool isDaytime(uint8_t currentHour) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    uint8_t dayStart = prefs.getUChar(NVS_KEY_DAY_START, DEFAULT_DAY_START_HOUR);
    uint8_t dayEnd   = prefs.getUChar(NVS_KEY_DAY_END, DEFAULT_DAY_END_HOUR);
    prefs.end();

    return (currentHour >= dayStart && currentHour < dayEnd);
}

}  // namespace PowerManager
