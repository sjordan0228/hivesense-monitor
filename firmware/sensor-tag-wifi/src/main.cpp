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

/// Apply a parsed config update to NVS. Only writes keys whose values
/// differ from what's already stored — preserves idempotency for retained
/// MQTT messages. Populates `applied` and `currentState` with the post-
/// write view, used by the ack message.
struct AckSummary {
    uint8_t  numEntries;
    AckEntry entries[ConfigParser::MAX_REJECTED_KEYS * 2];  // applied + rejected
    // Legacy fast-path counts for buildAckJson; updated by appendApplied/appendRejected.
    uint8_t numApplied;
    char    applied[ConfigParser::MAX_REJECTED_KEYS][ConfigParser::REJECTED_KEY_LEN];
};

void appendApplied(AckSummary& s, const char* key) {
    if (s.numApplied >= ConfigParser::MAX_REJECTED_KEYS) return;
    strncpy(s.applied[s.numApplied], key, ConfigParser::REJECTED_KEY_LEN - 1);
    s.applied[s.numApplied][ConfigParser::REJECTED_KEY_LEN - 1] = '\0';
    s.numApplied += 1;

    // Also record in unified AckEntry array for PR-2.
    if (s.numEntries < sizeof(s.entries) / sizeof(s.entries[0])) {
        strncpy(s.entries[s.numEntries].key,    key, sizeof(s.entries[0].key)    - 1);
        strncpy(s.entries[s.numEntries].result, "ok", sizeof(s.entries[0].result) - 1);
        s.entries[s.numEntries].key[sizeof(s.entries[0].key) - 1] = '\0';
        s.entries[s.numEntries].result[sizeof(s.entries[0].result) - 1] = '\0';
        s.numEntries++;
    }
}

/// Check an NVS put return value; on failure log and move the key to rejected.
/// Returns true if write succeeded, false on NVS write failure.
static bool checkNvsWrite(AckSummary& s, size_t bytesWritten, const char* key) {
    if (bytesWritten > 0) return true;
    Serial.printf("[CONFIG] NVS write failed for key=%s\n", key);
    // Record as a rejected entry.
    if (s.numEntries < sizeof(s.entries) / sizeof(s.entries[0])) {
        strncpy(s.entries[s.numEntries].key,    key,          sizeof(s.entries[0].key)    - 1);
        strncpy(s.entries[s.numEntries].result, "invalid:nvs", sizeof(s.entries[0].result) - 1);
        s.entries[s.numEntries].key[sizeof(s.entries[0].key) - 1] = '\0';
        s.entries[s.numEntries].result[sizeof(s.entries[0].result) - 1] = '\0';
        s.numEntries++;
    }
    return false;
}

AckSummary applyConfigToNvs(const ConfigParser::ConfigUpdate& u) {
    AckSummary s {};
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // RW

    if (u.has_sample_int) {
        uint16_t cur = prefs.getUShort(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC);
        if (cur != u.sample_int) {
            size_t written = prefs.putUShort(NVS_KEY_SAMPLE_INT, u.sample_int);
            if (checkNvsWrite(s, written, "sample_int")) {
                appendApplied(s, "sample_int");
            }
        }
    }
    if (u.has_upload_every) {
        uint8_t cur = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N);
        if (cur != u.upload_every) {
            size_t written = prefs.putUChar(NVS_KEY_UPLOAD_EVERY, u.upload_every);
            if (checkNvsWrite(s, written, "upload_every")) {
                appendApplied(s, "upload_every");
            }
        }
    }
    if (u.has_tag_name) {
        String cur = prefs.getString(NVS_KEY_TAG_NAME, "");
        if (cur != u.tag_name) {
            size_t written = prefs.putString(NVS_KEY_TAG_NAME, u.tag_name);
            if (checkNvsWrite(s, written, "tag_name")) {
                appendApplied(s, "tag_name");
            }
        }
    }
    if (u.has_ota_host) {
        String cur = prefs.getString(NVS_KEY_OTA_HOST, OTA_DEFAULT_HOST);
        if (cur != u.ota_host) {
            size_t written = prefs.putString(NVS_KEY_OTA_HOST, u.ota_host);
            if (checkNvsWrite(s, written, "ota_host")) {
                appendApplied(s, "ota_host");
            }
        }
    }

    prefs.end();
    return s;
}

/// Handle a /config/get request — returns current NVS state as JSON.
/// PR-1 stub: logs and returns without publishing.  PR-2 will implement.
void handleConfigGet(const char* /*topic*/) {
    Serial.println("[CONFIG] /config/get not implemented in PR-1");
}

/// Build the ack message JSON. Reads current NVS to populate current_state
/// (post-apply view).
size_t buildAckJson(const AckSummary& applied,
                    const ConfigParser::ConfigUpdate& parsed,
                    char* out, size_t outCap) {
    JsonDocument doc;

    JsonArray arrApplied = doc["applied"].to<JsonArray>();
    for (uint8_t i = 0; i < applied.numApplied; ++i) {
        arrApplied.add(applied.applied[i]);
    }

    JsonArray arrRejected = doc["rejected"].to<JsonArray>();
    for (uint8_t i = 0; i < parsed.num_rejected; ++i) {
        arrRejected.add(parsed.rejected[i]);
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    JsonObject state = doc["current_state"].to<JsonObject>();
    state["sample_int"]   = prefs.getUShort(NVS_KEY_SAMPLE_INT, DEFAULT_SAMPLE_INTERVAL_SEC);
    state["upload_every"] = prefs.getUChar(NVS_KEY_UPLOAD_EVERY, DEFAULT_UPLOAD_EVERY_N);
    state["tag_name"]     = prefs.getString(NVS_KEY_TAG_NAME, "");
    state["ota_host"]     = prefs.getString(NVS_KEY_OTA_HOST, OTA_DEFAULT_HOST);
    prefs.end();

    return serializeJson(doc, out, outCap);
}

/// Callback invoked by MqttClient::loop() when a message arrives on a
/// subscribed topic.  Routes to the appropriate handler by topic suffix.
void handleConfigMessage(const char* topic, const uint8_t* payload, size_t len) {
    // Topic dispatch — evaluated in specificity order.
    if (strstr(topic, "/scale/cmd") || strstr(topic, "/scale/config")) {
        Scale::onMessage(topic, reinterpret_cast<const char*>(payload), len);
        return;
    }
    if (strstr(topic, "/config/get")) {
        handleConfigGet(topic);
        return;
    }
    if (!strstr(topic, "/config")) {
        Serial.printf("[CONFIG] unroutable topic=%s — ignoring\n", topic);
        return;
    }

    // /config apply path.
    Serial.printf("[CONFIG] received %u bytes on %s\n",
                  static_cast<unsigned>(len), topic);

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

    // Pre-validate: PR-2 will enforce cross-key constraints here.
    AckEntry preValidEntries[ConfigParser::MAX_REJECTED_KEYS];
    size_t   preValidCount = 0;
    if (!preValidate(parsed, preValidEntries, &preValidCount)) {
        Serial.println("[CONFIG] preValidate rejected — aborting apply");
        return;
    }

    AckSummary applied = applyConfigToNvs(parsed);
    Serial.printf("[CONFIG] applied=%u rejected=%u\n",
                  applied.numApplied, parsed.num_rejected);

    // §3.2: re-publish capabilities after any successful config apply so the
    // backend sees the updated feat_* state immediately.  PR-2 will gate this
    // on "did any feat_* key actually change"; for PR-1 it's unconditional and
    // harmless (capabilities payload is identical when no feat_* keys changed).
    if (applied.numApplied > 0) {
        Serial.println("[CONFIG] re-publishing capabilities post-apply");
        if (!Capabilities::publish(rtcLastBootEpoch)) {
            Serial.println("[CAP] re-publish after config failed");
        }
    }

    // Publish ack to `combsense/hive/<id>/config/ack`.
    char ackTopic[96];
    snprintf(ackTopic, sizeof(ackTopic), "%s%s/config/ack",
             MQTT_TOPIC_PREFIX, deviceId);

    char ackBody[384];
    size_t ackLen = buildAckJson(applied, parsed, ackBody, sizeof(ackBody));
    if (ackLen > 0 && ackLen < sizeof(ackBody)) {
        if (!MqttClient::publishRaw(ackTopic, ackBody, false)) {  // not retained
            Serial.printf("[MQTT] publishRaw failed topic=%s\n", ackTopic);
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
