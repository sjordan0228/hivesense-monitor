#include "config_parser.h"

#include <ArduinoJson.h>
#include <cstring>

namespace ConfigParser {

namespace {

void recordReject(ConfigUpdate& out, const char* key) {
    if (out.num_rejected >= MAX_REJECTED_KEYS) return;
    char* slot = out.rejected[out.num_rejected];
    size_t n = strlen(key);
    if (n >= REJECTED_KEY_LEN) n = REJECTED_KEY_LEN - 1;
    memcpy(slot, key, n);
    slot[n] = '\0';
    out.num_rejected += 1;
}

void copyString(char* dst, size_t dstCap, const char* src) {
    size_t n = strlen(src);
    if (n >= dstCap) n = dstCap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool isAllowedStringValue(const char* s, size_t maxLen) {
    if (s == nullptr) return false;
    return strlen(s) < maxLen;  // strict-less so we have room for NUL
}

}  // namespace

bool parse(const char* json, ConfigUpdate& out) {
    memset(&out, 0, sizeof(out));

    if (json == nullptr) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    if (!doc.is<JsonObject>()) return false;

    JsonObject root = doc.as<JsonObject>();

    for (JsonPair kv : root) {
        const char* key = kv.key().c_str();

        // sample_int
        if (strcmp(key, "sample_int") == 0) {
            if (!kv.value().is<int>()) {
                recordReject(out, key);
                continue;
            }
            int v = kv.value().as<int>();
            if (v < SAMPLE_INT_MIN || v > SAMPLE_INT_MAX) {
                recordReject(out, key);
                continue;
            }
            out.has_sample_int = true;
            out.sample_int     = static_cast<uint16_t>(v);
            continue;
        }

        // upload_every
        if (strcmp(key, "upload_every") == 0) {
            if (!kv.value().is<int>()) {
                recordReject(out, key);
                continue;
            }
            int v = kv.value().as<int>();
            if (v < UPLOAD_EVERY_MIN || v > UPLOAD_EVERY_MAX) {
                recordReject(out, key);
                continue;
            }
            out.has_upload_every = true;
            out.upload_every     = static_cast<uint8_t>(v);
            continue;
        }

        // tag_name
        if (strcmp(key, "tag_name") == 0) {
            if (!kv.value().is<const char*>()) {
                recordReject(out, key);
                continue;
            }
            const char* s = kv.value().as<const char*>();
            if (!isAllowedStringValue(s, TAG_NAME_MAX_LEN)) {
                recordReject(out, key);
                continue;
            }
            out.has_tag_name = true;
            copyString(out.tag_name, TAG_NAME_MAX_LEN, s);
            continue;
        }

        // ota_host
        if (strcmp(key, "ota_host") == 0) {
            if (!kv.value().is<const char*>()) {
                recordReject(out, key);
                continue;
            }
            const char* s = kv.value().as<const char*>();
            if (!isAllowedStringValue(s, OTA_HOST_MAX_LEN)) {
                recordReject(out, key);
                continue;
            }
            out.has_ota_host = true;
            copyString(out.ota_host, OTA_HOST_MAX_LEN, s);
            continue;
        }

        // Anything else — unknown key OR an excluded-by-policy key
        // (wifi_ssid, wifi_pass, mqtt_host, mqtt_user, mqtt_pass).
        // Both classes get the same treatment in v1: silent reject.
        recordReject(out, key);
    }

    return true;
}

}  // namespace ConfigParser
