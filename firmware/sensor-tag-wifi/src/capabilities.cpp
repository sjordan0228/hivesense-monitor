#include "capabilities.h"
#include "config_runtime.h"
#include "scale_math.h"
#include "config.h"

#include <ArduinoJson.h>
#include <cstring>
#include <ctime>

#ifdef ARDUINO
#include "mqtt_client.h"
#else
// Native build stubs
namespace MqttClient {
    static const char* getDeviceId() { return "test0000"; }
    static bool publishRaw(const char*, const char*, bool) { return true; }
}
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "test"
#endif
#endif

namespace Capabilities {

size_t buildPayload(const FeatureFlags& flags, int64_t bootEpoch,
                    char* buf, size_t bufsz) {
    JsonDocument doc;

    // Per .mex/config-mqtt-contract.md §3.1: capabilities has its own
    // dedicated topic — no `event` discriminator field.
    doc["feat_ds18b20"] = flags.feat_ds18b20;
    doc["feat_sht31"]   = flags.feat_sht31;
    doc["feat_scale"]   = flags.feat_scale;
    doc["feat_mic"]     = flags.feat_mic;
    doc["hw_board"]     = HW_BOARD;
    doc["fw_version"]   = FIRMWARE_VERSION;

    // last_boot_ts — RFC3339 UTC; epoch=0 → sentinel string
    char lastBootBuf[22] = {};
    if (bootEpoch <= 0) {
        strncpy(lastBootBuf, "1970-01-01T00:00:00Z", sizeof(lastBootBuf) - 1);
    } else {
        if (formatRFC3339(bootEpoch, lastBootBuf, sizeof(lastBootBuf)) == 0) {
            strncpy(lastBootBuf, "1970-01-01T00:00:00Z", sizeof(lastBootBuf) - 1);
        }
    }
    doc["last_boot_ts"] = lastBootBuf;

    // ts — current wall time
    char tsBuf[22] = {};
    int64_t now = 0;
#ifdef ARDUINO
    time_t t = 0;
    time(&t);
    now = static_cast<int64_t>(t);
#else
    now = static_cast<int64_t>(time(nullptr));
#endif
    if (now <= 0 || formatRFC3339(now, tsBuf, sizeof(tsBuf)) == 0) {
        strncpy(tsBuf, "1970-01-01T00:00:00Z", sizeof(tsBuf) - 1);
    }
    doc["ts"] = tsBuf;

    return serializeJson(doc, buf, bufsz);
}

bool publish(int64_t bootEpoch) {
    FeatureFlags flags {
        Config::isEnabled("feat_ds18b20") ? 1 : 0,
        Config::isEnabled("feat_sht31")   ? 1 : 0,
        Config::isEnabled("feat_scale")   ? 1 : 0,
        Config::isEnabled("feat_mic")     ? 1 : 0,
    };

    char payload[256];
    size_t len = buildPayload(flags, bootEpoch, payload, sizeof(payload));
    if (len == 0 || len >= sizeof(payload)) return false;

    char topic[96];
    snprintf(topic, sizeof(topic), "%s%s/capabilities",
             MQTT_TOPIC_PREFIX, MqttClient::getDeviceId());

    return MqttClient::publishRaw(topic, payload, /*retained=*/true);
}

}  // namespace Capabilities
