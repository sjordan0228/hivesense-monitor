#include "ota.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_ota_ops.h>
#include <cstring>
#include <cstdlib>

#include "config.h"
#include "ota_decision.h"
#include "ota_manifest.h"
#include "ota_sha256.h"
#include "ota_state.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

#ifndef OTA_VARIANT
#define OTA_VARIANT "unknown"
#endif

namespace {

/// Read `ota_host` from NVS and sanitize it.
///
/// Tolerates a leading `http://` or `https://` scheme (some users provision
/// with a full URL — the URL template at the call site prepends its own
/// `http://`, so a scheme here produces `http://http://host/...` which
/// fails DNS resolution on the literal "http"). Also strips trailing
/// slashes so `host/` doesn't produce `host//firmware/...`.
///
/// The OTA transport is HTTP-only by design (raw WiFiClient via
/// `openHttpGet` — TLS is intentionally avoided to dodge the C6's
/// OpenThread DNS64 path). Any scheme prefix is therefore informational
/// at most and safe to discard.
void readOtaHost(char* out, size_t outCap) {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    String v = p.getString(NVS_KEY_OTA_HOST, OTA_DEFAULT_HOST);
    p.end();

    const char* host = v.c_str();
    if (strncmp(host, "http://", 7) == 0) {
        host += 7;
    } else if (strncmp(host, "https://", 8) == 0) {
        host += 8;
    }

    size_t n = strlen(host);
    while (n > 0 && host[n - 1] == '/') --n;

    if (n >= outCap) n = outCap - 1;
    memcpy(out, host, n);
    out[n] = '\0';
}

struct UrlParts {
    char     host[64];
    uint16_t port;
    char     path[160];
};

/// Parse `http://host[:port]/path` into UrlParts. Bypassing DNS later
/// requires the host as a separate string we can pass to IPAddress::fromString.
bool parseHttpUrl(const char* url, UrlParts& out) {
    if (strncmp(url, "http://", 7) != 0) return false;
    const char* p     = url + 7;
    const char* slash = strchr(p, '/');
    const char* colon = strchr(p, ':');

    size_t hostLen;
    if (colon && (!slash || colon < slash)) {
        hostLen  = static_cast<size_t>(colon - p);
        out.port = static_cast<uint16_t>(atoi(colon + 1));
    } else {
        hostLen  = slash ? static_cast<size_t>(slash - p) : strlen(p);
        out.port = 80;
    }
    if (hostLen == 0 || hostLen >= sizeof(out.host)) return false;
    memcpy(out.host, p, hostLen);
    out.host[hostLen] = '\0';

    if (slash) {
        size_t pathLen = strlen(slash);
        if (pathLen >= sizeof(out.path)) return false;
        memcpy(out.path, slash, pathLen + 1);
    } else {
        out.path[0] = '/'; out.path[1] = '\0';
    }
    return true;
}

/// Open a raw HTTP/1.0 GET via WiFiClient. Bypasses esp-tls/getaddrinfo,
/// which on ESP32-C6 routes through OpenThread DNS64 and fails for IPv4
/// literals (EAI_FAIL/202). PubSubClient uses the same WiFiClient path
/// successfully — this aligns OTA with that proven transport.
bool openHttpGet(WiFiClient& client, const UrlParts& u) {
    IPAddress ip;
    if (!ip.fromString(u.host)) {
        if (!WiFi.hostByName(u.host, ip)) return false;
    }
    client.setTimeout(OTA_HTTP_TIMEOUT_MS / 1000);
    if (!client.connect(ip, u.port)) return false;
    client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  u.path, u.host);
    return true;
}

/// Wait for client.available() up to OTA_HTTP_TIMEOUT_MS.
bool waitForBytes(WiFiClient& client) {
    uint32_t start = millis();
    while (!client.available() && client.connected()) {
        if (millis() - start > OTA_HTTP_TIMEOUT_MS) return false;
        delay(10);
    }
    return client.available();
}

/// Read and discard HTTP headers; return true on 200 OK status line.
bool readStatusAndDrainHeaders(WiFiClient& client) {
    if (!waitForBytes(client)) return false;
    String status = client.readStringUntil('\n');
    if (status.indexOf(" 200 ") < 0) {
        Serial.printf("[OTA] http status: %s\n", status.c_str());
        return false;
    }
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) return true;
    }
    return false;
}

bool fetchManifestText(const char* url, char* buf, size_t bufCap, int& outLen) {
    UrlParts u;
    if (!parseHttpUrl(url, u)) return false;

    WiFiClient client;
    if (!openHttpGet(client, u)) return false;
    if (!readStatusAndDrainHeaders(client)) { client.stop(); return false; }

    int total = 0;
    while ((client.connected() || client.available()) && total < (int)bufCap - 1) {
        if (client.available()) {
            int n = client.read(reinterpret_cast<uint8_t*>(buf + total),
                                bufCap - 1 - total);
            if (n > 0) total += n;
        } else {
            delay(5);
        }
    }
    client.stop();
    buf[total] = '\0';
    outLen     = total;
    return total > 0;
}

bool downloadAndStream(const Manifest& m, const esp_partition_t* target) {
    UrlParts u;
    if (!parseHttpUrl(m.url, u)) {
        Serial.printf("[OTA] bad url: %s\n", m.url);
        return false;
    }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_begin failed");
        return false;
    }

    WiFiClient client;
    if (!openHttpGet(client, u)) {
        Serial.println("[OTA] connect failed");
        esp_ota_abort(handle);
        return false;
    }
    if (!readStatusAndDrainHeaders(client)) {
        client.stop();
        esp_ota_abort(handle);
        return false;
    }

    Sha256Streamer hasher;
    uint8_t buf[1024];
    size_t  total = 0;
    while (client.connected() || client.available()) {
        if (client.available()) {
            int n = client.read(buf, sizeof(buf));
            if (n <= 0) continue;
            if (esp_ota_write(handle, buf, n) != ESP_OK) {
                Serial.printf("[OTA] write failed at %u bytes\n", (unsigned)total);
                client.stop();
                esp_ota_abort(handle);
                return false;
            }
            hasher.update(buf, n);
            total += n;
        } else {
            delay(5);
        }
    }
    client.stop();

    if (total != m.size) {
        Serial.printf("[OTA] size mismatch got=%u want=%u\n",
                      (unsigned)total, (unsigned)m.size);
        esp_ota_abort(handle);
        return false;
    }

    char hex[65] = {};
    hasher.finalizeToHex(hex);
    if (!hasher.matches(m.sha256)) {
        Serial.printf("[OTA] sha256 mismatch got=%s want=%s\n", hex, m.sha256);
        esp_ota_abort(handle);
        return false;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        Serial.println("[OTA] esp_ota_end failed");
        return false;
    }
    Serial.printf("[OTA] download bytes=%u sha256_ok=true\n", (unsigned)total);
    return true;
}

}  // namespace

namespace Ota {

void validateOnBoot() {
    char attempted[32] = {};
    OtaState::getAttempted(attempted, sizeof(attempted));

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    bool isPending = false;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        isPending = (state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    ValidateAction action = validateOnBootAction(FIRMWARE_VERSION, attempted, isPending);
    Serial.printf("[OTA] validate version=%s attempted=%s pending=%d action=%d\n",
                  FIRMWARE_VERSION, attempted, (int)isPending, (int)action);

    switch (action) {
        case ValidateAction::NoOp:
            break;
        case ValidateAction::ClearAttempted:
            OtaState::clearAttempted();
            OtaState::clearFailed();
            break;
        case ValidateAction::RecordFailed:
            OtaState::setFailed(attempted);
            OtaState::clearAttempted();
            break;
    }
}

void onPublishSuccess() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    Serial.println("[OTA] first publish ok — marking firmware valid");
    esp_ota_mark_app_valid_cancel_rollback();
    OtaState::clearAttempted();
    OtaState::clearFailed();
}

void checkAndApply(uint8_t batteryPct) {
    char host[64];
    readOtaHost(host, sizeof(host));

    char manifestUrl[160];
    snprintf(manifestUrl, sizeof(manifestUrl),
             "http://%s/firmware/sensor-tag-wifi/%s/manifest.json",
             host, OTA_VARIANT);

    char body[1024];
    int len = 0;
    if (!fetchManifestText(manifestUrl, body, sizeof(body), len)) {
        Serial.printf("[OTA] manifest fetch failed url=%s\n", manifestUrl);
        return;
    }

    Manifest m {};
    if (!parseManifest(body, (size_t)len, m)) {
        Serial.println("[OTA] manifest parse failed");
        return;
    }

    char failed[32] = {};
    OtaState::getFailed(failed, sizeof(failed));

    Serial.printf("[OTA] check version=%s manifest=%s failed=%s battery=%u\n",
                  FIRMWARE_VERSION, m.version, failed, batteryPct);

    if (!shouldApply(FIRMWARE_VERSION, m.version, failed, batteryPct)) {
        Serial.println("[OTA] check → skip");
        return;
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) { Serial.println("[OTA] no inactive partition"); return; }

    if (!downloadAndStream(m, target)) return;

    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        Serial.println("[OTA] set_boot_partition failed");
        return;
    }
    OtaState::setAttempted(m.version);
    Serial.printf("[OTA] reboot into %s\n", m.version);
    Serial.flush();
    esp_restart();
}

}  // namespace Ota
