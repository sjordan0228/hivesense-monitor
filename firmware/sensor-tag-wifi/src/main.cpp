#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sleep.h>

#include "config.h"
#include "config_ack.h"
#include "config_parser.h"
#include "config_runtime.h"
#include "capabilities.h"
#include "reading.h"
#include "sensor.h"
#include "battery.h"
#include "payload.h"
#include "ring_buffer.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "serial_console.h"
#include "ota.h"
#include "scale.h"
#include "scale_math.h"
#include <cmath>

namespace {

RTC_DATA_ATTR uint16_t  rtcSampleCounter   = 0;

/// Persists the epoch of the most recent cold-boot NTP sync across deep sleep.
/// Set once (on the first upload wake after a cold boot) and held until the
/// next cold boot resets it.  Zero means "not yet synced".
RTC_DATA_ATTR int64_t   rtcLastBootEpoch   = 0;
RTC_DATA_ATTR uint32_t  rtcLastBootMagic   = 0;
constexpr uint32_t      LAST_BOOT_MAGIC    = 0xCB50B001u;

uint8_t lastBatteryPct = 0;

char deviceId[9] = {0};

/// Derive an 8-hex-char device ID from the chip's BASE MAC (low 4 bytes).
///
/// IMPORTANT — DO NOT use `esp_efuse_mac_get_default()` here. On ESP32-C6
/// that function returns the first 6 bytes of the 8-byte 802.15.4 / Thread
/// EUI-64 — which has the constant fill bytes `FF:FE` inserted in the
/// middle. The result is e.g. `58:e6:c5:FF:FE:12` instead of the actual
/// BASE MAC `58:e6:c5:12:7f:04`. Taking bytes [2..5] of that EUI-64 gives
/// `c5:FF:FE:XX` which only varies in one byte across chips → collisions
/// at ~16 chips by birthday paradox. We saw this in practice.
///
/// `esp_read_mac(mac, ESP_MAC_WIFI_STA)` returns the actual 48-bit BASE
/// MAC. Its bytes [2..5] give 3 bytes of chip-unique entropy plus one
/// OUI byte — ~16M combinations, collision-safe at apiary scale.
void initDeviceId() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("[MAC] base=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
}

struct WakeCfg {
    uint16_t sampleIntervalSec;   // seconds (U16 matches serial console putUShort path)
    uint8_t  uploadEveryN;
};

WakeCfg loadConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    WakeCfg c {
        .sampleIntervalSec = prefs.getUShort(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC),
        .uploadEveryN      = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N),
    };
    prefs.end();
    return c;
}

/// Unified result from applyConfigToNvs.
/// entries[] hold per-key AckEntry records (ok/unchanged/invalid:nvs).
/// anyFeatChanged: true when at least one feat_* key was written (not unchanged).
struct ApplyResult {
    static constexpr size_t MAX = ConfigParser::MAX_REJECTED_KEYS * 2;
    AckEntry entries[MAX];
    size_t   numEntries     = 0;
    bool     anyFeatChanged = false;
    bool     anyFeatTouched = false;  // any feat_* key was processed at all
};

namespace {

void appendEntry(ApplyResult& r, const char* key, const char* result) {
    if (r.numEntries >= ApplyResult::MAX) return;
    AckEntry& e = r.entries[r.numEntries++];
    strncpy(e.key,    key,    sizeof(e.key)    - 1); e.key[sizeof(e.key) - 1]       = '\0';
    strncpy(e.result, result, sizeof(e.result) - 1); e.result[sizeof(e.result) - 1] = '\0';
}

/// Write a uint8 feat flag to NVS if the new value differs from current.
/// Records "ok" on write, "unchanged" if same, "invalid:nvs" on write failure.
void applyFeatFlag(Preferences& prefs, ApplyResult& r,
                   const char* nvsKey, ConfigParser::FeatFlag flag,
                   uint8_t defaultVal) {
    uint8_t incoming = (flag == ConfigParser::FeatFlag::On) ? 1 : 0;
    uint8_t cur      = prefs.getUChar(nvsKey, defaultVal);
    r.anyFeatTouched = true;
    if (cur == incoming) {
        appendEntry(r, nvsKey, "unchanged");
    } else {
        size_t written = prefs.putUChar(nvsKey, incoming);
        if (written > 0) {
            appendEntry(r, nvsKey, "ok");
            r.anyFeatChanged = true;
        } else {
            Serial.printf("[CONFIG] NVS write failed for key=%s\n", nvsKey);
            appendEntry(r, nvsKey, "invalid:nvs");
        }
    }
}

}  // namespace

/// Apply a parsed config update to NVS.
///
/// Per-key idempotency: if the new value matches NVS, records "unchanged"
/// and skips the write (preserves idempotency for retained MQTT messages).
///
/// Returns an ApplyResult with per-key AckEntry records and a flag indicating
/// whether any feat_* value actually changed (used to gate capabilities re-publish).
ApplyResult applyConfigToNvs(const ConfigParser::ConfigUpdate& u) {
    ApplyResult r {};
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // RW

    if (u.has_sample_int) {
        uint16_t cur = prefs.getUShort(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC);
        if (cur == u.sample_int) {
            appendEntry(r, "sample_int", "unchanged");
        } else {
            size_t written = prefs.putUShort(NVS_KEY_SAMPLE_INT, u.sample_int);
            if (written > 0) {
                appendEntry(r, "sample_int", "ok");
            } else {
                Serial.println("[CONFIG] NVS write failed for key=sample_int");
                appendEntry(r, "sample_int", "invalid:nvs");
            }
        }
    }
    if (u.has_upload_every) {
        uint8_t cur = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N);
        if (cur == u.upload_every) {
            appendEntry(r, "upload_every", "unchanged");
        } else {
            size_t written = prefs.putUChar(NVS_KEY_UPLOAD_EVERY, u.upload_every);
            if (written > 0) {
                appendEntry(r, "upload_every", "ok");
            } else {
                Serial.println("[CONFIG] NVS write failed for key=upload_every");
                appendEntry(r, "upload_every", "invalid:nvs");
            }
        }
    }
    if (u.has_tag_name) {
        String cur = prefs.getString(NVS_KEY_TAG_NAME, "");
        if (cur == u.tag_name) {
            appendEntry(r, "tag_name", "unchanged");
        } else {
            size_t written = prefs.putString(NVS_KEY_TAG_NAME, u.tag_name);
            if (written > 0) {
                appendEntry(r, "tag_name", "ok");
            } else {
                Serial.println("[CONFIG] NVS write failed for key=tag_name");
                appendEntry(r, "tag_name", "invalid:nvs");
            }
        }
    }
    if (u.has_ota_host) {
        String cur = prefs.getString(NVS_KEY_OTA_HOST, OTA_DEFAULT_HOST);
        if (cur == u.ota_host) {
            appendEntry(r, "ota_host", "unchanged");
        } else {
            size_t written = prefs.putString(NVS_KEY_OTA_HOST, u.ota_host);
            if (written > 0) {
                appendEntry(r, "ota_host", "ok");
            } else {
                Serial.println("[CONFIG] NVS write failed for key=ota_host");
                appendEntry(r, "ota_host", "invalid:nvs");
            }
        }
    }

    // feat_* flags — stored as UChar(0/1)
    if (u.feat_ds18b20 != ConfigParser::FeatFlag::Absent) {
        applyFeatFlag(prefs, r, "feat_ds18b20", u.feat_ds18b20, DEFAULT_FEAT_DS18B20);
    }
    if (u.feat_sht31 != ConfigParser::FeatFlag::Absent) {
        applyFeatFlag(prefs, r, "feat_sht31", u.feat_sht31, DEFAULT_FEAT_SHT31);
    }
    if (u.feat_scale != ConfigParser::FeatFlag::Absent) {
        applyFeatFlag(prefs, r, "feat_scale", u.feat_scale, DEFAULT_FEAT_SCALE);
    }
    if (u.feat_mic != ConfigParser::FeatFlag::Absent) {
        applyFeatFlag(prefs, r, "feat_mic", u.feat_mic, DEFAULT_FEAT_MIC);
    }

    prefs.end();
    return r;
}

/// Handle a /config/get request — returns current NVS state on config/state topic.
/// Implemented in Block 5.
void handleConfigGet(const char* topic, const uint8_t* payload, size_t len);

/// Callback invoked by MqttClient::loop() when a message arrives on a
/// subscribed topic.  Routes to the appropriate handler by topic suffix.
void handleConfigMessage(const char* topic, const uint8_t* payload, size_t len) {
    // Topic dispatch — evaluated in specificity order.
    if (strstr(topic, "/scale/cmd") || strstr(topic, "/scale/config")) {
        Scale::onMessage(topic, reinterpret_cast<const char*>(payload), len);
        return;
    }
    if (strstr(topic, "/config/get")) {
        handleConfigGet(topic, payload, len);
        return;
    }
    if (!strstr(topic, "/config")) {
        Serial.printf("[CONFIG] unroutable topic=%s — ignoring\n", topic);
        return;
    }

    // /config apply path.
    Serial.printf("[CONFIG] received %u bytes on %s\n",
                  static_cast<unsigned>(len), topic);

    // Empty payload = firmware-side retain-clear signal (never from iOS).
    if (len == 0) {
        Serial.println("[CONFIG] empty payload — retain-clear signal, ignoring");
        return;
    }

    // Make a NUL-terminated copy for the parser.
    char body[256];
    if (len >= sizeof(body)) {
        Serial.println("[CONFIG] payload too large, ignoring");
        return;
    }
    memcpy(body, payload, len);
    body[len] = '\0';

    ConfigParser::ConfigUpdate parsed;
    if (!ConfigParser::parse(body, parsed)) {
        Serial.println("[CONFIG] parse failed");
        return;
    }

    // Pre-validate: enforce cross-key constraints (feat_ds18b20 ⊕ feat_sht31).
    // Read current NVS state for the two mutually-exclusive temp-sensor flags
    // so preValidate can compute the post-apply state without doing I/O itself.
    TemperatureNvsState tempNvsState;
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        tempNvsState.ds18b20_enabled =
            (prefs.getUChar("feat_ds18b20", DEFAULT_FEAT_DS18B20) != 0);
        tempNvsState.sht31_enabled =
            (prefs.getUChar("feat_sht31", DEFAULT_FEAT_SHT31) != 0);
        prefs.end();
    }

    // Accumulate all per-key results in a single flat array for the rich ack.
    // Capacity: preValidate entries + apply entries + parser-rejected entries.
    constexpr size_t MAX_ACK = ApplyResult::MAX + ConfigParser::MAX_REJECTED_KEYS;
    AckEntry allEntries[MAX_ACK];
    size_t   numEntries = 0;

    AckEntry preValidEntries[ConfigParser::MAX_REJECTED_KEYS];
    size_t   preValidCount = 0;

    bool applyAllowed = preValidate(parsed, tempNvsState, preValidEntries, &preValidCount);

    if (!applyAllowed) {
        // Collect conflict entries from preValidate for the ack.
        for (size_t i = 0; i < preValidCount && numEntries < MAX_ACK; ++i) {
            allEntries[numEntries++] = preValidEntries[i];
        }
        // Also add parser-rejected keys (unknown / invalid value).
        for (uint8_t i = 0; i < parsed.num_rejected && numEntries < MAX_ACK; ++i) {
            AckEntry& e = allEntries[numEntries++];
            strncpy(e.key,    parsed.rejected[i], sizeof(e.key)    - 1); e.key[sizeof(e.key) - 1]       = '\0';
            strncpy(e.result, "unknown_key",       sizeof(e.result) - 1); e.result[sizeof(e.result) - 1] = '\0';
        }
        Serial.println("[CONFIG] preValidate rejected — aborting apply");
    } else {
        ApplyResult applied = applyConfigToNvs(parsed);
        Serial.printf("[CONFIG] numEntries=%u\n",
                      static_cast<unsigned>(applied.numEntries));

        // Merge apply results.
        for (size_t i = 0; i < applied.numEntries && numEntries < MAX_ACK; ++i) {
            allEntries[numEntries++] = applied.entries[i];
        }
        // Parser-rejected keys → "unknown_key" (they were not even attempted).
        for (uint8_t i = 0; i < parsed.num_rejected && numEntries < MAX_ACK; ++i) {
            AckEntry& e = allEntries[numEntries++];
            strncpy(e.key,    parsed.rejected[i], sizeof(e.key)    - 1); e.key[sizeof(e.key) - 1]       = '\0';
            strncpy(e.result, "unknown_key",       sizeof(e.result) - 1); e.result[sizeof(e.result) - 1] = '\0';
        }

        // §2: clear broker-side retain BEFORE publishing ack.
        char configTopic[80];
        snprintf(configTopic, sizeof(configTopic), "%s%s/config",
                 MQTT_TOPIC_PREFIX, deviceId);
        MqttClient::publishRaw(configTopic, "", /*retained=*/true);

        // §3.2: re-publish capabilities only when a feat_* key was processed
        // (any category — ok, unchanged, invalid, conflict) per the contract.
        // "Touched" is the correct gate: even an unchanged feat_* flag confirms
        // iOS's mental model of the capabilities state.
        if (anyFeatKeyPresent(applied.entries, applied.numEntries)) {
            Serial.println("[CONFIG] re-publishing capabilities (feat_* touched)");
            if (!Capabilities::publish(rtcLastBootEpoch)) {
                Serial.println("[CAP] re-publish after config failed");
            }
        }
    }

    // §5.1: publish rich ack to `combsense/hive/<id>/config/ack`.
    char ackTopic[96];
    snprintf(ackTopic, sizeof(ackTopic), "%s%s/config/ack",
             MQTT_TOPIC_PREFIX, deviceId);

    time_t nowEpoch = 0;
    time(&nowEpoch);

    char ackBody[512];
    size_t ackLen = buildRichAck(allEntries, numEntries,
                                 static_cast<int64_t>(nowEpoch),
                                 ackBody, sizeof(ackBody));
    if (ackLen > 0 && ackLen < sizeof(ackBody)) {
        if (!MqttClient::publishRaw(ackTopic, ackBody, false)) {  // not retained
            Serial.printf("[MQTT] publishRaw failed topic=%s\n", ackTopic);
        }
    }
}

/// Known config keys that can be returned by config/get → config/state.
/// Excluded-by-policy keys (wifi_pass, mqtt_pass) are absent from this list
/// and will never be returned, even if explicitly requested in the `keys` filter.
struct KnownConfigKey {
    const char* name;
    enum class Type { UShort, UChar, String } type;
    union Default {
        uint16_t u16;
        uint8_t  u8;
        const char* str;
    } def;
};

static const KnownConfigKey KNOWN_CONFIG_KEYS[] = {
    { "feat_ds18b20",  KnownConfigKey::Type::UChar,   { .u8  = DEFAULT_FEAT_DS18B20         } },
    { "feat_sht31",    KnownConfigKey::Type::UChar,   { .u8  = DEFAULT_FEAT_SHT31            } },
    { "feat_scale",    KnownConfigKey::Type::UChar,   { .u8  = DEFAULT_FEAT_SCALE            } },
    { "feat_mic",      KnownConfigKey::Type::UChar,   { .u8  = DEFAULT_FEAT_MIC              } },
    { "sample_int",    KnownConfigKey::Type::UShort,  { .u16 = DEFAULT_SAMPLE_INTERVAL_SEC  } },
    { "upload_every",  KnownConfigKey::Type::UChar,   { .u8  = DEFAULT_UPLOAD_EVERY_N        } },
    { "tag_name",      KnownConfigKey::Type::String,  { .str = ""                            } },
    { "ota_host",      KnownConfigKey::Type::String,  { .str = OTA_DEFAULT_HOST              } },
};
static constexpr size_t NUM_KNOWN_CONFIG_KEYS =
    sizeof(KNOWN_CONFIG_KEYS) / sizeof(KNOWN_CONFIG_KEYS[0]);

/// Handle a /config/get request per contract §7.
///
/// Empty payload → return all known keys.
/// {"keys":["sample_int","feat_scale"]} → return only the listed keys that
/// exist in the allowed set (excluded-by-policy keys silently omitted).
///
/// Response published to combsense/hive/<id>/config/state (retain=false).
void handleConfigGet(const char* /*topic*/, const uint8_t* payload, size_t len) {
    // Build the set of keys to return.  Default: all known keys.
    // If the payload is non-empty JSON with a "keys" array, filter to that subset.
    bool includeAll = true;
    char requestedKeys[NUM_KNOWN_CONFIG_KEYS][16] = {};
    size_t numRequested = 0;

    if (len > 0) {
        char body[256];
        if (len < sizeof(body)) {
            memcpy(body, payload, len);
            body[len] = '\0';
            JsonDocument req;
            if (deserializeJson(req, body) == DeserializationError::Ok) {
                JsonArray keysArr = req["keys"].as<JsonArray>();
                if (!keysArr.isNull()) {
                    includeAll = false;
                    for (JsonVariant v : keysArr) {
                        const char* k = v.as<const char*>();
                        if (k && numRequested < NUM_KNOWN_CONFIG_KEYS) {
                            strncpy(requestedKeys[numRequested], k,
                                    sizeof(requestedKeys[0]) - 1);
                            requestedKeys[numRequested][sizeof(requestedKeys[0]) - 1] = '\0';
                            numRequested++;
                        }
                    }
                }
            }
        }
    }

    auto shouldInclude = [&](const char* name) -> bool {
        // Policy gate: never return excluded keys regardless of filter.
        if (isConfigGetExcluded(name)) return false;
        if (includeAll) return true;
        for (size_t i = 0; i < numRequested; ++i) {
            if (strcmp(requestedKeys[i], name) == 0) return true;
        }
        return false;
    };

    // Read NVS values and build response JSON.
    JsonDocument doc;
    doc["event"] = "config_state";
    JsonObject values = doc["values"].to<JsonObject>();

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    for (size_t i = 0; i < NUM_KNOWN_CONFIG_KEYS; ++i) {
        const KnownConfigKey& k = KNOWN_CONFIG_KEYS[i];
        if (!shouldInclude(k.name)) continue;
        switch (k.type) {
            case KnownConfigKey::Type::UShort:
                values[k.name] = prefs.getUShort(k.name, k.def.u16);
                break;
            case KnownConfigKey::Type::UChar:
                values[k.name] = prefs.getUChar(k.name, k.def.u8);
                break;
            case KnownConfigKey::Type::String:
                values[k.name] = prefs.getString(k.name, k.def.str);
                break;
        }
    }
    prefs.end();

    char tsBuf[22] = {};
    time_t nowEpoch = 0;
    time(&nowEpoch);
    if (nowEpoch <= 0 || formatRFC3339(static_cast<int64_t>(nowEpoch), tsBuf, sizeof(tsBuf)) == 0) {
        strncpy(tsBuf, "1970-01-01T00:00:00Z", sizeof(tsBuf) - 1);
    }
    doc["ts"] = tsBuf;

    char stateTopic[96];
    snprintf(stateTopic, sizeof(stateTopic), "%s%s/config/state",
             MQTT_TOPIC_PREFIX, deviceId);

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (!MqttClient::publishRaw(stateTopic, buf, /*retained=*/false)) {
            Serial.printf("[MQTT] publishRaw failed topic=%s\n", stateTopic);
        }
    }
}

// Forward declaration — defined below uploadAndCheckOta.
void sampleAndEnqueue();

/// Drain readings over MQTT and run the OTA check inside a single WiFi window.
/// OTA must share the radio session with MQTT — a separate connect after
/// disconnect leaves a window where WiFi.mode(OFF) makes esp_http_client fail.
/// Uses rtcLastBootEpoch (set once on first post-cold-boot NTP sync) for
/// capabilities.last_boot_ts per contract §3.1.
void uploadAndCheckOta(uint8_t batteryPct) {
    if (!WifiManager::connect()) {
        Serial.println("[MAIN] no wifi — keeping buffer, skipping OTA");
        // Still sample so the reading goes into the buffer for next-wake retry.
        sampleAndEnqueue();
        return;
    }

    // Sync the RTC clock on every upload cycle. SNTP state persists across
    // deep sleep once set, so subsequent samples get real timestamps.
    WifiManager::getUnixTime();

    // Record cold-boot epoch on the first wake after a cold boot where NTP
    // succeeds.  rtcLastBootMagic guards against stale RTC memory.
    if (rtcLastBootEpoch == 0 || rtcLastBootMagic != LAST_BOOT_MAGIC) {
        time_t now = 0;
        time(&now);
        if (now > 1700000000) {
            rtcLastBootEpoch = static_cast<int64_t>(now);
            rtcLastBootMagic = LAST_BOOT_MAGIC;
            Serial.printf("[MAIN] cold-boot epoch set: %lld\n",
                          static_cast<long long>(rtcLastBootEpoch));
        }
    }

    // Connect MQTT regardless of buffer state — we still want to drain
    // any retained config messages even if there are no readings to send.
    bool mqttUp = MqttClient::connect(deviceId);
    if (!mqttUp) {
        Serial.println("[MAIN] no mqtt — keeping buffer, skipping config + OTA");
        // Still sample so the reading goes into the buffer for next-wake retry.
        sampleAndEnqueue();
    } else {
        // WiFi.RSSI() is int32_t; real-world range -100..-30 dBm fits int8_t.
        // Captured post-connect so association is confirmed.
        int8_t sessionRssi = static_cast<int8_t>(WiFi.RSSI());
        Serial.printf("[MAIN] mqtt connected rssi=%d dBm\n", sessionRssi);

        // Subscribe to the per-device config topic. Retained messages (if
        // any) are delivered immediately on subscribe; loop() pumps the
        // pubsub callback so handleConfigMessage runs in this same wake.
        MqttClient::setMessageHandler(handleConfigMessage);
        char configTopic[96];
        snprintf(configTopic, sizeof(configTopic), "%s%s/config",
                 MQTT_TOPIC_PREFIX, deviceId);
        MqttClient::subscribe(configTopic);
        MqttClient::loop(500);  // 500 ms drain window for retained config

        // Subscribe to scale/cmd and scale/config; waits up to 1.5s for retained config.
        Scale::onConnect();

        // §10.b: sample AFTER config is applied so feat_* flags are current.
        sampleAndEnqueue();

        // Publish capabilities with post-apply feature flag state.
        // rtcLastBootEpoch is 0 until the first NTP-confirmed cold-boot wake;
        // capabilities.buildPayload converts 0 → sentinel "1970-01-01T00:00:00Z".
        if (!Capabilities::publish(rtcLastBootEpoch)) Serial.println("[CAP] publish failed");

        // Now drain readings.
        uint8_t sent = 0;
        while (RingBuffer::size() > 0) {
            Reading r;
            if (!RingBuffer::peekOldest(r)) break;
            if (!MqttClient::publish(deviceId, r, sessionRssi)) break;

            // Dual-publish: also send dedicated weight topic for scale-aware consumers.
            if (Scale::isCalibrated() && std::isfinite(r.weight_kg)) {
                char weight_topic[80];
                char weight_payload[16];
                snprintf(weight_topic,   sizeof(weight_topic),
                         "combsense/hive/%s/weight", deviceId);
                snprintf(weight_payload, sizeof(weight_payload),
                         "%.3f", static_cast<double>(r.weight_kg));
                if (!MqttClient::publishRaw(weight_topic, weight_payload, /*retained=*/false)) {
                    Serial.printf("[MQTT] publishRaw failed topic=%s\n", weight_topic);
                }
            }

            RingBuffer::popOldest();
            sent++;
            Ota::onPublishSuccess();
        }
        Serial.printf("[MAIN] sent %u / remaining %u\n", sent, RingBuffer::size());

        // One more brief loop to flush any in-flight ack from the config
        // handler — publishRaw is QoS 0, return-on-buffered, not on broker
        // ack.
        MqttClient::loop(200);
        MqttClient::disconnect();
    }

    if (batteryPct > 0) {
        Ota::checkAndApply(batteryPct);
    }

    WifiManager::disconnect();
}

/// Take one sensor sample and push it into the ring buffer.
void sampleAndEnqueue() {
    if (!Sensor::begin()) {
        Serial.println("[MAIN] sensor init failed — skipping sample");
        return;
    }

    Reading r {};
    bool ok = Sensor::read(r);
    Sensor::deinit();
    if (!ok) {
        Serial.println("[MAIN] sensor read failed");
        return;
    }

    r.vbat_mV     = Battery::readMillivolts();
    r.battery_pct = Battery::percentFromMillivolts(r.vbat_mV);
    lastBatteryPct = r.battery_pct;

    // Sample the scale (no-op stub when SCALE_ENABLED is not defined).
    int32_t scale_raw = 0;
    double  scale_kg  = NAN;
    Scale::sampleAveraged(10, scale_raw, scale_kg);
    // Only record weight if the scale has been calibrated; NaN causes payload to omit the field.
    r.weight_kg = Scale::isCalibrated() ? static_cast<float>(scale_kg) : NAN;

    // System clock is set by drainBuffer()'s NTP sync and persists across deep
    // sleep. Samples taken before the first successful upload are tagged 0 and
    // backfilled by the collector/backend.
    time_t now = 0;
    time(&now);
    r.timestamp = (now > 1700000000) ? static_cast<uint32_t>(now) : 0;

    RingBuffer::push(r);
    Serial.printf("[MAIN] sample t1=%.2f t2=%.2f h1=%.2f h2=%.2f vbat=%umV b=%u buffered=%u\n",
                  r.temp1, r.temp2, r.humidity1, r.humidity2,
                  r.vbat_mV, r.battery_pct, RingBuffer::size());
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    // Give USB-CDC on the C6 time to enumerate on the host before printing.
    // Without this, the first ~1s of prints are lost because the host hasn't
    // finished CDC setup yet, and we race the provisioning console window.
    Serial.setTxTimeoutMs(0);
    delay(2000);

    initDeviceId();
    Serial.printf("[MAIN] combsense sensor-tag-wifi id=%s version=%s\n",
                  deviceId, FIRMWARE_VERSION);

    Ota::validateOnBoot();

    RingBuffer::initIfColdBoot();

    // Ensure NVS namespace exists for first boot
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    const bool hasSsid = prefs.getString(NVS_KEY_WIFI_SSID, "").length() > 0;
    prefs.end();

    if (!hasSsid) {
        // First boot (or wiped NVS) — the 3s keypress window races host-side
        // USB-CDC enumeration, so force-enter the console until provisioned.
        Serial.println("[MAIN] no wifi_ssid configured — entering console");
        SerialConsole::runBlocking();
    } else {
        SerialConsole::checkForConsole();
    }

    WakeCfg cfg = loadConfig();
    Serial.printf("[MAIN] sample_int=%lus upload_every=%u\n",
                  (unsigned long)cfg.sampleIntervalSec, cfg.uploadEveryN);

    Scale::init();

    // On cold boot, invalidate rtcLastBootEpoch so uploadAndCheckOta sets it
    // fresh after the first successful NTP sync.
    {
        esp_reset_reason_t reason = esp_reset_reason();
        bool coldBoot = (reason != ESP_RST_DEEPSLEEP) || (rtcLastBootMagic != LAST_BOOT_MAGIC);
        if (coldBoot) {
            rtcLastBootEpoch = 0;
            rtcLastBootMagic = 0;
        }
    }

    rtcSampleCounter++;

    if (rtcSampleCounter >= cfg.uploadEveryN) {
        uploadAndCheckOta(lastBatteryPct);
        rtcSampleCounter = 0;
    } else {
        // Non-upload cycle: still sample and buffer; no MQTT/capabilities.
        sampleAndEnqueue();
        Serial.printf("[MAIN] not uploading this cycle (%u/%u)\n",
                      rtcSampleCounter, cfg.uploadEveryN);
    }

    // Extended-awake loop: keep radio alive while scale is accepting tare/cal commands.
    if (Scale::inExtendedAwakeMode() && Scale::ntpSynced()) {
        while (Scale::inExtendedAwakeMode()) {
            MqttClient::loop(20);
            Scale::tick();
            delay(20);
        }
    }

    Scale::deinit();

    Serial.printf("[MAIN] sleeping %lus\n", (unsigned long)cfg.sampleIntervalSec);
    Serial.flush();
    esp_deep_sleep(static_cast<uint64_t>(cfg.sampleIntervalSec) * 1000000ULL);
}

void loop() {
    // Never reached — deep sleep restarts from setup()
}
