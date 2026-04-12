#pragma once

#include "types.h"

#define TINY_GSM_MODEM_SIM7080
#include <TinyGsmClient.h>

/// Connects to HiveMQ Cloud and publishes batched sensor data.
/// Subscribes to OTA command topic.
namespace MqttPublisher {

    /// Load MQTT credentials from NVS.
    bool initialize();

    /// Connect to MQTT broker using modem's native MQTT stack.
    bool connect(TinyGsm& modem);

    /// Publish all occupied buffer entries. Returns number published.
    uint8_t publishBatch(TinyGsm& modem, PayloadBuffer& buffer);

    /// Check for OTA commands on subscribed topic.
    /// Populates hiveId, tag, target on success.
    bool checkOtaCommand(TinyGsm& modem, char* hiveId, char* tag, char* target);

    /// Clean disconnect from broker.
    void disconnect(TinyGsm& modem);

}  // namespace MqttPublisher
