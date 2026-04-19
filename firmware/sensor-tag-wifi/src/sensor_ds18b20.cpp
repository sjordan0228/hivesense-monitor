#ifdef SENSOR_DS18B20

#include "sensor.h"
#include "config.h"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <cmath>

namespace {

OneWire           oneWire(PIN_ONE_WIRE);
DallasTemperature sensors(&oneWire);

DeviceAddress addrBrood {};
DeviceAddress addrTop   {};
bool          haveBrood = false;
bool          haveTop   = false;

/// Sort two 1-Wire addresses deterministically (lexicographic) so the same
/// physical probe always maps to t1/t2 across boots.
bool addrLessThan(const DeviceAddress& a, const DeviceAddress& b) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (a[i] != b[i]) return a[i] < b[i];
    }
    return false;
}

}  // anonymous namespace

namespace Sensor {

bool begin() {
    pinMode(PIN_ONE_WIRE, INPUT_PULLUP);
    sensors.begin();
    sensors.setResolution(DS18B20_RESOLUTION_BITS);
    sensors.setWaitForConversion(false);

    uint8_t count = sensors.getDeviceCount();
    Serial.printf("[DS18B20] found %u device(s)\n", count);
    if (count == 0) return false;

    DeviceAddress tmpA, tmpB;
    if (count >= 1) haveBrood = sensors.getAddress(tmpA, 0);
    if (count >= 2) haveTop   = sensors.getAddress(tmpB, 1);

    if (haveBrood && haveTop) {
        if (addrLessThan(tmpA, tmpB)) {
            memcpy(addrBrood, tmpA, 8);
            memcpy(addrTop,   tmpB, 8);
        } else {
            memcpy(addrBrood, tmpB, 8);
            memcpy(addrTop,   tmpA, 8);
        }
    } else if (haveBrood) {
        memcpy(addrBrood, tmpA, 8);
    }
    return haveBrood;
}

bool read(Reading& r) {
    r.humidity1 = NAN;
    r.humidity2 = NAN;

    sensors.requestTemperatures();
    delay(DS18B20_CONVERT_TIMEOUT_MS);

    r.temp1 = haveBrood ? sensors.getTempC(addrBrood) : NAN;
    r.temp2 = haveTop   ? sensors.getTempC(addrTop)   : NAN;

    // DallasTemperature returns -127.0 on read failure — convert to NAN
    if (r.temp1 == DEVICE_DISCONNECTED_C) r.temp1 = NAN;
    if (r.temp2 == DEVICE_DISCONNECTED_C) r.temp2 = NAN;

    return !std::isnan(r.temp1) || !std::isnan(r.temp2);
}

void deinit() {
    // 1-Wire is passive — pull GPIO low to reduce leakage via pullup
    pinMode(PIN_ONE_WIRE, OUTPUT);
    digitalWrite(PIN_ONE_WIRE, LOW);
}

}  // namespace Sensor

#endif  // SENSOR_DS18B20
