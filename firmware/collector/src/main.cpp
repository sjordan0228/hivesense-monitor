#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "espnow_receiver.h"
#include "cellular.h"
#include "mqtt_publisher.h"
#include "time_sync.h"
#include "ota_relay.h"
#include "ota_self.h"

namespace {

uint32_t lastPublishTime = 0;
const uint32_t publishIntervalMs = static_cast<uint32_t>(PUBLISH_INTERVAL_MIN) * 60UL * 1000UL;

void runPublishCycle() {
    Serial.println("[MAIN] === PUBLISH CYCLE ===");

    if (!Cellular::powerOn()) {
        Serial.println("[MAIN] Modem failed — skipping cycle");
        return;
    }

    if (!Cellular::waitForNetwork()) {
        Cellular::powerOff();
        return;
    }

    uint32_t epoch = Cellular::syncNtp();

    if (MqttPublisher::connect(Cellular::getModem())) {
        PayloadBuffer& buffer = EspNowReceiver::getBuffer();
        uint8_t count = MqttPublisher::publishBatch(Cellular::getModem(), buffer);
        Serial.printf("[MAIN] Published %u hive payloads\n", count);
        buffer.clear();

        char hiveId[16], tag[32], target[16];
        if (MqttPublisher::checkOtaCommand(Cellular::getModem(), hiveId, tag, target)) {
            if (strcmp(target, "collector") == 0) {
                Serial.printf("[MAIN] Self-OTA: %s\n", tag);
                MqttPublisher::disconnect(Cellular::getModem());
                OtaSelf::downloadAndApply(tag);
            } else if (strcmp(target, "node") == 0) {
                Serial.printf("[MAIN] OTA relay: %s -> %s\n", tag, hiveId);
                OtaRelay::downloadFirmware(tag);
                OtaRelay::startRelay(hiveId);
            }
        }

        MqttPublisher::disconnect(Cellular::getModem());
    }

    if (epoch > 0) {
        TimeSync::broadcast(epoch);
    }

    Cellular::powerOff();
    Serial.println("[MAIN] Publish cycle complete");
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    Serial.println("\n[MAIN] HiveSense Collector — starting");

    OtaSelf::validateNewFirmware();
    EspNowReceiver::initialize();
    MqttPublisher::initialize();

    lastPublishTime = millis();
    Serial.println("[MAIN] Ready — listening for ESP-NOW packets");
}

void loop() {
    if (millis() - lastPublishTime >= publishIntervalMs) {
        runPublishCycle();
        lastPublishTime = millis();
    }

    if (OtaRelay::isRelayInProgress()) {
        OtaRelay::continueRelay();
    }

    delay(10);
}
