#include "mqtt_client.h"
#include "config.h"
#include "payload.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Preferences.h>

namespace {

WiFiClient   wifiClient;
PubSubClient pubsub(wifiClient);

char mqttHost[64]   = "";
uint16_t mqttPort   = DEFAULT_MQTT_PORT;
char mqttUser[32]   = "";
char mqttPass[64]   = "";

void loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String host = prefs.getString(NVS_KEY_MQTT_HOST, DEFAULT_MQTT_HOST);
    mqttPort    = prefs.getUShort(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);
    String user = prefs.getString(NVS_KEY_MQTT_USER, "");
    String pass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();
    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
    strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);
}

}  // anonymous namespace

namespace MqttClient {

bool connect(const char* deviceId) {
    loadConfig();
    pubsub.setServer(mqttHost, mqttPort);
    pubsub.setSocketTimeout(MQTT_CONNECT_TIMEOUT_MS / 1000);
    // MQTT framing: 5B fixed header + 2B length + topic + payload. Topic scratch is 96B.
    if (!pubsub.setBufferSize(PAYLOAD_MAX_LEN + 96 + 8)) {
        Serial.println("[MQTT] setBufferSize failed");
        return false;
    }

    Serial.printf("[MQTT] connecting %s:%u as %s\n", mqttHost, mqttPort, deviceId);
    bool ok = (mqttUser[0] != '\0')
        ? pubsub.connect(deviceId, mqttUser, mqttPass)
        : pubsub.connect(deviceId);
    if (!ok) {
        Serial.printf("[MQTT] connect failed state=%d\n", pubsub.state());
    }
    return ok;
}

bool publish(const char* deviceId, const Reading& r) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s%s/reading", MQTT_TOPIC_PREFIX, deviceId);

    char payload[PAYLOAD_MAX_LEN];
    int n = Payload::serialize(deviceId, r, payload, sizeof(payload));
    if (n < 0) {
        Serial.println("[MQTT] payload too large");
        return false;
    }

    bool ok = pubsub.publish(topic, payload, false);
    if (!ok) {
        Serial.printf("[MQTT] publish failed state=%d\n", pubsub.state());
        return false;
    }
    // QoS 0 publishes land in the TCP write buffer and return true before the
    // broker has actually received them. WiFiClient::flush() on Arduino-ESP32
    // checks RX, not TX — so give TCP real wall-clock time to push the bytes
    // out before the caller tears down WiFi. 1s total is empirically what the
    // bench node needs; PubSubClient exposes no TX-empty signal to key off of.
    for (int i = 0; i < 20; i++) {
        pubsub.loop();
        delay(50);
    }
    return true;
}

void disconnect() {
    pubsub.disconnect();
    delay(100);
    wifiClient.stop();
}

}  // namespace MqttClient
