#include "ble_tag_reader.h"
#include "config.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <cstring>
#include <cmath>

namespace {

constexpr uint16_t MANUFACTURER_ID      = 0xFFFF;
constexpr uint8_t  TAG_PROTOCOL_VERSION = 0x01;
constexpr uint8_t  MFG_DATA_LENGTH      = 8;

float   lastTemp    = NAN;
float   lastHum     = NAN;
uint8_t lastBattery = 0;
bool    tagFound    = false;

char targetTagName[32] = "";

bool parseMfgData(const std::string& mfgData) {
    if (mfgData.length() < MFG_DATA_LENGTH) return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(mfgData.data());

    uint16_t mfgId = d[0] | (d[1] << 8);
    if (mfgId != MANUFACTURER_ID) return false;

    if (d[2] != TAG_PROTOCOL_VERSION) return false;

    int16_t rawTemp = d[3] | (d[4] << 8);
    lastTemp = (rawTemp / 100.0f) - 40.0f;

    uint16_t rawHum = d[5] | (d[6] << 8);
    lastHum = rawHum / 100.0f;

    lastBattery = d[7];

    return true;
}

class TagScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) override {
        if (strlen(targetTagName) == 0) return;
        if (!device->haveName()) return;
        if (strcmp(device->getName().c_str(), targetTagName) != 0) return;
        if (!device->haveManufacturerData()) return;

        std::string mfgData = device->getManufacturerData();
        if (parseMfgData(mfgData)) {
            tagFound = true;
            Serial.printf("[TAGREADER] %s: T=%.1fC H=%.1f%% B=%u%%\n",
                          targetTagName, lastTemp, lastHum, lastBattery);
            NimBLEDevice::getScan()->stop();
        }
    }
};

TagScanCallbacks scanCallbacks;

}  // anonymous namespace

namespace BleTagReader {

bool scan(uint16_t timeoutMs) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String name = prefs.getString("tag_name", "");
    prefs.end();

    if (name.length() == 0) {
        return false;
    }

    strncpy(targetTagName, name.c_str(), sizeof(targetTagName) - 1);

    tagFound    = false;
    lastTemp    = NAN;
    lastHum     = NAN;
    lastBattery = 0;

    NimBLEDevice::init("");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(100);

    Serial.printf("[TAGREADER] Scanning for '%s' (%ums)\n", targetTagName, timeoutMs);

    scan->start(timeoutMs / 1000, false);

    scan->clearResults();
    NimBLEDevice::deinit(true);

    return tagFound;
}

float getTemperature()  { return lastTemp; }
float getHumidity()     { return lastHum; }
uint8_t getBattery()    { return lastBattery; }

}  // namespace BleTagReader
