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

// Brood box tag (tag_name)
float   broodTemp    = NAN;
float   broodHum     = NAN;
uint8_t broodBatt    = 0;
bool    broodFound   = false;

// Top tag (tag_name_2)
float   topTemp      = NAN;
float   topHum       = NAN;
uint8_t topBatt      = 0;
bool    topFound     = false;

char broodTagName[32] = "";
char topTagName[32]   = "";

struct TagData {
    float*   temp;
    float*   hum;
    uint8_t* batt;
    bool*    found;
};

bool parseMfgData(const std::string& mfgData, TagData& tag) {
    if (mfgData.length() < MFG_DATA_LENGTH) return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(mfgData.data());

    uint16_t mfgId = d[0] | (d[1] << 8);
    if (mfgId != MANUFACTURER_ID) return false;
    if (d[2] != TAG_PROTOCOL_VERSION) return false;

    int16_t rawTemp = d[3] | (d[4] << 8);
    *tag.temp = (rawTemp / 100.0f) - 40.0f;

    uint16_t rawHum = d[5] | (d[6] << 8);
    *tag.hum = rawHum / 100.0f;

    *tag.batt = d[7];
    *tag.found = true;

    return true;
}

class TagScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) override {
        if (!device->haveName() || !device->haveManufacturerData()) return;

        const char* name = device->getName().c_str();
        std::string mfgData = device->getManufacturerData();

        // Check brood tag
        if (strlen(broodTagName) > 0 && !broodFound &&
            strcmp(name, broodTagName) == 0) {
            TagData tag = {&broodTemp, &broodHum, &broodBatt, &broodFound};
            if (parseMfgData(mfgData, tag)) {
                Serial.printf("[TAGREADER] Brood: %s T=%.1fC H=%.1f%% B=%u%%\n",
                              broodTagName, broodTemp, broodHum, broodBatt);
            }
        }

        // Check top tag
        if (strlen(topTagName) > 0 && !topFound &&
            strcmp(name, topTagName) == 0) {
            TagData tag = {&topTemp, &topHum, &topBatt, &topFound};
            if (parseMfgData(mfgData, tag)) {
                Serial.printf("[TAGREADER] Top: %s T=%.1fC H=%.1f%% B=%u%%\n",
                              topTagName, topTemp, topHum, topBatt);
            }
        }

        // Stop scan if we found everything we're looking for
        bool allFound = true;
        if (strlen(broodTagName) > 0 && !broodFound) allFound = false;
        if (strlen(topTagName) > 0 && !topFound) allFound = false;
        if (allFound) {
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
    String name1 = prefs.getString("tag_name", "");
    String name2 = prefs.getString("tag_name_2", "");
    prefs.end();

    if (name1.length() == 0 && name2.length() == 0) {
        return false;
    }

    strncpy(broodTagName, name1.c_str(), sizeof(broodTagName) - 1);
    strncpy(topTagName, name2.c_str(), sizeof(topTagName) - 1);

    broodTemp = NAN;  broodHum = NAN;  broodBatt = 0;  broodFound = false;
    topTemp   = NAN;  topHum   = NAN;  topBatt   = 0;  topFound   = false;

    NimBLEDevice::init("");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(100);

    Serial.printf("[TAGREADER] Scanning for '%s' + '%s' (%ums)\n",
                  broodTagName, topTagName, timeoutMs);

    scan->start(timeoutMs / 1000, false);

    scan->clearResults();
    NimBLEDevice::deinit(true);

    return broodFound || topFound;
}

float getBroodTemperature()  { return broodTemp; }
float getBroodHumidity()     { return broodHum; }
uint8_t getBroodBattery()    { return broodBatt; }
bool broodTagFound()         { return broodFound; }

float getTopTemperature()    { return topTemp; }
float getTopHumidity()       { return topHum; }
uint8_t getTopBattery()      { return topBatt; }
bool topTagFound()           { return topFound; }

}  // namespace BleTagReader
