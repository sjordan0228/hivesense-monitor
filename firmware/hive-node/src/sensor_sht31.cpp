#include "sensor_sht31.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

namespace {

Adafruit_SHT31 sensorInternal;
Adafruit_SHT31 sensorExternal;

}  // anonymous namespace

namespace SensorSHT31 {

bool initialize() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!sensorInternal.begin(SHT31_ADDR_INTERNAL)) {
        Serial.printf("[SHT31] ERROR: Internal sensor (0x%02X) not found\n",
                      SHT31_ADDR_INTERNAL);
        return false;
    }

    if (!sensorExternal.begin(SHT31_ADDR_EXTERNAL)) {
        Serial.printf("[SHT31] ERROR: External sensor (0x%02X) not found\n",
                      SHT31_ADDR_EXTERNAL);
        return false;
    }

    // Enable heater on internal sensor to combat condensation in hive
    sensorInternal.heater(true);

    Serial.println("[SHT31] Both sensors initialized — internal heater ON");
    return true;
}

bool readMeasurements(HivePayload& payload) {
    float tempInternal = sensorInternal.readTemperature();
    float humInternal  = sensorInternal.readHumidity();

    if (isnan(tempInternal) || isnan(humInternal)) {
        Serial.println("[SHT31] ERROR: Internal sensor read failed");
        return false;
    }

    float tempExternal = sensorExternal.readTemperature();
    float humExternal  = sensorExternal.readHumidity();

    if (isnan(tempExternal) || isnan(humExternal)) {
        Serial.println("[SHT31] ERROR: External sensor read failed");
        return false;
    }

    payload.temp_internal     = tempInternal;
    payload.humidity_internal = humInternal;
    payload.temp_external     = tempExternal;
    payload.humidity_external = humExternal;

    Serial.printf("[SHT31] Internal: %.1f°C / %.1f%% RH\n",
                  tempInternal, humInternal);
    Serial.printf("[SHT31] External: %.1f°C / %.1f%% RH\n",
                  tempExternal, humExternal);
    return true;
}

void enterSleep() {
    // Turn off internal heater during sleep to save power
    sensorInternal.heater(false);
    Serial.println("[SHT31] Heater OFF — entering idle");
}

}  // namespace SensorSHT31
