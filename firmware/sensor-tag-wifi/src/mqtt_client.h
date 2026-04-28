#pragma once

#include <cstddef>
#include <cstdint>
#include "reading.h"

namespace MqttClient {

/// Callback invoked once per incoming message after subscribe().
/// `payload` is NOT NUL-terminated — use `len`.
typedef void (*MessageHandler)(const char* topic,
                               const uint8_t* payload,
                               size_t len);

/// Connect to the broker using credentials from NVS. Requires WiFi up.
bool connect(const char* deviceId);

/// Publish a reading to `combsense/hive/<deviceId>/reading`. Returns true on ack.
bool publish(const char* deviceId, const Reading& r, int8_t rssi);

/// Publish a raw payload. Used for ack messages (config/ack topic).
/// `retained` true makes the broker keep the message for late subscribers.
bool publishRaw(const char* topic, const char* payload, bool retained);

/// Register the handler invoked for every message received on subscribed
/// topics. Idempotent — call once before connect() (or once per session).
void setMessageHandler(MessageHandler handler);

/// Subscribe to a topic. Must be called after connect(); retained messages
/// (if any) are delivered immediately on subscribe.
bool subscribe(const char* topic);

/// Pump pubsub.loop() for `ms` milliseconds. Called between subscribe and
/// publish to drain any pending messages — including retained ones.
void loop(uint32_t ms);

/// Disconnect gracefully.
void disconnect();

}  // namespace MqttClient
