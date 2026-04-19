#include "wifi_manager.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <cstring>

namespace {

// BSSID + channel cached in RTC for fast reconnect
RTC_DATA_ATTR uint8_t rtcBssid[6]   = {0};
RTC_DATA_ATTR int32_t rtcChannel    = 0;
RTC_DATA_ATTR uint8_t rtcBssidValid = 0;

constexpr const char* NTP_SERVER = "pool.ntp.org";

bool waitForConnect(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;
        delay(100);
    }
    return true;
}

}  // anonymous namespace

namespace WifiManager {

bool connect() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    String pass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();

    if (ssid.length() == 0) {
        Serial.println("[WIFI] no SSID configured");
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);

    if (rtcBssidValid) {
        Serial.printf("[WIFI] fast-connect ch=%ld\n", (long)rtcChannel);
        WiFi.begin(ssid.c_str(), pass.c_str(), rtcChannel, rtcBssid);
        if (waitForConnect(WIFI_CONNECT_TIMEOUT_MS / 2)) {
            Serial.printf("[WIFI] connected rssi=%d\n", WiFi.RSSI());
            return true;
        }
        Serial.println("[WIFI] fast-connect failed — full scan");
        WiFi.disconnect(true);
        rtcBssidValid = 0;
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    if (!waitForConnect(WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.println("[WIFI] connect timeout");
        return false;
    }

    memcpy(rtcBssid, WiFi.BSSID(), 6);
    rtcChannel    = WiFi.channel();
    rtcBssidValid = 1;
    Serial.printf("[WIFI] connected rssi=%d (cached bssid)\n", WiFi.RSSI());
    return true;
}

void disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

uint32_t getUnixTime() {
    configTime(0, 0, NTP_SERVER);
    time_t now = 0;
    for (uint8_t i = 0; i < 30; ++i) {
        time(&now);
        if (now > 1700000000) return static_cast<uint32_t>(now);
        delay(200);
    }
    return 0;
}

}  // namespace WifiManager
