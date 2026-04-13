#include "mqtt_publisher.h"
#include "config.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

char mqttHost[64] = "";
uint16_t mqttPort = 8883;
char mqttUser[32] = "";
char mqttPass[64] = "";

/// Publish a single value to an MQTT topic using SIM7080G native MQTT AT commands.
bool publishValue(TinyGsm& modem, const char* topic, const char* value) {
    int len = strlen(value);

    modem.sendAT("+SMPUB=\"", topic, "\",", len, ",1,0");
    if (modem.waitResponse(2000, ">") != 1) {
        Serial.printf("[MQTT] Publish prompt timeout for %s\n", topic);
        return false;
    }

    modem.stream.write(value, len);

    if (modem.waitResponse(5000) != 1) {
        Serial.printf("[MQTT] Publish failed for %s\n", topic);
        return false;
    }

    return true;
}

/// Publish all fields of a single HivePayload to their respective MQTT topics.
bool publishPayload(TinyGsm& modem, const HivePayload& p) {
    char topic[80];
    char value[16];

    const char* suffixes[] = {
        "weight", "temp/brood", "temp/top",
        "humidity/brood", "humidity/top",
        "bees/in", "bees/out", "bees/activity",
        "battery", "rssi"
    };

    // Format each value
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

    for (uint8_t i = 0; i < 10; i++) {
        snprintf(topic, sizeof(topic), "%s%s/%s",
                 MQTT_TOPIC_PREFIX, p.hive_id, suffixes[i]);

        if (!publishValue(modem, topic, values[i])) {
            return false;
        }

        Serial.printf("[MQTT] %s = %s\n", topic, values[i]);
    }

    return true;
}

}  // anonymous namespace

namespace MqttPublisher {

bool initialize() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    String host = prefs.getString(NVS_KEY_MQTT_HOST, "");
    mqttPort    = prefs.getUShort(NVS_KEY_MQTT_PORT, 8883);
    String user = prefs.getString(NVS_KEY_MQTT_USER, "");
    String pass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();

    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
    strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);

    if (strlen(mqttHost) == 0) {
        Serial.println("[MQTT] WARNING: No broker configured");
        return false;
    }

    Serial.printf("[MQTT] Configured: %s:%u\n", mqttHost, mqttPort);
    return true;
}

bool connect(TinyGsm& modem) {
    // SIM7080G native MQTT configuration
    modem.sendAT("+SMCONF=\"URL\",\"", mqttHost, "\",", mqttPort);
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"USERNAME\",\"", mqttUser, "\"");
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"PASSWORD\",\"", mqttPass, "\"");
    modem.waitResponse();

    modem.sendAT("+SMCONF=\"CLEANSS\",1");
    modem.waitResponse();

    modem.sendAT("+SMCONN");
    if (modem.waitResponse(MQTT_CONNECT_TIMEOUT_MS) != 1) {
        Serial.println("[MQTT] Connection failed");
        return false;
    }

    Serial.println("[MQTT] Connected");
    return true;
}

uint8_t publishBatch(TinyGsm& modem, PayloadBuffer& buffer) {
    uint8_t published = 0;

    for (uint8_t i = 0; i < MAX_HIVE_NODES; i++) {
        if (!buffer.entries[i].occupied) continue;

        if (publishPayload(modem, buffer.entries[i].payload)) {
            published++;
        }
    }

    Serial.printf("[MQTT] Published %u hive payloads\n", published);
    return published;
}

bool checkOtaCommand(TinyGsm& modem, char* hiveId, char* tag, char* target) {
    // Subscribe to OTA topic
    modem.sendAT("+SMSUB=\"", MQTT_OTA_TOPIC, "\",1");
    if (modem.waitResponse(5000) != 1) {
        return false;
    }

    // Brief wait for messages
    delay(2000);

    String response;
    if (modem.stream.available()) {
        response = modem.stream.readStringUntil('\n');
    }

    // Unsubscribe
    modem.sendAT("+SMUNSUB=\"", MQTT_OTA_TOPIC, "\"");
    modem.waitResponse();

    if (response.length() == 0) {
        return false;
    }

    // Parse: {"hive_id":"X","tag":"X","target":"X"}
    int hiveStart = response.indexOf("\"hive_id\":\"") + 11;
    int hiveEnd   = response.indexOf("\"", hiveStart);
    int tagStart  = response.indexOf("\"tag\":\"") + 7;
    int tagEnd    = response.indexOf("\"", tagStart);
    int tgtStart  = response.indexOf("\"target\":\"") + 10;
    int tgtEnd    = response.indexOf("\"", tgtStart);

    if (hiveStart < 11 || tagStart < 7 || tgtStart < 10) {
        return false;
    }

    response.substring(hiveStart, hiveEnd).toCharArray(hiveId, 16);
    response.substring(tagStart, tagEnd).toCharArray(tag, 32);
    response.substring(tgtStart, tgtEnd).toCharArray(target, 16);

    Serial.printf("[MQTT] OTA command: hive=%s tag=%s target=%s\n", hiveId, tag, target);
    return true;
}

void disconnect(TinyGsm& modem) {
    modem.sendAT("+SMDISC");
    modem.waitResponse();
    Serial.println("[MQTT] Disconnected");
}

}  // namespace MqttPublisher
