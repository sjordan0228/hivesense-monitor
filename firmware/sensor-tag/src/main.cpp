#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <Preferences.h>
#include "config.h"
#include "serial_console.h"

namespace {

Adafruit_SHT31 sht;
char tagName[32] = "HiveSense-Tag-001";
uint16_t advIntervalSec = DEFAULT_ADV_INTERVAL_SEC;

void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String name = prefs.getString(NVS_KEY_TAG_NAME, "HiveSense-Tag-001");
    advIntervalSec = prefs.getUShort(NVS_KEY_ADV_INT, DEFAULT_ADV_INTERVAL_SEC);
    prefs.end();

    strncpy(tagName, name.c_str(), sizeof(tagName) - 1);
}

uint8_t readBatteryPercent() {
    uint32_t mv = analogReadMilliVolts(A0);
    if (mv >= 3000) return 100;
    if (mv <= 2000) return 0;
    return static_cast<uint8_t>((mv - 2000) * 100 / 1000);
}

void advertiseData(float tempC, float humidity, uint8_t batteryPct) {
    BLEDevice::init(tagName);

    BLEAdvertising* advertising = BLEDevice::getAdvertising();

    // Manufacturer data: ID(2) + version(1) + temp(2) + humidity(2) + battery(1) = 8 bytes
    uint8_t mfgData[8];

    mfgData[0] = MANUFACTURER_ID & 0xFF;
    mfgData[1] = (MANUFACTURER_ID >> 8) & 0xFF;
    mfgData[2] = TAG_PROTOCOL_VERSION;

    int16_t rawTemp = static_cast<int16_t>((tempC + 40.0f) * 100.0f);
    mfgData[3] = rawTemp & 0xFF;
    mfgData[4] = (rawTemp >> 8) & 0xFF;

    uint16_t rawHum = static_cast<uint16_t>(humidity * 100.0f);
    mfgData[5] = rawHum & 0xFF;
    mfgData[6] = (rawHum >> 8) & 0xFF;

    mfgData[7] = batteryPct;

    BLEAdvertisementData advData;
    advData.setName(tagName);

    // Arduino-ESP32 v3.x expects String for setManufacturerData
    String mfgString;
    for (uint8_t i = 0; i < sizeof(mfgData); i++) {
        mfgString += static_cast<char>(mfgData[i]);
    }
    advData.setManufacturerData(mfgString);

    advertising->setAdvertisementData(advData);
    advertising->start();

    delay(ADV_DURATION_MS);

    advertising->stop();
    BLEDevice::deinit(true);
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    SerialConsole::checkForConsole();

    loadConfig();
    Serial.printf("[TAG] %s | interval=%us\n", tagName, advIntervalSec);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!sht.begin(SHT31_ADDR)) {
        Serial.println("[TAG] SHT31 not found — sleeping");
        esp_deep_sleep(static_cast<uint64_t>(advIntervalSec) * 1000000ULL);
    }

    float temp = sht.readTemperature();
    float hum  = sht.readHumidity();
    uint8_t batt = readBatteryPercent();

    if (!isnan(temp) && !isnan(hum)) {
        Serial.printf("[TAG] T=%.1fC H=%.1f%% B=%u%%\n", temp, hum, batt);
        advertiseData(temp, hum, batt);
    } else {
        Serial.println("[TAG] SHT31 read failed");
    }

    Serial.printf("[TAG] Sleep %us\n", advIntervalSec);
    Serial.flush();
    esp_deep_sleep(static_cast<uint64_t>(advIntervalSec) * 1000000ULL);
}

void loop() {
}
