#include "wifi_manager.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <lwip/ip_addr.h>
#include <time.h>
#include <cstring>

namespace {

// BSSID + channel cached in RTC for fast reconnect
RTC_DATA_ATTR uint8_t rtcBssid[6]   = {0};
RTC_DATA_ATTR int32_t rtcChannel    = 0;
RTC_DATA_ATTR uint8_t rtcBssidValid = 0;

// Google public NTP, anycast IPv4 literal. We bypass DNS deliberately —
// hostname resolution on the ESP32-C6 routes through OpenThread DNS64 and
// fails for SNTP the same way it does for HTTP (issue #19, mirroring the
// OTA fix in stack.md). Hardcoded for simplicity; pivot to NVS-configurable
// only if a deployment needs a different NTP source.
constexpr const char* NTP_SERVER = "216.239.35.4";

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

    // RTC memory only survives deep sleep. On any other reset (power-on,
    // brownout, watchdog) the cached BSSID is garbage — invalidate it.
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        rtcBssidValid = 0;
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
    // Bypass configTime() — on ESP32-C6 it routes through OpenThread DNS64
    // (even when given an IP literal) and fails with "Cannot find NAT64
    // prefix". Use the lower-level LWIP SNTP API with an ip_addr_t directly.
    // Same pattern as the OTA fix in stack.md.
    Serial.printf("[NTP] starting query to %s\n", NTP_SERVER);

    ip_addr_t addr;
    IP_ADDR4(&addr, 216, 239, 35, 4);  // Google NTP — matches NTP_SERVER literal

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setserver(0, &addr);
    esp_sntp_init();

    time_t now = 0;
    for (uint16_t i = 0; i < 50; ++i) {  // 10 s
        time(&now);
        if (now > 1700000000) {
            Serial.printf("[NTP] sync ok t=%lu (waited %ums)\n",
                          static_cast<unsigned long>(now), i * 200);
            return static_cast<uint32_t>(now);
        }
        if (i > 0 && i % 5 == 0) {
            Serial.printf("[NTP] still waiting... %us\n", i / 5);
        }
        delay(200);
    }
    Serial.println("[NTP] sync timeout (10s)");
    return 0;
}

}  // namespace WifiManager
