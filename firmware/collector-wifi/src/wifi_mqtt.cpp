#include "wifi_mqtt.h"
#include "config.h"
#include "hive_payload.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <time.h>

namespace {

char wifiSsid[32] = "";
char wifiPass[64] = "";
char mqttHost[64] = "";
uint16_t mqttPort = 1883;
char mqttUser[32] = "";
char mqttPass[64] = "";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

}  // anonymous namespace

namespace WifiMqtt {

bool initialize() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    String ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    String wpass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    String host = prefs.getString(NVS_KEY_MQTT_HOST, "");
    mqttPort    = prefs.getUShort(NVS_KEY_MQTT_PORT, 1883);
    String muser = prefs.getString(NVS_KEY_MQTT_USER, "");
    String mpass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();

    strncpy(wifiSsid, ssid.c_str(), sizeof(wifiSsid) - 1);
    strncpy(wifiPass, wpass.c_str(), sizeof(wifiPass) - 1);
    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    strncpy(mqttUser, muser.c_str(), sizeof(mqttUser) - 1);
    strncpy(mqttPass, mpass.c_str(), sizeof(mqttPass) - 1);

    if (strlen(wifiSsid) == 0) {
        Serial.println("[WIFI] No SSID configured");
        return false;
    }
    if (strlen(mqttHost) == 0) {
        Serial.println("[WIFI] No MQTT host configured");
        return false;
    }

    Serial.printf("[WIFI] SSID: %s, MQTT: %s:%u\n", wifiSsid, mqttHost, mqttPort);
    return true;
}

bool connectWifi() {
    // ESP-NOW and WiFi coexist on the same channel
    // WiFi.mode is already WIFI_STA from ESP-NOW init
    WiFi.begin(wifiSsid, wifiPass);

    Serial.printf("[WIFI] Connecting to %s", wifiSsid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Connection failed");
        return false;
    }

    Serial.printf("[WIFI] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool connectMqtt() {
    mqtt.setServer(mqttHost, mqttPort);

    Serial.printf("[MQTT] Connecting to %s:%u\n", mqttHost, mqttPort);

    bool connected;
    if (strlen(mqttUser) > 0) {
        connected = mqtt.connect("hivesense-collector", mqttUser, mqttPass);
    } else {
        connected = mqtt.connect("hivesense-collector");
    }

    if (!connected) {
        Serial.printf("[MQTT] Connection failed — rc=%d\n", mqtt.state());
        return false;
    }

    Serial.println("[MQTT] Connected");
    return true;
}

uint8_t publishBatch(PayloadBuffer& buffer) {
    uint8_t published = 0;
    char topic[80];
    char value[16];

    const char* suffixes[] = {
        "weight", "temp/brood", "temp/top",
        "humidity/brood", "humidity/top",
        "bees/in", "bees/out", "bees/activity",
        "battery", "rssi"
    };

    for (uint8_t i = 0; i < MAX_HIVE_NODES; i++) {
        if (!buffer.entries[i].occupied) continue;

        const HivePayload& p = buffer.entries[i].payload;

        char values[10][16];
        snprintf(values[0], 16, "%.2f", p.weight_kg);
        snprintf(values[1], 16, "%.1f", p.temp_brood);
        snprintf(values[2], 16, "%.1f", p.temp_top);
        snprintf(values[3], 16, "%.1f", p.humidity_brood);
        snprintf(values[4], 16, "%.1f", p.humidity_top);
        snprintf(values[5], 16, "%u", p.bees_in);
        snprintf(values[6], 16, "%u", p.bees_out);
        snprintf(values[7], 16, "%u", p.bees_activity);
        snprintf(values[8], 16, "%u", p.battery_pct);
        snprintf(values[9], 16, "%d", p.rssi);

        for (uint8_t t = 0; t < 10; t++) {
            snprintf(topic, sizeof(topic), "%s%s/%s",
                     MQTT_TOPIC_PREFIX, p.hive_id, suffixes[t]);
            mqtt.publish(topic, values[t]);
            Serial.printf("[MQTT] %s = %s\n", topic, values[t]);
        }

        published++;
    }

    Serial.printf("[MQTT] Published %u hive payloads\n", published);
    return published;
}

uint32_t syncNtp() {
    configTime(0, 0, "pool.ntp.org");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("[WIFI] NTP sync failed");
        return 0;
    }

    time_t epoch = mktime(&timeinfo);
    Serial.printf("[WIFI] NTP time: %lu\n", static_cast<uint32_t>(epoch));
    return static_cast<uint32_t>(epoch);
}

void disconnect() {
    mqtt.disconnect();
    WiFi.disconnect();
    Serial.println("[WIFI] Disconnected");
}

}  // namespace WifiMqtt
