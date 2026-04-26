#pragma once

#include <cstdint>
#include "reading.h"

namespace MqttClient {

/// Connect to the broker using credentials from NVS. Requires WiFi up.
bool connect(const char* deviceId);

/// Publish a reading to `combsense/hive/<deviceId>/reading`. Returns true on ack.
bool publish(const char* deviceId, const Reading& r, int8_t rssi);

/// Disconnect gracefully.
void disconnect();

}  // namespace MqttClient
