#ifdef SENSOR_SHT31

#include "sensor.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

namespace {

Adafruit_SHT31 brood;
Adafruit_SHT31 top;

// SHT31 has two I2C addresses selectable by ADDR pin: 0x44 (default) and 0x45
constexpr uint8_t SHT31_ADDR_BROOD = 0x44;
constexpr uint8_t SHT31_ADDR_TOP   = 0x45;

bool broodOk = false;
bool topOk   = false;

}  // anonymous namespace

namespace Sensor {

bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    broodOk = brood.begin(SHT31_ADDR_BROOD);
    topOk   = top.begin(SHT31_ADDR_TOP);
    if (!broodOk) Serial.println("[SHT31] brood (0x44) not found");
    if (!topOk)   Serial.println("[SHT31] top (0x45) not found");
    return broodOk || topOk;
}

bool read(Reading& r) {
    if (broodOk) {
        r.temp1     = brood.readTemperature();
        r.humidity1 = brood.readHumidity();
    } else {
        r.temp1 = NAN;
        r.humidity1 = NAN;
    }
    if (topOk) {
        r.temp2     = top.readTemperature();
        r.humidity2 = top.readHumidity();
    } else {
        r.temp2 = NAN;
        r.humidity2 = NAN;
    }
    return broodOk || topOk;
}

void deinit() {
    Wire.end();
}

}  // namespace Sensor

#endif  // SENSOR_SHT31
