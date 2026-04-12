#include "sensor_hx711.h"
#include "config.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>

namespace {

HX711 scale;

float weightOffset = 0.0f;
float weightScale  = 1.0f;

/// Load tare offset and calibration factor from NVS.
void loadCalibrationFromNVS() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    weightOffset = prefs.getFloat(NVS_KEY_WEIGHT_OFF, 0.0f);
    weightScale  = prefs.getFloat(NVS_KEY_WEIGHT_SCL, 1.0f);
    prefs.end();

    Serial.printf("[HX711] Calibration loaded — offset=%.2f, scale=%.2f\n",
                  weightOffset, weightScale);
}

}  // anonymous namespace

namespace SensorHX711 {

bool initialize() {
    scale.begin(PIN_HX711_DOUT, PIN_HX711_CLK);

    if (!scale.wait_ready_timeout(1000)) {
        Serial.println("[HX711] ERROR: HX711 not responding");
        return false;
    }

    loadCalibrationFromNVS();
    scale.set_offset(static_cast<long>(weightOffset));
    scale.set_scale(weightScale);

    Serial.println("[HX711] Initialized and calibrated");
    return true;
}

bool readMeasurements(HivePayload& payload) {
    if (!scale.is_ready()) {
        Serial.println("[HX711] ERROR: Not ready for reading");
        return false;
    }

    // Average 10 samples for stable weight reading
    constexpr int SAMPLE_COUNT = 10;
    float weightKg = scale.get_units(SAMPLE_COUNT);

    if (weightKg < 0.0f) {
        Serial.printf("[HX711] WARNING: Negative weight (%.2f kg) — possible calibration issue\n",
                      weightKg);
        weightKg = 0.0f;
    }

    payload.weight_kg = weightKg;

    Serial.printf("[HX711] Weight: %.2f kg\n", weightKg);
    return true;
}

void enterSleep() {
    scale.power_down();
    Serial.println("[HX711] Powered down");
}

}  // namespace SensorHX711
